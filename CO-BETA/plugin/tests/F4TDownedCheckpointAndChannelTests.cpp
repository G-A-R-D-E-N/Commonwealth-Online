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
	void TestDownedEventDecoding()
	{
		using namespace F4T::Downed;

		DownedEvent event;
		assert(TryDecodeDownedEvent("reviveRequest", nlohmann::json::object(), event) == DecodeStatus::kUnknown);
		assert(TryDecodeDownedEvent(kDownedStateType, { { "playerId", 5U }, { "downed", true }, { "remainingMs", 42000 } }, event) == DecodeStatus::kHandled);
		assert(event.kind == EventKind::kDownedState);
		assert(event.playerId == 5U);
		assert(event.downed);
		assert(event.remainingMs == 42000);
		assert(TryDecodeDownedEvent(kDownedStateType, { { "playerId", 5U } }, event) == DecodeStatus::kMalformed);

		assert(TryDecodeDownedEvent(kRevivedType, { { "byPlayerId", 3U }, { "healthFraction", 0.5 } }, event) == DecodeStatus::kHandled);
		assert(event.kind == EventKind::kRevived);
		assert(event.playerId == 3U);
		assert(event.healthFraction == 0.5);
		assert(TryDecodeDownedEvent(kRevivedType, { { "byPlayerId", 3U } }, event) == DecodeStatus::kMalformed);

		assert(TryDecodeDownedEvent(kDiedType, nlohmann::json::object(), event) == DecodeStatus::kHandled);
		assert(event.kind == EventKind::kDied);

		assert(TryDecodeDownedEvent(kPlayerDiedType, { { "playerId", 8U } }, event) == DecodeStatus::kHandled);
		assert(event.kind == EventKind::kPlayerDied);
		assert(event.playerId == 8U);
		assert(TryDecodeDownedEvent(kPlayerDiedType, { { "playerId", 0U } }, event) == DecodeStatus::kMalformed);

		assert(TryDecodeDownedEvent(kRespawnGrantType, { { "point", "friend" }, { "friendId", 4U } }, event) == DecodeStatus::kHandled);
		assert(event.kind == EventKind::kRespawnGrant);
		assert(event.point == RespawnPoint::kFriend);
		assert(event.friendId.value() == 4U);
		assert(TryDecodeDownedEvent(kRespawnGrantType, { { "point", "friend" } }, event) == DecodeStatus::kMalformed);
	}

	void TestCheckpointRespawnPoint()
	{
		using namespace F4T::Downed;

		assert(std::string{ GetRespawnPointName(RespawnPoint::kCheckpoint) } == "checkpoint");
		assert(ParseRespawnPoint(GetRespawnPointName(RespawnPoint::kCheckpoint)) == RespawnPoint::kCheckpoint);
		assert(IsRespawnRequestValid(RespawnPoint::kCheckpoint, std::nullopt));

		assert(PreferredRespawnPoint(true, true) == RespawnPoint::kCheckpoint);
		assert(PreferredRespawnPoint(true, false) == RespawnPoint::kCheckpoint);
		assert(PreferredRespawnPoint(false, true) == RespawnPoint::kLastBed);
		assert(PreferredRespawnPoint(false, false) == RespawnPoint::kSanctuary);

		const Checkpoint saved{ true, 0x0001A254U, 10.0F, -20.0F, 30.0F, 1.5F };
		assert(IsValidCheckpoint(saved));
		assert(!IsValidCheckpoint(Checkpoint{ true, 0U, 0.0F, 0.0F, 0.0F, 0.0F }));
		assert(!IsValidCheckpoint(Checkpoint{ true, 0xFF000001U, 0.0F, 0.0F, 0.0F, 0.0F }));
		assert(!IsValidCheckpoint(Checkpoint{ true, 1U, 3.0e8F, 0.0F, 0.0F, 0.0F }));
	}

	void TestCheckpointWire()
	{
		using namespace F4T::Downed;

		const Checkpoint saved{ false, 0x0000F00DU, 1.0F, 2.0F, 3.0F, 0.5F };
		const auto report = EncodeCheckpointReport(saved);
		assert(report.has_value());
		assert(report->at("type").get<std::string>() == kCheckpointType);
		assert(report->at("interior") == false);
		assert(report->at("space") == 0x0000F00DU);
		assert(report->at("x").get<float>() == 1.0F);
		assert(report->at("rotZ").get<float>() == 0.5F);
		assert(!EncodeCheckpointReport(Checkpoint{ true, 0U, 0.0F, 0.0F, 0.0F, 0.0F }).has_value());

		const auto granted = DecodeRespawnGrant({
			{ "type", "respawnGrant" },
			{ "point", "checkpoint" },
			{ "checkpoint", {
				{ "interior", true },
				{ "space", 0x0001A254U },
				{ "x", 10.5 },
				{ "y", -20.0 },
				{ "z", 30.0 },
				{ "rotZ", 90.0 } } } });
		assert(granted.has_value());
		assert(granted->point == RespawnPoint::kCheckpoint);
		assert(granted->checkpoint.has_value());
		assert(granted->checkpoint->interior);
		assert(granted->checkpoint->space == 0x0001A254U);
		assert(granted->checkpoint->y == -20.0F);
		assert(granted->checkpoint->rotZ == 90.0F);
		assert(!granted->friendId.has_value());

		const auto noRot = DecodeRespawnGrant({
			{ "point", "checkpoint" },
			{ "checkpoint", { { "interior", false }, { "space", 1U }, { "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 } } } });
		assert(noRot.has_value());
		assert(noRot->checkpoint.has_value());
		assert(noRot->checkpoint->rotZ == 0.0F);

		assert(!DecodeRespawnGrant({ { "point", "checkpoint" } }).has_value());
		assert(!DecodeRespawnGrant({
			{ "point", "checkpoint" },
			{ "checkpoint", { { "interior", true }, { "space", 0U }, { "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 } } } }).has_value());
		assert(!DecodeRespawnGrant({
			{ "point", "checkpoint" },
			{ "checkpoint", { { "interior", true }, { "space", 1U }, { "x", 1.0e9 }, { "y", 0.0 }, { "z", 0.0 } } } }).has_value());
		assert(!DecodeRespawnGrant({
			{ "point", "checkpoint" },
			{ "checkpoint", { { "interior", "yes" }, { "space", 1U }, { "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 } } } }).has_value());

		const auto request = EncodeRespawnRequest(RespawnPoint::kCheckpoint, std::nullopt);
		assert(request.has_value());
		assert(request->at("type").get<std::string>() == kRespawnRequestType);
		assert(request->at("point").get<std::string>() == "checkpoint");
		assert(!request->contains("checkpoint"));
	}

	void TestReviveChannelPolicy()
	{
		using namespace F4T::Downed;

		assert(HasStimpakForRevive(std::nullopt));
		assert(HasStimpakForRevive(std::optional<std::uint32_t>{ 1 }));
		assert(!HasStimpakForRevive(std::optional<std::uint32_t>{ 0 }));

		const ReviveChannel channel{ 42, 3000, 100.0 };
		assert(ReviveChannelRemainingMs(channel, 100.0) == 3000);
		assert(ReviveChannelRemainingMs(channel, 101.5) == 1500);
		assert(ReviveChannelRemainingMs(channel, 103.0) == 0);
		assert(ReviveChannelRemainingMs(channel, 99.0) == 3000);
		assert(ReviveChannelRemainingMs(channel, std::numeric_limits<double>::quiet_NaN()) == 3000);
		assert(ReviveChannelProgress(channel, 100.0) == 0.0);
		assert(ReviveChannelProgress(channel, 101.5) == 0.5);
		assert(ReviveChannelProgress(channel, 104.0) == 1.0);

		const ReviveChannel instant{ 42, 0, 100.0 };
		assert(ReviveChannelRemainingMs(instant, 100.0) == 0);
		assert(ReviveChannelProgress(instant, 100.0) == 1.0);

		const auto held = EvaluateReviveChannel(true, true, true, true, std::optional<std::uint32_t>{ 2 });
		assert(held.outcome == ReviveChannelOutcome::kContinue);
		assert(held.reason.empty());

		const auto gone = EvaluateReviveChannel(true, false, true, true, std::optional<std::uint32_t>{ 2 });
		assert(gone.outcome == ReviveChannelOutcome::kCancelTargetGone);
		assert(gone.reason == "target_gone");

		const auto up = EvaluateReviveChannel(true, true, false, true, std::optional<std::uint32_t>{ 2 });
		assert(up.outcome == ReviveChannelOutcome::kResolved);
		assert(up.reason.empty());

		const auto away = EvaluateReviveChannel(true, true, true, false, std::optional<std::uint32_t>{ 2 });
		assert(away.outcome == ReviveChannelOutcome::kCancelMovedAway);
		assert(away.reason == "moved_away");

		const auto released = EvaluateReviveChannel(false, true, true, true, std::optional<std::uint32_t>{ 2 });
		assert(released.outcome == ReviveChannelOutcome::kCancelReleased);
		assert(released.reason == "released");

		const auto empty = EvaluateReviveChannel(true, true, true, true, std::optional<std::uint32_t>{ 0 });
		assert(empty.outcome == ReviveChannelOutcome::kCancelOutOfStimpaks);
		assert(empty.reason == "no_stimpak");

		assert(empty.reason.size() <= kMaximumReasonChars);

		assert(ShouldApplyCanStimpak(false, true));
		assert(ShouldApplyCanStimpak(true, false));
		assert(!ShouldApplyCanStimpak(true, true));
		assert(!ShouldApplyCanStimpak(false, false));
	}

	void TestReviveChannelWire()
	{
		using namespace F4T::Downed;

		const auto plain = EncodeReviveRequest(7);
		assert(plain.has_value());
		assert(plain->at("type").get<std::string>() == kReviveRequestType);
		assert(plain->at("target").get<std::uint32_t>() == 7);
		assert(!plain->contains("stimpaks"));

		const auto counted = EncodeReviveRequest(7, std::optional<std::uint32_t>{ 3 });
		assert(counted.has_value());
		assert(counted->at("stimpaks").get<std::uint32_t>() == 3);

		const auto zero = EncodeReviveRequest(7, std::optional<std::uint32_t>{ 0 });
		assert(zero.has_value());
		assert(zero->at("stimpaks").get<std::uint32_t>() == 0);

		assert(!EncodeReviveRequest(0).has_value());
		assert(!EncodeReviveRequest(7, std::optional<std::uint32_t>{ kMaximumStimpaksReported + 1 }).has_value());

		const auto cancel = EncodeReviveCancel(7, "moved_away");
		assert(cancel.has_value());
		assert(cancel->at("type").get<std::string>() == kReviveCancelType);
		assert(cancel->at("target").get<std::uint32_t>() == 7);
		assert(cancel->at("reason").get<std::string>() == "moved_away");
		assert(!EncodeReviveCancel(0, "released").has_value());
		assert(!EncodeReviveCancel(7, "").has_value());
		assert(!EncodeReviveCancel(7, std::string(kMaximumReasonChars + 1, 'x')).has_value());

		const auto started = DecodeReviveStarted({ { "playerId", 4U }, { "byPlayerId", 9U }, { "channelMs", 3000 } });
		assert(started.has_value());
		assert(started->playerId == 4);
		assert(started->byPlayerId == 9);
		assert(started->channelMs == 3000);

		const auto instantStart = DecodeReviveStarted({ { "playerId", 4U }, { "byPlayerId", 9U }, { "channelMs", 0 } });
		assert(instantStart.has_value());
		assert(instantStart->channelMs == 0);

		assert(!DecodeReviveStarted({ { "playerId", 4U }, { "channelMs", 3000 } }).has_value());
		assert(!DecodeReviveStarted({ { "playerId", 4U }, { "byPlayerId", 9U } }).has_value());
		assert(!DecodeReviveStarted({ { "playerId", 4U }, { "byPlayerId", 9U }, { "channelMs", -1 } }).has_value());
		assert(!DecodeReviveStarted({
			{ "playerId", 4U }, { "byPlayerId", 9U }, { "channelMs", static_cast<std::int64_t>(kMaximumReviveChannelMs) + 1 } }).has_value());

		const auto cancelled = DecodeReviveCancelled({ { "playerId", 4U }, { "byPlayerId", 9U }, { "reason", "moved_away" } });
		assert(cancelled.has_value());
		assert(cancelled->reason == "moved_away");
		const auto noReason = DecodeReviveCancelled({ { "playerId", 4U }, { "byPlayerId", 9U } });
		assert(noReason.has_value());
		assert(noReason->reason.empty());
		assert(!DecodeReviveCancelled({ { "byPlayerId", 9U } }).has_value());
		assert(!DecodeReviveCancelled({ { "playerId", 4U }, { "byPlayerId", 9U }, { "reason", 5 } }).has_value());

		const auto dropped = DecodeHeldItemDropped({ { "type", "heldItemDropped" }, { "reason", "bledOut" } });
		assert(dropped.has_value());
		assert(*dropped == "bledOut");
		const auto droppedNoReason = DecodeHeldItemDropped({ { "type", "heldItemDropped" } });
		assert(droppedNoReason.has_value());
		assert(droppedNoReason->empty());
		assert(!DecodeHeldItemDropped({ { "reason", 1 } }).has_value());
	}

	void TestReviveChannelEvents()
	{
		using namespace F4T::Downed;

		DownedEvent event;

		assert(TryDecodeDownedEvent(kReviveStartedType, { { "playerId", 4U }, { "byPlayerId", 9U }, { "channelMs", 3000 } }, event) == DecodeStatus::kHandled);
		assert(event.kind == EventKind::kReviveStarted);
		assert(event.playerId == 4);
		assert(event.byPlayerId == 9);
		assert(event.channelMs == 3000);

		assert(TryDecodeDownedEvent(kReviveCancelledType, { { "playerId", 4U }, { "byPlayerId", 9U }, { "reason", "released" } }, event) == DecodeStatus::kHandled);
		assert(event.kind == EventKind::kReviveCancelled);
		assert(event.playerId == 4);
		assert(event.byPlayerId == 9);
		assert(event.reason == "released");

		assert(TryDecodeDownedEvent(kHeldItemDroppedType, { { "reason", "bledOut" } }, event) == DecodeStatus::kHandled);
		assert(event.kind == EventKind::kHeldItemDropped);
		assert(event.reason == "bledOut");

		assert(TryDecodeDownedEvent(kReviveStartedType, { { "playerId", 4U } }, event) == DecodeStatus::kMalformed);
		assert(TryDecodeDownedEvent(kReviveCancelledType, { { "reason", "released" } }, event) == DecodeStatus::kMalformed);
		assert(TryDecodeDownedEvent("unheardOf", { { "playerId", 4U } }, event) == DecodeStatus::kUnknown);
	}

	void TestReviveIdleDiagnosis()
	{
		using namespace F4T::Downed;

		const ReviveIdleFacts healthy{ true, true, true, false };
		assert(DiagnoseReviveIdle(healthy) == ReviveIdleBlocker::kNone);
		assert(std::string{ DescribeReviveIdleBlocker(ReviveIdleBlocker::kNone) } == "none");

		assert(DiagnoseReviveIdle(ReviveIdleFacts{ false, false, true, false }) == ReviveIdleBlocker::kKeywordFormMissing);
		assert(DiagnoseReviveIdle(ReviveIdleFacts{ true, false, true, false }) == ReviveIdleBlocker::kKeywordMissing);
		assert(DiagnoseReviveIdle(ReviveIdleFacts{ true, true, false, false }) == ReviveIdleBlocker::kNotHumanRace);
		assert(DiagnoseReviveIdle(ReviveIdleFacts{ true, true, true, true }) == ReviveIdleBlocker::kTargetDead);

		assert(DiagnoseReviveIdle(ReviveIdleFacts{ false, false, false, true }) == ReviveIdleBlocker::kKeywordFormMissing);
		assert(DiagnoseReviveIdle(ReviveIdleFacts{ true, true, false, true }) == ReviveIdleBlocker::kTargetDead);
		assert(DiagnoseReviveIdle(ReviveIdleFacts{ true, false, false, false }) == ReviveIdleBlocker::kKeywordMissing);

		std::vector<std::string> described;
		for (const auto blocker : { ReviveIdleBlocker::kKeywordFormMissing, ReviveIdleBlocker::kKeywordMissing, ReviveIdleBlocker::kNotHumanRace, ReviveIdleBlocker::kTargetDead }) {
			const std::string text{ DescribeReviveIdleBlocker(blocker) };
			assert(!text.empty());
			assert(text != "none");
			assert(std::find(described.begin(), described.end(), text) == described.end());
			described.push_back(text);
		}
	}

	void TestReviveChannelRun()
	{
		using namespace F4T::Downed;

		const std::optional<std::uint32_t> oneStimpak{ 1 };
		ReviveChannelRunner runner;
		assert(!runner.IsLive());
		assert(!runner.HasSpent());

		const auto idle = runner.Tick(100.0, true, true, true, true, oneStimpak);
		assert(idle.outcome == ReviveChannelOutcome::kContinue);
		assert(!idle.spend);

		assert(!runner.Begin(0, 3000, 100.0));
		assert(!runner.Begin(42, kMaximumReviveChannelMs + 1, 100.0));
		assert(!runner.IsLive());

		assert(runner.Begin(42, 3000, 100.0));
		assert(runner.IsLive());
		assert(runner.GetChannel().targetPlayerId == 42);
		assert(runner.GetChannel().channelMs == 3000);

		const auto opening = runner.Tick(100.0, true, true, true, true, oneStimpak);
		assert(opening.outcome == ReviveChannelOutcome::kContinue);
		assert(opening.progress == 0.0);
		assert(!opening.spend);

		const auto halfway = runner.Tick(101.5, true, true, true, true, oneStimpak);
		assert(halfway.outcome == ReviveChannelOutcome::kContinue);
		assert(halfway.progress == 0.5);
		assert(!halfway.spend);
		assert(runner.IsLive());

		const auto away = runner.Tick(101.8, true, true, true, false, oneStimpak);
		assert(away.outcome == ReviveChannelOutcome::kCancelMovedAway);
		assert(away.reason == "moved_away");
		assert(!away.spend);
		assert(!runner.IsLive());
		assert(!runner.HasSpent());
		assert(runner.Tick(101.9, true, true, true, true, oneStimpak).outcome == ReviveChannelOutcome::kContinue);

		assert(runner.Begin(42, 3000, 102.0));
		assert(runner.Tick(103.5, true, true, true, true, oneStimpak).progress == 0.5);
		assert(runner.Tick(105.0, true, true, true, true, oneStimpak).outcome == ReviveChannelOutcome::kContinue);
		assert(runner.Tick(105.0, true, true, true, true, oneStimpak).progress == 1.0);
		assert(runner.IsLive());

		const auto resolved = runner.Tick(105.2, true, true, false, true, oneStimpak);
		assert(resolved.outcome == ReviveChannelOutcome::kResolved);
		assert(resolved.spend);
		assert(resolved.reason.empty());
		assert(!runner.IsLive());
		assert(runner.HasSpent());

		const auto again = runner.Tick(105.3, true, true, false, true, oneStimpak);
		assert(again.outcome == ReviveChannelOutcome::kContinue);
		assert(!again.spend);

		assert(runner.Begin(43, 3000, 106.0));
		const auto empty = runner.Tick(106.5, true, true, true, true, std::optional<std::uint32_t>{ 0 });
		assert(empty.outcome == ReviveChannelOutcome::kCancelOutOfStimpaks);
		assert(empty.reason == "no_stimpak");
		assert(!empty.spend);
		assert(!runner.IsLive());

		assert(runner.Begin(43, 3000, 107.0));
		assert(!runner.HasSpent());
		const auto released = runner.Tick(107.4, false, true, true, true, oneStimpak);
		assert(released.outcome == ReviveChannelOutcome::kCancelReleased);
		assert(released.reason == "released");

		assert(runner.Begin(44, 0, 108.0));
		const auto instant = runner.Tick(108.0, true, true, false, true, oneStimpak);
		assert(instant.outcome == ReviveChannelOutcome::kResolved);
		assert(instant.spend);
		assert(instant.progress == 1.0);
	}
}

void RunDownedCheckpointAndChannelTests()
{
	TestCheckpointRespawnPoint();
	TestCheckpointWire();
	TestDownedEventDecoding();
	TestReviveChannelPolicy();
	TestReviveChannelWire();
	TestReviveChannelEvents();
	TestReviveChannelRun();
	TestReviveIdleDiagnosis();
}

