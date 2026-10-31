#include "F4TLocalPlayerMovement.h"

#include "F4TDebugLogging.h"
#include "F4TLocalPlayerState.h"
#include "F4TProxyMovementDirection.h"

#include "RE/A/ACTOR_STANCE.h"
#include "RE/A/AIProcess.h"
#include "RE/M/MiddleHighProcessData.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <string_view>

namespace F4T::LocalPlayerMovement
{
	using F4T::ReplicationRuntime::PlayerTransform;
	using F4T::ReplicationRuntime::PlayerLocation;
	using F4T::ReplicationRuntime::PlayerMovementState;
	using F4T::LocalPlayerState::LogInfoWithLocalPlayerPrefix;

	namespace
	{
	struct SneakDetectionState
	{
		bool apiSneaking;
		bool actorStateSneaking;
		bool actorStateForceSneak;
		std::uint32_t actorStateStance;
		bool finalIsSneaking;
	};

	constexpr auto kTransformSendPositionThreshold = 3.0F;
	constexpr auto kTransformSendRotationThreshold = 0.02F;
	constexpr auto kTeleportDistanceThreshold = 5000.0F;
	constexpr auto kMovementStopHoldDuration = 100ms;
	constexpr auto kJumpVerticalSpeedThreshold = 80.0F;
	constexpr auto kJumpStateHoldDuration = 150ms;
	constexpr auto kJumpDiagnosticLogInterval = 1s;
	constexpr auto kSneakDiagnosticLogInterval = 2s;
	constexpr auto kDirectionIdleSpeed = 1.0F;
	constexpr auto kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
	std::atomic<bool> s_pendingExplicitTeleport{ false };

	float GetAnimationDirectionDegrees(
		const PlayerTransform* a_previousSample,
		const PlayerTransform& a_currentTransform,
		float a_elapsedSeconds)
	{
		if (!a_previousSample || a_elapsedSeconds <= 0.0F) {
			return 0.0F;
		}

		const auto velocityX = (a_currentTransform.x - a_previousSample->x) / a_elapsedSeconds;
		const auto velocityY = (a_currentTransform.y - a_previousSample->y) / a_elapsedSeconds;
		const auto radians = F4T::ProxyMovementDirection::RelativeMovementDirection(
			velocityX,
			velocityY,
			a_currentTransform.angleZ,
			kDirectionIdleSpeed);

		return std::isfinite(radians) ? radians * kRadiansToDegrees : 0.0F;
	}

	float GetAimPitchDegrees(const RE::PlayerCharacter& a_player)
	{
		const auto* process = a_player.currentProcess;
		const auto* middleHigh = process ? process->middleHigh : nullptr;
		const auto radians = middleHigh ? middleHigh->rotation.x : 0.0F;
		return std::isfinite(radians) ? radians * kRadiansToDegrees : 0.0F;
	}

	float GetTurnDeltaDegreesPerSecond(
		const PlayerTransform* a_previousSample,
		const PlayerTransform& a_currentTransform,
		float a_elapsedSeconds)
	{
		if (!a_previousSample || a_elapsedSeconds <= 0.0F) {
			return 0.0F;
		}

		constexpr auto kPi = 3.14159265358979323846F;
		constexpr auto kTwoPi = 2.0F * kPi;
		auto delta = a_currentTransform.angleZ - a_previousSample->angleZ;
		if (!std::isfinite(delta)) {
			return 0.0F;
		}

		delta = std::remainder(delta, kTwoPi);

		return (delta / a_elapsedSeconds) * kRadiansToDegrees;
	}

	float GetSquaredPositionDistance(const PlayerTransform& a_previous, const PlayerTransform& a_current)
	{
		const auto deltaX = a_current.x - a_previous.x;
		const auto deltaY = a_current.y - a_previous.y;
		const auto deltaZ = a_current.z - a_previous.z;

		return (deltaX * deltaX) + (deltaY * deltaY) + (deltaZ * deltaZ);
	}

	float GetSampleElapsedSeconds(
		const PlayerTransform* a_previousSample,
		std::chrono::steady_clock::time_point a_previousSampleTime,
		std::chrono::steady_clock::time_point a_currentTime)
	{
		if (!a_previousSample || a_previousSampleTime.time_since_epoch().count() == 0) {
			return 0.0F;
		}

		const auto elapsedSeconds = std::chrono::duration<float>(a_currentTime - a_previousSampleTime).count();
		if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0F) {
			return 0.0F;
		}

