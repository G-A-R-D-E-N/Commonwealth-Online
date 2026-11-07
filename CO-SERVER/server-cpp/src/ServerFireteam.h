#pragma once

#include "ServerOutboundFrames.h"
#include "ServerProtocol.h"

#include "F4TFireteam.h"
#include "F4TPacketRateLimit.h"
#include "F4TQuestProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct FireteamSessionState
	{
		F4T::PacketRateLimit::TokenBucket invites;
		F4T::PacketRateLimit::TokenBucket responses;
	};

	class FireteamService
	{
	public:
		using Json = nlohmann::json;

		struct FireteamPeer
		{
			std::uint32_t connectionId{};
			std::uint32_t playerId{};
			std::string   profileId;
			std::string   name;
			bool          capable{};
		};

		using Peers = std::vector<FireteamPeer>;

		std::vector<OutboundFrame> Dispatch(
			std::string_view a_type,
			const FireteamPeer& a_self,
			FireteamSessionState& a_state,
			const Peers& a_peers,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);

		[[nodiscard]] std::vector<OutboundFrame> SnapshotFrames(
			std::uint32_t a_connectionId,
			std::uint32_t a_playerId,
			double a_unixTimeSeconds) const;

		std::vector<OutboundFrame> DisconnectFrames(std::uint32_t a_playerId, double a_unixTimeSeconds);

		std::vector<OutboundFrame> ExpiryFrames(double a_nowSeconds);

		[[nodiscard]] std::string TeamIdOfPlayer(std::uint32_t a_playerId) const;
		[[nodiscard]] std::vector<std::uint32_t> MembersOfPlayer(std::uint32_t a_playerId) const;
		[[nodiscard]] bool Capable(const FireteamPeer& a_peer) const;
		void NotePeers(const Peers& a_peers);
		[[nodiscard]] std::vector<std::string> TakeDisbandedTeams();

	private:
		struct TrackedPeer
		{
			std::uint32_t connectionId{};
			std::string   name;
			bool          capable{};
		};

		struct PendingInvite
		{
			std::uint32_t fromPlayerId{};
			std::uint32_t targetPlayerId{};
			double        expiresAtSeconds{};
		};

		[[nodiscard]] static std::string_view ReasonName(F4T::Fireteam::JoinRefusal a_refusal);
		[[nodiscard]] static OutboundFrame ReliableFrame(std::uint32_t a_connectionId, const Json& a_packet)
		{
			return ::CommonwealthOnline::Server::ReliableFrame(a_connectionId, a_packet);
		}
		static void AppendFrames(std::vector<OutboundFrame>& a_destination, std::vector<OutboundFrame> a_source)
		{
			::CommonwealthOnline::Server::AppendFrames(a_destination, std::move(a_source));
		}

		void ForgetPlayer(std::uint32_t a_playerId);
		void DropInvitesInvolving(std::uint32_t a_playerId);
		[[nodiscard]] const TrackedPeer* TrackedFor(std::uint32_t a_playerId) const;
		[[nodiscard]] const TrackedPeer* ReachableFor(std::uint32_t a_playerId) const;
		[[nodiscard]] std::string StoredName(std::uint32_t a_playerId) const;
		[[nodiscard]] F4T::QuestProtocol::FireteamSnapshot SnapshotFor(std::uint32_t a_playerId, const Peers* a_peers) const;
		[[nodiscard]] std::vector<OutboundFrame> SnapshotFramesFor(
			const std::vector<std::uint32_t>& a_players,
			const Peers* a_peers,
			double a_unixTimeSeconds) const;
		[[nodiscard]] static OutboundFrame EmptySnapshotFrame(std::uint32_t a_connectionId, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> RefusalFrames(
			const FireteamPeer& a_self,
			F4T::Fireteam::JoinRefusal a_refusal,
			double a_unixTimeSeconds) const;

		std::vector<OutboundFrame> HandleInvite(
			const FireteamPeer& a_self,
			FireteamSessionState& a_state,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleRespond(
			const FireteamPeer& a_self,
			FireteamSessionState& a_state,
			const Peers& a_peers,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleLeave(const FireteamPeer& a_self, const Peers& a_peers, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleKick(const FireteamPeer& a_self, const Peers& a_peers, const Json& a_packet, double a_unixTimeSeconds);

		F4T::Fireteam::FireteamTable table;
		std::map<std::uint32_t, TrackedPeer> tracked;
		std::map<std::string, PendingInvite> pendingInvites;
		std::vector<std::string> disbandedTeams;
	};
}
