#include "F4TDownedState.h"
#include "F4TDownedWire.h"

#include <cassert>
#include <cmath>
#include <functional>
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
		assert(revived->stimpaksSpent == 0);

		const auto paid = DecodeRevived({ { "byPlayerId", 3U }, { "healthFraction", 0.5 }, { "stimpaksSpent", 1U } });
		assert(paid.has_value());
		assert(paid->stimpaksSpent == 1);
		assert(!DecodeRevived({
			{ "byPlayerId", 3U },
			{ "healthFraction", 0.5 },
			{ "stimpaksSpent", 100U }
		}).has_value());
		assert(!DecodeRevived({
			{ "byPlayerId", 3U },
			{ "healthFraction", 0.5 },
			{ "stimpaksSpent", -1 }
		}).has_value());

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

		F4T::Downed::DownedEvent bagEvent;
		const std::string deathId = "0123456789abcdef0123456789abcdef";
		assert(F4T::Downed::TryDecodeDownedEvent("deathBagCreate", { { "type", "deathBagCreate" }, { "deathId", deathId } }, bagEvent) == F4T::Downed::DecodeStatus::kHandled);
		assert(bagEvent.kind == F4T::Downed::EventKind::kDeathBagCreate && bagEvent.deathId == deathId);
		const auto healthGrant = nlohmann::json{ { "type", "deathBagHealthGrant" }, { "deathId", deathId }, { "operationId", "op" },
			{ "itemKey", "test.esm:00000002" }, { "count", 1u }, { "metadata", *F4T::ItemHealthSnapshot::Encode(0.5f) } };
		assert(F4T::Downed::TryDecodeDownedEvent("deathBagHealthGrant", healthGrant, bagEvent) == F4T::Downed::DecodeStatus::kHandled);
		assert(bagEvent.kind == F4T::Downed::EventKind::kDeathBagHealthGrant && bagEvent.deathBagGrant == healthGrant);
		assert(F4T::Downed::TryDecodeDownedEvent("deathBagCreate", { { "type", "deathBagCreate" }, { "deathId", "bad" } }, bagEvent) == F4T::Downed::DecodeStatus::kMalformed);
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

	void TestDeathBagWireDecodes()
	{
		using namespace F4T::Downed;

		DownedEvent decoded;
		const std::string deathId = "0123456789abcdef0123456789abcdef";
		assert(TryDecodeDownedEvent("deathBagDepositResult", { { "type", "deathBagDepositResult" }, { "deathId", deathId }, { "status", "committed" } }, decoded) == DecodeStatus::kHandled);
		assert(decoded.kind == EventKind::kDeathBagDepositResult && decoded.deathId == deathId && decoded.deathBagCommitted);
		assert(TryDecodeDownedEvent("deathBagDepositResult", { { "type", "deathBagDepositResult" }, { "deathId", deathId }, { "status", "rejected" } }, decoded) == DecodeStatus::kHandled);
		assert(decoded.kind == EventKind::kDeathBagDepositResult && !decoded.deathBagCommitted);
		assert(TryDecodeDownedEvent("deathBagDepositResult", { { "type", "deathBagDepositResult" }, { "deathId", deathId }, { "status", "maybe" } }, decoded) == DecodeStatus::kMalformed);
		assert(TryDecodeDownedEvent("deathBagVoided", { { "type", "deathBagVoided" }, { "deathId", deathId }, { "stacks", nlohmann::json::array() } }, decoded) == DecodeStatus::kHandled);
		assert(decoded.kind == EventKind::kDeathBagVoided && decoded.deathId == deathId);
		assert(TryDecodeDownedEvent("deathBagVoided", { { "type", "deathBagVoided" }, { "deathId", "bad" }, { "stacks", nlohmann::json::array() } }, decoded) == DecodeStatus::kMalformed);
		assert(TryDecodeDownedEvent("deathBagPlacedResult", { { "type", "deathBagPlacedResult" }, { "deathId", deathId }, { "status", "placed" } }, decoded) == DecodeStatus::kHandled);
		assert(decoded.kind == EventKind::kDeathBagPlacedResult && decoded.deathId == deathId && decoded.deathBagPlaced);
		assert(TryDecodeDownedEvent("deathBagPlacedResult", { { "type", "deathBagPlacedResult" }, { "deathId", deathId }, { "status", "rejected" } }, decoded) == DecodeStatus::kHandled);
		assert(decoded.kind == EventKind::kDeathBagPlacedResult && !decoded.deathBagPlaced);
		assert(TryDecodeDownedEvent("deathBagPlacedResult", { { "type", "deathBagPlacedResult" }, { "deathId", deathId }, { "status", "maybe" } }, decoded) == DecodeStatus::kMalformed);
		assert(TryDecodeDownedEvent("deathBagPlacedResult", { { "type", "deathBagPlacedResult" }, { "deathId", "bad" }, { "status", "placed" } }, decoded) == DecodeStatus::kMalformed);
		assert(TryDecodeDownedEvent("deathBagDiscovery", { { "type", "deathBagDiscovery" }, { "bags", nlohmann::json::array() } }, decoded) == DecodeStatus::kHandled);
		assert(decoded.kind == EventKind::kDeathBagDiscovery && decoded.deathBagBags.is_array());
	}

	void TestRequestRoundTrip()
	{
		using namespace F4T::Downed;

		assert(!EncodeReviveRequest(0).has_value());
		const auto revive = EncodeReviveRequest(12);
		assert(revive.has_value());
		assert(revive->at("type").get<std::string>() == kReviveRequestType);
		assert(revive->at("target") == 12U);

		assert(!ClampReportedStimpaks(std::nullopt).has_value());
		assert(ClampReportedStimpaks(std::optional<std::uint32_t>{ 7 }) == std::optional<std::uint32_t>{ 7 });
		assert(ClampReportedStimpaks(std::optional<std::uint32_t>{ kMaximumStimpaksReported }) == std::optional<std::uint32_t>{ kMaximumStimpaksReported });
		assert(ClampReportedStimpaks(std::optional<std::uint32_t>{ kMaximumStimpaksReported + 1 }) == std::optional<std::uint32_t>{ kMaximumStimpaksReported });
		const auto clampedRequest = EncodeReviveRequest(12, ClampReportedStimpaks(std::optional<std::uint32_t>{ 250 }));
		assert(clampedRequest.has_value());
		assert(clampedRequest->at("stimpaks").get<std::uint32_t>() == kMaximumStimpaksReported);

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

		assert(ShouldPushDownedOverlay(42000, false, std::nullopt, std::nullopt, 2U, std::nullopt));
		assert(!ShouldPushDownedOverlay(42000, false, 42000, false, 2U, std::optional<std::uint32_t>{ 2U }));
		assert(!ShouldPushDownedOverlay(42500, false, 42000, false, 2U, std::optional<std::uint32_t>{ 2U }));
		assert(ShouldPushDownedOverlay(41000, false, 42000, false, 2U, std::optional<std::uint32_t>{ 2U }));
		assert(ShouldPushDownedOverlay(42000, true, 42000, false, 2U, std::optional<std::uint32_t>{ 2U }));
		assert(ShouldPushDownedOverlay(42000, false, std::nullopt, false, 2U, std::optional<std::uint32_t>{ 2U }));
		assert(ShouldPushDownedOverlay(42000, false, 42000, std::nullopt, 2U, std::optional<std::uint32_t>{ 2U }));
		assert(ShouldPushDownedOverlay(42000, false, 42000, false, 3U, std::optional<std::uint32_t>{ 2U }));
		assert(ShouldPushDownedOverlay(42000, false, 42000, false, 2U, std::nullopt));
		assert(!ShouldPushDownedOverlay(42000, false, 42000, false, 0U, std::optional<std::uint32_t>{ 0U }));
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

	struct RecoveryRig
	{
		F4T::Downed::BleedoutRecoveryRunner runner;
		std::vector<bool> sent;
		bool accept{ true };
		double now{ 0.0 };
		std::function<void()> onSend;

		auto Sender()
		{
			return [this](bool a_noRecovery) {
				sent.push_back(a_noRecovery);
				if (onSend) {
					onSend();
				}
				return accept;
			};
		}

		F4T::Downed::RecoveryStep Tick(double a_now, bool a_canTouch, bool a_essentialDown, bool a_sessionApplied = true)
		{
			now = a_now;
			return runner.Update(a_now, a_canTouch, a_essentialDown, a_sessionApplied, Sender());
		}

		void Cancel(bool a_canSend)
		{
			runner.Cancel(now, a_canSend, Sender());
		}
	};

	void TestRecoveryThrottleAndEssentialCap()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.runner.Request();
		assert(rig.Tick(10.0, true, true) == RecoveryStep::kDispatched);
		assert(rig.Tick(10.5, true, true) == RecoveryStep::kWaiting);
		assert(rig.sent.size() == 1 && !rig.sent[0]);
		double now = 11.0;
		for (std::uint32_t i = 1; i < kMaximumRecoveryDispatches; ++i, now += 1.0) {
			assert(rig.Tick(now, true, true) == RecoveryStep::kDispatched);
		}
		assert(rig.sent.size() == kMaximumRecoveryDispatches);
		assert(rig.Tick(now, true, true) == RecoveryStep::kGaveUp);
		assert(rig.sent.size() == kMaximumRecoveryDispatches + 1 && rig.sent.back());
		assert(!rig.runner.IsPending() && !rig.runner.IsResendOwed());
		assert(rig.Tick(now + 5.0, true, true) == RecoveryStep::kIdle);
		assert(rig.sent.size() == kMaximumRecoveryDispatches + 1);
	}

	void TestRecoveryStandingCapAndRefusedResend()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.accept = false;
		rig.runner.Request();
		double now = 1.0;
		for (std::uint32_t i = 0; i < kMaximumRecoveryDispatches; ++i, now += 1.0) {
			assert(rig.Tick(now, true, false) == RecoveryStep::kWaiting);
		}
		assert(rig.sent.size() == kMaximumRecoveryDispatches);
		assert(rig.Tick(now, true, false) == RecoveryStep::kGaveUp);
		assert(!rig.runner.IsPending());
		const auto afterGiveUp = rig.sent.size();
		for (int i = 0; i < 100; ++i) {
			now += 1.0;
			static_cast<void>(rig.Tick(now, true, false));
		}
		assert(rig.sent.size() - afterGiveUp <= kMaximumRecoveryDispatches);
		assert(!rig.runner.IsResendOwed());
		const auto settled = rig.sent.size();
		static_cast<void>(rig.Tick(now + 10.0, true, false));
		assert(rig.sent.size() == settled);

		RecoveryRig stand;
		stand.runner.Request();
		assert(stand.Tick(1.0, true, false) == RecoveryStep::kStoodUp);
		assert(!stand.runner.IsPending());
		RecoveryRig ended;
		ended.runner.Request();
		assert(ended.Tick(1.0, true, false, false) == RecoveryStep::kStoodUp);
		assert(ended.sent.empty());
	}

	void TestRecoveryRedownAndRepeatRequest()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.runner.Request();
		assert(rig.Tick(1.0, true, true) == RecoveryStep::kDispatched);
		assert(rig.Tick(2.0, true, true) == RecoveryStep::kDispatched);
		assert(rig.runner.Attempts() == 2);
		rig.runner.Request();
		assert(rig.runner.Attempts() == 2);
		rig.Cancel(true);
		assert(!rig.runner.IsPending() && !rig.runner.IsResendOwed());
		assert(rig.sent.back());
		rig.runner.Request();
		assert(rig.runner.Attempts() == 0);
		assert(rig.Tick(3.0, true, true) == RecoveryStep::kDispatched);
		assert(rig.runner.Attempts() == 1);
	}

	void TestRecoveryHappyPathAndEssentialFlipReset()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.runner.Request();
		assert(rig.Tick(1.0, true, true) == RecoveryStep::kDispatched);
		assert(rig.Tick(2.0, true, true) == RecoveryStep::kDispatched);
		assert(rig.Tick(3.0, true, false) == RecoveryStep::kStoodUp);
		assert((rig.sent == std::vector<bool>{ false, false, true }));
		assert(!rig.runner.IsPending());

		RecoveryRig flip;
		flip.accept = false;
		flip.runner.Request();
		assert(flip.Tick(1.0, true, true) == RecoveryStep::kDispatched);
		assert(flip.Tick(2.0, true, true) == RecoveryStep::kDispatched);
		assert(flip.runner.Attempts() == 2);
		assert(flip.Tick(3.0, true, false) == RecoveryStep::kWaiting);
		assert(flip.runner.Attempts() == 1);
	}

	void TestRememberedCameraKeepsLastFirstOrThirdPerson()
	{
		// Invariant: a stale or unknown camera id never replaces the last first or third person sample, so the post-revive restore cannot pick the wrong view.
		using namespace F4T::Downed;
		using Memory = std::optional<std::int32_t>;
		constexpr std::int32_t kIronSights = 4;
		constexpr std::int32_t kUnknownState = 6;
		constexpr std::int32_t kBleedout = 0xB;
		assert(NextRememberedCamera(std::nullopt, kCameraFirstPerson) == Memory{ kCameraFirstPerson });
		assert(NextRememberedCamera(std::nullopt, kCameraThirdPerson) == Memory{ kCameraThirdPerson });
		assert(NextRememberedCamera(Memory{ kCameraThirdPerson }, kCameraFirstPerson) == Memory{ kCameraFirstPerson });
		assert(NextRememberedCamera(Memory{ kCameraThirdPerson }, kIronSights) == Memory{ kCameraThirdPerson });
		assert(NextRememberedCamera(Memory{ kCameraFirstPerson }, kUnknownState) == Memory{ kCameraFirstPerson });
		assert(NextRememberedCamera(Memory{ kCameraFirstPerson }, kBleedout) == Memory{ kCameraFirstPerson });
		assert(!NextRememberedCamera(std::nullopt, kIronSights).has_value());
		assert(!NextRememberedCamera(std::nullopt, kBleedout).has_value());
	}

	void TestPostRecoveryFixesWaitUntilSettledAfterGiveUp()
	{
		// Invariant: the weapon reset and camera restore wait for a continuous 0.75 s settled dwell and a clear drops both, catching a weapon reset or camera restore running in the Resurrect frame; CancelBleedoutRecovery and ClearPostRecoveryState glue is not covered by this harness.
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.runner.Request();
		assert(rig.Tick(10.0, true, true) == RecoveryStep::kDispatched);
		double now = 11.0;
		for (std::uint32_t i = 1; i < kMaximumRecoveryDispatches; ++i, now += 1.0) {
			assert(rig.Tick(now, true, true) == RecoveryStep::kDispatched);
		}
		assert(rig.Tick(now, true, true) == RecoveryStep::kGaveUp);
		auto& runner = rig.runner;
		const double t = now;
		assert(runner.ConsumeCameraRestore(t, true, true, true, true) == CameraRestoreStep::kWaiting);
		assert(runner.ConsumeCameraRestore(t + 1.0, true, false, true, true) == CameraRestoreStep::kWaiting);
		assert(runner.ConsumeCameraRestore(t + 2.0, false, false, false, true) == CameraRestoreStep::kWaiting);
		assert(runner.ConsumeCameraRestore(t + 3.0, true, false, false, true) == CameraRestoreStep::kWaiting);
		assert(runner.ConsumeCameraRestore(t + 3.5, true, false, false, true) == CameraRestoreStep::kWaiting);
		assert(runner.ConsumeCameraRestore(t + 3.0 + kPostRecoverySettleSeconds, true, false, false, true) == CameraRestoreStep::kRestore);
		assert(runner.ConsumeCameraRestore(t + 5.0, true, false, false, true) == CameraRestoreStep::kWaiting);

		RecoveryRig healthy;
		healthy.runner.RequestCameraRestore();
		assert(healthy.runner.ConsumeCameraRestore(1.0, true, false, false, false) == CameraRestoreStep::kWaiting);
		assert(healthy.runner.ConsumeCameraRestore(2.0, true, false, false, false) == CameraRestoreStep::kNotNeeded);
		assert(healthy.runner.ConsumeCameraRestore(3.0, true, false, false, true) == CameraRestoreStep::kWaiting);

		RecoveryRig broken;
		broken.runner.RequestCameraRestore();
		assert(broken.runner.ConsumeCameraRestore(1.0, true, false, false, true) == CameraRestoreStep::kWaiting);
		assert(broken.runner.ConsumeCameraRestore(1.5, true, true, false, true) == CameraRestoreStep::kWaiting);
		assert(broken.runner.ConsumeCameraRestore(1.6, true, false, false, true) == CameraRestoreStep::kWaiting);
		assert(broken.runner.ConsumeCameraRestore(2.3, true, false, false, true) == CameraRestoreStep::kWaiting);
		assert(broken.runner.ConsumeCameraRestore(2.4, true, false, false, true) == CameraRestoreStep::kRestore);

		RecoveryRig weapon;
		assert(!weapon.runner.ConsumeWeaponReset(1.0, true, false, false));
		weapon.runner.RequestWeaponReset();
		assert(!weapon.runner.ConsumeWeaponReset(2.0, true, true, false));
		assert(!weapon.runner.ConsumeWeaponReset(3.0, true, false, true));
		assert(!weapon.runner.ConsumeWeaponReset(4.0, true, false, false));
		assert(weapon.runner.ConsumeWeaponReset(4.0 + kPostRecoverySettleSeconds, true, false, false));
		assert(!weapon.runner.ConsumeWeaponReset(6.0, true, false, false));

		RecoveryRig cleared;
		cleared.runner.RequestCameraRestore();
		cleared.runner.RequestWeaponReset();
		cleared.runner.ClearPostRecovery();
		assert(cleared.runner.ConsumeCameraRestore(1.0, true, false, false, true) == CameraRestoreStep::kWaiting);
		assert(cleared.runner.ConsumeCameraRestore(5.0, true, false, false, true) == CameraRestoreStep::kWaiting);
		assert(!cleared.runner.ConsumeWeaponReset(6.0, true, false, false));
		assert(!cleared.runner.ConsumeWeaponReset(10.0, true, false, false));
	}

	void TestRecoveryCapResendIsThrottled()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.accept = false;
		rig.runner.Request();
		double now = 1.0;
		for (std::uint32_t i = 0; i < kMaximumRecoveryDispatches; ++i, now += 1.0) {
			static_cast<void>(rig.Tick(now, true, false));
		}
		assert(rig.Tick(now, true, false) == RecoveryStep::kGaveUp);
		const auto sent = rig.sent.size();
		assert(rig.runner.IsResendOwed());
		static_cast<void>(rig.Tick(now + 0.5, true, false));
		assert(rig.sent.size() == sent);
		static_cast<void>(rig.Tick(now + 1.0, true, false));
		assert(rig.sent.size() == sent + 1);
	}

	void TestRecoveryDeadCancelEveryFrameIsThrottled()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.accept = false;
		rig.runner.Request();
		assert(rig.Tick(1.0, true, true) == RecoveryStep::kDispatched);
		for (int i = 0; i < 10; ++i) {
			rig.now = 2.0 + i * 0.1;
			rig.Cancel(true);
		}
		assert(rig.sent.size() == 2);
		for (int i = 10; i < 150; ++i) {
			rig.now = 2.0 + i * 0.1;
			rig.Cancel(true);
		}
		assert(rig.sent.size() == 1 + kMaximumRecoveryDispatches);
		assert(!rig.runner.IsResendOwed());
	}

	void TestRecoveryDeathMidRecovery()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.runner.Request();
		assert(rig.Tick(1.0, true, true) == RecoveryStep::kDispatched);
		rig.Cancel(false);
		assert(rig.runner.IsResendOwed() && !rig.runner.IsPending());
		const auto sentBefore = rig.sent.size();
		assert(rig.Tick(2.0, false, true) == RecoveryStep::kIdle);
		assert(rig.sent.size() == sentBefore);
		assert(rig.Tick(3.0, true, false) == RecoveryStep::kIdle);
		assert(rig.sent.size() == sentBefore + 1 && rig.sent.back());
		assert(!rig.runner.IsResendOwed());
	}

	void TestRecoveryReentrantCancelDuringFirstSend()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.runner.Request();
		bool fired = false;
		rig.onSend = [&rig, &fired] {
			if (!fired) {
				fired = true;
				rig.Cancel(false);
			}
		};
		assert(rig.Tick(1.0, true, true) == RecoveryStep::kDispatched);
		assert(rig.runner.Attempts() == 1);
		assert(rig.runner.IsResendOwed() && !rig.runner.IsPending());
		assert(rig.Tick(2.5, true, true) == RecoveryStep::kIdle);
		assert(rig.sent.size() == 2 && rig.sent.back());
		assert(!rig.runner.IsResendOwed());
	}

	void TestRecoveryGaveUpLatchUntilStateChanges()
	{
		using namespace F4T::Downed;
		const auto giveUp = [](RecoveryRig& a_rig) {
			a_rig.runner.Request();
			double now = 1.0;
			for (std::uint32_t i = 0; i < kMaximumRecoveryDispatches; ++i, now += 1.0) {
				assert(a_rig.Tick(now, true, true) == RecoveryStep::kDispatched);
			}
			assert(a_rig.Tick(now, true, true) == RecoveryStep::kGaveUp);
			return now;
		};
		RecoveryRig rig;
		const double now = giveUp(rig);
		const auto givenUp = rig.sent.size();
		rig.runner.Request();
		assert(rig.Tick(now + 5.0, true, true) == RecoveryStep::kIdle);
		assert(rig.sent.size() == givenUp);
		rig.runner.Reset();
		rig.runner.Request();
		assert(rig.Tick(now + 6.0, true, true) == RecoveryStep::kDispatched);

		RecoveryRig episode;
		const double episodeNow = giveUp(episode);
		episode.runner.ClearGaveUp();
		episode.runner.Request();
		assert(episode.Tick(episodeNow + 5.0, true, true) == RecoveryStep::kDispatched);
	}

	void TestRecoverySessionEndClearsGiveUpLatch()
	{
		using namespace F4T::Downed;
		RecoveryRig rig;
		rig.runner.Request();
		double now = 1.0;
		for (std::uint32_t i = 0; i < kMaximumRecoveryDispatches; ++i, now += 1.0) {
			assert(rig.Tick(now, true, true) == RecoveryStep::kDispatched);
		}
		assert(rig.Tick(now, true, true) == RecoveryStep::kGaveUp);
		const auto givenUp = rig.sent.size();
		rig.runner.Request();
		assert(rig.Tick(now + 1.0, true, true) == RecoveryStep::kIdle);
		assert(rig.sent.size() == givenUp);
		rig.runner.ClearGaveUp();
		rig.runner.Request();
		assert(rig.Tick(now + 2.0, true, true) == RecoveryStep::kDispatched);
		assert(rig.sent.size() == givenUp + 1 && !rig.sent.back());
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
	TestDeathBagWireDecodes();
	TestRequestRoundTrip();
	TestRemotePoseApplyPolicy();
	TestReviveGating();
	TestOverlayPushPolicy();
	TestPendingEventGate();
	TestRespawnRequestPolicy();
	TestServerDeadlineOverridesLocalFallback();
	TestRecoveryAfterLocalDeadline();
	TestRevivedHealthFractionClamp();
	TestRecoveryThrottleAndEssentialCap();
	TestRecoveryStandingCapAndRefusedResend();
	TestRecoveryRedownAndRepeatRequest();
	TestRecoveryHappyPathAndEssentialFlipReset();
	TestRememberedCameraKeepsLastFirstOrThirdPerson();
	TestPostRecoveryFixesWaitUntilSettledAfterGiveUp();
	TestRecoveryCapResendIsThrottled();
	TestRecoveryDeadCancelEveryFrameIsThrottled();
	TestRecoveryDeathMidRecovery();
	TestRecoveryReentrantCancelDuringFirstSend();
	TestRecoveryGaveUpLatchUntilStateChanges();
	TestRecoverySessionEndClearsGiveUpLatch();
	RunDownedCheckpointAndChannelTests();
	return 0;
}
