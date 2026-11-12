#pragma once

#include <cstdint>

namespace F4T::NetworkPrefs
{
	struct InterpolationTuning
	{
		double minDelaySeconds{ 0.060 };
		double defaultDelaySeconds{ 0.100 };
		double maxDelaySeconds{ 0.250 };
		double intervalScale{ 1.5 };
		double extrapolationSeconds{ 0.250 };
		double blendSeconds{ 0.100 };
	};

	[[nodiscard]] constexpr InterpolationTuning TuningFor(bool a_smoothing, std::uint8_t a_quality) noexcept
	{
		if (!a_smoothing) {
			return InterpolationTuning{ 0.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
		}
		switch (a_quality) {
		case 0:
			return InterpolationTuning{ 0.020, 0.040, 0.080, 1.0, 0.100, 0.050 };
		case 1:
			return InterpolationTuning{ 0.040, 0.070, 0.150, 1.25, 0.150, 0.075 };
		default:
			return InterpolationTuning{};
		}
	}
}
