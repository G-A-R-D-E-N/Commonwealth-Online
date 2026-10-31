#include "ServerChat.h"

#include "ServerFactions.h"
#include "ServerOutboundFrames.h"

#include "F4TChatValidation.h"

#include <algorithm>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::size_t kMaximumChatHistory = 20;
		constexpr double kChatMessageWindowSeconds = 10.0;
		constexpr std::size_t kMaximumChatMessagesPerWindow = 5;
		constexpr double kChatDuplicateWindowSeconds = 30.0;
		constexpr std::size_t kMaximumIdenticalChatMessagesPerWindow = 2;
		constexpr double kChatHistoryWindowSeconds = 2.0;

		std::string_view SendErrorReason(F4T::ChatValidation::ChatSendError a_error)
		{
			return a_error == F4T::ChatValidation::ChatSendError::TooLong ? "tooLong" :
				a_error == F4T::ChatValidation::ChatSendError::ChannelUnavailable ? "channelUnavailable" :
				"invalidText";
		}

		std::optional<std::string_view> SendLimit(ChatSessionState& a_state, std::string_view a_text, double a_monotonicSeconds)
		{
			std::erase_if(a_state.recent, [a_monotonicSeconds](const ChatMessageRecord& a_record) {
				const auto age = a_monotonicSeconds - a_record.monotonicSeconds;
				return !(age >= 0.0) || age > kChatDuplicateWindowSeconds;
			});
			std::size_t recent = 0;
			std::size_t identical = 0;
			for (const auto& record : a_state.recent) {
				if (a_monotonicSeconds - record.monotonicSeconds <= kChatMessageWindowSeconds) {
					recent++;
				}
				if (record.text == a_text) {
					identical++;
				}
			}
			if (recent >= kMaximumChatMessagesPerWindow) {
				return "rateLimited";
			}
			if (identical >= kMaximumIdenticalChatMessagesPerWindow) {
				return "duplicate";
			}
			return std::nullopt;
		}
	}

	void ChatService::SetMuted(std::string_view a_profileId, bool a_muted)
	{
		if (a_profileId.empty()) {
			return;
		}
		if (a_muted) {
			mutedProfileIds.emplace(a_profileId);
		} else {
			mutedProfileIds.erase(std::string{ a_profileId });
		}
	}

	std::optional<std::vector<OutboundFrame>> ChatService::Dispatch(
		std::string_view a_type,
		const ChatPeer& a_self,
		ChatSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		if (!enabled) {
			return std::nullopt;
		}
		if (a_type == "chatSend") {
			return HandleSend(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
		if (a_type == "chatHistoryRequest") {
			return HandleHistoryRequest(a_self.connectionId, a_state, a_monotonicSeconds);
		}
		return std::nullopt;
	}

	std::vector<OutboundFrame> ChatService::Reject(std::uint32_t a_connectionId, std::string_view a_reason)
	{
		stats.chatRejected++;
		return { ReliableFrame(a_connectionId, Json{ { "type", "chatRejected" }, { "reason", std::string{ a_reason } } }) };
	}

	std::vector<OutboundFrame> ChatService::LimitReply(
		std::uint32_t a_connectionId,
		ChatSessionState& a_state,
		std::string_view a_reason,
		double a_monotonicSeconds)
	{
		if (a_state.lastLimitReplyMonotonic) {
			const auto age = a_monotonicSeconds - *a_state.lastLimitReplyMonotonic;
			if (age >= 0.0 && age < kChatMessageWindowSeconds) {
				stats.chatRejected++;
				return {};
			}
		}
		a_state.lastLimitReplyMonotonic = a_monotonicSeconds;
		return Reject(a_connectionId, a_reason);
	}

	std::vector<OutboundFrame> ChatService::HandleSend(
		const ChatPeer& a_self,
		ChatSessionState& a_state,
		const Peers& a_peers,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		stats.chatReceived++;
		const auto normalized = F4T::ChatValidation::NormalizeChatSend(a_packet);
		if (normalized.error == F4T::ChatValidation::ChatSendError::Malformed) {
			stats.packetsRejected++;
			stats.chatRejected++;
			return {};
		}
		if (normalized.error) {
			return Reject(a_self.connectionId, SendErrorReason(*normalized.error));
		}
		const auto isFaction = normalized.channel == "faction";
		const auto factionMembers = isFaction ? factions.MembersOf(a_self.profileId) : nullptr;
		if (isFaction && factionMembers == nullptr) {
			return Reject(a_self.connectionId, "notInFaction");
		}
		if (mutedProfileIds.contains(a_self.profileId)) {
			return Reject(a_self.connectionId, "muted");
		}
		if (const auto limit = SendLimit(a_state, normalized.text, a_monotonicSeconds)) {
			return LimitReply(a_self.connectionId, a_state, *limit, a_monotonicSeconds);
		}
		a_state.recent.push_back(ChatMessageRecord{ a_monotonicSeconds, normalized.text });
		stats.chatRelayed++;
		Json message{
			{ "type", "chatMessage" },
			{ "channel", normalized.channel },
			{ "messageId", nextMessageId++ },
			{ "fromPlayerId", a_self.playerId },
			{ "fromName", F4T::ChatValidation::SanitizeChatDisplayName(a_self.characterName) },
			{ "text", normalized.text },
			{ "serverTime", a_unixTimeSeconds }
		};
		std::vector<OutboundFrame> frames;
		if (factionMembers) {
			for (const auto& profileId : *factionMembers) {
				const auto member = std::find_if(a_peers.begin(), a_peers.end(), [&profileId](const ChatPeer& a_peer) { return a_peer.profileId == profileId; });
				if (member != a_peers.end()) frames.push_back(ReliableFrame(member->connectionId, message));
			}
		} else {
			for (const auto& peer : a_peers) {
				frames.push_back(ReliableFrame(peer.connectionId, message));
			}
			history.push_back(std::move(message));
			if (history.size() > kMaximumChatHistory) {
				history.pop_front();
			}
		}
		return frames;
	}

	std::vector<OutboundFrame> ChatService::HandleHistoryRequest(std::uint32_t a_connectionId, ChatSessionState& a_state, double a_monotonicSeconds)
	{
		if (a_state.lastHistoryMonotonic) {
			const auto age = a_monotonicSeconds - *a_state.lastHistoryMonotonic;
			if (age >= 0.0 && age < kChatHistoryWindowSeconds) {
				return LimitReply(a_connectionId, a_state, "rateLimited", a_monotonicSeconds);
			}
		}
		a_state.lastHistoryMonotonic = a_monotonicSeconds;
		return { ReliableFrame(a_connectionId, Json{ { "type", "chatHistory" }, { "messages", Json(history) } }) };
	}
}
