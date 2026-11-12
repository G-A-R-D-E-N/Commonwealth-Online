#pragma once

#include <cmath>

namespace F4T::SleepClockMath
{
	inline constexpr double kHoursPerDay = 24.0;
	inline constexpr double kSecondsPerHour = 3600.0;

	struct Baseline
	{
		double hour{ 0.0 };
		double daysPassed{ 0.0 };
		double realSeconds{ 0.0 };
		double timeScale{ 0.0 };
	};

	inline Baseline Begin(double a_hour, double a_daysPassed, double a_realSeconds, double a_timeScale) noexcept
	{
		return Baseline{ a_hour, a_daysPassed, a_realSeconds, a_timeScale };
	}

	inline double ElapsedHours(const Baseline& a_baseline, double a_realNow) noexcept
	{
		if (!(a_baseline.timeScale > 0.0)) {
			return 0.0;
		}

		const auto elapsedRealSeconds = a_realNow - a_baseline.realSeconds;
		if (!(elapsedRealSeconds > 0.0)) {
			return 0.0;
		}

		return elapsedRealSeconds * a_baseline.timeScale / kSecondsPerHour;
	}

	inline void SharedAt(const Baseline& a_baseline, double a_realNow, double& a_hour, double& a_daysPassed) noexcept
	{
		const auto elapsed = ElapsedHours(a_baseline, a_realNow);
		const auto total = a_baseline.hour + elapsed;
		a_hour = total - std::floor(total / kHoursPerDay) * kHoursPerDay;
		a_daysPassed = a_baseline.daysPassed + elapsed / kHoursPerDay;
	}

	inline float HourToFloat(double a_hour) noexcept
	{
		const auto hour = static_cast<float>(a_hour);
		return hour >= 24.0F ? 0.0F : hour;
	}
}
