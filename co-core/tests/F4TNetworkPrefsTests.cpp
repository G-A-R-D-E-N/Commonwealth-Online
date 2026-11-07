#include "F4TNetworkPrefs.h"

#include <cassert>
#include <cmath>
#include <cstdint>

namespace
{
	using F4T::NetworkPrefs::InterpolationTuning;
	using F4T::NetworkPrefs::TuningFor;

	bool Near(double a_lhs, double a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) <= 1.0e-9;
	}

	bool SameTuning(const InterpolationTuning& a_lhs, const InterpolationTuning& a_rhs)
	{
		return Near(a_lhs.minDelaySeconds, a_rhs.minDelaySeconds) &&
			Near(a_lhs.defaultDelaySeconds, a_rhs.defaultDelaySeconds) &&
			Near(a_lhs.maxDelaySeconds, a_rhs.maxDelaySeconds) &&
			Near(a_lhs.intervalScale, a_rhs.intervalScale) &&
			Near(a_lhs.extrapolationSeconds, a_rhs.extrapolationSeconds) &&
			Near(a_lhs.blendSeconds, a_rhs.blendSeconds);
	}
}

int main()
{
	{
		const InterpolationTuning high = TuningFor(true, 2);
		assert(Near(high.minDelaySeconds, 0.060));
		assert(Near(high.defaultDelaySeconds, 0.100));
		assert(Near(high.maxDelaySeconds, 0.250));
		assert(Near(high.intervalScale, 1.5));
		assert(Near(high.extrapolationSeconds, 0.250));
		assert(Near(high.blendSeconds, 0.100));
		assert(SameTuning(high, InterpolationTuning{}));
	}

	{
		const InterpolationTuning medium = TuningFor(true, 1);
		assert(Near(medium.minDelaySeconds, 0.040));
		assert(Near(medium.defaultDelaySeconds, 0.070));
		assert(Near(medium.maxDelaySeconds, 0.150));
		assert(Near(medium.intervalScale, 1.25));
		assert(Near(medium.extrapolationSeconds, 0.150));
		assert(Near(medium.blendSeconds, 0.075));
	}

	{
		const InterpolationTuning low = TuningFor(true, 0);
		assert(Near(low.minDelaySeconds, 0.020));
		assert(Near(low.defaultDelaySeconds, 0.040));
		assert(Near(low.maxDelaySeconds, 0.080));
		assert(Near(low.intervalScale, 1.0));
		assert(Near(low.extrapolationSeconds, 0.100));
		assert(Near(low.blendSeconds, 0.050));
	}

	for (std::uint8_t quality = 0; quality <= 2; ++quality) {
		const InterpolationTuning off = TuningFor(false, quality);
		assert(Near(off.minDelaySeconds, 0.0));
		assert(Near(off.defaultDelaySeconds, 0.0));
		assert(Near(off.maxDelaySeconds, 0.0));
		assert(Near(off.extrapolationSeconds, 0.0));
		assert(Near(off.blendSeconds, 0.0));
	}

	{
		const InterpolationTuning fallback = TuningFor(true, 3);
		assert(SameTuning(fallback, TuningFor(true, 2)));
		assert(SameTuning(TuningFor(true, 255), TuningFor(true, 2)));
	}

	{
		const InterpolationTuning low = TuningFor(true, 0);
		const InterpolationTuning medium = TuningFor(true, 1);
		const InterpolationTuning high = TuningFor(true, 2);
		assert(low.minDelaySeconds < medium.minDelaySeconds && medium.minDelaySeconds < high.minDelaySeconds);
		assert(low.defaultDelaySeconds < medium.defaultDelaySeconds && medium.defaultDelaySeconds < high.defaultDelaySeconds);
		assert(low.maxDelaySeconds < medium.maxDelaySeconds && medium.maxDelaySeconds < high.maxDelaySeconds);
		assert(low.extrapolationSeconds < medium.extrapolationSeconds && medium.extrapolationSeconds < high.extrapolationSeconds);
		assert(low.blendSeconds < medium.blendSeconds && medium.blendSeconds < high.blendSeconds);
		assert(low.minDelaySeconds <= low.defaultDelaySeconds && low.defaultDelaySeconds <= low.maxDelaySeconds);
		assert(medium.minDelaySeconds <= medium.defaultDelaySeconds && medium.defaultDelaySeconds <= medium.maxDelaySeconds);
		assert(high.minDelaySeconds <= high.defaultDelaySeconds && high.defaultDelaySeconds <= high.maxDelaySeconds);
	}

	return 0;
}
