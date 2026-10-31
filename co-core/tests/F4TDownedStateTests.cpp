#include "F4TDownedState.h"
#include "F4TDownedStateTestSupport.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace F4TDownedStateTestSupport;

void RunDownedStateTrackerTests();

int main()
{
	{
		Tracker tracker{ InstantConfig() };
		assert(tracker.GetConfig().downedSeconds == 60);
		assert(tracker.GetConfig().reviveHealthFraction == 0.5);
		assert(tracker.GetConfig().reviveRangeMeters == 3.0);

		tracker.Join(1, "profile-a", 10.0);
		assert(StateOf(tracker, 1) == State::Alive);
		assert(tracker.RemainingMs(1, 10.0) == 0);

		const auto downed = tracker.ObserveVitals(1, true, false, 10.0);
		assert(downed.size() == 1);
		assert(downed.front().kind == EventKind::Downed);
		assert(downed.front().playerId == 1);
		assert(downed.front().remainingMs == 60000);
		assert(StateOf(tracker, 1) == State::Downed);

		assert(tracker.ObserveVitals(1, true, false, 20.0).empty());
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.RemainingMs(1, 20.0) == 50000);

		assert(tracker.ObserveVitals(1, false, false, 30.0).empty());
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.RemainingMs(1, 30.0) == 40000);

		assert(tracker.ObserveVitals(1, false, true, 40.0).empty());
		assert(StateOf(tracker, 1) == State::Downed);

		const auto snapshot = tracker.DownedSnapshot(45.0);
		assert(snapshot.size() == 1);
		assert(snapshot.front().playerId == 1);
		assert(snapshot.front().remainingMs == 25000);

		assert(tracker.Expire(69.9).empty());
		assert(StateOf(tracker, 1) == State::Downed);
		const auto expired = tracker.Expire(70.0);
		assert(expired.size() == 2);
		assert(expired.front().kind == EventKind::HeldItemDropped);
		assert(expired.front().reason == "bledOut");
		assert(expired.back().kind == EventKind::Died);
		assert(expired.back().playerId == 1);
		assert(StateOf(tracker, 1) == State::Dead);
		assert(tracker.RemainingMs(1, 71.0) == 0);
		assert(tracker.DownedSnapshot(71.0).empty());
		assert(tracker.Expire(72.0).empty());
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		const auto died = tracker.ObserveVitals(1, false, true, 11.0);
		assert(died.size() == 1);
		assert(died.front().kind == EventKind::Died);
		assert(StateOf(tracker, 1) == State::Dead);
		assert(tracker.ObserveVitals(1, false, false, 12.0).empty());
		assert(StateOf(tracker, 1) == State::Dead);

		const auto respawn = tracker.Respawn(1, "sanctuary", 0, 12.0);
		assert(respawn.verdict == RespawnVerdict::Accepted);
		assert(respawn.events.size() == 1);
		assert(respawn.events.front().kind == EventKind::RespawnGranted);
		assert(respawn.events.front().point == "sanctuary");
		assert(respawn.events.front().friendPlayerId == 0);
		assert(StateOf(tracker, 1) == State::Respawning);

		assert(tracker.Respawn(1, "lastbed", 0, 12.5).verdict == RespawnVerdict::NotDead);
		assert(tracker.ObserveVitals(1, false, false, 13.0).empty());
		assert(StateOf(tracker, 1) == State::Alive);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, false, true, 10.5);
		assert(tracker.Respawn(1, "friend", 0, 10.5).verdict == RespawnVerdict::UnknownFriend);
		assert(tracker.Respawn(1, "friend", 1, 10.5).verdict == RespawnVerdict::UnknownFriend);
		assert(tracker.Respawn(1, "friend", 9, 10.5).verdict == RespawnVerdict::UnknownFriend);
		assert(tracker.Respawn(1, "somewhere", 0, 10.5).verdict == RespawnVerdict::InvalidPoint);
		assert(tracker.Respawn(1, "", 0, 10.5).verdict == RespawnVerdict::InvalidPoint);
		assert(tracker.Respawn(7, "sanctuary", 0, 10.5).verdict == RespawnVerdict::NotDead);

		tracker.ObserveVitals(2, true, false, 11.0);
		assert(tracker.Respawn(1, "friend", 2, 11.0).verdict == RespawnVerdict::UnknownFriend);
		tracker.AdminRevive(2, 11.0);
		const auto granted = tracker.Respawn(1, "friend", 2, 11.0);
		assert(granted.verdict == RespawnVerdict::Accepted);
		assert(granted.events.front().point == "friend");
		assert(granted.events.front().friendPlayerId == 2);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);

		const auto revived = tracker.Revive(2, 1, std::optional{ 1.0 }, 10.5);
		assert(revived.verdict == ReviveVerdict::Accepted);
		assert(revived.events.size() == 1);
		assert(revived.events.front().kind == EventKind::Revived);
		assert(revived.events.front().playerId == 1);
		assert(revived.events.front().actorPlayerId == 2);
		assert(revived.events.front().healthFraction == 0.5);
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.ObserveVitals(1, false, false, 10.55).empty());
		assert(StateOf(tracker, 1) == State::Alive);

		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.6).verdict == ReviveVerdict::TargetNotDowned);
		assert(tracker.Revive(1, 1, std::optional{ 1.0 }, 10.7).verdict == ReviveVerdict::SelfRevive);
		assert(tracker.Revive(2, 9, std::optional{ 1.0 }, 10.8).verdict == ReviveVerdict::UnknownTarget);
		assert(tracker.Revive(9, 2, std::optional{ 1.0 }, 10.9).verdict == ReviveVerdict::ReviverNotAlive);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.Join(3, "profile-c", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		tracker.ObserveVitals(3, true, false, 10.0);
		assert(tracker.Revive(1, 3, std::optional{ 1.0 }, 10.5).verdict == ReviveVerdict::ReviverNotAlive);
		assert(StateOf(tracker, 3) == State::Downed);
		assert(tracker.Revive(2, 3, std::optional{ 1.0 }, 10.6).verdict == ReviveVerdict::Accepted);
		assert(StateOf(tracker, 3) == State::Respawning);
		assert(tracker.Revive(1, 3, std::optional{ 1.0 }, 10.7).verdict == ReviveVerdict::ReviverNotAlive);

		Tracker downedReviver;
		downedReviver.Join(1, "profile-a", 10.0);
		downedReviver.Join(2, "profile-b", 10.0);
		downedReviver.ObserveVitals(1, true, false, 10.0);
		downedReviver.ObserveVitals(2, true, false, 10.0);
		assert(downedReviver.Revive(1, 2, std::optional{ 1.0 }, 10.5).verdict == ReviveVerdict::ReviverNotAlive);
		assert(downedReviver.Revive(2, 1, std::optional{ 1.0 }, 10.5).verdict == ReviveVerdict::ReviverNotAlive);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 2.9 }, 10.1).verdict == ReviveVerdict::Accepted);

		tracker.ObserveVitals(1, false, false, 15.0);
		tracker.ObserveVitals(1, true, false, 20.0);
		assert(tracker.Revive(2, 1, std::optional{ 3.1 }, 20.1).verdict == ReviveVerdict::OutOfRange);
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.Revive(2, 1, std::optional{ 3.0 }, 20.2).verdict == ReviveVerdict::Accepted);
		assert(StateOf(tracker, 1) == State::Respawning);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.1).verdict == ReviveVerdict::Accepted);
		tracker.ObserveVitals(1, false, false, 10.15);
		tracker.ObserveVitals(1, true, false, 10.2);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.3).verdict == ReviveVerdict::Accepted);
		tracker.ObserveVitals(1, false, false, 10.35);
		tracker.ObserveVitals(1, true, false, 10.4);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.5).verdict == ReviveVerdict::RateLimited);
		assert(StateOf(tracker, 1) == State::Downed);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.6).verdict == ReviveVerdict::RateLimited);
		tracker.ObserveVitals(1, true, false, 10.7);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 11.6).verdict == ReviveVerdict::Accepted);
		assert(StateOf(tracker, 1) == State::Respawning);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.Revive(2, 1, std::optional{ 100.0 }, 10.1).verdict == ReviveVerdict::OutOfRange);
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 10.2).verdict == ReviveVerdict::Accepted);

		tracker.ObserveVitals(1, false, false, 15.0);
		tracker.ObserveVitals(1, true, false, 20.0);
		Config ranged = tracker.GetConfig();
		ranged.reviveRangeMeters = 50.0;
		tracker.SetConfig(ranged);
		assert(tracker.Revive(2, 1, std::optional{ 40.0 }, 20.1).verdict == ReviveVerdict::Accepted);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);

		tracker.Leave(2);
		assert(!tracker.StateOf(2).has_value());
		assert(tracker.Revive(2, 1, std::optional{ 1.0 }, 11.0).verdict == ReviveVerdict::ReviverNotAlive);
		assert(tracker.Revive(1, 1, std::optional{ 1.0 }, 11.1).verdict == ReviveVerdict::SelfRevive);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		tracker.Leave(1);
		assert(!tracker.StateOf(1).has_value());
		assert(tracker.TrackedCount() == 1);

		const auto rejoined = tracker.Join(5, "profile-a", 30.0);
		assert(rejoined.size() == 1);
		assert(rejoined.front().kind == EventKind::Downed);
		assert(rejoined.front().playerId == 5);
		assert(rejoined.front().remainingMs == 40000);
		assert(StateOf(tracker, 5) == State::Downed);
		assert(tracker.TrackedCount() == 1);

		assert(tracker.Expire(69.0).empty());
		tracker.Leave(5);
		const auto late = tracker.Join(6, "profile-a", 80.0);
		assert(late.size() == 2);
		assert(late.front().kind == EventKind::HeldItemDropped);
		assert(late.back().kind == EventKind::Died);
		assert(late.back().playerId == 6);
		assert(StateOf(tracker, 6) == State::Dead);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.ObserveVitals(1, false, true, 11.0);
		tracker.Leave(1);
		const auto rejoined = tracker.Join(9, "profile-a", 60.0);
		assert(rejoined.size() == 1);
		assert(rejoined.front().kind == EventKind::Died);
		assert(StateOf(tracker, 9) == State::Dead);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Leave(1);
		assert(tracker.TrackedCount() == 0);
		const auto fresh = tracker.Join(4, "profile-a", 20.0);
		assert(fresh.empty());
		assert(StateOf(tracker, 4) == State::Alive);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "", 10.0);
		assert(StateOf(tracker, 1) == State::Alive);
		tracker.ObserveVitals(1, true, false, 10.0);
		tracker.Leave(1);
		assert(tracker.TrackedCount() == 1);
		const auto rejoined = tracker.Join(2, "", 15.0);
		assert(rejoined.empty());
		assert(StateOf(tracker, 2) == State::Alive);
		assert(tracker.TrackedCount() == 2);
	}

	{
		constexpr auto kMaximum = F4T::DownedState::MaximumTrackedPlayers;
		Tracker full;
		for (std::uint32_t id = 1; id <= kMaximum; ++id) {
			full.Join(id, "profile-" + std::to_string(id), 10.0);
		}
		assert(full.TrackedCount() == kMaximum);
		assert(CountKind(full.ObserveVitals(1, true, false, 10.0), EventKind::Downed) == 1);

		const auto refused = static_cast<std::uint32_t>(kMaximum) + 1;
		assert(full.Join(refused, "profile-refused", 11.0).empty());
		assert(full.StateOf(refused) == std::nullopt);
		assert(full.TrackedCount() == kMaximum);
		assert(full.ObserveVitals(refused, true, false, 11.0).empty());
		assert(full.StateOf(refused) == std::nullopt);
		assert(full.TrackedCount() == kMaximum);

		assert(StateOf(full, 1) == State::Downed);
		assert(full.RemainingMs(1, 11.0) == 59000);
		assert(full.DownedSnapshot(11.0).size() == 1);

		full.Leave(2);
		assert(full.TrackedCount() == kMaximum - 1);
		assert(full.Join(refused, "profile-refused", 12.0).empty());
		assert(StateOf(full, refused) == State::Alive);
		assert(full.TrackedCount() == kMaximum);

		Tracker withOffline;
		for (std::uint32_t id = 1; id <= kMaximum; ++id) {
			withOffline.Join(id, "profile-" + std::to_string(id), 10.0);
		}
		withOffline.ObserveVitals(1, true, false, 10.0);
		withOffline.Leave(1);
		assert(withOffline.TrackedCount() == kMaximum);
		assert(withOffline.Join(refused, "profile-refused", 11.0).empty());
		assert(StateOf(withOffline, refused) == State::Alive);
		assert(withOffline.TrackedCount() == kMaximum);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		assert(CountKind(tracker.AdminKill(1), EventKind::Died) == 1);
		assert(StateOf(tracker, 1) == State::Dead);
		assert(tracker.AdminKill(1).empty());
		assert(tracker.AdminKill(7).empty());

		const auto revivedDead = tracker.AdminRevive(1, 10.0);
		assert(CountKind(revivedDead, EventKind::Revived) == 1);
		const auto grant = FindKind(revivedDead, EventKind::RespawnGranted);
		assert(grant != nullptr);
		assert(grant->point == "lastbed");
		assert(grant->friendPlayerId == 0);
		assert(StateOf(tracker, 1) == State::Respawning);
		assert(tracker.AdminRevive(1, 10.1).empty());
		assert(tracker.AdminRevive(7, 10.2).empty());
		assert(tracker.RemainingMs(1, 12.0) == 0);
		assert(tracker.DownedSnapshot(12.0).empty());
		assert(tracker.ObserveVitals(1, false, false, 12.0).empty());
		assert(StateOf(tracker, 1) == State::Alive);

		tracker.ObserveVitals(2, true, false, 12.0);
		assert(CountKind(tracker.AdminRevive(2, 12.0), EventKind::Revived) == 1);
		assert(StateOf(tracker, 2) == State::Respawning);
		assert(tracker.ObserveVitals(2, true, false, 12.5).empty());
		assert(StateOf(tracker, 2) == State::Respawning);
		assert(tracker.ObserveVitals(2, false, false, 12.6).empty());
		assert(StateOf(tracker, 2) == State::Alive);

		assert(CountKind(tracker.AdminKill(2), EventKind::Died) == 1);
		assert(StateOf(tracker, 2) == State::Dead);
		const auto revivedDeadAgain = tracker.AdminRevive(2, 12.7);
		assert(FindKind(revivedDeadAgain, EventKind::Revived)->healthFraction == 0.5);
		assert(FindKind(revivedDeadAgain, EventKind::RespawnGranted) != nullptr);
		assert(StateOf(tracker, 2) == State::Respawning);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 0.0);
		tracker.ObserveVitals(1, true, false, 0.0);
		assert(tracker.RemainingMs(1, 0.0) == 60000);
		assert(tracker.RemainingMs(1, 59.9995) == 1);
		assert(tracker.RemainingMs(1, 59.999) == 1);
		assert(tracker.RemainingMs(1, 60.0) == 0);
		assert(tracker.RemainingMs(1, 1000.0) == 0);
	}

	{
		Tracker tracker{ InstantConfig() };
		tracker.Join(1, "profile-a", 10.0);
		tracker.ObserveVitals(1, true, false, 10.0);
		assert(tracker.StateOf(0) == std::nullopt);
		assert(tracker.StateOf(999) == std::nullopt);
		assert(tracker.ObserveVitals(0, true, true, 10.0).empty());
		assert(tracker.Join(0, "profile-zero", 10.0).empty());
		assert(tracker.Revive(0, 1, std::optional{ 1.0 }, 10.0).verdict == ReviveVerdict::ReviverNotAlive);
	}

	{
		Tracker tracker{ Config{ .downedSeconds = 15, .reviveHealthFraction = 0.25, .reviveRangeMeters = 10.0 } };
		tracker.Join(1, "profile-a", 10.0);
		tracker.Join(2, "profile-b", 10.0);
		const auto downed = tracker.ObserveVitals(1, true, false, 10.0);
		assert(downed.front().remainingMs == 15000);
		assert(tracker.Expire(24.0).empty());
		assert(CountKind(tracker.Expire(25.0), EventKind::Died) == 1);

		assert(F4T::DownedState::IsValidDownedSeconds(5));
		assert(F4T::DownedState::IsValidDownedSeconds(600));
		assert(!F4T::DownedState::IsValidDownedSeconds(4));
		assert(!F4T::DownedState::IsValidDownedSeconds(601));

		assert(F4T::DownedState::IsActive(State::Alive));
		assert(F4T::DownedState::IsActive(State::Respawning));
		assert(!F4T::DownedState::IsActive(State::Downed));
		assert(!F4T::DownedState::IsActive(State::Dead));

		assert(F4T::DownedState::IsValidRespawnPoint("sanctuary"));
		assert(F4T::DownedState::IsValidRespawnPoint("lastbed"));
		assert(F4T::DownedState::IsValidRespawnPoint("friend"));
		assert(F4T::DownedState::IsValidRespawnPoint("checkpoint"));
		assert(!F4T::DownedState::IsValidRespawnPoint("lastBed"));
		assert(!F4T::DownedState::IsValidRespawnPoint(""));

		assert(tracker.GetConfig().reviveChannelSeconds == 3.0);
		assert(tracker.GetConfig().dropHeldOnBleedOut);
		assert(F4T::DownedState::IsValidReviveChannelSeconds(0.0));
		assert(F4T::DownedState::IsValidReviveChannelSeconds(15.0));
		assert(!F4T::DownedState::IsValidReviveChannelSeconds(-1.0));
		assert(!F4T::DownedState::IsValidReviveChannelSeconds(15.5));

		assert(F4T::DownedState::ReviveVerdictReason(ReviveVerdict::OutOfRange) == "outOfRange");
		assert(F4T::DownedState::ReviveVerdictReason(ReviveVerdict::AlreadyBeingRevived) == "alreadyBeingRevived");
		assert(F4T::DownedState::ReviveVerdictReason(ReviveVerdict::MissingStimpak) == "missingStimpak");
		assert(F4T::DownedState::RespawnVerdictReason(RespawnVerdict::InvalidPoint) == "invalidPoint");
		assert(F4T::DownedState::RespawnVerdictReason(RespawnVerdict::NoCheckpoint) == "noCheckpoint");
	}

	RunDownedStateTrackerTests();

	return 0;
}

