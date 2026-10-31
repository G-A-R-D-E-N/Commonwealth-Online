#pragma once

#include "F4TProxyMovementSmoothing.h"
#include "F4TProxySneakAction.h"
#include "F4TProxyWalkStart.h"
#include "F4TRemotePlayerState.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

namespace RE
{
	class Actor;
}

namespace F4T::ProxyAnimationSync
{
	namespace GraphVar
	{
		inline constexpr const char* kSpeed = "Speed";
		inline constexpr const char* kSpeedSmoothed = "SpeedSmoothed";
		inline constexpr const char* kDirection = "Direction";
		inline constexpr const char* kIsSprinting = "IsSprinting";
		inline constexpr const char* kInJumpState = "bInJumpState";
		inline constexpr const char* kIsInSneak = "iIsInSneak";
		inline constexpr const char* kSyncWalkRun = "iSyncWalkRun";
		inline constexpr const char* kSyncLocomotionSpeed = "iSyncLocomotionSpeed";
		inline constexpr const char* kSyncJumpState = "iSyncJumpState";
		inline constexpr const char* kSyncGunDown = "iSyncGunDown";
		inline constexpr const char* kRifleDrawnStateID = "iRifleDrawnStateID";
	}

	enum class LocomotionTier : std::int32_t
	{
		kWalk = 0,
		kJog = 1,
		kRun = 2,
	};

	LocomotionTier ComputeLocomotionTier(float a_graphSpeed, bool a_isSprinting);

	float MapMovementSpeedToGraphSpeed(float a_movementSpeed, bool a_isSprinting);

	float ResolveLocomotionGraphSpeed(
		float a_movementSpeed,
		float a_animationGraphSpeed,
		bool a_isMoving,
		bool a_isSprinting);

	struct ProxyAppliedAnimationState
	{
		bool hasAppliedGraphState{ false };
		bool lastIsMoving{ false };
		bool lastIsSprinting{ false };
		bool lastIsSneaking{ false };
		bool lastIsJumping{ false };
		bool lastIsCrouching{ false };
		bool lastWeaponDrawn{ false };
		float lastGraphSpeed{ 0.0F };
		float lastDirection{ 0.0F };
		std::optional<LocomotionTier> lastLocomotionTier{};
		std::chrono::steady_clock::time_point movingHoldUntil{};
		std::chrono::steady_clock::time_point jumpHoldUntil{};
		std::chrono::steady_clock::time_point sneakReassertDue{};
		F4T::ProxyWalkStart::RetryState walkStart{};
		std::uint64_t sneakGraphWrites{ 0 };
		std::uint32_t walkStartSends{ 0 };
	};

	void ResetAppliedAnimationState(ProxyAppliedAnimationState& a_state);

	void ApplyProxyAnimationFromRemoteState(
		ProxyAppliedAnimationState& a_state,
		RE::Actor& a_proxy,
		const RemotePlayerState::RemotePlayerState& a_remotePlayer,
		F4T::ProxyMovementSmoothing::Gait a_smoothedGait);

	void ClearProxyAnimationState(
		ProxyAppliedAnimationState& a_state,
		RE::Actor& a_proxy,
		std::uint32_t a_remotePlayerId,
		std::string_view a_reason);

	bool TrySendProxyGraphEvent(RE::Actor& a_proxy, std::string_view a_eventName);
}
