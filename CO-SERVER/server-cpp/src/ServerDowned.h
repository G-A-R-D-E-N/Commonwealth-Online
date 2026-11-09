#pragma once

#include "F4TPacketRateLimit.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
		std::string cellKey;
		std::string worldspaceKey;
	};

	struct DownedPeer
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::optional<DownedPosition> position;
		std::string profileId;
		std::string factionId;
		bool visibilitySuppressed{};
		std::uint64_t playerIncarnation{};
		std::string persistenceProfile;
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

		explicit DownedService(ProtocolStats& a_stats, ServerEvents* a_serverEvents = nullptr) :
			stats(a_stats),
			serverEvents(a_serverEvents)
		{}

		void SetConfig(F4T::DownedState::Config a_config) noexcept { downedStates.SetConfig(a_config); }
		void SetRetirementHandler(std::function<bool(std::string_view)> a_handler) { retireUntransferred = std::move(a_handler); }
		void SetDeathBagIdHandler(std::function<std::optional<std::string>(std::string_view)> a_handler) { deathBagId = std::move(a_handler); }
		void SetDeathHandler(std::function<bool(std::uint32_t, std::string_view)> a_handler) { recordDeath = std::move(a_handler); }
		void SetDownedHandler(std::function<void(std::uint32_t, std::uint32_t)> a_handler) { recordDowned = std::move(a_handler); }
		void SetExpiryHandler(std::function<std::vector<OutboundFrame>(const Peers&)> a_handler) { expirePersisted = std::move(a_handler); }
		std::vector<OutboundFrame> Leave(const Peers& a_peers, std::uint32_t a_playerId) {
			pendingDeaths.erase(a_playerId);
			return EventFrames(a_peers, downedStates.Leave(a_playerId));
		}
		[[nodiscard]] std::optional<F4T::DownedState::State> StateOf(std::uint32_t a_playerId) const { return downedStates.StateOf(a_playerId); }
		[[nodiscard]] bool RestoreOffline(std::string_view a_profile, F4T::DownedState::State a_state, double a_remaining, double a_now, std::string_view a_character) {
			return downedStates.RestoreOffline(a_profile, a_state, a_remaining, a_now, a_character);
		}
		std::optional<Json> AuthoritativeState(std::uint32_t a_playerId);

		std::vector<OutboundFrame> Poll(const Peers& a_peers, double a_monotonicSeconds);
		std::vector<OutboundFrame> ResumeFrames(const Peers& a_peers, std::uint32_t a_playerId, double a_monotonicSeconds);
		std::vector<OutboundFrame> Join(
			const Peers& a_peers,
			std::uint32_t a_connectionId,
			std::uint32_t a_playerId,
			std::string_view a_profileId,
			double a_monotonicSeconds, std::string_view a_characterId = {});
		std::vector<OutboundFrame> ObserveVitals(
			const std::function<Peers()>& a_peers,
			std::uint32_t a_playerId,
			bool a_isDowned,
			bool a_isDead,
			double a_monotonicSeconds,
			bool a_hasStateFlags);
		std::vector<OutboundFrame> AdminRevive(const Peers& a_peers, std::uint32_t a_playerId, double a_monotonicSeconds);
		std::vector<OutboundFrame> AdminKill(const Peers& a_peers, std::uint32_t a_playerId, bool* a_refusedOut = nullptr);
		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			const Peers& a_peers,
			std::uint32_t a_playerId,
			DownedSessionState& a_state,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_maximumPositionAgeSeconds,
			bool a_clientReportsStimpaks = true);

	private:
		[[nodiscard]] bool RetireForEvents(const Peers& a_peers, const std::vector<F4T::DownedState::Event>& a_events);
		[[nodiscard]] bool FinalizeDeaths(const Peers& a_peers, const std::vector<F4T::DownedState::Event>& a_events);
		[[nodiscard]] std::set<std::uint32_t> RefusedPlayers(const Peers& a_peers, const std::vector<F4T::DownedState::Event>& a_events);
		void StampState(Json& a_packet, const Peers& a_peers, std::uint32_t a_playerId);
		std::uint64_t nextStateRevision{};
		std::unordered_map<std::uint32_t, std::pair<std::uint64_t, std::uint8_t>> stateRevisions;
		std::vector<OutboundFrame> EventFrames(const Peers& a_peers, const std::vector<F4T::DownedState::Event>& a_events);
		std::vector<OutboundFrame> RetryPendingDeaths(const Peers& a_peers);
		std::vector<OutboundFrame> SnapshotFrames(const Peers& a_peers, std::uint32_t a_connectionId, double a_monotonicSeconds);
		std::vector<OutboundFrame> HandleReviveRequest(
			const Peers& a_peers,
			std::uint32_t a_playerId,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_maximumPositionAgeSeconds,
			bool a_clientReportsStimpaks);
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
		ServerEvents* serverEvents{};
		std::function<bool(std::string_view)> retireUntransferred;
		std::function<bool(std::uint32_t, std::string_view)> recordDeath;
		std::function<std::optional<std::string>(std::string_view)> deathBagId;
		std::function<void(std::uint32_t, std::uint32_t)> recordDowned;
		std::function<std::vector<OutboundFrame>(const Peers&)> expirePersisted;
		F4T::DownedState::Tracker downedStates;
		double lastDownedBroadcastMonotonic{};
		std::unordered_set<std::uint32_t> pendingDeaths;
	};
}
