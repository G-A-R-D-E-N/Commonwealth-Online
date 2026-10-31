#include "pch.h"

#include "F4TDownedGlueInternal.h"

namespace
{
	using namespace F4T::Downed::GlueInternal;
	using namespace F4T::Downed::Engine;
}

namespace F4T::Downed::GlueInternal
{
	void QueueEvent(DownedEvent a_event)
	{
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
		auto* player = LocalPlayer();
		const auto* health = HealthInfo();
		if (player && health && CanTouchWorld()) {
			(void)SetHealth(*player, *health, static_cast<float>(a_event.healthFraction) * GetMaximumHealth(player, *health));
		}
		LeaveIncapacitated(false);
		HideOverlay();
		HideRespawnMenu();
		g_pendingRespawnPoint.reset();
		g_pendingRespawnFriendId.reset();
		F4T::DebugLogging::Log(
			kLogComponent,
			"local player revived by playerId={} at healthFraction={:.2f}",
			a_event.playerId,
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
				"Could not drop the held item on bleed-out.");
		}
	}

	void ApplyPendingEvents(double a_nowSeconds)
	{
		auto events = DrainEvents();
		if (!F4T::Downed::ShouldApplyPendingEvents(CanTouchWorld(), !events.empty())) {
			RestoreEvents(events);
			return;
		}
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
			}
		}
	}
}

