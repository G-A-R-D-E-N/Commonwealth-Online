#include "ServerDowned.h"
#include "ServerOutboundFrames.h"

#include "F4TDownedValidation.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <memory>
#include <string>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr double kGameUnitsPerMeter = 70.0;
		constexpr double kRespawnRequestRefillPerSecond = 2.0;
		constexpr double kRespawnRequestBurst = 4.0;
		constexpr double kMaximumCheckpointDriftMeters = 250.0;

		std::string HexFormId(std::uint32_t a_formId)
		{
			char text[9]{};
			std::snprintf(text, sizeof(text), "%08X", a_formId);
			return text;
		}

		std::vector<OutboundFrame> BroadcastReliable(const DownedService::Peers& a_peers, const Json& a_packet)
		{
			std::vector<OutboundFrame> frames;
			for (const auto& peer : a_peers) {
				frames.push_back(ReliableFrame(peer.connectionId, a_packet));
			}
			return frames;
		}

		Json DownedStatePacket(std::uint32_t a_playerId, bool a_downed, std::uint32_t a_remainingMs)
		{
			return Json{
				{ "type", "downedState" },
				{ "playerId", a_playerId },
				{ "downed", a_downed },
				{ "remainingMs", a_remainingMs }
			};
		}

		const DownedPeer* FindPeer(const DownedService::Peers& a_peers, std::uint32_t a_playerId)
		{
			for (const auto& peer : a_peers) {
				if (peer.playerId == a_playerId) {
					return std::addressof(peer);
				}
			}
			return nullptr;
		}

		std::optional<double> ReviveDistanceMeters(
			const DownedService::Peers& a_peers,
			std::uint32_t a_reviverPlayerId,
			std::uint32_t a_targetPlayerId,
			double a_monotonicSeconds,
			double a_maximumPositionAgeSeconds)
		{
			const auto* reviver = FindPeer(a_peers, a_reviverPlayerId);
			const auto* target = FindPeer(a_peers, a_targetPlayerId);
			if (reviver == nullptr || target == nullptr || !reviver->position || !target->position) {
				return std::nullopt;
			}
			const auto& a = *reviver->position;
			const auto& b = *target->position;
			for (const auto stamp : { a.monotonicSeconds, b.monotonicSeconds }) {
				const auto age = a_monotonicSeconds - stamp;
				if (!(age >= 0.0) || age > a_maximumPositionAgeSeconds) {
					return std::nullopt;
				}
			}
			if (a.cellId != b.cellId || a.worldspaceId != b.worldspaceId) {
				return std::nullopt;
			}
			const auto dx = a.x - b.x;
			const auto dy = a.y - b.y;
			const auto dz = a.z - b.z;
			const auto units = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (!std::isfinite(units)) {
				return std::nullopt;
			}
			return units / kGameUnitsPerMeter;
		}

		// Constraint: a checkpoint must sit on the reporter's last accepted transform.
		bool CheckpointMatchesPosition(
			const DownedService::Peers& a_peers,
			std::uint32_t a_playerId,
			const F4T::DownedState::Checkpoint& a_checkpoint,
			double a_monotonicSeconds,
			double a_maximumPositionAgeSeconds)
		{
			const auto* peer = FindPeer(a_peers, a_playerId);
			if (peer == nullptr || !peer->position) {
				return false;
			}
			const auto& position = *peer->position;
			const auto age = a_monotonicSeconds - position.monotonicSeconds;
			if (!(age >= 0.0) || age > a_maximumPositionAgeSeconds) {
				return false;
			}
			const auto& space = a_checkpoint.interior ? position.cellId : position.worldspaceId;
			if (space != HexFormId(a_checkpoint.space)) {
				return false;
			}
			const auto dx = position.x - a_checkpoint.x;
			const auto dy = position.y - a_checkpoint.y;
			const auto dz = position.z - a_checkpoint.z;
			const auto units = std::sqrt(dx * dx + dy * dy + dz * dz);
			return std::isfinite(units) && units <= kMaximumCheckpointDriftMeters * kGameUnitsPerMeter;
		}
	}

	std::vector<OutboundFrame> DownedService::Poll(const Peers& a_peers, double a_monotonicSeconds)
	{
		auto frames = EventFrames(a_peers, downedStates.Expire(a_monotonicSeconds));
		if (a_monotonicSeconds - lastDownedBroadcastMonotonic < F4T::DownedState::BroadcastIntervalSeconds) {
			return frames;
		}
		lastDownedBroadcastMonotonic = a_monotonicSeconds;
		for (const auto& peer : a_peers) {
			AppendFrames(frames, SnapshotFrames(peer.connectionId, a_monotonicSeconds));
		}
		return frames;
	}

	std::vector<OutboundFrame> DownedService::Join(
		const Peers& a_peers,
		std::uint32_t a_connectionId,
		std::uint32_t a_playerId,
		std::string_view a_profileId,
		double a_monotonicSeconds)
	{
		auto frames = EventFrames(a_peers, downedStates.Join(a_playerId, a_profileId, a_monotonicSeconds));
		AppendFrames(frames, SnapshotFrames(a_connectionId, a_monotonicSeconds));
		return frames;
	}

	std::vector<OutboundFrame> DownedService::ObserveVitals(
		const std::function<Peers()>& a_peers,
		std::uint32_t a_playerId,
		bool a_isDowned,
		bool a_isDead,
		double a_monotonicSeconds,
		bool a_hasStateFlags)
	{
		const auto events = downedStates.ObserveVitals(a_playerId, a_isDowned, a_isDead, a_monotonicSeconds, a_hasStateFlags);
		if (events.empty()) {
			return {};
		}
		return EventFrames(a_peers(), events);
	}

	std::vector<OutboundFrame> DownedService::AdminRevive(const Peers& a_peers, std::uint32_t a_playerId, double a_monotonicSeconds)
	{
		return EventFrames(a_peers, downedStates.AdminRevive(a_playerId, a_monotonicSeconds));
	}

	std::vector<OutboundFrame> DownedService::AdminKill(const Peers& a_peers, std::uint32_t a_playerId)
	{
		return EventFrames(a_peers, downedStates.AdminKill(a_playerId));
	}

	std::optional<std::vector<OutboundFrame>> DownedService::Dispatch(
		std::string_view a_type,
		const Peers& a_peers,
		std::uint32_t a_playerId,
		DownedSessionState& a_state,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_maximumPositionAgeSeconds)
	{
		if (a_type == "reviveRequest") {
			return HandleReviveRequest(a_peers, a_playerId, a_packet, a_monotonicSeconds, a_maximumPositionAgeSeconds);
		}
		if (a_type == "respawnRequest") {
			return HandleRespawnRequest(a_peers, a_playerId, a_state, a_packet, a_monotonicSeconds);
		}
		if (a_type == "reviveCancel") {
			return HandleReviveCancel(a_peers, a_playerId, a_packet, a_monotonicSeconds);
		}
		if (a_type == "checkpoint") {
			return HandleCheckpoint(a_peers, a_playerId, a_packet, a_monotonicSeconds, a_maximumPositionAgeSeconds);
		}
		return std::nullopt;
	}

	std::vector<OutboundFrame> DownedService::SnapshotFrames(std::uint32_t a_connectionId, double a_monotonicSeconds) const
	{
		std::vector<OutboundFrame> frames;
		for (const auto& event : downedStates.DownedSnapshot(a_monotonicSeconds)) {
			frames.push_back(ReliableFrame(a_connectionId, DownedStatePacket(event.playerId, true, event.remainingMs)));
		}
		return frames;
	}

	std::vector<OutboundFrame> DownedService::EventFrames(const Peers& a_peers, const std::vector<F4T::DownedState::Event>& a_events)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& event : a_events) {
			stats.downedEventsEmitted++;
			const auto* entry = FindPeer(a_peers, event.playerId);
			switch (event.kind) {
			case F4T::DownedState::EventKind::Downed:
				AppendFrames(frames, BroadcastReliable(a_peers, DownedStatePacket(event.playerId, true, event.remainingMs)));
				break;
			case F4T::DownedState::EventKind::Revived:
				if (entry != nullptr) {
					frames.push_back(ReliableFrame(entry->connectionId, Json{
						{ "type", "revived" },
						{ "byPlayerId", event.actorPlayerId },
						{ "healthFraction", event.healthFraction },
						{ "stimpaksSpent", event.stimpaksSpent } }));
				}
				AppendFrames(frames, BroadcastReliable(a_peers, DownedStatePacket(event.playerId, false, 0)));
				break;
			case F4T::DownedState::EventKind::Died:
				if (entry != nullptr) {
					frames.push_back(ReliableFrame(entry->connectionId, Json{
						{ "type", "died" },
						{ "playerId", event.playerId } }));
				}
				AppendFrames(frames, BroadcastReliable(a_peers, Json{
					{ "type", "playerDied" },
					{ "playerId", event.playerId } }));
				break;
			case F4T::DownedState::EventKind::RespawnGranted:
				if (entry != nullptr) {
					Json grant{
						{ "type", "respawnGrant" },
						{ "point", event.point }
					};
					if (event.friendPlayerId != 0) {
						grant["friendId"] = event.friendPlayerId;
					}
					if (event.checkpoint) {
						grant["checkpoint"] = Json{
							{ "interior", event.checkpoint->interior },
							{ "space", event.checkpoint->space },
							{ "x", event.checkpoint->x },
							{ "y", event.checkpoint->y },
							{ "z", event.checkpoint->z },
							{ "rotZ", event.checkpoint->rotZ }
						};
					}
					frames.push_back(ReliableFrame(entry->connectionId, grant));
				}
				break;
			case F4T::DownedState::EventKind::ReviveStarted:
				AppendFrames(frames, BroadcastReliable(a_peers, Json{
					{ "type", "reviveStarted" },
					{ "playerId", event.playerId },
					{ "byPlayerId", event.actorPlayerId },
					{ "channelMs", event.channelMs } }));
				break;
			case F4T::DownedState::EventKind::ReviveCancelled:
				AppendFrames(frames, BroadcastReliable(a_peers, Json{
					{ "type", "reviveCancelled" },
					{ "playerId", event.playerId },
					{ "byPlayerId", event.actorPlayerId },
					{ "reason", event.reason } }));
				break;
			case F4T::DownedState::EventKind::HeldItemDropped:
				if (entry != nullptr) {
					frames.push_back(ReliableFrame(entry->connectionId, Json{
						{ "type", "heldItemDropped" },
						{ "reason", event.reason } }));
				}
				break;
			}
		}
		return frames;
	}

	std::vector<OutboundFrame> DownedService::HandleReviveRequest(
		const Peers& a_peers,
		std::uint32_t a_playerId,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_maximumPositionAgeSeconds)
	{
		stats.reviveRequestsReceived++;
		const auto request = F4T::DownedValidation::NormalizeReviveRequest(a_packet);
		if (request.error) {
			stats.reviveRequestsRejected++;
			stats.packetsRejected++;
			return {};
		}
		const auto outcome = downedStates.Revive(
			a_playerId,
			request.targetPlayerId,
			ReviveDistanceMeters(a_peers, a_playerId, request.targetPlayerId, a_monotonicSeconds, a_maximumPositionAgeSeconds),
			a_monotonicSeconds,
			request.stimpaksAvailable);
		if (outcome.verdict != F4T::DownedState::ReviveVerdict::Accepted) {
			stats.reviveRequestsRejected++;
			return {};
		}
		return EventFrames(a_peers, outcome.events);
	}

	std::vector<OutboundFrame> DownedService::HandleRespawnRequest(
		const Peers& a_peers,
		std::uint32_t a_playerId,
		DownedSessionState& a_state,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		stats.respawnRequestsReceived++;
		if (!F4T::PacketRateLimit::TakeToken(
				a_state.respawnRequests,
				a_monotonicSeconds,
				kRespawnRequestRefillPerSecond,
				kRespawnRequestBurst)) {
			stats.respawnRequestsRejected++;
			return {};
		}
		const auto request = F4T::DownedValidation::NormalizeRespawnRequest(a_packet);
		if (request.error) {
			stats.respawnRequestsRejected++;
			stats.packetsRejected++;
			return {};
		}
		const auto outcome = downedStates.Respawn(a_playerId, request.point, request.friendPlayerId, a_monotonicSeconds);
		if (outcome.verdict != F4T::DownedState::RespawnVerdict::Accepted) {
			stats.respawnRequestsRejected++;
			return {};
		}
		return EventFrames(a_peers, outcome.events);
	}

	std::vector<OutboundFrame> DownedService::HandleReviveCancel(
		const Peers& a_peers,
		std::uint32_t a_playerId,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		stats.reviveCancelsReceived++;
		const auto request = F4T::DownedValidation::NormalizeReviveCancelRequest(a_packet);
		if (request.error) {
			stats.packetsRejected++;
			return {};
		}
		// Constraint: the cancelling reviver is the session player id, never the packet's.
		return EventFrames(a_peers, downedStates.CancelRevive(
			request.targetPlayerId,
			a_playerId,
			request.reason,
			a_monotonicSeconds));
	}

	std::vector<OutboundFrame> DownedService::HandleCheckpoint(
		const Peers& a_peers,
		std::uint32_t a_playerId,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_maximumPositionAgeSeconds)
	{
		stats.checkpointsReceived++;
		const auto request = F4T::DownedValidation::NormalizeCheckpointRequest(a_packet);
		if (request.error) {
			stats.packetsRejected++;
			stats.checkpointsRejected++;
			return {};
		}
		if (!CheckpointMatchesPosition(
				a_peers,
				a_playerId,
				request.checkpoint,
				a_monotonicSeconds,
				a_maximumPositionAgeSeconds)
			|| !downedStates.RecordCheckpoint(a_playerId, request.checkpoint)) {
			stats.checkpointsRejected++;
		}
		return {};
	}
}
