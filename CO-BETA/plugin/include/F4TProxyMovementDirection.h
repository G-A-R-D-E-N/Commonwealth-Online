#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace F4T::ProxyMovementDirection
{
	inline float RelativeMovementDirection(float a_moveX, float a_moveY, float a_headingZ, float a_idleSpeed)
	{
		if (!std::isfinite(a_moveX) || !std::isfinite(a_moveY) || !std::isfinite(a_headingZ) ||
			!(std::hypot(a_moveX, a_moveY) >= a_idleSpeed)) {
			return 0.0F;
		}

		const auto sinHeading = std::sin(a_headingZ);
		const auto cosHeading = std::cos(a_headingZ);
		const auto forward = (a_moveX * sinHeading) + (a_moveY * cosHeading);
		const auto right = (a_moveX * cosHeading) - (a_moveY * sinHeading);
		const auto angle = std::atan2(right, forward);
		return angle <= -std::numbers::pi_v<float> ? angle + (2.0F * std::numbers::pi_v<float>) : angle;
	}

	inline float RadiansToGraphDirection(float a_radians)
	{
		constexpr auto kTwoPi = 2.0F * std::numbers::pi_v<float>;
		const auto turns = std::fmod(a_radians + kTwoPi, kTwoPi) / kTwoPi;
		return std::clamp(turns, 0.0F, std::nextafter(1.0F, 0.0F));
	}
}
