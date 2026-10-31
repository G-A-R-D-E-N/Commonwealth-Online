#include "F4TDownedState.h"
#include "F4TDownedStateTestSupport.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace F4TDownedStateTestSupport;

void RunDownedStateTrackerTests()
{
	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		tracker.Leave(1);
		assert(tracker.Expire(80.0).empty());
		assert(tracker.DownedSnapshot(80.0).empty());
		assert(!tracker.StateOf(1).has_value());
		const auto rejoined = tracker.Join(2, "profile-a", 81.0);
		assert(rejoined.size() == 1);
		assert(rejoined.front().kind == EventKind::Died);
		assert(StateOf(tracker, 2) == State::Dead);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 11.0).verdict == ReviveVerdict::Accepted);

		assert(tracker.ObserveVitals(1, true, false, 12.0).empty());
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.RemainingMs(1, 12.0) == 0);
		assert(tracker.DownedSnapshot(12.0).empty());
		assert(tracker.Expire(100.0).empty());
		assert(tracker.ObserveVitals(1, false, true, 12.5).empty());
		assert(StateOf(tracker, 1) == State::Respawning);

		assert(tracker.ObserveVitals(1, false, false, 13.0).empty());
		assert(StateOf(tracker, 1) == State::Alive);
		assert(CountKind(tracker.ObserveVitals(1, true, false, 14.0), EventKind::Downed) == 1);
		assert(tracker.RemainingMs(1, 14.0) == 60000);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.ObserveVitals(1, false, true, 10.5);
		assert(tracker.Respawn(1, "sanctuary", 0, 11.0).verdict == RespawnVerdict::Accepted);

		assert(tracker.ObserveVitals(1, false, true, 11.0).empty());
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.ObserveVitals(1, true, false, 11.5).empty());
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.Respawn(1, "sanctuary", 0, 12.2).verdict == RespawnVerdict::NotDead);

		assert(tracker.ObserveVitals(1, false, false, 12.0).empty());
		assert(StateOf(tracker, 1) == State::Alive);
		assert(CountKind(tracker.ObserveVitals(1, false, true, 13.0), EventKind::Died) == 1);
		assert(StateOf(tracker, 1) == State::Dead);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(!tracker.AdminRevive(1, 10.5).empty());
		assert(tracker.ObserveVitals(1, true, false, 11.0).empty());
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.Revive(2, 1, std::nullopt, 12.0).verdict == ReviveVerdict::TargetNotDowned);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::nullopt, 10.1).verdict == ReviveVerdict::OutOfRange);
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.RemainingMs(1, 10.1) == 59900);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 11.0).verdict == ReviveVerdict::Accepted);

		assert(tracker.ObserveVitals(1, true, false, 18.9).empty());
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.ObserveVitals(1, false, true, 18.9).empty());
		assert(StateOf(tracker, 1) == State::Respawning);

		const auto downed = tracker.ObserveVitals(1, true, false, 19.0);
		assert(CountKind(downed, EventKind::Downed) == 1);
		assert(StateOf(tracker, 1) == State::Downed);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, false, true, 10.5);
		assert(tracker.Respawn(1, "sanctuary", 0, 12.0).verdict == RespawnVerdict::Accepted);

		assert(tracker.ObserveVitals(1, false, true, 19.9).empty());
		assert(StateOf(tracker, 1) == State::Respawning);
		const auto died = tracker.ObserveVitals(1, false, true, 20.0);
		assert(CountKind(died, EventKind::Died) == 1);
		assert(StateOf(tracker, 1) == State::Dead);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(CountKind(tracker.AdminRevive(1, 10.5), EventKind::Revived) == 1);
		assert(tracker.ObserveVitals(1, false, false, 30.0).empty());
		assert(StateOf(tracker, 1) == State::Alive);

		tracker.ObserveVitals(1, true, false, 31.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 31.5).verdict == ReviveVerdict::Accepted);
		assert(tracker.ObserveVitals(1, false, false, 40.0, false).empty());
		assert(StateOf(tracker, 1) == State::Alive);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 11.0).verdict == ReviveVerdict::Accepted);

		assert(tracker.ObserveVitals(1, false, false, 12.0, false).empty());
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.ObserveVitals(1, false, false, 12.1).empty());
		assert(StateOf(tracker, 1) == State::Alive);
	}

	{
		Tracker tracker;
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.Join(3, "profile-c", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);

		const auto claimed = tracker.Revive(2, 1, std::optional{ 1.0 }, 10.5, std::optional{ 2U });
		assert(claimed.verdict == ReviveVerdict::Accepted);
		assert(claimed.events.size() == 1);
		assert(claimed.events.front().kind == EventKind::ReviveStarted);
		assert(claimed.events.front().playerId == 1);
		assert(claimed.events.front().actorPlayerId == 2);
		assert(claimed.events.front().channelMs == 3000);
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.ReviveChannelRemainingMs(1, 10.5) == 3000);

		const auto snapshot = tracker.DownedSnapshot(10.7);
		assert(snapshot.size() == 1);
		assert(snapshot.front().actorPlayerId == 2);
		assert(snapshot.front().channelMs == 2800);

		assert(tracker.Revive(3, 1, std::optional{ 1.0 }, 10.6, std::optional{ 1U }).verdict == ReviveVerdict::AlreadyBeingRevived);
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.ReviveChannelRemainingMs(1, 10.6) == 2900);

		assert(tracker.AdvanceChannels(13.4).empty());
		assert(StateOf(tracker, 1) == State::Downed);

		const auto completed = tracker.AdvanceChannels(13.5);
		assert(completed.size() == 1);
		assert(completed.front().kind == EventKind::Revived);
		assert(completed.front().playerId == 1);
		assert(completed.front().actorPlayerId == 2);
		assert(completed.front().stimpaksSpent == 1);
		assert(completed.front().healthFraction == 0.5);
		assert(tracker.ReviveChannelRemainingMs(1, 13.5) == 0);
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.AdvanceChannels(14.0).empty());
	}

	{
		Tracker tracker;
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 69.5, std::optional{ 1U }).verdict == ReviveVerdict::Accepted);
		assert(tracker.RemainingMs(1, 69.5) == 3000);
		assert(tracker.Expire(70.5).empty());
		assert(StateOf(tracker, 1) == State::Downed);
		assert(CountKind(tracker.Expire(73.0), EventKind::Revived) == 1);
		assert(StateOf(tracker, 1) == State::Respawning);
	}

	{
		Tracker tracker;
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.5, std::optional{ 0U }).verdict == ReviveVerdict::MissingStimpak);
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.ReviveChannelRemainingMs(1, 10.5) == 0);

		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.6, std::optional{ 1U }).verdict == ReviveVerdict::Accepted);
		const auto completed = tracker.AdvanceChannels(13.6);
		assert(completed.size() == 1);
		assert(completed.front().stimpaksSpent == 1);

		Tracker quiet;
		quiet.Join(1, "profile-a", 10.0);
		quiet.Join(2, "profile-b", 10.0);
		quiet.ObserveVitals(1, true, false, 10.0);
		assert(quiet.Revive(2, 1, std::optional{ 1.0 }, 10.5).verdict == ReviveVerdict::Accepted);
		const auto legacy = quiet.AdvanceChannels(13.5);
		assert(legacy.size() == 1);
		assert(legacy.front().stimpaksSpent == 0);
	}

	{
		Tracker tracker;
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.Join(3, "profile-c", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.5, std::optional{ 1U }).verdict == ReviveVerdict::Accepted);
		assert(tracker.CancelRevive(1, 3, "movedAway", 11.0).empty());
		assert(tracker.ReviveChannelRemainingMs(1, 11.0) == 2500);
		assert(tracker.CancelRevive(9, 2, "movedAway", 11.0).empty());

		const auto cancelled = tracker.CancelRevive(1, 2, "movedAway", 11.5);
		assert(cancelled.size() == 1);
		assert(cancelled.front().kind == EventKind::ReviveCancelled);
		assert(cancelled.front().playerId == 1);
		assert(cancelled.front().actorPlayerId == 2);
		assert(cancelled.front().reason == "movedAway");
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.ReviveChannelRemainingMs(1, 11.5) == 0);
		assert(tracker.AdvanceChannels(20.0).empty());

		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 12.0, std::optional{ 1U }).verdict == ReviveVerdict::Accepted);
		const auto unnamed = tracker.CancelRevive(1, 2, "", 12.5);
		assert(unnamed.size() == 1);
		assert(unnamed.front().reason == "cancelled");

		const auto bledOut = tracker.Expire(70.0);
		assert(CountKind(bledOut, EventKind::Died) == 1);
		assert(CountKind(bledOut, EventKind::HeldItemDropped) == 1);
	}

	{
		Tracker tracker;
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.5, std::optional{ 1U }).verdict == ReviveVerdict::Accepted);
		tracker.ObserveVitals(2, true, false, 11.0);
		const auto advanced = tracker.AdvanceChannels(20.0);
		assert(advanced.size() == 1);
		assert(advanced.front().kind == EventKind::ReviveCancelled);
		assert(advanced.front().playerId == 1);
		assert(advanced.front().reason == "reviverLeft");
		assert(StateOf(tracker, 1) == State::Downed);

		Tracker left;
		left.Join(1, "profile-a", 10.0);
		left.Join(2, "profile-b", 10.0);
		left.ObserveVitals(1, true, false, 10.0);
		assert(left.Revive(2, 1, std::optional{ 1.0 }, 10.5, std::optional{ 1U }).verdict == ReviveVerdict::Accepted);
		assert(left.ReviveChannelRemainingMs(1, 10.5) == 3000);
		left.Leave(2);
		assert(left.ReviveChannelRemainingMs(1, 10.6) == 0);
		assert(StateOf(left, 1) == State::Downed);
	}

	{
		Tracker tracker;
		tracker.Join(1, "profile-a", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Expire(69.9).empty());
		const auto bledOut = tracker.Expire(70.0);
		assert(bledOut.size() == 2);
		assert(bledOut.front().kind == EventKind::HeldItemDropped);
		assert(bledOut.front().playerId == 1);
		assert(bledOut.front().reason == "bledOut");
		assert(bledOut.back().kind == EventKind::Died);
		assert(StateOf(tracker, 1) == State::Dead);

		Config keepsItem;
		keepsItem.dropHeldOnBleedOut = false;
		Tracker noDrop{ keepsItem };
		noDrop.Join(1, "profile-a", 10.0);
		noDrop.ObserveVitals(1, true, false, 10.0);
		const auto quiet = noDrop.Expire(70.0);
		assert(quiet.size() == 1);
		assert(quiet.front().kind == EventKind::Died);

		Tracker reported;
		reported.Join(1, "profile-a", 10.0);
		const auto reportedDeath = reported.ObserveVitals(1, false, true, 11.0);
		assert(reportedDeath.size() == 1);
		assert(reportedDeath.front().kind == EventKind::Died);
	}

	{
		Tracker tracker;
		tracker.Join(1, "profile-a", 10.0);
		tracker.ObserveVitals(1, false, true, 10.5);
		assert(!tracker.CheckpointOf(1).has_value());
		assert(tracker.Respawn(1, "checkpoint", 0, 11.0).verdict == RespawnVerdict::NoCheckpoint);

		const F4T::DownedState::Checkpoint save{
			.interior = true,
			.space = 0x0001A254,
			.x = 10.0,
			.y = -20.0,
			.z = 30.0,
			.rotZ = 90.0
		};
		assert(tracker.RecordCheckpoint(1, save));
		const auto recorded = tracker.CheckpointOf(1);
		assert(recorded.has_value());
		assert(recorded->interior);
		assert(recorded->space == 0x0001A254);
		assert(recorded->x == 10.0);
		assert(recorded->y == -20.0);
		assert(recorded->z == 30.0);
		assert(recorded->rotZ == 90.0);

		assert(!tracker.RecordCheckpoint(1, F4T::DownedState::Checkpoint{ .space = 0 }));
		assert(!tracker.RecordCheckpoint(1, F4T::DownedState::Checkpoint{ .space = 0xFF000001U }));
		assert(!tracker.RecordCheckpoint(1, F4T::DownedState::Checkpoint{ .space = 1, .x = 5.0e8, .rotZ = 0.0 }));
		assert(!tracker.RecordCheckpoint(9, save));
		assert(tracker.CheckpointOf(1)->space == 0x0001A254);
		assert(!tracker.CheckpointOf(9).has_value());

		const auto granted = tracker.Respawn(1, "checkpoint", 0, 11.5);
		assert(granted.verdict == RespawnVerdict::Accepted);
		assert(granted.events.size() == 1);
		assert(granted.events.front().kind == EventKind::RespawnGranted);
		assert(granted.events.front().point == "checkpoint");
		assert(granted.events.front().checkpoint.has_value());
		assert(granted.events.front().checkpoint->interior);
		assert(granted.events.front().checkpoint->space == 0x0001A254);
		assert(granted.events.front().checkpoint->y == -20.0);
		assert(StateOf(tracker, 1) == State::Respawning);
	}
}

