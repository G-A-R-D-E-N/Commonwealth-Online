#include "pch.h"

#include "F4TInterpolationSystem.h"

#include <cmath>
#include <algorithm>

namespace F4T::InterpolationSystem
{
	RE::NiPoint3 Lerp(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_alpha) noexcept
	{
		a_alpha = std::clamp(a_alpha, 0.0F, 1.0F);

		return RE::NiPoint3{
			a_from.x + (a_to.x - a_from.x) * a_alpha,
			a_from.y + (a_to.y - a_from.y) * a_alpha,
			a_from.z + (a_to.z - a_from.z) * a_alpha
		};
	}

	float LerpRotation(float a_from, float a_to, float a_alpha) noexcept
	{
		a_alpha = std::clamp(a_alpha, 0.0F, 1.0F);

		const auto normalize = [](float a_angle) {
			while (a_angle < 0.0F) a_angle += 2.0F * 3.14159265F;
			while (a_angle >= 2.0F * 3.14159265F) a_angle -= 2.0F * 3.14159265F;
			return a_angle;
		};

		float from = normalize(a_from);
		float to = normalize(a_to);

		float delta = to - from;
		if (delta > 3.14159265F) {
			delta -= 2.0F * 3.14159265F;
		} else if (delta < -3.14159265F) {
			delta += 2.0F * 3.14159265F;
		}

		return normalize(from + delta * a_alpha);
	}
}
