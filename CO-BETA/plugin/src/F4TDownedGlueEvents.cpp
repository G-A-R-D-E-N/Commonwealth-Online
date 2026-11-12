#include "pch.h"

#include "F4TDownedGlueInternal.h"
#include "F4TDeathBagLocal.h"
#include "F4TDeathBagView.h"
#include "F4TDeathBagGrant.h"
#include "F4TDeathBagGrantLedger.h"
#include "F4TDeathBagPlaceRecovery.h"
#include "F4TDeathBagPendingLimits.h"
#include "F4TFailClosedNotice.h"
#include <algorithm>
#include <map>
#include <optional>
#include <string_view>

namespace
{
	using namespace F4T::Downed::GlueInternal;
	using namespace F4T::Downed::Engine;

	struct PlacedUnacked
	{
		RE::ObjectRefHandle bag;
		double nextAt{};
		double delay{};
	};
	struct UnplacedRetry
	{
		double nextAt{};
		double delay{};
	};
	std::map<std::string, PlacedUnacked> g_placedUnacked;
	std::map<std::string, UnplacedRetry> g_unplacedUnacked;
	std::map<std::string, RE::ObjectRefHandle> g_unreturnedRejections;
	std::uint64_t g_placedGeneration{};
	std::uint64_t g_unplacedGeneration{};
	std::optional<std::uint64_t> g_resolvedLoadGeneration;
	constexpr double kPlacedRetryMaximumSeconds = 30.0;
	constexpr double kPlacedRetrySeconds = 2.0;
	double g_placeFailureRetryAt{};
	std::uint64_t g_placeFailureGeneration{};
	constexpr double kPlaceFailureRetrySeconds = 5.0;
	constexpr std::string_view kSnapshotRefusedNotice = "Commonwealth Online could not put your items into a death bag, so they stay with you.";
	constexpr std::string_view kPlaceFailedRestoredNotice = "Commonwealth Online could not place your death bag, so your items were returned to you.";
	constexpr std::string_view kPlaceFailedStuckNotice = "Commonwealth Online could not place your death bag. Some items remain in a container at your location.";
	constexpr std::string_view kRejectedReturnedNotice = "Commonwealth Online could not keep your death bag, so its items were returned to you.";
	constexpr std::string_view kRejectedStuckNotice = "Commonwealth Online could not keep your death bag. Some items remain in its container at your location.";

	bool ReturnRejectedPlacement(const std::string& a_deathId, RE::ObjectRefHandle a_bag)
	{
		auto* player = LocalPlayer();
		auto bag = a_bag.get();
		if (!player || !bag) {
			return false;
		}
		nlohmann::json remaining = nlohmann::json::array();
		const bool returned = F4T::DeathBagLocal::ReturnAll(*player, *bag, remaining);
		if (returned) {
			F4T::DeathBagView::Void(a_deathId, true);
		}
		F4T::DeathBagGrantLedger::RecordPlaceFailure(a_deathId, returned, remaining);
		F4T::DeathBagGrantLedger::ForgetPendingPlace(a_deathId);
		g_placeFailureRetryAt = 0.0;
		F4T::FailClosedNotice::Show(returned ? kRejectedReturnedNotice : kRejectedStuckNotice);
		return true;
	}

	void RetryUnreturnedRejections()
	{
		for (auto it = g_unreturnedRejections.begin(); it != g_unreturnedRejections.end();) {
			if (ReturnRejectedPlacement(it->first, it->second)) {
				it = g_unreturnedRejections.erase(it);
			} else {
				++it;
			}
		}
	}

	void ResendPlaced(double a_nowSeconds)
	{
		const auto generation = F4T::Networking::GetConnectionGeneration();
		if (generation != g_placedGeneration) {
			g_placedGeneration = generation;
			for (auto& entry : g_placedUnacked) {
				entry.second.nextAt = 0.0;
				entry.second.delay = kPlacedRetrySeconds;
			}
		}
		auto* transport = g_transport.load();
		if (!transport) {
			return;
		}
		for (auto& entry : g_placedUnacked) {
			auto& unacked = entry.second;
			if (a_nowSeconds < unacked.nextAt) {
				continue;
			}
			if (const auto packet = F4T::DeathBagWire::EncodePlaced(entry.first)) {
				(void)transport->Send(packet->dump());
			}
			unacked.nextAt = a_nowSeconds + unacked.delay;
			unacked.delay = (std::min)(unacked.delay * 2.0, kPlacedRetryMaximumSeconds);
		}
	}

