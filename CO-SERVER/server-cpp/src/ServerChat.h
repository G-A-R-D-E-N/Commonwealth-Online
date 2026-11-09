#pragma once

#include "F4TChatValidation.h"
#include "ServerProtocol.h"
#include "ServerSocialGraph.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	class FactionService;

	struct ChatMessageRecord
	{
		double monotonicSeconds{};
		std::string text;
	};

	struct ChatSessionState
	{
		std::deque<ChatMessageRecord> recent;
		std::optional<double> lastHistoryMonotonic;
		std::optional<double> lastLimitReplyMonotonic;
		bool profanityFilter{};
	};

	struct ChatPeer
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::string profileId;
		std::string socialKey;
		std::string characterName;
		bool socialNames{};
		bool profanityFilter{};
	};

	class ChatService
	{
	public:
		using Json = nlohmann::json;
		using Peers = std::vector<ChatPeer>;

		ChatService(ProtocolStats& a_stats, const FactionService& a_factions, const SocialGraph& a_social) :
			stats(a_stats),
			factions(a_factions),
			social(a_social)
		{}

		[[nodiscard]] bool Enabled() const noexcept { return enabled; }
		void SetEnabled(bool a_enabled) noexcept { enabled = a_enabled; }
		[[nodiscard]] std::uint64_t NextMessageId() noexcept { return nextMessageId++; }
		void SetMuted(std::string_view a_profileId, bool a_muted);
		void SetCommandRouter(ChatCommandRouter a_router) { commandRouter = std::move(a_router); }

		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			const ChatPeer& a_self,
			ChatSessionState& a_state,
			const Peers& a_peers,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			const std::function<bool(const F4T::ChatValidation::ChatSendResult&)>& a_allow);

	private:
		std::vector<OutboundFrame> Reject(std::uint32_t a_connectionId, std::string_view a_reason);
		std::vector<OutboundFrame> LimitReply(std::uint32_t a_connectionId, ChatSessionState& a_state, std::string_view a_reason, double a_monotonicSeconds);
		std::vector<OutboundFrame> HandleSend(const ChatPeer& a_self, ChatSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds, const std::function<bool(const F4T::ChatValidation::ChatSendResult&)>& a_allow);
		std::vector<OutboundFrame> HandleHistoryRequest(
			const ChatPeer& a_self,
			ChatSessionState& a_state,
			double a_monotonicSeconds);
		[[nodiscard]] Json HistoryFor(const ChatPeer& a_viewer) const;
		[[nodiscard]] Json MessageFor(const ChatPeer& a_viewer, const Json& a_message, bool a_isSender = false) const;
		[[nodiscard]] OutboundFrame HistoryFrame(const ChatPeer& a_self) const;
		[[nodiscard]] SocialGraph::NameView SenderNameView(
			const ChatPeer& a_viewer,
			const std::string& a_senderProfileId,
			const std::string& a_senderSocialKey,
			std::string_view a_realName) const;

		ProtocolStats& stats;
		const FactionService& factions;
		const SocialGraph& social;
		bool enabled{};
		std::set<std::string> mutedProfileIds;
		std::unordered_map<std::string, std::deque<ChatMessageRecord>> recentByProfile;
		std::deque<Json> history;
		std::uint64_t nextMessageId{ 1 };
		ChatCommandRouter commandRouter;
	};
}
