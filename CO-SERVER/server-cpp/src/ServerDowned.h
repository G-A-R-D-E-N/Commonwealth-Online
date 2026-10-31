#pragma once

#include "F4TPacketRateLimit.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct DownedPosition
	{
		double x{};
		double y{};
		double z{};
		std::string cellId;
		std::string worldspaceId;
		double monotonicSeconds{};
	};

	struct DownedPeer
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::optional<DownedPosition> position;
	};

	struct DownedSessionState
	{
		F4T::PacketRateLimit::TokenBucket respawnRequests;
	};

	class DownedService
	{
	public:
		using Json = nlohmann::json;
		using Peers = std::vector<DownedPeer>;

		explicit DownedService(ProtocolStats& a_stats) :
			stats(a_stats)
		{}

		void SetConfig(F4T::DownedState::Config a_config) noexcept { downedStates.SetConfig(a_config); }
		void Leave(std::uint32_t a_playerId) { downedStates.Leave(a_playerId); }

		std::vector<OutboundFrame> Poll(const Peers& a_peers, double a_monotonicSeconds);
		std::vector<OutboundFrame> Join(
			const Peers& a_peers,
			std::uint32_t a_connectionId,
			std::uint32_t a_playerId,
			std::string_view a_profileId,
			double a_monotonicSeconds);
		std::vector<OutboundFrame> ObserveVitals(
			const std::function<Peers()>& a_peers,
			std::uint32_t a_playerId,
			bool a_isDowned,
			bool a_isDead,
			double a_monotonicSeconds,
			bool a_hasStateFlags);
		std::vector<OutboundFrame> AdminRevive(const Peers& a_peers, std::uint32_t a_playerId, double a_monotonicSeconds);
		std::vector<OutboundFrame> AdminKill(const Peers& a_peers, std::uint32_t a_playerId);
		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			const Peers& a_peers,
			std::uint32_t a_playerId,
			DownedSessionState& a_state,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_maximumPositionAgeSeconds);

	private:
		std::vector<OutboundFrame> EventFrames(const Peers& a_peers, const std::vector<F4T::DownedState::Event>& a_events);
		std::vector<OutboundFrame> SnapshotFrames(std::uint32_t a_connectionId, double a_monotonicSeconds) const;
		std::vector<OutboundFrame> HandleReviveRequest(
			const Peers& a_peers,
			std::uint32_t a_playerId,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_maximumPositionAgeSeconds);
		std::vector<OutboundFrame> HandleReviveCancel(
			const Peers& a_peers,
			std::uint32_t a_playerId,
			const Json& a_packet,
			double a_monotonicSeconds);
		std::vector<OutboundFrame> HandleCheckpoint(
			const Peers& a_peers,
			std::uint32_t a_playerId,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_maximumPositionAgeSeconds);
		std::vector<OutboundFrame> HandleRespawnRequest(
			const Peers& a_peers,
			std::uint32_t a_playerId,
			DownedSessionState& a_state,
			const Json& a_packet,
			double a_monotonicSeconds);

		ProtocolStats& stats;
		F4T::DownedState::Tracker downedStates;
		double lastDownedBroadcastMonotonic{};
	};
}