	void ResendPlaceFailures(double a_nowSeconds)
	{
		const auto generation = F4T::Networking::GetConnectionGeneration();
		if (generation != g_placeFailureGeneration) {
			g_placeFailureGeneration = generation;
			g_placeFailureRetryAt = 0.0;
		}
		auto* transport = g_transport.load();
		if (!transport || a_nowSeconds < g_placeFailureRetryAt) {
			return;
		}
		g_placeFailureRetryAt = a_nowSeconds + kPlaceFailureRetrySeconds;
		for (const auto& failure : F4T::DeathBagGrantLedger::PlaceFailures()) {
			if (const auto packet = F4T::DeathBagWire::EncodePlaceFailed(failure.deathId, failure.restored, failure.remaining)) {
				(void)transport->Send(packet->dump());
			} else {
				F4T::Networking::Detail::LogThrottledWarning(
					"death_bag_place_failed_encode",
					"A death bag place-failure record cannot be encoded; it is kept and retried until it expires.");
			}
		}
	}

	void AnnouncePlaced(const std::string& a_deathId, RE::ObjectRefHandle a_bag, double a_nowSeconds)
	{
		if (const auto packet = F4T::DeathBagWire::EncodePlaced(a_deathId)) {
			if (auto* transport = g_transport.load()) (void)transport->Send(packet->dump());
			g_placedUnacked[a_deathId] = PlacedUnacked{ a_bag, a_nowSeconds + kPlacedRetrySeconds, kPlacedRetrySeconds };
		}
	}

	std::string UnplacedOperation(const std::string& a_deathId)
	{
		return "p" + a_deathId.substr(0, 15);
	}

	void ResendUnplaced(double a_nowSeconds)
	{
		const auto generation = F4T::Networking::GetConnectionGeneration();
		if (generation != g_unplacedGeneration) {
			g_unplacedGeneration = generation;
			for (auto& entry : g_unplacedUnacked) {
				entry.second.nextAt = 0.0;
				entry.second.delay = kPlacedRetrySeconds;
			}
		}
		auto* transport = g_transport.load();
		if (!transport || g_unplacedUnacked.empty()) {
			return;
		}
		const auto pending = F4T::DeathBagGrantLedger::PendingPlaces();
		for (auto it = g_unplacedUnacked.begin(); it != g_unplacedUnacked.end();) {
			const auto deathId = it->first;
			const bool stillPending = std::any_of(pending.begin(), pending.end(), [&deathId](const auto& entry) { return entry.deathId == deathId; });
			if (!stillPending) {
				it = g_unplacedUnacked.erase(it);
				continue;
			}
			auto& retry = it->second;
			if (a_nowSeconds >= retry.nextAt) {
				if (const auto packet = F4T::DeathBagWire::EncodeUnplaced(deathId, UnplacedOperation(deathId))) (void)transport->Send(packet->dump());
				retry.nextAt = a_nowSeconds + retry.delay;
				retry.delay = (std::min)(retry.delay * 2.0, kPlacedRetryMaximumSeconds);
			}
			++it;
		}
	}