		return elapsedSeconds;
	}

	float GetMovementSpeed(
		const PlayerTransform* a_previousSample,
		const PlayerTransform& a_currentTransform,
		float a_elapsedSeconds)
	{
		if (!a_previousSample || a_elapsedSeconds <= 0.0F) {
			return 0.0F;
		}

		auto movementSpeed = std::sqrt(GetSquaredPositionDistance(*a_previousSample, a_currentTransform)) / a_elapsedSeconds;
		if (!std::isfinite(movementSpeed) || movementSpeed < 0.0F) {
			return 0.0F;
		}

		return movementSpeed;
	}

	float GetVerticalSpeed(
		const PlayerTransform* a_previousSample,
		const PlayerTransform& a_currentTransform,
		float a_elapsedSeconds)
	{
		if (!a_previousSample || a_elapsedSeconds <= 0.0F) {
			return 0.0F;
		}

		const auto verticalSpeed = (a_currentTransform.z - a_previousSample->z) / a_elapsedSeconds;
		if (!std::isfinite(verticalSpeed)) {
			return 0.0F;
		}

		return verticalSpeed;
	}

	bool ShouldLogJumpDetection(bool a_isJumping, std::chrono::steady_clock::time_point a_currentTime)
	{
		static bool lastLoggedJumping = false;
		static auto lastLogTime = std::chrono::steady_clock::time_point{};

		const auto jumpStateChanged = a_isJumping != lastLoggedJumping;
		const auto logThrottledJumpState =
			a_isJumping && (lastLogTime.time_since_epoch().count() == 0 || a_currentTime - lastLogTime >= kJumpDiagnosticLogInterval);
		if (!jumpStateChanged && !logThrottledJumpState) {
			return false;
		}

		lastLoggedJumping = a_isJumping;
		lastLogTime = a_currentTime;
		return true;
	}

	SneakDetectionState GetSneakDetectionState(RE::PlayerCharacter& a_player)
	{
		const auto apiSneaking = a_player.IsSneaking();
		const auto actorStateStance = a_player.stance;
		const auto actorStateSneaking =
			actorStateStance == static_cast<std::uint32_t>(RE::ACTOR_STANCE::kSneaking);
		const auto actorStateForceSneak = a_player.forceSneak != 0;

		return {
			apiSneaking,
			actorStateSneaking,
			actorStateForceSneak,
			actorStateStance,
			apiSneaking || actorStateSneaking || actorStateForceSneak
		};
	}

	bool GetCrouchState(RE::PlayerCharacter& a_player)
	{
		return F4T::LocalPlayerState::GetLocalPlayerCrouching(a_player);
	}

	bool ShouldLogSneakDetection(
		const SneakDetectionState& a_sneakDetection,
		std::chrono::steady_clock::time_point a_currentTime)
	{
		static bool hasLastLoggedDetection = false;
		static SneakDetectionState lastLoggedDetection{};
		static auto lastLogTime = std::chrono::steady_clock::time_point{};

		const auto detectionChanged = !hasLastLoggedDetection ||
		                              lastLoggedDetection.apiSneaking != a_sneakDetection.apiSneaking ||
		                              lastLoggedDetection.actorStateSneaking != a_sneakDetection.actorStateSneaking ||
		                              lastLoggedDetection.actorStateForceSneak != a_sneakDetection.actorStateForceSneak ||
		                              lastLoggedDetection.actorStateStance != a_sneakDetection.actorStateStance ||
		                              lastLoggedDetection.finalIsSneaking != a_sneakDetection.finalIsSneaking;
		const auto diagnosticThrottled =
			lastLogTime.time_since_epoch().count() == 0 ||
			a_currentTime - lastLogTime >= kSneakDiagnosticLogInterval;

		if (!detectionChanged && !diagnosticThrottled) {
			return false;
		}

		hasLastLoggedDetection = true;
		lastLoggedDetection = a_sneakDetection;
		lastLogTime = a_currentTime;
		return true;
	}

