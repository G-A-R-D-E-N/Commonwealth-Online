#include "F4TSleepClockMath.h"

#include <cassert>
#include <cmath>

namespace
{
	bool NearlyEqual(double a_lhs, double a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) < 1.0e-6;
	}
}

int main()
{
	using namespace F4T::SleepClockMath;

	double hour = 0.0;
	double daysPassed = 0.0;

	const auto advancing = Begin(12.0, 5.0, 100.0, 20.0);
	assert(NearlyEqual(ElapsedHours(advancing, 280.0), 1.0));
	SharedAt(advancing, 280.0, hour, daysPassed);
	assert(NearlyEqual(hour, 13.0));
	assert(NearlyEqual(daysPassed, 5.0 + 1.0 / 24.0));
	SharedAt(advancing, 100.0, hour, daysPassed);
	assert(NearlyEqual(hour, 12.0));
	assert(NearlyEqual(daysPassed, 5.0));

	const auto wrapping = Begin(23.0, 5.0, 0.0, 20.0);
	SharedAt(wrapping, 180.0, hour, daysPassed);
	assert(NearlyEqual(hour, 0.0));
	assert(NearlyEqual(daysPassed, 5.0 + 1.0 / 24.0));
	SharedAt(wrapping, 360.0, hour, daysPassed);
	assert(NearlyEqual(hour, 1.0));
	assert(NearlyEqual(daysPassed, 5.0 + 2.0 / 24.0));

	const auto frozen = Begin(7.5, 3.0, 50.0, 0.0);
	assert(NearlyEqual(ElapsedHours(frozen, 1.0e6), 0.0));
	SharedAt(frozen, 1.0e6, hour, daysPassed);
	assert(NearlyEqual(hour, 7.5));
	assert(NearlyEqual(daysPassed, 3.0));
	const auto reversed = Begin(7.5, 3.0, 50.0, -20.0);
	SharedAt(reversed, 1.0e6, hour, daysPassed);
	assert(NearlyEqual(hour, 7.5));
	assert(NearlyEqual(daysPassed, 3.0));

	const auto clamped = Begin(10.0, 2.0, 100.0, 20.0);
	assert(NearlyEqual(ElapsedHours(clamped, 40.0), 0.0));
	SharedAt(clamped, 40.0, hour, daysPassed);
	assert(NearlyEqual(hour, 10.0));
	assert(NearlyEqual(daysPassed, 2.0));

	const auto lateNight = Begin(23.9, 2.0, 0.0, 20.0);
	SharedAt(lateNight, 0.0, hour, daysPassed);
	assert(NearlyEqual(hour, 23.9));
	assert(NearlyEqual(daysPassed, 2.0));
	SharedAt(lateNight, 36.0, hour, daysPassed);
	assert(NearlyEqual(hour, 0.1));
	assert(NearlyEqual(daysPassed, 2.0 + 0.2 / 24.0));

	const auto midnight = Begin(23.9, 5.995, 0.0, 20.0);
	SharedAt(midnight, 36.0, hour, daysPassed);
	assert(NearlyEqual(hour, 0.1));
	assert(NearlyEqual(daysPassed, 5.995 + 0.2 / 24.0));
	assert(HourToFloat(23.9999999999) == 0.0F);
	assert(HourToFloat(12.5) == 12.5F);

	const auto longGap = Begin(1.0, 0.0, 0.0, 20.0);
	SharedAt(longGap, 4320090.0, hour, daysPassed);
	assert(NearlyEqual(hour, 1.5));
	assert(NearlyEqual(daysPassed, 1000.0 + 0.5 / 24.0));
	SharedAt(longGap, 4320000.0, hour, daysPassed);
	assert(NearlyEqual(hour, 1.0));
	assert(NearlyEqual(daysPassed, 1000.0));

	return 0;
}
