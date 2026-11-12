#pragma once

#include "F4TProxyAnimationFieldClamps.h"
#include "F4TProxyMovementSmoothing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>

namespace F4T::ProxyRemoteAnimationState
{
	inline constexpr float kDirectionEpsilonRadians = 0.02F;
	inline constexpr double kAimPitchEaseRatePerSecond = 8.0;
	inline constexpr float kAimPitchSnapEpsilonDegrees = 0.5F;
	inline constexpr auto kSneakReassertInterval = std::chrono::milliseconds{ 1000 };

	enum class SneakTransition
	{
		kNone,
		kStart,
		kStop
	};

	struct RemoteAnimationFields
	{
		float animationDirection{ 0.0F };
		bool hasAnimationDirection{ false };
		float aimPitch{ 0.0F };
		float turnDelta{ 0.0F };
		bool hasAimState{ false };
	};

	[[nodiscard]] inline float EaseAimPitchDegrees(float a_current, float a_target, double a_deltaSeconds)
	{
		const auto current = ClampAimPitchDegrees(a_current);
		const auto target = ClampAimPitchDegrees(a_target);
		const auto alpha = F4T::ProxyMovementSmoothing::SmoothingAlpha(a_deltaSeconds, kAimPitchEaseRatePerSecond);
		const auto eased = current + ((target - current) * alpha);

		return std::fabs(target - eased) < kAimPitchSnapEpsilonDegrees ? target : eased;
	}

	[[nodiscard]] inline SneakTransition DetectSneakTransition(
		bool a_wasInitialized,
		bool a_previousSneaking,
		bool a_currentSneaking)
	{
		if (a_wasInitialized && a_previousSneaking == a_currentSneaking) {
			return SneakTransition::kNone;
		}

		if (a_currentSneaking) {
			return SneakTransition::kStart;
		}

		return a_wasInitialized ? SneakTransition::kStop : SneakTransition::kNone;
	}

	[[nodiscard]] inline bool ShouldApplyProxyAnimation(
		bool a_hasAppliedGraphState,
		bool a_desiredStateChanged,
		bool a_isJumping,
		bool a_sneakReassertDue)
	{
		return !a_hasAppliedGraphState || a_desiredStateChanged || a_isJumping || a_sneakReassertDue;
	}

	[[nodiscard]] inline RemoteAnimationFields MakeRemoteAnimationFields(
		std::optional<double> a_animationDirection,
		std::optional<double> a_aimPitch,
		std::optional<double> a_turnDelta)
	{
		RemoteAnimationFields fields{};

		if (a_animationDirection) {
			fields.animationDirection = ClampAnimationDirectionDegrees(static_cast<float>(*a_animationDirection));
			fields.hasAnimationDirection = true;
		}

		if (a_aimPitch) {
			fields.aimPitch = ClampAimPitchDegrees(static_cast<float>(*a_aimPitch));
			fields.hasAimState = true;
		}

		if (a_turnDelta) {
			fields.turnDelta = ClampTurnDeltaDegreesPerSecond(static_cast<float>(*a_turnDelta));
			fields.hasAimState = true;
		}

		return fields;
	}
}
