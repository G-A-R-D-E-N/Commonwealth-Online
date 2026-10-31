#include "F4TWorldTimeMath.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace
{
	// Same tolerances the plugin uses for the send and apply thresholds.
	constexpr float kHourThreshold = 0.05F;
	constexpr float kDayThreshold = 0.001F;

	bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) < 1.0e-4F;
	}

	bool Consistent(float a_lhsDays, float a_lhsHour, float a_rhsDays, float a_rhsHour)
	{
		return F4T::WorldTimeMath::TimeStateIsConsistent(
			a_lhsDays, a_lhsHour, a_rhsDays, a_rhsHour, kHourThreshold, kDayThreshold);
	}
}

int main()
{
	using F4T::WorldTimeMath::HourDifference;
	using F4T::WorldTimeMath::WrapGameHour;

	// Hours wrap into [0, 24).
	assert(NearlyEqual(WrapGameHour(0.0F), 0.0F));
	assert(NearlyEqual(WrapGameHour(23.99F), 23.99F));
	assert(NearlyEqual(WrapGameHour(24.0F), 0.0F));
	assert(NearlyEqual(WrapGameHour(25.5F), 1.5F));
	assert(NearlyEqual(WrapGameHour(-0.5F), 23.5F));
	assert(NearlyEqual(WrapGameHour(std::numeric_limits<float>::quiet_NaN()), 0.0F));

	// Midnight wraps take the short way around.
	assert(NearlyEqual(HourDifference(23.95F, 0.02F), -0.07F));
	assert(NearlyEqual(HourDifference(0.02F, 23.95F), 0.07F));
	assert(NearlyEqual(HourDifference(12.0F, 0.0F), 12.0F));
	// The short arc is signed in (-12, 12], so a half day resolves to +12 h.
	assert(NearlyEqual(HourDifference(0.0F, 12.0F), 12.0F));
	assert(NearlyEqual(HourDifference(13.0F, 11.0F), 2.0F));
	assert(NearlyEqual(HourDifference(24.5F, 0.5F), 0.0F));
	assert(NearlyEqual(HourDifference(1.5F, 1.5F), 0.0F));

	// Same instant: consistent, so no time is sent and none is applied.
	assert(Consistent(12.0F, 12.00F, 12.0F, 12.01F));
	assert(Consistent(12.0F, 12.00F, 12.0F, 12.02F));
	// A real hour change is not consistent.
	assert(!Consistent(12.0F, 12.00F, 12.0F, 12.10F));
	// Across midnight with the day counter unchanged: 0.02 h apart, not 23.98 h.
	assert(Consistent(12.0F, 0.01F, 12.0F, 23.99F));
	assert(Consistent(12.0F, 23.99F, 12.0F, 0.01F));
	// Across midnight with the day counter advanced by less than the day threshold.
	assert(Consistent(12.0005F, 0.01F, 12.0F, 23.99F));
	assert(Consistent(12.0F, 23.99F, 12.0005F, 0.01F));
	// A new-day jump: the counter moved a whole day, the hour did not.
	assert(!Consistent(13.0F, 0.01F, 12.0F, 23.99F));
	// A backwards load of a whole day is not consistent either.
	assert(!Consistent(12.0F, 23.99F, 13.0F, 0.01F));
	// A multi-hour wait that straddles midnight is not consistent.
	assert(!Consistent(12.0F, 23.99F, 12.0F, 6.00F));
	assert(!Consistent(12.0F, 6.00F, 12.0F, 23.99F));
	assert(!Consistent(13.0F, 6.00F, 12.0F, 23.99F));
	// A whole-day difference with the same hour stays a whole-day difference.
	assert(!Consistent(13.0F, 12.00F, 12.0F, 12.00F));
	// Multi-day drift stays inconsistent.
	assert(!Consistent(13.0F, 0.01F, 9.0F, 0.01F));
	assert(!Consistent(9.0F, 0.01F, 13.0F, 0.01F));

	return 0;
}
