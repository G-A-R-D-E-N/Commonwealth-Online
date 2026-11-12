#include "ServerChat.h"

#include "ServerFactions.h"
#include "ServerOutboundFrames.h"

#include "F4TChatValidation.h"
#include "F4TProfanityFilter.h"

#include <algorithm>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::size_t kMaximumChatHistory = 20;
		constexpr std::size_t kMaximumTotalChatHistory = kMaximumChatHistory * 101;
		constexpr std::size_t kMaximumChatRateProfiles = 4096;
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

		Json TextFor(const Json& a_message, bool a_profanityFilter)
		{
			if (!a_profanityFilter && !F4T::ProfanityFilter::ProfanityForced()) {
				return a_message;
			}
			Json masked = a_message;
			masked["text"] = F4T::ProfanityFilter::MaskBlockedWords(a_message.at("text").get<std::string>(), F4T::ProfanityFilter::Scope::All);
			return masked;
		}

		std::optional<std::string_view> SendLimit(std::deque<ChatMessageRecord>& a_recent, std::string_view a_text, double a_monotonicSeconds)
		{
			std::erase_if(a_recent, [a_monotonicSeconds](const ChatMessageRecord& a_record) {
				const auto age = a_monotonicSeconds - a_record.monotonicSeconds;
				return !(age >= 0.0) || age > kChatDuplicateWindowSeconds;
			});
			std::size_t recent = 0;
			std::size_t identical = 0;
			for (const auto& record : a_recent) {
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
		double a_unixTimeSeconds,
		const std::function<bool(const F4T::ChatValidation::ChatSendResult&)>& a_allow)
	{
		if (!enabled) {
			return std::nullopt;
		}
		if (a_type == "chatSend") {
			return HandleSend(a_self, a_state, a_peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds, a_allow);
		}
		if (a_type == "chatHistoryRequest") {
			return HandleHistoryRequest(a_self, a_state, a_monotonicSeconds);
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
		double a_unixTimeSeconds,
		const std::function<bool(const F4T::ChatValidation::ChatSendResult&)>& a_allow)
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
		auto* recent = &a_state.recent;
		if (!a_self.profileId.empty()) {
			auto found = recentByProfile.find(a_self.profileId);
			if (found == recentByProfile.end()) {
				if (recentByProfile.size() >= kMaximumChatRateProfiles) {
					std::erase_if(recentByProfile, [a_monotonicSeconds](const auto& entry) {
						if (entry.second.empty()) return true;
						const auto age = a_monotonicSeconds - entry.second.back().monotonicSeconds;
						return !(age >= 0.0) || age > kChatDuplicateWindowSeconds;
					});
				}
				if (recentByProfile.size() >= kMaximumChatRateProfiles) return LimitReply(a_self.connectionId, a_state, "rateLimited", a_monotonicSeconds);
				found = recentByProfile.emplace(a_self.profileId, std::deque<ChatMessageRecord>{}).first;
			}
			recent = &found->second;
		}
		if (const auto limit = SendLimit(*recent, normalized.text, a_monotonicSeconds)) {
			return LimitReply(a_self.connectionId, a_state, *limit, a_monotonicSeconds);
		}
		recent->push_back(ChatMessageRecord{ a_monotonicSeconds, normalized.text });
		if (commandRouter) {
			if (auto frames = commandRouter(a_self.playerId, normalized.text)) {
				return std::move(*frames);
			}
		}
		if (!a_allow(normalized)) {
			auto frames = Reject(a_self.connectionId, "vetoed");
			frames.push_back(HistoryFrame(a_self));
			return frames;
		}
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
		message["fromProfileId"] = a_self.profileId;
		message["fromSocialKey"] = a_self.socialKey;
		if (auto tag = factions.TagForProfile(a_self.profileId); !tag.empty()) {
			message["factionTag"] = std::move(tag);
		}
		if (factionMembers) {
			message["factionId"] = factions.FactionIdOf(a_self.profileId);
			message["factionMembers"] = *factionMembers;
		}
		std::vector<OutboundFrame> frames;
		for (const auto& peer : a_peers) {
			if (!factionMembers || std::find(factionMembers->begin(), factionMembers->end(), peer.profileId) != factionMembers->end()) {
				frames.push_back(ReliableFrame(peer.connectionId, MessageFor(peer, message, peer.connectionId == a_self.connectionId)));
			}
		}
		const auto bucketFactionId = message.value("factionId", std::string{});
		history.push_back(std::move(message));
		const auto sameBucket = [isFaction, bucketFactionId](const Json& a_entry) {
			return (a_entry.value("channel", std::string{}) == "faction") == isFaction &&
				a_entry.value("factionId", std::string{}) == bucketFactionId;
		};
		if (static_cast<std::size_t>(std::count_if(history.begin(), history.end(), sameBucket)) > kMaximumChatHistory) {
			history.erase(std::find_if(history.begin(), history.end(), sameBucket));
		}
		if (history.size() > kMaximumTotalChatHistory) {
			history.erase(std::find_if(history.begin(), history.end(), [](const Json& a_entry) {
				return a_entry.value("channel", std::string{}) == "faction";
			}));
		}
		return frames;
	}

	SocialGraph::NameView ChatService::SenderNameView(
		const ChatPeer& a_viewer,
		const std::string& a_senderProfileId,
		const std::string& a_senderSocialKey,
		std::string_view a_realName) const
	{
		const std::string& viewerKey = a_viewer.socialKey;
		const std::string& senderKey = a_senderSocialKey;
		return social.NameViewFor(viewerKey, a_viewer.profileId, senderKey, a_realName, factions.SameFaction(a_viewer.profileId, a_senderProfileId));
	}

	std::vector<OutboundFrame> ChatService::HandleHistoryRequest(
		const ChatPeer& a_self,
		ChatSessionState& a_state,
		double a_monotonicSeconds)
	{
		const std::uint32_t connectionId = a_self.connectionId;
		if (a_state.lastHistoryMonotonic) {
			const auto age = a_monotonicSeconds - *a_state.lastHistoryMonotonic;
			if (age >= 0.0 && age < kChatHistoryWindowSeconds) {
				return LimitReply(connectionId, a_state, "rateLimited", a_monotonicSeconds);
			}
		}
		a_state.lastHistoryMonotonic = a_monotonicSeconds;
		return { HistoryFrame(a_self) };
	}

	ChatService::Json ChatService::MessageFor(const ChatPeer& a_viewer, const Json& a_message, bool a_isSender) const
	{
		Json masked = TextFor(a_message, a_viewer.profanityFilter);
		const auto sender = a_message.value("fromProfileId", std::string{});
		const auto realName = a_message.value("fromName", std::string{});
		const auto view = a_isSender ?
			SocialGraph::NameView{ realName, realName } :
			SenderNameView(a_viewer, sender, a_message.value("fromSocialKey", std::string{}), realName);
		masked["displayName"] = view.displayName;
		if (view.disclosedName) masked["fromName"] = *view.disclosedName;
		// Constraint: legacy clients get display text in fromName instead of no fromName.
		else if (!a_viewer.socialNames) masked["fromName"] = view.displayName;
		else masked.erase("fromName");
		masked.erase("fromProfileId");
		masked.erase("fromSocialKey");
		masked.erase("factionId");
		masked.erase("factionMembers");
		return masked;
	}

	ChatService::Json ChatService::HistoryFor(const ChatPeer& a_viewer) const
	{
		Json messages = Json::array();
		const auto factionId = factions.FactionIdOf(a_viewer.profileId);
		for (const auto& stored : history) {
			if (stored.value("channel", std::string{}) == "faction") {
				const auto members = stored.find("factionMembers");
				if (factionId.empty() || stored.value("factionId", std::string{}) != factionId ||
					members == stored.end() || !members->is_array() ||
					std::find(members->begin(), members->end(), Json(a_viewer.profileId)) == members->end()) {
					continue;
				}
			}
			messages.push_back(MessageFor(a_viewer, stored));
		}
		if (messages.size() > kMaximumChatHistory) {
			messages.erase(messages.begin(), messages.end() - kMaximumChatHistory);
		}
		return messages;
	}

	OutboundFrame ChatService::HistoryFrame(const ChatPeer& a_self) const
	{
		return ReliableFrame(a_self.connectionId, Json{ { "type", "chatHistory" }, { "messages", HistoryFor(a_self) }, { "profanityForced", F4T::ProfanityFilter::ProfanityForced() } });
	}
}
