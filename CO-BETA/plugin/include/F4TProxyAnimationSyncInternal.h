#pragma once

#include "F4AnimationDescriptor.h"
#include "F4TProxyAnimationSync.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::ProxyAnimationSync::detail
{
	inline constexpr float kIdleSpeedThreshold = 1.0F;
	inline constexpr float kSpeedEpsilon = 0.5F;
	inline constexpr auto kProxyMovingStopHoldDuration = std::chrono::milliseconds{ 150 };

	inline constexpr float kGraphSpeedMin = 0.0F;
	inline constexpr float kGraphSpeedMax = 500.0F;
	inline constexpr float kGraphSpeedIdle = 0.0F;

	struct DesiredProxyAnimationState
	{
		bool isMoving{ false };
		bool isSprinting{ false };
		bool isSneaking{ false };
		bool isJumping{ false };
		bool isCrouching{ false };
		bool weaponDrawn{ false };
		float graphSpeed{ 0.0F };
		float direction{ 0.0F };
	};

	void LogInfoWithLocalPlayerPrefix(std::string_view a_message);

	void LogWarningWithLocalPlayerPrefix(std::string_view a_message);

	bool AreFloatsDifferent(float a_lhs, float a_rhs);

	bool ProxyHasLoaded3D(const RE::Actor& a_proxy);

	bool ProxyMeshReadyForPresentation(const RE::Actor& a_proxy);

	bool CanWriteProxyGraph(RE::Actor& a_proxy, std::uint32_t a_remotePlayerId, std::uint32_t a_actorId);

	bool TrySendGraphEvent(RE::Actor& a_proxy, const char* a_eventName);

	bool TrySetGraphBool(
		RE::IAnimationGraphManagerHolder& a_holder,
		const char* a_variableName,
		bool a_value);

	bool TrySetGraphInt(
		RE::IAnimationGraphManagerHolder& a_holder,
		const char* a_variableName,
		std::int32_t a_value);

	bool TrySetGraphFloat(
		RE::IAnimationGraphManagerHolder& a_holder,
		const char* a_variableName,
		float a_value);

	float ClampGraphSpeed(float a_speed);

	void PopulateLocomotionSpeedSnapshot(
		F4T::AnimationDescriptor::AnimationVariableSnapshot& a_snapshot,
		const F4T::AnimationDescriptor::F4AnimationDescriptor& a_descriptor,
		float a_graphSpeed);

	float ComputeGraphSpeed(const F4T::RemotePlayerState::RemotePlayerState& a_remotePlayer);

	void LogAppliedTransition(
		std::uint32_t a_remotePlayerId,
		std::uint32_t a_actorId,
		const DesiredProxyAnimationState& a_desired,
		bool a_initial);

	DesiredProxyAnimationState BuildDesiredState(const F4T::RemotePlayerState::RemotePlayerState& a_remotePlayer);

	bool ApplyMovingHoldState(
		ProxyAppliedAnimationState& a_state,
		bool a_rawIsMoving);

	DesiredProxyAnimationState BuildDebouncedDesiredState(
		ProxyAppliedAnimationState& a_state,
		const F4T::RemotePlayerState::RemotePlayerState& a_remotePlayer);
}
