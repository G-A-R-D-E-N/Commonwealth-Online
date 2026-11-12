#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace F4T::ProxyRemoteAnimationState
{
	inline constexpr float kMaximumAnimationDirectionDegrees = 360.0F;
	inline constexpr float kMaximumAimPitchDegrees = 80.0F;
	inline constexpr float kMaximumTurnDeltaDegreesPerSecond = 720.0F;

	[[nodiscard]] inline float ClampAnimationDirectionDegrees(float a_degrees)
	{
		if (!std::isfinite(a_degrees)) {
			return 0.0F;
		}

		return std::clamp(a_degrees, -kMaximumAnimationDirectionDegrees, kMaximumAnimationDirectionDegrees);
	}

	[[nodiscard]] inline float AnimationDirectionDegreesToRadians(float a_degrees)
	{
		constexpr auto kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
		return ClampAnimationDirectionDegrees(a_degrees) * kDegreesToRadians;
	}

	[[nodiscard]] inline float ClampAimPitchDegrees(float a_degrees)
	{
		if (!std::isfinite(a_degrees)) {
			return 0.0F;
		}

		return std::clamp(a_degrees, -kMaximumAimPitchDegrees, kMaximumAimPitchDegrees);
	}

	[[nodiscard]] inline float ClampTurnDeltaDegreesPerSecond(float a_value)
	{
		if (!std::isfinite(a_value)) {
			return 0.0F;
		}

		return std::clamp(a_value, -kMaximumTurnDeltaDegreesPerSecond, kMaximumTurnDeltaDegreesPerSecond);
	}
}
