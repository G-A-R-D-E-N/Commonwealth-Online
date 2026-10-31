#pragma once

#include "F4TReplicationRuntime.h"

#include "RE/P/PlayerCharacter.h"

#include <chrono>
#include <string_view>

namespace F4T::LocalPlayerMovement
{
	inline constexpr auto kMovingSpeedThreshold = 1.0F;

	F4T::ReplicationRuntime::PlayerMovementState GetPlayerMovementState(
		RE::PlayerCharacter& a_player,
		const F4T::ReplicationRuntime::PlayerTransform* a_previousSample,
		std::chrono::steady_clock::time_point a_previousSampleTime,
		const F4T::ReplicationRuntime::PlayerTransform& a_currentTransform,
		std::chrono::steady_clock::time_point a_currentTime,
		std::chrono::steady_clock::time_point& a_movingHoldUntil,
		bool a_logDiagnostics);
	bool HasMovementStateChanged(
		const F4T::ReplicationRuntime::PlayerMovementState& a_previous,
		const F4T::ReplicationRuntime::PlayerMovementState& a_current);
	bool HasMeaningfulTransformDelta(
		const F4T::ReplicationRuntime::PlayerTransform& a_previous,
		const F4T::ReplicationRuntime::PlayerTransform& a_current);
	void LogPlayerTransform(const F4T::ReplicationRuntime::PlayerTransform& a_transform);
	void RequestExplicitTeleport();
	const char* GetSpecialMovementType(
		const F4T::ReplicationRuntime::PlayerLocation& a_previousLocation,
		const F4T::ReplicationRuntime::PlayerLocation& a_currentLocation,
		const F4T::ReplicationRuntime::PlayerTransform& a_previousTransform,
		const F4T::ReplicationRuntime::PlayerTransform& a_currentTransform);
	void LogSpecialMovement(std::string_view a_movementType);
}
