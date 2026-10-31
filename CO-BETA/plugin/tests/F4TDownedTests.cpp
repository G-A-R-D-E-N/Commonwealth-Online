#include "F4TDownedState.h"
#include "F4TDownedWire.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
	void TestDamageResolution()
	{
		using namespace F4T::Downed;

		const Config config{};
		const auto nonLethal = ResolveLethalDamage(50.0F, 100.0F, 20.0F, config);
		assert(nonLethal.appliedDamage == 20.0F);
		assert(!nonLethal.enteredDowned);

		const auto lethal = ResolveLethalDamage(10.0F, 100.0F, 50.0F, config);
		assert(lethal.enteredDowned);
		assert(std::fabs(lethal.appliedDamage - 5.0F) < 0.001F);

		const auto alreadyDown = ResolveLethalDamage(0.0F, 100.0F, 5.0F, config);
		assert(alreadyDown.enteredDowned);
		assert(alreadyDown.appliedDamage == 0.0F);

		const auto noDamage = ResolveLethalDamage(50.0F, 100.0F, 0.0F, config);
		assert(!noDamage.enteredDowned);
		assert(noDamage.appliedDamage == 0.0F);

		const auto notANumber = ResolveLethalDamage(50.0F, 100.0F, std::numeric_limits<float>::quiet_NaN(), config);
		assert(!notANumber.enteredDowned);
		assert(notANumber.appliedDamage == 0.0F);
	}

	void TestSliverHealth()
	{
		using namespace F4T::Downed;

		const Config config{};
		assert(ComputeSliverHealth(100.0F, config) == 5.0F);
		assert(ComputeSliverHealth(10.0F, config) == 1.0F);
		assert(ComputeSliverHealth(0.0F, config) == 1.0F);
		assert(ComputeSliverHealth(-5.0F, config) == 1.0F);

		Config withoutMinimum = config;
		withoutMinimum.minimumSliverHealth = 0.0F;
		assert(ComputeSliverHealth(0.0F, withoutMinimum) == 1.0F);

		Config hugeFraction = config;
		hugeFraction.sliverFraction = 0.9;
		assert(ComputeSliverHealth(100.0F, hugeFraction) == 50.0F);
	}

	void TestLocalDownedTimer()
	{
		using namespace F4T::Downed;

		State state;
		assert(state.GetPhase() == Phase::kAlive);
		assert(!state.IsIncapacitated());
		assert(state.EnterDowned(1000.0));
		assert(!state.EnterDowned(1001.0));
		assert(state.GetPhase() == Phase::kDowned);
		assert(state.IsIncapacitated());
		assert(state.GetDownCount() == 1);
		assert(!state.HasServerDeadline());
		assert(state.GetRemainingMs(1000.0) == 60000);
		assert(state.GetRemainingMs(1030.0) == 30000);
		assert(!state.Tick(1059.999));
		assert(state.Tick(1060.0));
		assert(state.GetPhase() == Phase::kDead);
		assert(state.GetRemainingMs(1060.0) == 0);
		assert(!state.Tick(1061.0));
	}

	void TestServerDeadline()
	{
		using namespace F4T::Downed;

		State state;
		assert(state.EnterDowned(0.0));
		assert(!state.ApplyServerDownedState(true, 5000, 10.0));
		assert(state.HasServerDeadline());
		assert(state.GetRemainingMs(10.0) == 5000);
		assert(state.GetRemainingMs(12.0) == 3000);
		assert(!state.Tick(14.999));
		assert(state.Tick(15.0));
		assert(state.GetPhase() == Phase::kDead);

		State fresh;
		assert(fresh.ApplyServerDownedState(true, 3000, 0.0));
		assert(fresh.GetPhase() == Phase::kDowned);
		assert(fresh.GetRemainingMs(1.0) == 2000);
		assert(!fresh.ApplyServerDownedState(false, 0, 1.0));
		assert(!fresh.ApplyServerDownedState(true, 1000, std::numeric_limits<double>::quiet_NaN()));

		State clamped;
		assert(clamped.ApplyServerDownedState(true, kMaximumServerRemainingMs + 1, 0.0));
		assert(clamped.GetRemainingMs(0.0) == kMaximumServerRemainingMs);
	}

	void TestReviveDiedRespawn()
	{
		using namespace F4T::Downed;

		State state;
		assert(!state.ApplyRevived());
		assert(state.EnterDowned(0.0));
		assert(state.ApplyRevived());
		assert(state.GetPhase() == Phase::kAlive);
		assert(!state.ApplyRevived());
		assert(state.ApplyDied());
		assert(state.GetPhase() == Phase::kDead);
		assert(!state.ApplyDied());
		assert(!state.EnterDowned(1.0));
		assert(state.ApplyRespawnGrant());
		assert(state.GetPhase() == Phase::kAlive);
		assert(state.GetDownCount() == 1);
		assert(!state.Reset());

		State downed;
		assert(downed.EnterDowned(5.0));
		assert(downed.Reset());
		assert(downed.GetPhase() == Phase::kAlive);
		assert(!downed.HasServerDeadline());
		assert(downed.GetRemainingMs(5.0) == 0);
	}

	void TestReviveTargetSelection()
	{
		using namespace F4T::Downed;

		const Config config{};
		const std::vector<ReviveCandidate> candidates{
			{ 7, 50.0F, 0.9F },
			{ 9, 20.0F, 0.8F },
			{ 11, 10.0F, 0.1F },
			{ 13, 500.0F, 0.9F },
			{ 0, 5.0F, 0.9F }
		};
		const auto target = SelectReviveTarget(candidates, config);
		assert(target.has_value());
		assert(*target == 9);
		assert(!SelectReviveTarget(std::span<const ReviveCandidate>{}, config).has_value());

		const std::vector<ReviveCandidate> notANumber{ { 21, std::numeric_limits<float>::quiet_NaN(), 0.9F } };
		assert(!SelectReviveTarget(notANumber, config).has_value());
	}

	void TestReviveHold()
	{
		using namespace F4T::Downed;

		const Config config{};
		ReviveHold hold;
		assert(!AdvanceReviveHold(hold, 42, true, 0.0, config));
		assert(hold.armed);
		assert(hold.target.value() == 42U);
		assert(!AdvanceReviveHold(hold, 42, true, 1.0, config));
		assert(AdvanceReviveHold(hold, 42, true, 1.5, config));
		assert(!hold.armed);
		assert(!AdvanceReviveHold(hold, 42, false, 2.0, config));
		assert(!AdvanceReviveHold(hold, 43, true, 3.0, config));
		assert(!AdvanceReviveHold(hold, 44, true, 4.0, config));
		assert(hold.target.value() == 44U);
		assert(!AdvanceReviveHold(hold, std::nullopt, true, 5.0, config));
		assert(!hold.armed);
	}

	void TestRespawnPointNames()
	{
		using namespace F4T::Downed;

		assert(ParseRespawnPoint(GetRespawnPointName(RespawnPoint::kSanctuary)) == RespawnPoint::kSanctuary);
		assert(ParseRespawnPoint(GetRespawnPointName(RespawnPoint::kLastBed)) == RespawnPoint::kLastBed);
		assert(ParseRespawnPoint(GetRespawnPointName(RespawnPoint::kFriend)) == RespawnPoint::kFriend);
		assert(!ParseRespawnPoint("nowhere").has_value());
		assert(!ParseRespawnPoint("").has_value());
	}

	void TestDownedStateWire()
	{
		using namespace F4T::Downed;

		const auto downed = DecodeDownedState({
			{ "type", kDownedStateType },
			{ "playerId", 5U },
			{ "downed", true },
			{ "remainingMs", 42000 }
		});
		assert(downed.has_value());
		assert(downed->playerId == 5U);
		assert(downed->downed);
		assert(downed->remainingMs == 42000);

		const auto alive = DecodeDownedState({ { "playerId", 5U }, { "downed", false }, { "remainingMs", 0 } });
		assert(alive.has_value());
		assert(!alive->downed);

		assert(!DecodeDownedState({ { "playerId", 5U }, { "downed", true } }).has_value());
		assert(!DecodeDownedState({ { "playerId", 5U }, { "downed", true }, { "remainingMs", -1 } }).has_value());
		assert(!DecodeDownedState({
			{ "playerId", 5U },
			{ "downed", true },
			{ "remainingMs", kMaximumServerRemainingMs + 1 }
		}).has_value());
		assert(!DecodeDownedState({ { "playerId", 0U }, { "downed", true }, { "remainingMs", 1 } }).has_value());
		assert(!DecodeDownedState({ { "playerId", 5U }, { "downed", 1 }, { "remainingMs", 1 } }).has_value());
	}

	void TestRevivedWire()
	{
		using namespace F4T::Downed;

		const auto revived = DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", 0.5 } });
		assert(revived.has_value());
		assert(revived->byPlayerId == 3U);
		assert(std::fabs(revived->healthFraction - 0.5) < 1e-9);

		assert(!DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", 1.5 } }).has_value());
		assert(!DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", -0.1 } }).has_value());
		assert(!DecodeRevived({
			{ "byPlayerId", 3U },
			{ "healthFraction", std::numeric_limits<double>::quiet_NaN() }
		}).has_value());
		assert(!DecodeRevived({ { "byPlayerId", 3U } }).has_value());
		assert(!DecodeRevived({ { "byPlayerId", 3 }, { "healthFraction", 0.5 } }).has_value());
	}

	void TestDiedAndRespawnGrantWire()
	{
		using namespace F4T::Downed;

		assert(IsDiedPacket({ { "type", "died" } }));
		assert(!IsDiedPacket({ { "type", "revived" } }));
		assert(!IsDiedPacket({ { "playerId", 1U } }));

		const auto died = DecodePlayerDied({ { "type", "playerDied" }, { "playerId", 8U } });
		assert(died.has_value());
		assert(died->playerId == 8U);
		assert(!DecodePlayerDied({ { "type", "playerDied" } }).has_value());
		assert(!DecodePlayerDied({ { "type", "playerDied" }, { "playerId", 0U } }).has_value());

		const auto sanctuary = DecodeRespawnGrant({ { "type", "respawnGrant" }, { "point", "sanctuary" } });
		assert(sanctuary.has_value());
		assert(sanctuary->point == RespawnPoint::kSanctuary);
		assert(!sanctuary->friendId.has_value());

		assert(!DecodeRespawnGrant({ { "type", "respawnGrant" }, { "point", "friend" } }).has_value());
		const auto friendGrant = DecodeRespawnGrant({
			{ "type", "respawnGrant" },
			{ "point", "friend" },
			{ "friendId", 4U }
		});
		assert(friendGrant.has_value());
		assert(friendGrant->point == RespawnPoint::kFriend);
		assert(friendGrant->friendId.value() == 4U);
		assert(!DecodeRespawnGrant({ { "type", "respawnGrant" }, { "point", "elsewhere" } }).has_value());
	}

	void TestRequestRoundTrip()
	{
		using namespace F4T::Downed;

		assert(!EncodeReviveRequest(0).has_value());
		const auto revive = EncodeReviveRequest(12);
		assert(revive.has_value());
		assert(revive->at("type").get<std::string>() == kReviveRequestType);
		assert(revive->at("target") == 12U);

		assert(!EncodeRespawnRequest(RespawnPoint::kFriend, std::nullopt).has_value());
		assert(!EncodeRespawnRequest(RespawnPoint::kFriend, std::optional<std::uint32_t>{ 0 }).has_value());

		const auto sanctuaryRequest = EncodeRespawnRequest(RespawnPoint::kSanctuary, std::nullopt);
		assert(sanctuaryRequest.has_value());
		assert(sanctuaryRequest->at("type").get<std::string>() == kRespawnRequestType);
		const auto sanctuaryGrant = DecodeRespawnGrant(*sanctuaryRequest);
		assert(sanctuaryGrant.has_value());
		assert(sanctuaryGrant->point == RespawnPoint::kSanctuary);
		assert(!sanctuaryGrant->friendId.has_value());

		const auto friendRequest = EncodeRespawnRequest(RespawnPoint::kFriend, std::optional<std::uint32_t>{ 9 });
		assert(friendRequest.has_value());
		const auto friendGrant = DecodeRespawnGrant(*friendRequest);
		assert(friendGrant.has_value());
		assert(friendGrant->point == RespawnPoint::kFriend);
		assert(friendGrant->friendId.value() == 9U);
	}
	void TestRemotePoseApplyPolicy()
	{
		using namespace F4T::Downed;

		const RemotePoseEntry fresh{};
		assert(!ShouldApplyRemotePose(fresh, false, 0.0));
		assert(!ShouldApplyRemotePose(fresh, false, 1000.0));
		assert(ShouldApplyRemotePose(fresh, true, 0.0));

		const RemotePoseEntry applied{ true, false, 0.0 };
		assert(!ShouldApplyRemotePose(applied, true, 1.0));
		assert(!ShouldApplyRemotePose(applied, true, 1000.0));
		assert(ShouldApplyRemotePose(applied, false, 1.0));

		const RemotePoseEntry retrying{ false, true, 10.0 };
		assert(!ShouldApplyRemotePose(retrying, true, 9.9));
		assert(ShouldApplyRemotePose(retrying, true, 10.0));
		assert(!ShouldApplyRemotePose(retrying, true, std::numeric_limits<double>::quiet_NaN()));
		assert(!ShouldApplyRemotePose(retrying, false, 9.9));
	}

	void TestReviveGating()
	{
		using namespace F4T::Downed;

		assert(CanReviveOthers(Phase::kAlive));
		assert(!CanReviveOthers(Phase::kDowned));
		assert(!CanReviveOthers(Phase::kDead));

		assert(IsReviveCandidate(true, true, false));
		assert(!IsReviveCandidate(true, true, true));
		assert(!IsReviveCandidate(true, false, false));
		assert(!IsReviveCandidate(false, true, false));
	}

	void TestOverlayPushPolicy()
	{
		using namespace F4T::Downed;

		assert(ShouldPushDownedOverlay(42000, false, std::nullopt, std::nullopt));
		assert(!ShouldPushDownedOverlay(42000, false, 42000, false));
		assert(!ShouldPushDownedOverlay(42500, false, 42000, false));
		assert(ShouldPushDownedOverlay(41000, false, 42000, false));
		assert(ShouldPushDownedOverlay(42000, true, 42000, false));
		assert(ShouldPushDownedOverlay(42000, false, std::nullopt, false));
		assert(ShouldPushDownedOverlay(42000, false, 42000, std::nullopt));
	}

	void TestPendingEventGate()
	{
		using namespace F4T::Downed;

		assert(!ShouldApplyPendingEvents(false, true));
		assert(!ShouldApplyPendingEvents(true, false));
		assert(ShouldApplyPendingEvents(true, true));
	}

	void TestRespawnRequestPolicy()
	{
		using namespace F4T::Downed;

		assert(IsRespawnRequestValid(RespawnPoint::kSanctuary, std::nullopt));
		assert(IsRespawnRequestValid(RespawnPoint::kLastBed, std::nullopt));
		assert(!IsRespawnRequestValid(RespawnPoint::kFriend, std::nullopt));
		assert(!IsRespawnRequestValid(RespawnPoint::kFriend, std::optional<std::uint32_t>{ 0 }));
		assert(IsRespawnRequestValid(RespawnPoint::kFriend, std::optional<std::uint32_t>{ 4 }));

		const Config config{};
		assert(ComputeRespawnRetryDelay(config.respawnRequestRetrySeconds, 0) == 2.0);
		assert(ComputeRespawnRetryDelay(config.respawnRequestRetrySeconds, 1) == 4.0);
		assert(ComputeRespawnRetryDelay(config.respawnRequestRetrySeconds, 2) == 8.0);
		assert(ComputeRespawnRetryDelay(config.respawnRequestRetrySeconds, 99) == 8.0);
		assert(ComputeRespawnRetryDelay(0.0, 0) == 2.0);

		std::uint32_t attempts = 0;
		while (ShouldRetryRespawnRequest(attempts)) {
			++attempts;
		}
		assert(attempts == kMaximumRespawnRequestRetries);
	}

	void TestServerDeadlineOverridesLocalFallback()
	{
		using namespace F4T::Downed;

		State state;
		assert(state.EnterDowned(0.0));
		assert(state.GetRemainingMs(0.0) == 60000);
		assert(!state.ApplyServerDownedState(true, 90000, 10.0));
		assert(state.GetRemainingMs(10.0) == 90000);
		assert(!state.Tick(99.9));
		assert(state.Tick(100.0));
		assert(state.GetPhase() == Phase::kDead);
	}

	void TestRecoveryAfterLocalDeadline()
	{
		using namespace F4T::Downed;

		State state;
		assert(state.EnterDowned(0.0));
		assert(state.Tick(60.0));
		assert(state.GetPhase() == Phase::kDead);
		assert(state.ApplyRevived());
		assert(state.GetPhase() == Phase::kAlive);
		assert(!state.HasServerDeadline());
		assert(state.GetRemainingMs(60.0) == 0);

		State granted;
		assert(granted.EnterDowned(0.0));
		assert(granted.Tick(60.0));
		assert(granted.GetPhase() == Phase::kDead);
		assert(granted.ApplyRespawnGrant());
		assert(granted.GetPhase() == Phase::kAlive);
		assert(!granted.ApplyRespawnGrant());
	}

	void TestRevivedHealthFractionClamp()
	{
		using namespace F4T::Downed;

		const auto zero = DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", 0.0 } });
		assert(zero.has_value());
		assert(zero->healthFraction == kMinimumRevivedHealthFraction);

		const auto sliver = DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", 0.04 } });
		assert(sliver.has_value());
		assert(sliver->healthFraction == kMinimumRevivedHealthFraction);

		const auto half = DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", 0.5 } });
		assert(half.has_value());
		assert(half->healthFraction == 0.5);

		const auto full = DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", 1.0 } });
		assert(full.has_value());
		assert(full->healthFraction == 1.0);

		assert(!DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", -0.1 } }).has_value());
	}
}

void RunDownedCheckpointAndChannelTests();

int main()
{
	TestDamageResolution();
	TestSliverHealth();
	TestLocalDownedTimer();
	TestServerDeadline();
	TestReviveDiedRespawn();
	TestReviveTargetSelection();
	TestReviveHold();
	TestRespawnPointNames();
	TestDownedStateWire();
	TestRevivedWire();
	TestDiedAndRespawnGrantWire();
	TestRequestRoundTrip();
	TestRemotePoseApplyPolicy();
	TestReviveGating();
	TestOverlayPushPolicy();
	TestPendingEventGate();
	TestRespawnRequestPolicy();
	TestServerDeadlineOverridesLocalFallback();
	TestRecoveryAfterLocalDeadline();
	TestRevivedHealthFractionClamp();
	RunDownedCheckpointAndChannelTests();
	return 0;
}
