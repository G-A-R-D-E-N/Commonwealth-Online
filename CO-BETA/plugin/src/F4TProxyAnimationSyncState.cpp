#include "pch.h"

#include "F4TProxyAnimationSyncInternal.h"

#include <chrono>

namespace F4T::ProxyAnimationSync::detail
{
	DesiredProxyAnimationState BuildDesiredState(const F4T::RemotePlayerState::RemotePlayerState& a_remotePlayer)
	{
		DesiredProxyAnimationState desired{};
		desired.isMoving =
			a_remotePlayer.isMoving && a_remotePlayer.movementSpeed >= kIdleSpeedThreshold;
		desired.isSprinting = desired.isMoving && a_remotePlayer.isSprinting;
		desired.isSneaking = a_remotePlayer.isSneaking;
		desired.isJumping = a_remotePlayer.isJumping;
		desired.isCrouching = a_remotePlayer.isCrouching;
		desired.weaponDrawn = a_remotePlayer.weaponDrawn;
		desired.graphSpeed = ComputeGraphSpeed(a_remotePlayer);
		return desired;
	}

	bool ApplyMovingHoldState(
		F4T::ProxyAnimationSync::ProxyAppliedAnimationState& a_state,
		bool a_rawIsMoving)
	{
		const auto now = std::chrono::steady_clock::now();
		if (a_rawIsMoving) {
			a_state.movingHoldUntil = now + kProxyMovingStopHoldDuration;
			return true;
		}

		return a_state.movingHoldUntil.time_since_epoch().count() != 0 && now < a_state.movingHoldUntil;
	}

	DesiredProxyAnimationState BuildDebouncedDesiredState(
		F4T::ProxyAnimationSync::ProxyAppliedAnimationState& a_state,
		const F4T::RemotePlayerState::RemotePlayerState& a_remotePlayer)
	{
		const auto rawDesired = BuildDesiredState(a_remotePlayer);
		auto desired = rawDesired;
		desired.isMoving = ApplyMovingHoldState(a_state, rawDesired.isMoving);
		if (!rawDesired.isMoving &&
			desired.isMoving &&
			a_state.hasAppliedGraphState &&
			a_state.lastIsMoving) {
			desired.graphSpeed = a_state.lastGraphSpeed;
			desired.isSprinting = a_state.lastIsSprinting;
		}

		if (desired.isJumping) {
			desired.isMoving = false;
			desired.isSprinting = false;
		}

		if (!desired.isMoving) {
			desired.isSprinting = false;
			desired.graphSpeed = kGraphSpeedIdle;
		} else {
			if (!a_remotePlayer.isSprinting) {
				desired.isSprinting = false;
			}
		}

		return desired;
	}
}
