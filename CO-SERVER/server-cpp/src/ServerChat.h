#pragma once

#include "ServerProtocol.h"

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
	};

	class ChatService
	{
	public:
		using Json = nlohmann::json;
		using Peers = std::vector<ChatPeer>;

		ChatService(ProtocolStats& a_stats, const FactionService& a_factions) :
			stats(a_stats),
			factions(a_factions)
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
		std::vector<OutboundFrame> HandleHistoryRequest(std::uint32_t a_connectionId, ChatSessionState& a_state, double a_monotonicSeconds);

		ProtocolStats& stats;
		const FactionService& factions;
		bool enabled{};
		std::set<std::string> mutedProfileIds;
		std::deque<Json> history;
		std::uint64_t nextMessageId{ 1 };
	};
}
