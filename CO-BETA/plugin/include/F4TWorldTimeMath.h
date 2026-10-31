#pragma once

#include <cmath>

namespace F4T::WorldTimeMath
{
	inline constexpr float kHoursPerDay = 24.0F;
	inline constexpr float kHalfDayHours = kHoursPerDay / 2.0F;

	inline float WrapGameHour(float a_gameHour) noexcept
	{
		if (!std::isfinite(a_gameHour)) {
			return 0.0F;
		}

		auto wrapped = std::fmod(a_gameHour, kHoursPerDay);
		if (wrapped < 0.0F) {
			wrapped += kHoursPerDay;
		}
		return wrapped;
	}

	inline float HourDifference(float a_lhsGameHour, float a_rhsGameHour) noexcept
	{
		auto difference = WrapGameHour(a_lhsGameHour) - WrapGameHour(a_rhsGameHour);
		if (difference > kHalfDayHours) {
			difference -= kHoursPerDay;
		} else if (difference <= -kHalfDayHours) {
			difference += kHoursPerDay;
		}
		return difference;
	}

	inline bool TimeStateIsConsistent(
		float a_lhsGameDaysPassed, float a_lhsGameHour,
		float a_rhsGameDaysPassed, float a_rhsGameHour,
		float a_hourThreshold, float a_dayThreshold) noexcept
	{
		if (!(std::abs(HourDifference(a_lhsGameHour, a_rhsGameHour)) <= a_hourThreshold)) {
			return false;
		}

		const auto dayCounterDelta =
			static_cast<double>(a_lhsGameDaysPassed) - static_cast<double>(a_rhsGameDaysPassed);
		return std::fabs(dayCounterDelta) <= static_cast<double>(a_dayThreshold);
	}
}
