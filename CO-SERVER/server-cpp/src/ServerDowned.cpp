#include "ServerDowned.h"
#include "ServerEvents.h"
#include "ServerOutboundFrames.h"

#include "F4TDownedValidation.h"
#include "F4TDeathBagLocalPlan.h"

#include <algorithm>
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

		std::vector<OutboundFrame> BroadcastReliable(const DownedService::Peers& a_peers, const Json& a_packet, std::uint32_t a_subjectId)
		{
			bool suppressed = false;
			for (const auto& peer : a_peers) {
				if (peer.playerId == a_subjectId && peer.visibilitySuppressed) suppressed = true;
			}
			std::vector<OutboundFrame> frames;
			for (const auto& peer : a_peers) {
				if (!suppressed || peer.playerId == a_subjectId) {
					frames.push_back(ReliableFrame(peer.connectionId, a_packet));
				}
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

	std::optional<DownedService::Json> DownedService::AuthoritativeState(std::uint32_t a_playerId)
	{
		const auto state = downedStates.StateOf(a_playerId);
		if (!state) return std::nullopt;
		const bool dead = *state == F4T::DownedState::State::Dead;
		const bool downed = *state == F4T::DownedState::State::Downed || dead;
		const auto flags = static_cast<std::uint8_t>((downed ? 1 : 0) | (dead ? 2 : 0));
		auto& revision = stateRevisions[a_playerId];
		if (revision.first == 0 || revision.second != flags) {
			revision = { ++nextStateRevision, flags };
		}
		return Json{ { "revision", revision.first }, { "downed", downed }, { "dead", dead } };
	}

	void DownedService::StampState(Json& a_packet, const Peers& a_peers, std::uint32_t a_playerId)
	{
		if (const auto* peer = FindPeer(a_peers, a_playerId)) a_packet["playerIncarnation"] = peer->playerIncarnation;
		if (const auto state = AuthoritativeState(a_playerId)) a_packet["serverDowned"] = *state;
	}

	std::vector<OutboundFrame> DownedService::Poll(const Peers& a_peers, double a_monotonicSeconds)
	{
		std::vector<OutboundFrame> expiryFrames;
		if (expirePersisted && a_monotonicSeconds - lastDownedBroadcastMonotonic >= F4T::DownedState::BroadcastIntervalSeconds) expiryFrames = expirePersisted(a_peers);
		auto candidate = downedStates;
		const auto events = candidate.Expire(a_monotonicSeconds);
		if (recordDeath) {
			for (const auto& event : events) {
				if (event.kind != F4T::DownedState::EventKind::Died) continue;
				const auto* peer = FindPeer(a_peers, event.playerId);
				if (peer && !recordDeath(event.playerId, peer->persistenceProfile)) return expiryFrames;
			}
		}
		if (!RetireForEvents(a_peers, events)) return expiryFrames;
		downedStates = std::move(candidate);
		auto frames = std::move(expiryFrames);
		AppendFrames(frames, EventFrames(a_peers, events));
		if (a_monotonicSeconds - lastDownedBroadcastMonotonic < F4T::DownedState::BroadcastIntervalSeconds) {
			return frames;
		}
		lastDownedBroadcastMonotonic = a_monotonicSeconds;
		for (const auto& peer : a_peers) {
			AppendFrames(frames, SnapshotFrames(a_peers, peer.connectionId, a_monotonicSeconds));
		}
		return frames;
	}

	std::vector<OutboundFrame> DownedService::ResumeFrames(const Peers& a_peers, std::uint32_t a_playerId, double a_monotonicSeconds)
	{
		const auto state = downedStates.StateOf(a_playerId);
		if (!state) return {};
		std::uint32_t remaining = 0;
		if (*state == F4T::DownedState::State::Downed) {
			for (const auto& event : downedStates.DownedSnapshot(a_monotonicSeconds)) {
				if (event.playerId == a_playerId) remaining = event.remainingMs;
			}
		}
		auto packet = DownedStatePacket(a_playerId, *state == F4T::DownedState::State::Downed, remaining);
		StampState(packet, a_peers, a_playerId);
		auto frames = BroadcastReliable(a_peers, packet, a_playerId);
		if (*state == F4T::DownedState::State::Dead) {
			Json died{ { "type", "playerDied" }, { "playerId", a_playerId } };
			StampState(died, a_peers, a_playerId);
			AppendFrames(frames, BroadcastReliable(a_peers, died, a_playerId));
		}
		return frames;
	}

	std::vector<OutboundFrame> DownedService::Join(
		const Peers& a_peers,
		std::uint32_t a_connectionId,
		std::uint32_t a_playerId,
		std::string_view a_profileId,
		double a_monotonicSeconds,
		std::string_view a_characterId)
	{
		auto frames = EventFrames(a_peers, downedStates.Join(a_playerId, a_profileId, a_monotonicSeconds, a_characterId));
		AppendFrames(frames, SnapshotFrames(a_peers, a_connectionId, a_monotonicSeconds));
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
		auto candidate = downedStates;
		const auto events = candidate.AdminRevive(a_playerId, a_monotonicSeconds);
		if (!RetireForEvents(a_peers, events)) return {};
		downedStates = std::move(candidate);
		return EventFrames(a_peers, events);
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
		double a_maximumPositionAgeSeconds,
		bool a_clientReportsStimpaks)
	{
		if (a_type == "reviveRequest") {
			return HandleReviveRequest(a_peers, a_playerId, a_packet, a_monotonicSeconds, a_maximumPositionAgeSeconds, a_clientReportsStimpaks);
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

	std::vector<OutboundFrame> DownedService::SnapshotFrames(const Peers& a_peers, std::uint32_t a_connectionId, double a_monotonicSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& event : downedStates.DownedSnapshot(a_monotonicSeconds)) {
			const auto* subject = FindPeer(a_peers, event.playerId);
			if (subject && subject->visibilitySuppressed && subject->connectionId != a_connectionId) continue;
			auto packet = DownedStatePacket(event.playerId, true, event.remainingMs);
			StampState(packet, a_peers, event.playerId);
			frames.push_back(ReliableFrame(a_connectionId, packet));
		}
		return frames;
	}

	bool DownedService::RetireForEvents(const Peers& a_peers, const std::vector<F4T::DownedState::Event>& a_events)
	{
		if (!retireUntransferred) return true;
		for (const auto& event : a_events) {
			const bool retire = event.kind == F4T::DownedState::EventKind::Revived ||
				(event.kind == F4T::DownedState::EventKind::RespawnGranted && std::none_of(a_events.begin(), a_events.end(), [&](const auto& other) {
					return other.playerId == event.playerId && other.kind == F4T::DownedState::EventKind::Revived;
				}));
			if (!retire) continue;
			const auto* peer = FindPeer(a_peers, event.playerId);
			if (peer != nullptr && !peer->persistenceProfile.empty() && !retireUntransferred(peer->persistenceProfile)) return false;
		}
		return true;
	}

	std::vector<OutboundFrame> DownedService::EventFrames(const Peers& a_peers, const std::vector<F4T::DownedState::Event>& a_events)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& event : a_events) {
			stats.downedEventsEmitted++;
			const auto* entry = FindPeer(a_peers, event.playerId);
			const auto* actor = FindPeer(a_peers, event.actorPlayerId);
			const auto visibleActorId = actor && actor->visibilitySuppressed && actor->playerId != event.playerId ? 0 : event.actorPlayerId;
			switch (event.kind) {
			case F4T::DownedState::EventKind::Downed: {
				if (entry != nullptr && recordDowned) recordDowned(event.playerId, event.remainingMs);
				auto packet = DownedStatePacket(event.playerId, true, event.remainingMs);
				StampState(packet, a_peers, event.playerId);
				AppendFrames(frames, BroadcastReliable(a_peers, packet, event.playerId));
				break;
			}
			case F4T::DownedState::EventKind::Revived:
				if (entry != nullptr) {
					frames.push_back(ReliableFrame(entry->connectionId, Json{
						{ "type", "revived" },
						{ "byPlayerId", visibleActorId },
						{ "healthFraction", event.healthFraction },
						{ "stimpaksSpent", event.stimpaksSpent } }));
				}
				{
					auto packet = DownedStatePacket(event.playerId, false, 0);
					StampState(packet, a_peers, event.playerId);
					AppendFrames(frames, BroadcastReliable(a_peers, packet, event.playerId));
				}
				break;
			case F4T::DownedState::EventKind::Died:
				if (entry != nullptr && recordDeath) (void)recordDeath(event.playerId, entry->persistenceProfile);
				if (serverEvents != nullptr) serverEvents->Notify(ServerEvent{ ServerEventKind::PlayerDeath, entry != nullptr ? entry->connectionId : 0u, event.playerId, entry != nullptr ? entry->profileId : std::string{}, nullptr });
				if (entry != nullptr) {
					frames.push_back(ReliableFrame(entry->connectionId, Json{
						{ "type", "died" },
						{ "playerId", event.playerId } }));
					if (F4T::DeathBagLocal::Enabled && deathBagId) {
						if (const auto id = deathBagId(entry->persistenceProfile))
							frames.push_back(ReliableFrame(entry->connectionId, Json{ { "type", "deathBagCreate" }, { "deathId", *id } }));
					}
				}
				{
					Json packet{ { "type", "playerDied" }, { "playerId", event.playerId } };
					StampState(packet, a_peers, event.playerId);
					AppendFrames(frames, BroadcastReliable(a_peers, packet, event.playerId));
				}
				break;
			case F4T::DownedState::EventKind::RespawnGranted:
				if (std::none_of(a_events.begin(), a_events.end(), [&](const auto& other) {
					return other.playerId == event.playerId && other.kind == F4T::DownedState::EventKind::Revived;
				})) {
					auto packet = DownedStatePacket(event.playerId, false, 0);
					StampState(packet, a_peers, event.playerId);
					AppendFrames(frames, BroadcastReliable(a_peers, packet, event.playerId));
				}
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
				for (const auto& peer : a_peers) {
					if (entry && entry->visibilitySuppressed && peer.playerId != event.playerId) {
						continue;
					}
					frames.push_back(ReliableFrame(peer.connectionId, Json{
						{ "type", "reviveStarted" },
						{ "playerId", event.playerId },
						{ "byPlayerId", peer.playerId == event.actorPlayerId ? event.actorPlayerId : visibleActorId },
						{ "channelMs", event.channelMs },
						{ "stimpaksSpent", event.stimpaksSpent } }));
				}
				break;
			case F4T::DownedState::EventKind::ReviveCancelled:
				AppendFrames(frames, BroadcastReliable(a_peers, Json{
					{ "type", "reviveCancelled" },
					{ "playerId", event.playerId },
					{ "byPlayerId", visibleActorId },
					{ "reason", event.reason } }, event.playerId));
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
		double a_maximumPositionAgeSeconds,
		bool a_clientReportsStimpaks)
	{
		stats.reviveRequestsReceived++;
		const auto request = F4T::DownedValidation::NormalizeReviveRequest(a_packet);
		if (request.error) {
			stats.reviveRequestsRejected++;
			stats.packetsRejected++;
			return {};
		}
		auto candidate = downedStates;
		if (request.targetPlayerId == a_playerId) {
			const auto selfOutcome = candidate.SelfRevive(
				a_playerId,
				request.stimpaksAvailable,
				false,
				a_monotonicSeconds);
			if (selfOutcome.verdict != F4T::DownedState::ReviveVerdict::Accepted) {
				downedStates = std::move(candidate);
				stats.reviveRequestsRejected++;
				return {};
			}
			if (!RetireForEvents(a_peers, selfOutcome.events)) return {};
			downedStates = std::move(candidate);
			return EventFrames(a_peers, selfOutcome.events);
		}
		const auto outcome = candidate.Revive(
			a_playerId,
			request.targetPlayerId,
			ReviveDistanceMeters(a_peers, a_playerId, request.targetPlayerId, a_monotonicSeconds, a_maximumPositionAgeSeconds),
			a_monotonicSeconds,
			request.stimpaksAvailable,
			!a_clientReportsStimpaks);
		if (outcome.verdict != F4T::DownedState::ReviveVerdict::Accepted) {
			downedStates = std::move(candidate);
			stats.reviveRequestsRejected++;
			return {};
		}
		if (!RetireForEvents(a_peers, outcome.events)) return {};
		downedStates = std::move(candidate);
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
		if (request.point == "friend") {
			const auto* self = FindPeer(a_peers, a_playerId);
			const auto* friendPeer = FindPeer(a_peers, request.friendPlayerId);
			if (self == nullptr || friendPeer == nullptr || self->factionId.empty() || self->factionId != friendPeer->factionId) {
				stats.respawnRequestsRejected++;
				return {};
			}
		}
		auto candidate = downedStates;
		const auto outcome = candidate.Respawn(a_playerId, request.point, request.friendPlayerId, a_monotonicSeconds);
		if (outcome.verdict != F4T::DownedState::RespawnVerdict::Accepted) {
			downedStates = std::move(candidate);
			stats.respawnRequestsRejected++;
			return {};
		}
		if (!RetireForEvents(a_peers, outcome.events)) return {};
		downedStates = std::move(candidate);
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