	void ResolvePendingPlace(const F4T::DeathBagGrantLedger::PendingPlace& a_pending, double a_nowSeconds)
	{
		using Holding = F4T::DeathBagPlaceRecovery::Holding;
		using Resolution = F4T::DeathBagPlaceRecovery::Resolution;
		const auto deathId = a_pending.deathId;
		g_placedUnacked.erase(deathId);
		g_unplacedUnacked.erase(deathId);
		const auto failures = F4T::DeathBagGrantLedger::PlaceFailures();
		if (std::any_of(failures.begin(), failures.end(), [&deathId](const auto& failure) { return failure.deathId == deathId; })) {
			F4T::DeathBagGrantLedger::ForgetPendingPlace(deathId);
			return;
		}
		auto* player = LocalPlayer();
		const auto carried = player ? F4T::DeathBagLocal::CarriedStacks(*player) : std::nullopt;
		if (!carried) {
			REX::WARN("Death bag placement record kept after a reload: the carried inventory could not be read.");
			return;
		}
		const auto holding = F4T::DeathBagPlaceRecovery::Classify(a_pending.stacks, *carried);
		const bool exact = F4T::DeathBagPlaceRecovery::Exact(a_pending.stacks, *carried);
		const auto resolution = F4T::DeathBagPlaceRecovery::Resolve(a_pending.phase, holding, exact);
		if (resolution == Resolution::kRecordRestoredFailure) {
			F4T::DeathBagGrantLedger::RecordPlaceFailure(deathId, true, nlohmann::json::array());
			F4T::DeathBagGrantLedger::ForgetPendingPlace(deathId);
			g_placeFailureRetryAt = 0.0;
			return;
		}
		if (resolution == Resolution::kKeep) {
			if (holding == Holding::kFull) {
				REX::WARN("Death bag placement record kept after a reload: the carried items differ from the placed bag.");
			} else {
				REX::WARN("Death bag placement record kept after a reload: only part of the bag is carried.");
			}
			return;
		}
		if (resolution == Resolution::kReplace) {
			if (!F4T::DeathBagLocal::Snapshot(*player, deathId)) {
				REX::WARN("Death bag re-placement after a reload was refused; the placement record is kept.");
				return;
			}
			const auto replaced = F4T::DeathBagLocal::Place(*player, deathId);
			if (!replaced.bag.get()) {
				REX::WARN("Death bag re-placement after a reload failed; the placement record is kept.");
				return;
			}
			AnnouncePlaced(deathId, replaced.bag, a_nowSeconds);
			return;
		}
		if (resolution == Resolution::kResendPlaced) {
			g_placedUnacked[deathId] = PlacedUnacked{ {}, a_nowSeconds, kPlacedRetrySeconds };
			return;
		}
		g_unplacedUnacked[deathId] = UnplacedRetry{ a_nowSeconds, kPlacedRetrySeconds };
	}

	void ResolvePendingPlaces(double a_nowSeconds)
	{
		if (!CanTouchWorld()) {
			return;
		}
		const auto generation = F4T::DeathBagGrantLedger::LoadGeneration();
		if (g_resolvedLoadGeneration && *g_resolvedLoadGeneration == generation) {
			return;
		}
		g_resolvedLoadGeneration = generation;
		F4T::DeathBagPendingLimits::Prune(0);
		for (const auto& pending : F4T::DeathBagGrantLedger::PendingPlaces()) {
			try {
				ResolvePendingPlace(pending, a_nowSeconds);
			} catch (...) {
				REX::WARN("Death bag placement record kept after a reload: it could not be resolved.");
			}
		}
	}
}

namespace F4T::Downed::GlueInternal
{
	void QueueEvent(DownedEvent a_event)
	{
		if (a_event.kind == EventKind::kDeathBagHealthGrant) a_event.connectionGeneration = F4T::Networking::GetConnectionGeneration();
		const std::scoped_lock lock(g_pendingMutex);
		if (g_pendingEvents.size() >= kMaximumPendingEvents) {
			g_pendingEvents.pop_front();
		}
		g_pendingEvents.push_back(std::move(a_event));
	}

	std::deque<DownedEvent> DrainEvents()
	{
		std::deque<DownedEvent> events;
		const std::scoped_lock lock(g_pendingMutex);
		events.swap(g_pendingEvents);
		return events;
	}

	void RestoreEvents(std::deque<DownedEvent>& a_events)
	{
		const std::scoped_lock lock(g_pendingMutex);
		g_pendingEvents.insert(
			g_pendingEvents.begin(),
			std::make_move_iterator(a_events.begin()),
			std::make_move_iterator(a_events.end()));
		while (g_pendingEvents.size() > kMaximumPendingEvents) {
			g_pendingEvents.pop_front();
		}
	}

	void ApplyDownedStateEvent(const DownedEvent& a_event, double a_nowSeconds)
	{
		if (F4T::RemotePlayerState::IsAssignedPlayerId(a_event.playerId)) {
			if (g_state.ApplyServerDownedState(a_event.downed, a_event.remainingMs, a_nowSeconds)) {
				CancelBleedoutRecovery();
				LogInfo("downedState for the local player entered the downed phase.");
			}
			return;
		}
		F4T::RemotePlayerState::SetRemotePlayerDownedState(a_event.playerId, a_event.downed, false);
	}

