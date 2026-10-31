#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace F4T::Downed
{
	enum class Phase
	{
		kAlive,
		kDowned,
		kDead
	};

	enum class RespawnPoint
	{
		kSanctuary,
		kLastBed,
		kFriend,
		kCheckpoint
	};

	struct Checkpoint
	{
		bool interior{ false };
		std::uint32_t space{};
		float x{};
		float y{};
		float z{};
		float rotZ{};
	};

	inline constexpr float kMaximumCheckpointCoordinate = 200000.0F;

	struct Config
	{
		double downedSeconds{ 60.0 };
		double sliverFraction{ 0.05 };
		float minimumSliverHealth{ 1.0F };
		double reviveHoldSeconds{ 1.5 };
		float reviveRange{ 200.0F };
		float reviveFacingCosine{ 0.5F };
		double respawnRequestRetrySeconds{ 2.0 };
	};

	inline constexpr std::int32_t kMaximumServerRemainingMs = 600000;
	inline constexpr std::uint32_t kMaximumStimpaksReported = 99;
	inline constexpr std::uint32_t kMaximumReviveChannelMs = 15000;
	inline constexpr std::size_t kMaximumReasonChars = 32;

	[[nodiscard]] inline const char* GetRespawnPointName(RespawnPoint a_point)
	{
		switch (a_point) {
		case RespawnPoint::kLastBed:
			return "lastbed";
		case RespawnPoint::kFriend:
			return "friend";
		case RespawnPoint::kCheckpoint:
			return "checkpoint";
		case RespawnPoint::kSanctuary:
		default:
			return "sanctuary";
		}
	}

	[[nodiscard]] inline bool IsValidCheckpoint(const Checkpoint& a_checkpoint)
	{
		if (a_checkpoint.space == 0 || (a_checkpoint.space >> 24) == 0xFF) {
			return false;
		}
		for (const auto value : { a_checkpoint.x, a_checkpoint.y, a_checkpoint.z, a_checkpoint.rotZ }) {
			if (!std::isfinite(value) || std::fabs(value) > kMaximumCheckpointCoordinate) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] inline RespawnPoint PreferredRespawnPoint(bool a_hasCheckpoint, bool a_hasLastBed)
	{
		if (a_hasCheckpoint) {
			return RespawnPoint::kCheckpoint;
		}
		return a_hasLastBed ? RespawnPoint::kLastBed : RespawnPoint::kSanctuary;
	}

	[[nodiscard]] inline std::optional<RespawnPoint> ParseRespawnPoint(std::string_view a_name)
	{
		if (a_name == "sanctuary") {
			return RespawnPoint::kSanctuary;
		}
		if (a_name == "lastbed") {
			return RespawnPoint::kLastBed;
		}
		if (a_name == "friend") {
			return RespawnPoint::kFriend;
		}
		if (a_name == "checkpoint") {
			return RespawnPoint::kCheckpoint;
		}
		return std::nullopt;
	}

	[[nodiscard]] inline float ComputeSliverHealth(float a_maximumHealth, const Config& a_config)
	{
		const auto minimum = std::isfinite(a_config.minimumSliverHealth) && a_config.minimumSliverHealth > 0.0F ?
			a_config.minimumSliverHealth :
			1.0F;
		if (!std::isfinite(a_maximumHealth) || a_maximumHealth <= 0.0F) {
			return minimum;
		}
		const auto fraction = std::isfinite(a_config.sliverFraction) && a_config.sliverFraction > 0.0 ?
			a_config.sliverFraction :
			0.0;
		const auto scaled = static_cast<float>(fraction * static_cast<double>(a_maximumHealth));
		return (std::min)((std::max)(minimum, scaled), a_maximumHealth * 0.5F);
	}

	struct DamageOutcome
	{
		float appliedDamage{ 0.0F };
		bool enteredDowned{ false };
	};

	[[nodiscard]] inline DamageOutcome ResolveLethalDamage(
		float a_healthBefore,
		float a_maximumHealth,
		float a_damage,
		const Config& a_config)
	{
		if (!std::isfinite(a_damage) || a_damage <= 0.0F) {
			return DamageOutcome{ 0.0F, false };
		}
		if (!std::isfinite(a_healthBefore) || a_healthBefore <= 0.0F) {
			return DamageOutcome{ 0.0F, true };
		}

		if (a_healthBefore - a_damage > 0.0F) {
			return DamageOutcome{ a_damage, false };
		}

		const auto sliver = ComputeSliverHealth(a_maximumHealth, a_config);
		return DamageOutcome{ (std::max)(0.0F, a_healthBefore - sliver), true };
	}

	struct ReviveCandidate
	{
		std::uint32_t playerId{};
		float distance{};
		float facingCosine{};
	};

	[[nodiscard]] inline std::optional<std::uint32_t> SelectReviveTarget(
		std::span<const ReviveCandidate> a_candidates,
		const Config& a_config)
	{
		std::optional<std::uint32_t> best;
		auto bestDistance = std::numeric_limits<float>::infinity();
		for (const auto& candidate : a_candidates) {
			if (candidate.playerId == 0 ||
				!std::isfinite(candidate.distance) ||
				!std::isfinite(candidate.facingCosine)) {
				continue;
			}
			if (candidate.distance > a_config.reviveRange || candidate.facingCosine < a_config.reviveFacingCosine) {
				continue;
			}
			if (candidate.distance < bestDistance) {
				bestDistance = candidate.distance;
				best = candidate.playerId;
			}
		}
		return best;
	}

	[[nodiscard]] inline bool CanReviveOthers(Phase a_phase)
	{
		return a_phase == Phase::kAlive;
	}

	// Constraint: an unreported count is accepted; a reported 0 is not.
	[[nodiscard]] inline bool HasStimpakForRevive(std::optional<std::uint32_t> a_stimpaks)
	{
		return !a_stimpaks || *a_stimpaks >= 1;
	}

	struct ReviveChannel
	{
		std::uint32_t targetPlayerId{};
		std::uint32_t channelMs{};
		double startedAtSeconds{ 0.0 };
	};

	[[nodiscard]] inline std::int32_t ReviveChannelRemainingMs(const ReviveChannel& a_channel, double a_nowSeconds)
	{
		if (a_channel.channelMs == 0) {
			return 0;
		}
		if (!std::isfinite(a_nowSeconds) || !std::isfinite(a_channel.startedAtSeconds)) {
			return static_cast<std::int32_t>(a_channel.channelMs);
		}
		const auto elapsedMs = (a_nowSeconds - a_channel.startedAtSeconds) * 1000.0;
		if (!std::isfinite(elapsedMs) || elapsedMs <= 0.0) {
			return static_cast<std::int32_t>(a_channel.channelMs);
		}
		const auto remaining = static_cast<double>(a_channel.channelMs) - elapsedMs;
		return remaining <= 0.0 ? 0 : static_cast<std::int32_t>(remaining);
	}

	[[nodiscard]] inline double ReviveChannelProgress(const ReviveChannel& a_channel, double a_nowSeconds)
	{
		if (a_channel.channelMs == 0) {
			return 1.0;
		}
		const auto remaining = ReviveChannelRemainingMs(a_channel, a_nowSeconds);
		const auto progress = 1.0 - (static_cast<double>(remaining) / static_cast<double>(a_channel.channelMs));
		return (std::clamp)(progress, 0.0, 1.0);
	}

	enum class ReviveChannelOutcome
	{
		kContinue,
		kResolved,
		kCancelReleased,
		kCancelMovedAway,
		kCancelOutOfStimpaks,
		kCancelTargetGone
	};

	struct ReviveChannelEvaluation
	{
		ReviveChannelOutcome outcome{ ReviveChannelOutcome::kContinue };
		std::string_view reason;
	};

	[[nodiscard]] inline ReviveChannelEvaluation EvaluateReviveChannel(
		bool a_activateHeld,
		bool a_targetPresent,
		bool a_targetDowned,
		bool a_targetInRange,
		std::optional<std::uint32_t> a_stimpaksLeft)
	{
		if (!a_targetPresent) {
			return { ReviveChannelOutcome::kCancelTargetGone, "target_gone" };
		}
		if (!a_targetDowned) {
			return { ReviveChannelOutcome::kResolved, {} };
		}
		if (!a_targetInRange) {
			return { ReviveChannelOutcome::kCancelMovedAway, "moved_away" };
		}
		if (!a_activateHeld) {
			return { ReviveChannelOutcome::kCancelReleased, "released" };
		}
		if (!HasStimpakForRevive(a_stimpaksLeft)) {
			return { ReviveChannelOutcome::kCancelOutOfStimpaks, "no_stimpak" };
		}
		return { ReviveChannelOutcome::kContinue, {} };
	}

	[[nodiscard]] inline bool ShouldApplyCanStimpak(bool a_hasKeyword, bool a_downed)
	{
		return a_hasKeyword != a_downed;
	}

	struct ReviveIdleFacts
	{
		bool keywordResolved{ false };
		bool keywordPresent{ false };
		bool humanRace{ false };
		bool targetDead{ false };
	};

	enum class ReviveIdleBlocker
	{
		kNone,
		kKeywordFormMissing,
		kKeywordMissing,
		kNotHumanRace,
		kTargetDead
	};

	[[nodiscard]] inline ReviveIdleBlocker DiagnoseReviveIdle(const ReviveIdleFacts& a_facts)
	{
		if (!a_facts.keywordResolved) {
			return ReviveIdleBlocker::kKeywordFormMissing;
		}
		if (a_facts.targetDead) {
			return ReviveIdleBlocker::kTargetDead;
		}
		if (!a_facts.keywordPresent) {
			return ReviveIdleBlocker::kKeywordMissing;
		}
		if (!a_facts.humanRace) {
			return ReviveIdleBlocker::kNotHumanRace;
		}
		return ReviveIdleBlocker::kNone;
	}

	[[nodiscard]] inline const char* DescribeReviveIdleBlocker(ReviveIdleBlocker a_blocker)
	{
		switch (a_blocker) {
		case ReviveIdleBlocker::kKeywordFormMissing:
			return "the playerCanStimpak form is missing";
		case ReviveIdleBlocker::kKeywordMissing:
			return "the target does not carry playerCanStimpak";
		case ReviveIdleBlocker::kNotHumanRace:
			return "the target is not HumanRace";
		case ReviveIdleBlocker::kTargetDead:
			return "the target is dead rather than bleeding out";
		case ReviveIdleBlocker::kNone:
		default:
			return "none";
		}
	}

	struct ReviveChannelStep
	{
		ReviveChannelOutcome outcome{ ReviveChannelOutcome::kContinue };
		std::string_view reason;
		double progress{ 0.0 };
		bool spend{ false };
	};

	class ReviveChannelRunner
	{
	public:
		bool Begin(std::uint32_t a_targetPlayerId, std::uint32_t a_channelMs, double a_nowSeconds)
		{
			if (a_targetPlayerId == 0 || a_channelMs > kMaximumReviveChannelMs || !std::isfinite(a_nowSeconds)) {
				return false;
			}
			channel_ = ReviveChannel{ a_targetPlayerId, a_channelMs, a_nowSeconds };
			spent_ = false;
			return true;
		}

		[[nodiscard]] bool IsLive() const noexcept { return channel_.has_value(); }
		[[nodiscard]] const ReviveChannel& GetChannel() const noexcept { return *channel_; }
		[[nodiscard]] bool HasSpent() const noexcept { return spent_; }

		void Clear() noexcept
		{
			channel_.reset();
			spent_ = false;
		}

		[[nodiscard]] ReviveChannelStep Tick(
			double a_nowSeconds,
			bool a_activateHeld,
			bool a_targetPresent,
			bool a_targetDowned,
			bool a_targetInRange,
			std::optional<std::uint32_t> a_stimpaksLeft)
		{
			ReviveChannelStep step;
			if (!channel_) {
				return step;
			}
			const auto evaluation = EvaluateReviveChannel(
				a_activateHeld,
				a_targetPresent,
				a_targetDowned,
				a_targetInRange,
				a_stimpaksLeft);
			step.outcome = evaluation.outcome;
			step.reason = evaluation.reason;
			step.progress = ReviveChannelProgress(*channel_, a_nowSeconds);
			if (evaluation.outcome == ReviveChannelOutcome::kContinue) {
				return step;
			}
			if (evaluation.outcome == ReviveChannelOutcome::kResolved) {
				step.spend = !spent_;
				spent_ = true;
			}
			channel_.reset();
			return step;
		}

	private:
		std::optional<ReviveChannel> channel_;
		bool spent_{ false };
	};

	[[nodiscard]] inline bool IsReviveCandidate(bool a_hasVitals, bool a_isDowned, bool a_isDead)
	{
		return a_hasVitals && a_isDowned && !a_isDead;
	}

	struct RemotePoseEntry
	{
		bool downed{ false };
		bool retryPending{ false };
		double nextAttemptSeconds{ 0.0 };
	};

	[[nodiscard]] inline bool ShouldApplyRemotePose(
		const RemotePoseEntry& a_entry,
		bool a_targetDowned,
		double a_nowSeconds)
	{
		if (a_entry.downed == a_targetDowned) {
			return false;
		}
		if (a_entry.retryPending) {
			return std::isfinite(a_nowSeconds) && a_nowSeconds >= a_entry.nextAttemptSeconds;
		}
		return true;
	}

	[[nodiscard]] inline bool ShouldPushDownedOverlay(
		std::int32_t a_remainingMs,
		bool a_rescuerNearby,
		std::optional<std::int32_t> a_lastRemainingMs,
		std::optional<bool> a_lastRescuerNearby)
	{
		if (!a_lastRemainingMs || !a_lastRescuerNearby) {
			return true;
		}
		return (a_remainingMs / 1000) != (*a_lastRemainingMs / 1000) || a_rescuerNearby != *a_lastRescuerNearby;
	}

	[[nodiscard]] inline bool IsRespawnRequestValid(RespawnPoint a_point, std::optional<std::uint32_t> a_friendId)
	{
		return a_point != RespawnPoint::kFriend || (a_friendId && *a_friendId != 0);
	}

	inline constexpr std::uint32_t kMaximumRespawnRequestRetries = 8;
	inline constexpr std::uint32_t kMaximumRespawnRetryBackoffShift = 2;

	[[nodiscard]] inline bool ShouldRetryRespawnRequest(std::uint32_t a_attempts)
	{
		return a_attempts < kMaximumRespawnRequestRetries;
	}

	[[nodiscard]] inline double ComputeRespawnRetryDelay(double a_baseSeconds, std::uint32_t a_attempts)
	{
		const auto base = std::isfinite(a_baseSeconds) && a_baseSeconds > 0.0 ? a_baseSeconds : 2.0;
		const auto shift = (std::min)(a_attempts, kMaximumRespawnRetryBackoffShift);
		return base * static_cast<double>(1U << shift);
	}

	[[nodiscard]] inline bool ShouldApplyPendingEvents(bool a_canTouchWorld, bool a_hasEvents)
	{
		return a_canTouchWorld && a_hasEvents;
	}

	struct ReviveHold
	{
		std::optional<std::uint32_t> target;
		bool armed{ false };
		double armedAtSeconds{ 0.0 };
	};

	inline void ResetReviveHold(ReviveHold& a_hold)
	{
		a_hold.target.reset();
		a_hold.armed = false;
		a_hold.armedAtSeconds = 0.0;
	}

	[[nodiscard]] inline bool AdvanceReviveHold(
		ReviveHold& a_hold,
		std::optional<std::uint32_t> a_target,
		bool a_activateHeld,
		double a_nowSeconds,
		const Config& a_config)
	{
		if (!a_target || !a_activateHeld || !std::isfinite(a_nowSeconds)) {
			ResetReviveHold(a_hold);
			return false;
		}
		if (!a_hold.armed || a_hold.target != a_target) {
			a_hold.target = a_target;
			a_hold.armed = true;
			a_hold.armedAtSeconds = a_nowSeconds;
			return false;
		}
		const auto holdSeconds = std::isfinite(a_config.reviveHoldSeconds) && a_config.reviveHoldSeconds > 0.0 ?
			a_config.reviveHoldSeconds :
			0.0;
		if (a_nowSeconds - a_hold.armedAtSeconds < holdSeconds) {
			return false;
		}
		ResetReviveHold(a_hold);
		return true;
	}

	class State
	{
	public:
		explicit State(Config a_config = {}) :
			config_(a_config)
		{}

		[[nodiscard]] const Config& GetConfig() const noexcept { return config_; }
		[[nodiscard]] Phase GetPhase() const noexcept { return phase_; }
		[[nodiscard]] bool IsIncapacitated() const noexcept { return phase_ != Phase::kAlive; }
		[[nodiscard]] bool HasServerDeadline() const noexcept { return hasServerDeadline_; }
		[[nodiscard]] std::uint32_t GetDownCount() const noexcept { return downCount_; }

		[[nodiscard]] std::int32_t GetRemainingMs(double a_nowSeconds) const noexcept
		{
			if (phase_ != Phase::kDowned || !std::isfinite(a_nowSeconds)) {
				return 0;
			}
			const auto remaining = (EffectiveDeadlineSeconds() - a_nowSeconds) * 1000.0;
			if (!std::isfinite(remaining) || remaining <= 0.0) {
				return 0;
			}
			return static_cast<std::int32_t>((std::min)(remaining, static_cast<double>(kMaximumServerRemainingMs)));
		}

		bool EnterDowned(double a_nowSeconds)
		{
			if (phase_ != Phase::kAlive || !std::isfinite(a_nowSeconds)) {
				return false;
			}
			phase_ = Phase::kDowned;
			++downCount_;
			ClearDeadlines();
			localDeadlineSeconds_ = a_nowSeconds + config_.downedSeconds;
			return true;
		}

		bool ApplyServerDownedState(bool a_downed, std::int32_t a_remainingMs, double a_nowSeconds)
		{
			if (!a_downed || !std::isfinite(a_nowSeconds) || phase_ == Phase::kDead) {
				return false;
			}
			const bool entered = phase_ == Phase::kAlive ? EnterDowned(a_nowSeconds) : false;
			hasServerDeadline_ = true;
			serverRemainingMs_ = (std::clamp)(a_remainingMs, 0, kMaximumServerRemainingMs);
			serverRemainingAtSeconds_ = a_nowSeconds;
			return entered;
		}

		bool ApplyRevived()
		{
			if (phase_ == Phase::kAlive) {
				return false;
			}
			phase_ = Phase::kAlive;
			ClearDeadlines();
			return true;
		}

		bool ApplyDied()
		{
			if (phase_ == Phase::kDead) {
				return false;
			}
			phase_ = Phase::kDead;
			ClearDeadlines();
			return true;
		}

		bool ApplyRespawnGrant()
		{
			return ApplyRevived();
		}

		bool Tick(double a_nowSeconds)
		{
			if (phase_ != Phase::kDowned || !std::isfinite(a_nowSeconds)) {
				return false;
			}
			if (a_nowSeconds < EffectiveDeadlineSeconds()) {
				return false;
			}
			phase_ = Phase::kDead;
			ClearDeadlines();
			return true;
		}

		bool Reset()
		{
			const bool changed = phase_ != Phase::kAlive;
			phase_ = Phase::kAlive;
			ClearDeadlines();
			return changed;
		}

	private:
		[[nodiscard]] double EffectiveDeadlineSeconds() const noexcept
		{
			if (hasServerDeadline_) {
				return serverRemainingAtSeconds_ + (static_cast<double>(serverRemainingMs_) / 1000.0);
			}
			return localDeadlineSeconds_;
		}

		void ClearDeadlines() noexcept
		{
			localDeadlineSeconds_ = 0.0;
			hasServerDeadline_ = false;
			serverRemainingMs_ = 0;
			serverRemainingAtSeconds_ = 0.0;
		}

		Config config_;
		Phase phase_{ Phase::kAlive };
		double localDeadlineSeconds_{ 0.0 };
		bool hasServerDeadline_{ false };
		std::int32_t serverRemainingMs_{ 0 };
		double serverRemainingAtSeconds_{ 0.0 };
		std::uint32_t downCount_{ 0 };
	};
}
