#include "F4TProxyRemoteAnimationState.h"
#include "F4TProxyPuppetLocomotionState.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace
{
	constexpr float kEpsilon = 1.0e-4F;
	constexpr float kPi = std::numbers::pi_v<float>;

	bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) < kEpsilon;
	}

	using F4T::ProxyRemoteAnimationState::MakeRemoteAnimationFields;
	using F4T::ProxyRemoteAnimationState::SneakTransition;
}

int main()
{
	const auto nan = std::numeric_limits<float>::quiet_NaN();
	const auto infinity = std::numeric_limits<float>::infinity();

	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(0.0F) == 0.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(90.0F) == 90.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(-90.0F) == -90.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(360.0F) == 360.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(-360.0F) == -360.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(720.0F) == 360.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(-720.0F) == -360.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(nan) == 0.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(infinity) == 0.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAnimationDirectionDegrees(-infinity) == 0.0F);

	assert(NearlyEqual(F4T::ProxyRemoteAnimationState::AnimationDirectionDegreesToRadians(0.0F), 0.0F));
	assert(NearlyEqual(F4T::ProxyRemoteAnimationState::AnimationDirectionDegreesToRadians(90.0F), kPi / 2.0F));
	assert(NearlyEqual(F4T::ProxyRemoteAnimationState::AnimationDirectionDegreesToRadians(-90.0F), -kPi / 2.0F));
	assert(NearlyEqual(F4T::ProxyRemoteAnimationState::AnimationDirectionDegreesToRadians(180.0F), kPi));
	assert(NearlyEqual(
		F4T::ProxyRemoteAnimationState::AnimationDirectionDegreesToRadians(900.0F),
		F4T::ProxyRemoteAnimationState::AnimationDirectionDegreesToRadians(360.0F)));
	assert(F4T::ProxyRemoteAnimationState::AnimationDirectionDegreesToRadians(nan) == 0.0F);

	assert(F4T::ProxyRemoteAnimationState::ClampAimPitchDegrees(0.0F) == 0.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAimPitchDegrees(45.0F) == 45.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAimPitchDegrees(80.0F) == 80.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAimPitchDegrees(180.0F) == 80.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAimPitchDegrees(-180.0F) == -80.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAimPitchDegrees(nan) == 0.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampAimPitchDegrees(-infinity) == 0.0F);

	assert(F4T::ProxyRemoteAnimationState::ClampTurnDeltaDegreesPerSecond(0.0F) == 0.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampTurnDeltaDegreesPerSecond(720.0F) == 720.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampTurnDeltaDegreesPerSecond(-720.0F) == -720.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampTurnDeltaDegreesPerSecond(10000.0F) == 720.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampTurnDeltaDegreesPerSecond(-10000.0F) == -720.0F);
	assert(F4T::ProxyRemoteAnimationState::ClampTurnDeltaDegreesPerSecond(nan) == 0.0F);

	assert(F4T::ProxyRemoteAnimationState::EaseAimPitchDegrees(0.0F, 40.0F, 0.0) == 0.0F);
	assert(F4T::ProxyRemoteAnimationState::EaseAimPitchDegrees(0.0F, 0.0F, 0.05) == 0.0F);

	const auto eased = F4T::ProxyRemoteAnimationState::EaseAimPitchDegrees(0.0F, 40.0F, 0.05);
	assert(eased > 0.0F && eased < 40.0F);

	const auto nearlySettled = F4T::ProxyRemoteAnimationState::EaseAimPitchDegrees(39.9F, 40.0F, 0.05);
	assert(nearlySettled == 40.0F);

	const auto clampedCurrent = F4T::ProxyRemoteAnimationState::EaseAimPitchDegrees(180.0F, 0.0F, 0.05);
	assert(clampedCurrent >= 0.0F && clampedCurrent <= 80.0F);

	assert(F4T::ProxyRemoteAnimationState::EaseAimPitchDegrees(0.0F, nan, 0.05) == 0.0F);

	const auto fields = MakeRemoteAnimationFields(90.0, 30.0, 45.0);
	assert(fields.hasAnimationDirection);
	assert(NearlyEqual(fields.animationDirection, 90.0F));
	assert(fields.hasAimState);
	assert(NearlyEqual(fields.aimPitch, 30.0F));
	assert(NearlyEqual(fields.turnDelta, 45.0F));

	const auto clampedFields = MakeRemoteAnimationFields(720.0, 180.0, 10000.0);
	assert(NearlyEqual(clampedFields.animationDirection, 360.0F));
	assert(NearlyEqual(clampedFields.aimPitch, 80.0F));
	assert(NearlyEqual(clampedFields.turnDelta, 720.0F));

	const auto missingFields = MakeRemoteAnimationFields(std::nullopt, std::nullopt, std::nullopt);
	assert(!missingFields.hasAnimationDirection);
	assert(missingFields.animationDirection == 0.0F);
	assert(!missingFields.hasAimState);
	assert(missingFields.aimPitch == 0.0F);
	assert(missingFields.turnDelta == 0.0F);

	const auto directionOnly = MakeRemoteAnimationFields(45.0, std::nullopt, std::nullopt);
	assert(directionOnly.hasAnimationDirection);
	assert(!directionOnly.hasAimState);

	const auto pitchOnly = MakeRemoteAnimationFields(std::nullopt, -30.0, std::nullopt);
	assert(!pitchOnly.hasAnimationDirection);
	assert(pitchOnly.hasAimState);
	assert(NearlyEqual(pitchOnly.aimPitch, -30.0F));

	const auto turnOnly = MakeRemoteAnimationFields(std::nullopt, std::nullopt, 12.5);
	assert(turnOnly.hasAimState);
	assert(turnOnly.aimPitch == 0.0F);
	assert(NearlyEqual(turnOnly.turnDelta, 12.5F));

	const auto nonFiniteFields = MakeRemoteAnimationFields(
		std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(),
		-std::numeric_limits<double>::infinity());
	assert(nonFiniteFields.hasAnimationDirection);
	assert(nonFiniteFields.animationDirection == 0.0F);
	assert(nonFiniteFields.hasAimState);
	assert(nonFiniteFields.aimPitch == 0.0F);
	assert(nonFiniteFields.turnDelta == 0.0F);

	assert(F4T::ProxyRemoteAnimationState::DetectSneakTransition(false, false, false) == SneakTransition::kNone);
	assert(F4T::ProxyRemoteAnimationState::DetectSneakTransition(false, true, false) == SneakTransition::kNone);
	assert(F4T::ProxyRemoteAnimationState::DetectSneakTransition(false, false, true) == SneakTransition::kStart);
	assert(F4T::ProxyRemoteAnimationState::DetectSneakTransition(false, true, true) == SneakTransition::kStart);
	assert(F4T::ProxyRemoteAnimationState::DetectSneakTransition(true, false, false) == SneakTransition::kNone);
	assert(F4T::ProxyRemoteAnimationState::DetectSneakTransition(true, true, true) == SneakTransition::kNone);
	assert(F4T::ProxyRemoteAnimationState::DetectSneakTransition(true, false, true) == SneakTransition::kStart);
	assert(F4T::ProxyRemoteAnimationState::DetectSneakTransition(true, true, false) == SneakTransition::kStop);

	assert(F4T::ProxyRemoteAnimationState::kSneakReassertInterval == std::chrono::milliseconds{ 1000 });

	using F4T::ProxyRemoteAnimationState::ShouldApplyProxyAnimation;
	assert(ShouldApplyProxyAnimation(false, false, false, false));
	assert(!ShouldApplyProxyAnimation(true, false, false, false));
	assert(ShouldApplyProxyAnimation(true, true, false, false));
	assert(ShouldApplyProxyAnimation(true, false, true, false));
	assert(ShouldApplyProxyAnimation(true, false, false, true));

	F4T::ProxyPuppet::ProxyLocomotionState locomotion{};
	locomotion.aimPitchDegrees = 12.5F;
	locomotion.turnDelta = -3.0F;
	locomotion.hasAimState = true;
	F4T::ProxyPuppet::ApplyLocomotionMotionFields(locomotion, 120.0F, 45.0F, 1.0F, 2.0F, 3.0F);
	assert(locomotion.hasAimState);
	assert(locomotion.aimPitchDegrees == 12.5F);
	assert(locomotion.turnDelta == -3.0F);
	assert(locomotion.speed == 120.0F);
	assert(locomotion.direction == 45.0F);
	assert(locomotion.velocityX == 1.0F);
	assert(locomotion.velocityY == 2.0F);
	assert(locomotion.velocityZ == 3.0F);

	F4T::ProxyPuppet::ApplyLocomotionMotionFields(locomotion, -1.0F, 90.0F, 0.0F, 0.0F, 0.0F);
	assert(locomotion.hasAimState);
	assert(locomotion.aimPitchDegrees == 12.5F);
	assert(locomotion.turnDelta == -3.0F);
	assert(locomotion.speed == -1.0F);
	assert(locomotion.direction == 90.0F);

	return 0;
}