	void ApplyRevivedEvent(const DownedEvent& a_event)
	{
		if (!g_state.ApplyRevived()) {
			return;
		}
		const auto localId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const bool selfRevive = localId && a_event.byPlayerId == *localId && a_event.stimpaksSpent > 0;
		g_selfReviveRequested.store(false);
		g_selfRevivePendingAt.reset();
		auto* player = LocalPlayer();
		if (player && CanTouchWorld() && player->IsDead(true)) {
			player->Resurrect(false, true);
			ReseatLocalPlayer();
			RequestWeaponReset();
			LogInfo("revived event resurrected the engine-dead local player.");
		}
		LogLocalBleedoutState("revived before recovery");
		const auto* health = HealthInfo();
		if (player && health && CanTouchWorld()) {
			(void)SetHealth(*player, *health, static_cast<float>(a_event.healthFraction) * GetMaximumHealth(player, *health));
		}
		if (selfRevive) {
			const auto spent = F4T::Downed::SpendStimpaks(a_event.stimpaksSpent, [] { return SpendStimpak(); });
			F4T::DebugLogging::Log(kLogComponent, "self revive spent {} of {} Stimpak(s)", spent, a_event.stimpaksSpent);
		}
		LeaveIncapacitated(false);
		HideOverlay();
		HideRespawnMenu();
		g_pendingRespawnPoint.reset();
		g_pendingRespawnFriendId.reset();
		F4T::DebugLogging::Log(
			kLogComponent,
			"local player revived by playerId={} at healthFraction={:.2f}",
			a_event.byPlayerId,
			a_event.healthFraction);
	}

	void ApplyDiedEvent(double a_nowSeconds)
	{
		if (!g_state.ApplyDied()) {
			return;
		}
		EnterDeadPhase(a_nowSeconds);
		ShowRespawnMenu();
	}

	void ApplyPlayerDiedEvent(const DownedEvent& a_event, double a_nowSeconds)
	{
		if (F4T::RemotePlayerState::IsAssignedPlayerId(a_event.playerId)) {
			ApplyDiedEvent(a_nowSeconds);
			return;
		}
		F4T::RemotePlayerState::SetRemotePlayerDownedState(a_event.playerId, true, true);
	}