	bool ResolveMovementHoldState(
		bool a_derivedMoving,
		std::chrono::steady_clock::time_point a_currentTime,
		std::chrono::steady_clock::time_point& a_movingHoldUntil)
	{
		if (a_derivedMoving) {
			a_movingHoldUntil = a_currentTime + kMovementStopHoldDuration;
			return true;
		}

		return a_movingHoldUntil.time_since_epoch().count() != 0 && a_currentTime < a_movingHoldUntil;
	}

	PlayerMovementState BuildPlayerMovementState(
		RE::PlayerCharacter& a_player,
		float a_movementSpeed,
		float a_verticalSpeed,
		std::chrono::steady_clock::time_point a_currentTime,
		std::chrono::steady_clock::time_point& a_movingHoldUntil,
		bool a_logDiagnostics)
	{
		static auto jumpHoldUntil = std::chrono::steady_clock::time_point{};

		const auto derivedMoving = a_movementSpeed >= kMovingSpeedThreshold;
		const auto isMoving = ResolveMovementHoldState(derivedMoving, a_currentTime, a_movingHoldUntil);
		const auto sneakDetection = GetSneakDetectionState(a_player);
		const auto apiJumping = a_player.IsJumping();
		if (apiJumping) {
			jumpHoldUntil = a_currentTime + kJumpStateHoldDuration;
		}

		const auto heldJumping = a_currentTime < jumpHoldUntil;
		if (a_logDiagnostics && ShouldLogJumpDetection(heldJumping, a_currentTime)) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Local jump detection: apiJumping={}, verticalSpeed={:.1f}, heldJumping={}",
				apiJumping,
				a_verticalSpeed,
				heldJumping));
		}

		if (a_logDiagnostics && ShouldLogSneakDetection(sneakDetection, a_currentTime)) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Local sneak detection: apiSneaking={}, actorStateSneaking={}, actorStateForceSneak={}, actorStateStance={}, finalIsSneaking={}",
				sneakDetection.apiSneaking,
				sneakDetection.actorStateSneaking,
				sneakDetection.actorStateForceSneak,
				sneakDetection.actorStateStance,
				sneakDetection.finalIsSneaking));
		}

		return {
			isMoving,
			a_player.DoGetSprinting(),
			sneakDetection.finalIsSneaking,
			heldJumping,
			GetCrouchState(a_player),
			a_player.GetWeaponMagicDrawn(),
			a_movementSpeed
		};
	}

	bool HasCellChanged(const PlayerLocation& a_previous, const PlayerLocation& a_current)
	{
		return a_previous.hasCellId != a_current.hasCellId ||
		       (a_previous.hasCellId && a_previous.cellId != a_current.cellId);
	}

	bool HasWorldspaceChanged(const PlayerLocation& a_previous, const PlayerLocation& a_current)
	{
		return a_previous.hasWorldspaceId != a_current.hasWorldspaceId ||
		       (a_previous.hasWorldspaceId && a_previous.worldspaceId != a_current.worldspaceId);
	}

	bool HasTeleportDistance(const PlayerTransform& a_previous, const PlayerTransform& a_current)
	{
		constexpr auto squaredTeleportThreshold = kTeleportDistanceThreshold * kTeleportDistanceThreshold;
		return GetSquaredPositionDistance(a_previous, a_current) > squaredTeleportThreshold;
	}
	}

	PlayerMovementState GetPlayerMovementState(
		RE::PlayerCharacter& a_player,
		const PlayerTransform* a_previousSample,
		std::chrono::steady_clock::time_point a_previousSampleTime,
		const PlayerTransform& a_currentTransform,
		std::chrono::steady_clock::time_point a_currentTime,
		std::chrono::steady_clock::time_point& a_movingHoldUntil,
		bool a_logDiagnostics)
	{
		const auto elapsedSeconds = GetSampleElapsedSeconds(a_previousSample, a_previousSampleTime, a_currentTime);
		const auto movementSpeed = GetMovementSpeed(a_previousSample, a_currentTransform, elapsedSeconds);
		const auto verticalSpeed = GetVerticalSpeed(a_previousSample, a_currentTransform, elapsedSeconds);

		auto state = BuildPlayerMovementState(
			a_player,
			movementSpeed,
			verticalSpeed,
			a_currentTime,
			a_movingHoldUntil,
			a_logDiagnostics);
		state.animationDirectionDegrees = GetAnimationDirectionDegrees(a_previousSample, a_currentTransform, elapsedSeconds);
		state.aimPitchDegrees = GetAimPitchDegrees(a_player);
		state.turnDeltaDegreesPerSecond = GetTurnDeltaDegreesPerSecond(a_previousSample, a_currentTransform, elapsedSeconds);
		static std::atomic<std::uint64_t> s_aimCaptureCount{ 0 };
		const auto aimCaptureCount = s_aimCaptureCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (aimCaptureCount <= 5 || aimCaptureCount % 300 == 0) {
			F4T::DebugLogging::Log(
				"LocalAim",
				"[RT-VERIFY 364-A] local aim capture #{} dataAngleXRadians={:.4f} aimPitchDegrees={:.2f} animationDirection={:.2f} turnDelta={:.2f} sneaking={} crouching={}",
				aimCaptureCount,
				a_player.data.angle.x,
				state.aimPitchDegrees,
				state.animationDirectionDegrees,
				state.turnDeltaDegreesPerSecond,
				state.isSneaking,
				state.isCrouching);
		}

		return state;
	}

	bool HasMovementStateChanged(const PlayerMovementState& a_previous, const PlayerMovementState& a_current)
	{
		return a_previous.isMoving != a_current.isMoving ||
		       a_previous.isSprinting != a_current.isSprinting ||
		       a_previous.isSneaking != a_current.isSneaking ||
		       a_previous.isJumping != a_current.isJumping ||
		       a_previous.isCrouching != a_current.isCrouching ||
		       a_previous.weaponDrawn != a_current.weaponDrawn ||
		       F4T::ReplicationRuntime::HasAnimationFieldsChanged(a_previous, a_current);
	}

	bool HasMeaningfulTransformDelta(const PlayerTransform& a_previous, const PlayerTransform& a_current)
	{
		auto deltaAngleZ = a_current.angleZ - a_previous.angleZ;

		constexpr auto fullRotationRadians = 360.0F * 3.14159265358979323846F / 180.0F;
		constexpr auto halfRotationRadians = fullRotationRadians / 2.0F;
		while (deltaAngleZ > halfRotationRadians) {
			deltaAngleZ -= fullRotationRadians;
		}
		while (deltaAngleZ < -halfRotationRadians) {
			deltaAngleZ += fullRotationRadians;
		}

		const auto squaredPositionDelta = GetSquaredPositionDistance(a_previous, a_current);
		constexpr auto squaredPositionThreshold = kTransformSendPositionThreshold * kTransformSendPositionThreshold;

		return squaredPositionDelta > squaredPositionThreshold ||
		       deltaAngleZ > kTransformSendRotationThreshold || deltaAngleZ < -kTransformSendRotationThreshold;
	}

	void LogPlayerTransform(const PlayerTransform& a_transform)
	{
		LogInfoWithLocalPlayerPrefix(std::format("Player position: X={:.2f}, Y={:.2f}, Z={:.2f}, AngleZ={:.2f}",
			a_transform.x,
			a_transform.y,
			a_transform.z,
			a_transform.angleZ));
	}

	void RequestExplicitTeleport()
	{
		s_pendingExplicitTeleport.store(true);
	}

	const char* GetSpecialMovementType(
		const PlayerLocation& a_previousLocation,
		const PlayerLocation& a_currentLocation,
		const PlayerTransform& a_previousTransform,
		const PlayerTransform& a_currentTransform)
	{
		const auto explicitTeleport = s_pendingExplicitTeleport.exchange(false);
		if (HasWorldspaceChanged(a_previousLocation, a_currentLocation)) {
			return "worldspace_change";
		}
		if (HasCellChanged(a_previousLocation, a_currentLocation)) {
			return "cell_change";
		}
		if (explicitTeleport || HasTeleportDistance(a_previousTransform, a_currentTransform)) {
			return "teleport";
		}

		return "normal";
	}

	void LogSpecialMovement(std::string_view a_movementType)
	{
		if (a_movementType == "worldspace_change") {
			LogInfoWithLocalPlayerPrefix("Detected worldspace change. Sending immediate transform update.");
		} else if (a_movementType == "cell_change") {
			LogInfoWithLocalPlayerPrefix("Detected cell change. Sending immediate transform update.");
		} else if (a_movementType == "teleport") {
			LogInfoWithLocalPlayerPrefix("Detected teleport/large position jump. Sending immediate transform update.");
		}
	}
}
