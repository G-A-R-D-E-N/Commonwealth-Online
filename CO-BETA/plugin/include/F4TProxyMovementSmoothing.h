#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace F4T::ProxyMovementSmoothing
{
	inline constexpr double kPositionSmoothingRatePerSecond = 9.75;
	inline constexpr double kFrameApproachRatePerSecond = 17.26;
	inline constexpr double kDefaultFrameDeltaSeconds = 1.0 / 60.0;
	inline constexpr double kMinFrameDeltaSeconds = 1.0 / 120.0;
	inline constexpr double kMaxFrameDeltaSeconds = 0.1;
	inline constexpr double kUnsetSampleSeconds = 0.0;
	inline constexpr float kTierHintAgreementRatio = 0.4F;
	inline constexpr float kNoMeasuredSpeed = std::numeric_limits<float>::quiet_NaN();
	inline constexpr float kRunGaitSpeed = 280.0F;

	struct Parameters
	{
		double holdSeconds{ 0.100 };
		double idleSeconds{ 0.250 };
		double gaitHysteresisSeconds{ 0.300 };
		float gaitFloorScale{ 0.95F };
		float runGaitThreshold{ kRunGaitSpeed };
		float walkGaitCeiling{ 279.0F };
		float motionSpeedThreshold{ 8.0F };
	};

	enum class Gait
	{
		kIdle,
		kWalk,
		kRun
	};

	struct State
	{
		bool hasLocomotion{ false };
		double lastLocomotionTime{ 0.0 };
		float lastReceivedSpeed{ 0.0F };
		bool lastReceivedSprinting{ false };
		float lastReceivedGraphSpeed{ -1.0F };
		Gait gait{ Gait::kIdle };
		bool hasGaitCandidate{ false };
		Gait gaitCandidate{ Gait::kIdle };
		double gaitCandidateSince{ 0.0 };
	};

	struct Decision
	{
		bool engaging{ false };
		bool idle{ true };
		Gait gait{ Gait::kIdle };
		float coastSpeed{ 0.0F };
		float gaitSpeed{ 0.0F };
	};

	inline void Reset(State& a_state)
	{
		a_state = State{};
	}

	inline float SmoothingAlpha(double a_deltaSeconds, double a_ratePerSecond)
	{
		if (!(a_deltaSeconds > 0.0) || !(a_ratePerSecond > 0.0)) {
			return 0.0F;
		}

		return static_cast<float>(-std::expm1(-a_ratePerSecond * a_deltaSeconds));
	}

	inline float FrameDeltaSeconds(double& a_lastSampleSeconds, double a_nowSeconds)
	{
		float deltaSeconds = static_cast<float>(kDefaultFrameDeltaSeconds);
		if (a_lastSampleSeconds > kUnsetSampleSeconds) {
			deltaSeconds = static_cast<float>(a_nowSeconds - a_lastSampleSeconds);
		}

		a_lastSampleSeconds = a_nowSeconds;
		return static_cast<float>(std::clamp(
			static_cast<double>(deltaSeconds),
			kMinFrameDeltaSeconds,
			kMaxFrameDeltaSeconds));
	}

	inline void NoteRemoteSample(
		State& a_state,
		double a_nowSeconds,
		float a_movementSpeed,
		bool a_isSprinting,
		bool a_isLocomoting,
		float a_animationGraphSpeed)
	{
		if (!a_isLocomoting) {
			return;
		}

		a_state.hasLocomotion = true;
		a_state.lastLocomotionTime = a_nowSeconds;
		a_state.lastReceivedSpeed =
			std::isfinite(a_movementSpeed) ? std::max(a_movementSpeed, 0.0F) : 0.0F;
		a_state.lastReceivedSprinting = a_isSprinting;
		a_state.lastReceivedGraphSpeed =
			std::isfinite(a_animationGraphSpeed) ? a_animationGraphSpeed : -1.0F;
	}

	inline float HeldSpeed(const State& a_state, double a_nowSeconds, const Parameters& a_parameters = {})
	{
		if (!a_state.hasLocomotion) {
			return 0.0F;
		}

		const auto heldFor = a_nowSeconds - a_state.lastLocomotionTime;
		if (heldFor <= a_parameters.holdSeconds) {
			return a_state.lastReceivedSpeed;
		}
		if (heldFor >= a_parameters.idleSeconds) {
			return 0.0F;
		}

		const auto coastFraction = (a_parameters.idleSeconds - heldFor) /
			(a_parameters.idleSeconds - a_parameters.holdSeconds);
		return a_state.lastReceivedSpeed * static_cast<float>(std::clamp(coastFraction, 0.0, 1.0));
	}

	inline float FloorGaitSpeed(
		float a_lastReceivedSpeed,
		float a_coastSpeed,
		float a_measuredSpeed,
		const Parameters& a_parameters = {})
	{
		float gaitSpeed = std::max(a_coastSpeed, 0.0F);
		if (a_lastReceivedSpeed > 0.0F && a_measuredSpeed >= a_parameters.motionSpeedThreshold) {
			gaitSpeed = std::max(gaitSpeed, a_parameters.gaitFloorScale * a_lastReceivedSpeed);
		}
		return gaitSpeed;
	}

	inline Gait DesiredGait(float a_speed, bool a_isSprinting, const Parameters& a_parameters = {})
	{
		if (!(a_speed > 0.0F)) {
			return Gait::kIdle;
		}

		return (a_isSprinting || a_speed >= a_parameters.runGaitThreshold) ? Gait::kRun : Gait::kWalk;
	}

	inline Gait DesiredTierGait(
		const State& a_state,
		float a_coastSpeed,
		float a_measuredSpeed,
		const Parameters& a_parameters = {})
	{
		const bool moving = a_measuredSpeed >= a_parameters.motionSpeedThreshold;
		const auto hint = a_state.lastReceivedGraphSpeed;
		if (!moving || !(hint > 0.0F)) {
			return DesiredGait(moving ? a_measuredSpeed : a_coastSpeed, a_state.lastReceivedSprinting, a_parameters);
		}

		if (std::fabs(hint - a_measuredSpeed) <= kTierHintAgreementRatio * hint) {
			return DesiredGait(hint, a_state.lastReceivedSprinting, a_parameters);
		}

		return DesiredGait(a_measuredSpeed, false, a_parameters);
	}

	inline float GaitGraphSpeed(float a_graphSpeed, Gait a_gait, const Parameters& a_parameters = {})
	{
		if (a_gait == Gait::kRun) {
			return std::max(a_graphSpeed, a_parameters.runGaitThreshold);
		}
		if (a_gait == Gait::kWalk) {
			return std::min(a_graphSpeed, a_parameters.walkGaitCeiling);
		}
		return a_graphSpeed;
	}

	inline Gait ResolveGait(
		State& a_state,
		double a_nowSeconds,
		Gait a_desired,
		const Parameters& a_parameters = {})
	{
		if (a_desired == a_state.gait) {
			a_state.hasGaitCandidate = false;
			return a_state.gait;
		}

		if (!a_state.hasGaitCandidate || a_state.gaitCandidate != a_desired) {
			a_state.hasGaitCandidate = true;
			a_state.gaitCandidate = a_desired;
			a_state.gaitCandidateSince = a_nowSeconds;
			return a_state.gait;
		}

		if (a_nowSeconds - a_state.gaitCandidateSince >= a_parameters.gaitHysteresisSeconds) {
			a_state.gait = a_desired;
			a_state.hasGaitCandidate = false;
		}

		return a_state.gait;
	}

	inline Decision Update(
		State& a_state,
		double a_nowSeconds,
		float a_measuredSpeed,
		const Parameters& a_parameters = {})
	{
		Decision decision{};
		decision.coastSpeed = HeldSpeed(a_state, a_nowSeconds, a_parameters);
		decision.engaging = a_state.hasLocomotion && decision.coastSpeed > 0.0F;
		decision.idle = !decision.engaging;

		if (decision.idle) {
			a_state.gait = Gait::kIdle;
			a_state.hasGaitCandidate = false;
			return decision;
		}

		decision.gaitSpeed = FloorGaitSpeed(
			a_state.lastReceivedSpeed,
			decision.coastSpeed,
			a_measuredSpeed,
			a_parameters);

		const auto desired = DesiredTierGait(a_state, decision.coastSpeed, a_measuredSpeed, a_parameters);
		if (a_state.gait == Gait::kIdle) {
			a_state.gait = desired;
			a_state.hasGaitCandidate = false;
		} else if (!std::isnan(a_measuredSpeed)) {
			ResolveGait(a_state, a_nowSeconds, desired, a_parameters);
		}

		decision.gait = a_state.gait;
		if (decision.gait == Gait::kWalk) {
			decision.gaitSpeed = std::min(decision.gaitSpeed, a_parameters.walkGaitCeiling);
		}

		return decision;
	}
}