	void ApplyHeldItemDroppedEvent(const DownedEvent& a_event)
	{
		auto* player = LocalPlayer();
		if (!player || !CanTouchWorld()) {
			return;
		}
		F4T::DebugLogging::Log(kLogComponent, "bleed-out drop requested: {}", a_event.reason);
		if (!DropHeldItem(*player)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_held_item_drop",
				"Held item was not dropped immediately on bleed-out (no weapon in hand, or the owned-drop gate is holding it).");
		}
	}

	void ApplyPendingEvents(double a_nowSeconds)
	{
		ResendPlaceFailures(a_nowSeconds);
		ResendPlaced(a_nowSeconds);
		ResendUnplaced(a_nowSeconds);
		ResolvePendingPlaces(a_nowSeconds);
		auto events = DrainEvents();
		if (!F4T::Downed::ShouldApplyPendingEvents(CanTouchWorld(), !events.empty())) {
			RestoreEvents(events);
			return;
		}
		RetryUnreturnedRejections();
		for (const auto& event : events) {
			switch (event.kind) {
			case EventKind::kDownedState:
				ApplyDownedStateEvent(event, a_nowSeconds);
				break;
			case EventKind::kRevived:
				ApplyRevivedEvent(event);
				break;
			case EventKind::kDied:
				ApplyDiedEvent(a_nowSeconds);
				break;
			case EventKind::kPlayerDied:
				ApplyPlayerDiedEvent(event, a_nowSeconds);
				break;
			case EventKind::kRespawnGrant:
				ApplyRespawnGrantEvent(event);
				break;
			case EventKind::kRespawnChoice:
				ApplyRespawnChoiceEvent(event);
				break;
			case EventKind::kReviveStarted:
				ApplyReviveStartedEvent(event, a_nowSeconds);
				break;
			case EventKind::kReviveCancelled:
				ApplyReviveCancelledEvent(event);
				break;
			case EventKind::kHeldItemDropped:
				ApplyHeldItemDroppedEvent(event);
				break;
			case EventKind::kDeathBagHealthGrant:
				if (auto* player = LocalPlayer()) {
					if (const auto grant = F4T::DeathBagWire::DecodeHealthGrant(event.deathBagGrant)) {
						(void)F4T::Networking::TryRunForConnectionGeneration(event.connectionGeneration, [&] {
							const auto outcome = F4T::DeathBagGrant::ApplyHealth(*player, *grant);
							if (outcome == F4T::DeathBagGrant::Outcome::Attempted || outcome == F4T::DeathBagGrant::Outcome::Duplicate) F4T::DeathBagGrant::AwaitSave(*grant);
							else F4T::DeathBagGrant::RetryFresh(*grant);
						});
					}
				}
				break;
			case EventKind::kDeathBagCreate: {
				if (F4T::DeathBagView::IsMirrored(event.deathId)) break;
				auto* player = LocalPlayer();
				auto* transport = g_transport.load();
				const auto snapshot = player && transport ? F4T::DeathBagLocal::Snapshot(*player, event.deathId) : std::nullopt;
				const auto packet = snapshot ? F4T::DeathBagWire::EncodeSnapshot(event.deathId, snapshot->stacks, snapshot->retained) : std::nullopt;
				if (!packet || !transport->Send(packet->dump())) {
					F4T::DeathBagLocal::Abandon(event.deathId);
					F4T::FailClosedNotice::Show(kSnapshotRefusedNotice);
				}
				break;
			}
			case EventKind::kDeathBagDepositResult: {
				auto* player = LocalPlayer();
				if (!event.deathBagCommitted || !player) {
					F4T::DeathBagLocal::Abandon(event.deathId);
					break;
				}
				F4T::DeathBagPendingLimits::Prune(1);
				if (!F4T::DeathBagGrantLedger::RecordPendingPlace(event.deathId, F4T::DeathBagLocal::PendingStacks(event.deathId)) ||
					!F4T::DeathBagGrantLedger::SetPendingPlacePhase(event.deathId, "placing")) {
					F4T::DeathBagGrantLedger::RecordPlaceFailure(event.deathId, true, nlohmann::json::array());
					g_placeFailureRetryAt = 0.0;
					F4T::DeathBagLocal::Abandon(event.deathId);
					F4T::FailClosedNotice::Show(kPlaceFailedRestoredNotice);
					break;
				}
				const auto placed = F4T::DeathBagLocal::Place(*player, event.deathId);
				if (!placed.bag.get()) {
					F4T::DeathBagGrantLedger::RecordPlaceFailure(event.deathId, placed.restored, placed.remaining);
					F4T::DeathBagGrantLedger::ForgetPendingPlace(event.deathId);
					g_placeFailureRetryAt = 0.0;
					F4T::DeathBagLocal::Abandon(event.deathId);
					F4T::FailClosedNotice::Show(placed.restored ? kPlaceFailedRestoredNotice : kPlaceFailedStuckNotice);
					break;
				}
				if (!F4T::DeathBagGrantLedger::SetPendingPlacePhase(event.deathId, "placed")) {
					(void)ReturnRejectedPlacement(event.deathId, placed.bag);
					break;
				}
				AnnouncePlaced(event.deathId, placed.bag, a_nowSeconds);
				break;
			}
			case EventKind::kDeathBagPlacedResult: {
				const auto unacked = g_placedUnacked.find(event.deathId);
				if (unacked == g_placedUnacked.end()) break;
				const auto bag = unacked->second.bag;
				g_placedUnacked.erase(unacked);
				F4T::DeathBagGrantLedger::ForgetPendingPlace(event.deathId);
				if (!event.deathBagPlaced) {
					REX::WARN("Death bag placement was rejected by the server.");
					if (!bag.get()) {
						REX::WARN("Rejected death bag placement has no handle after a reload; its items cannot be returned from here.");
						F4T::FailClosedNotice::Show(kRejectedStuckNotice);
						break;
					}
					if (!ReturnRejectedPlacement(event.deathId, bag)) g_unreturnedRejections.insert_or_assign(event.deathId, bag);
				}
				break;
			}
			case EventKind::kDeathBagVoided:
				g_placedUnacked.erase(event.deathId);
				F4T::DeathBagGrantLedger::ForgetPendingPlace(event.deathId);
				F4T::DeathBagLocal::Abandon(event.deathId);
				F4T::DeathBagView::Void(event.deathId);
				break;
			case EventKind::kDeathBagDiscovery:
				F4T::DeathBagView::NoteDiscovery(event.deathBagBags);
				break;
			}
		}
	}
}
