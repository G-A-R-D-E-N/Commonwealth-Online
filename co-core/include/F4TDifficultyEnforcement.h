#pragma once

#include <cstdint>
#include <optional>

namespace F4T::DifficultyEnforcement
{
	inline constexpr std::int32_t kVeryHard = 4;
	inline constexpr std::int32_t kMinimumDifficulty = 0;
	inline constexpr std::int32_t kMaximumDifficulty = 6;
	inline constexpr double kGraceSeconds = 60.0;
	inline constexpr double kSilenceSeconds = 240.0;
	inline constexpr double kSweepIntervalSeconds = 10.0;
	inline constexpr double kKickAfterWarningSeconds = 30.0;

	struct Settings
	{
		bool survivalMode{};
		bool requireReport{ true };
	};

	struct SessionState
	{
		double joinedAt{};
		std::optional<std::int32_t> reportedDifficulty;
		double reportedAt{};
		std::optional<double> warnedAt;
		bool kicked{};
	};

	enum class Status : std::uint8_t
	{
		Disabled,
		Grace,
		Ok,
		Mismatch,
		Silent
	};

	enum class Action : std::uint8_t
	{
		None,
		Warn,
		Kick
	};

	[[nodiscard]] constexpr bool IsValidDifficulty(std::int64_t a_value) noexcept
	{
		return a_value >= kMinimumDifficulty && a_value <= kMaximumDifficulty;
	}

	[[nodiscard]] constexpr Status Evaluate(const SessionState& a_state, const Settings& a_settings, double a_nowSeconds) noexcept
	{
		if (!a_settings.survivalMode) {
			return Status::Disabled;
		}
		if (a_state.reportedDifficulty) {
			if (a_settings.requireReport && a_nowSeconds - a_state.reportedAt > kSilenceSeconds) {
				return Status::Silent;
			}
			if (*a_state.reportedDifficulty == kVeryHard) {
				return Status::Ok;
			}
			return a_nowSeconds - a_state.joinedAt < kGraceSeconds ? Status::Grace : Status::Mismatch;
		}
		if (!a_settings.requireReport) {
			return Status::Disabled;
		}
		return a_nowSeconds - a_state.joinedAt > kSilenceSeconds ? Status::Silent : Status::Grace;
	}

	[[nodiscard]] constexpr Action Step(SessionState& a_state, Status a_status, double a_nowSeconds) noexcept
	{
		if (a_status == Status::Ok) {
			a_state.warnedAt.reset();
			return Action::None;
		}
		if (a_status != Status::Mismatch && a_status != Status::Silent) {
			return Action::None;
		}
		if (a_state.kicked) {
			return Action::None;
		}
		if (!a_state.warnedAt) {
			a_state.warnedAt = a_nowSeconds;
			return Action::Warn;
		}
		if (a_nowSeconds - *a_state.warnedAt < kKickAfterWarningSeconds) {
			return Action::None;
		}
		a_state.kicked = true;
		return Action::Kick;
	}

	constexpr void ResetTracking(SessionState& a_state, double a_nowSeconds) noexcept
	{
		a_state.joinedAt = a_nowSeconds;
		a_state.reportedAt = a_nowSeconds;
		a_state.warnedAt.reset();
		a_state.kicked = false;
	}
}
