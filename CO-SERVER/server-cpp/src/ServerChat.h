#pragma once

#include "ServerProtocol.h"
#include "ServerSocialGraph.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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
	};

	struct ChatPeer
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::string profileId;
		std::string characterName;
		bool socialNames{};
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
		void SetMuted(std::string_view a_profileId, bool a_muted);

		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			const ChatPeer& a_self,
			ChatSessionState& a_state,
			const Peers& a_peers,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);

	private:
		std::vector<OutboundFrame> Reject(std::uint32_t a_connectionId, std::string_view a_reason);
		std::vector<OutboundFrame> LimitReply(std::uint32_t a_connectionId, ChatSessionState& a_state, std::string_view a_reason, double a_monotonicSeconds);
		std::vector<OutboundFrame> HandleSend(const ChatPeer& a_self, ChatSessionState& a_state, const Peers& a_peers, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleHistoryRequest(
			std::uint32_t a_connectionId,
			const std::string& a_requesterProfileId,
			bool a_requesterSocialNames,
			ChatSessionState& a_state,
			double a_monotonicSeconds);
		[[nodiscard]] SocialGraph::NameView SenderNameView(
			const std::string& a_viewerProfileId,
			const std::string& a_senderProfileId,
			std::string_view a_realName) const;

		ProtocolStats& stats;
		const FactionService& factions;
		const SocialGraph& social;
		bool enabled{};
		std::set<std::string> mutedProfileIds;
		std::deque<Json> history;
		std::uint64_t nextMessageId{ 1 };
	};
}
