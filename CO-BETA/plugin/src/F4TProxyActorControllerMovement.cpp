#include "pch.h"

#include "F4TProxyActorControllerInternal.h"
#include "F4TProxyMovementSmoothing.h"
#include "F4TProxyMovementDiagnostics.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr float kProxyRotationLerpAlpha = 0.25F;
	constexpr auto kProxyMovementDiagnosticLogInterval = 2s;
	std::optional<std::uint32_t> g_lastObservedSneakPlayerId;
	std::optional<bool> g_lastObservedSneakState;

	void ApplyRuntimeProxyTransformWithAI(
		RE::Actor& a_proxy,
		const RE::NiPoint3& a_nextPosition,
		float a_heading,
		const RemotePlayer& a_remotePlayer,
		bool a_shouldSnap,
		float a_timeSinceLastSync)
	{
		(void)a_remotePlayer;

		a_proxy.SetHeading(a_heading);

		const float DRIFT_THRESHOLD = 100.0F;
		const float SYNC_INTERVAL = 0.2F;

		RE::NiPoint3 currentPosition = a_proxy.GetPosition();
		float distanceDrift = (a_nextPosition - currentPosition).Length();

		if (a_shouldSnap || a_timeSinceLastSync >= SYNC_INTERVAL || distanceDrift > DRIFT_THRESHOLD) {
			a_proxy.SetPosition(a_nextPosition, true);
		}
	}

	static std::unordered_set<RE::Actor*> g_loggedProxies;

	void ApplyRuntimeProxyTransform(
		RE::Actor& a_proxy,
		const RE::NiPoint3& a_nextPosition,
		float a_heading,
		const RemotePlayer& a_remotePlayer,
		bool a_shouldSnap,
		ProxyActorSlot* a_optSlot = nullptr)
	{
		if (g_loggedProxies.find(&a_proxy) == g_loggedProxies.end()) {
			g_loggedProxies.insert(&a_proxy);
			bool hasAI = a_proxy.currentProcess != nullptr;
			LogInfoWithLocalPlayerPrefix(std::format(
				"ApplyRuntimeProxyTransform: Proxy {:08X} has AIProcess: {}",
				a_proxy.GetFormID(), hasAI));
		}

		RE::NiPoint3 currentPosition = a_proxy.GetPosition();
		RE::NiPoint3 deltaPos = a_nextPosition - currentPosition;
		float distanceDrift = deltaPos.Length();

		constexpr float kLargeDriftSnapDistance = 1800.0F;
		constexpr float kInterpolationRatePerSecond = 8.0F;
		constexpr float kMinInterpolationAlpha = 0.08F;
		constexpr float kMaxInterpolationAlpha = 0.35F;

		const auto now = std::chrono::steady_clock::now();
		float deltaSeconds = 1.0F / 60.0F;
		if (a_optSlot && a_optSlot->lastPositionSyncTime != std::chrono::steady_clock::time_point{}) {
			deltaSeconds = std::chrono::duration<float>(now - a_optSlot->lastPositionSyncTime).count();
		}
		deltaSeconds = std::clamp(deltaSeconds, 1.0F / 120.0F, 0.1F);

		RE::NiPoint3 positionToApply = a_nextPosition;
		if (!a_shouldSnap && distanceDrift < kLargeDriftSnapDistance) {
			const float alpha = std::clamp(
				deltaSeconds * kInterpolationRatePerSecond,
				kMinInterpolationAlpha,
				kMaxInterpolationAlpha);
			positionToApply = F4T::InterpolationSystem::Lerp(currentPosition, a_nextPosition, alpha);
		}

		float headingToApply = a_heading;
		if (!a_optSlot) {
			const bool shouldSnapHeading =
				ShouldSnapRemoteMovement(a_remotePlayer.movementType) || distanceDrift >= kLargeDriftSnapDistance;
			if (!shouldSnapHeading) {
				const float rotationAlpha = a_shouldSnap ?
					kProxyRotationLerpAlpha :
					std::clamp(
						deltaSeconds * kInterpolationRatePerSecond,
						kMinInterpolationAlpha,
						kMaxInterpolationAlpha);
				headingToApply = F4T::InterpolationSystem::LerpRotation(
					a_proxy.GetHeading(),
					a_heading,
					rotationAlpha);
			}
		}

		const bool shouldForcePosition =
			a_shouldSnap || distanceDrift >= kLargeDriftSnapDistance;
		F4T::ProxyGameAPI::PositionRemoteActor(
			a_proxy,
			positionToApply,
			headingToApply,
			shouldForcePosition);

		if (a_optSlot) {
			a_optSlot->lastPositionSyncTime = now;
		}
	}

	double SteadySeconds(const std::chrono::steady_clock::time_point& a_time)
	{
		return std::chrono::duration<double>(a_time.time_since_epoch()).count();
	}

	float FrameDeltaSeconds(
		std::chrono::steady_clock::time_point& a_lastTime,
		const std::chrono::steady_clock::time_point& a_now)
	{
		double previousSeconds = 0.0;
		if (a_lastTime.time_since_epoch().count() != 0) {
			previousSeconds = SteadySeconds(a_lastTime);
		}

		const float deltaSeconds = F4T::ProxyMovementSmoothing::FrameDeltaSeconds(
			previousSeconds,
			SteadySeconds(a_now));
		a_lastTime = a_now;
		return deltaSeconds;
	}

	float Lerp(float a_current, float a_target, float a_alpha)
	{
		return a_current + ((a_target - a_current) * a_alpha);
	}

	RE::NiPoint3 LerpPosition(const RE::NiPoint3& a_current, const RE::NiPoint3& a_target, float a_alpha)
	{
		return {
			Lerp(a_current.x, a_target.x, a_alpha),
			Lerp(a_current.y, a_target.y, a_alpha),
			Lerp(a_current.z, a_target.z, a_alpha)
		};
	}

	F4T::Replication::MovementSnapshot MakeMovementSnapshot(
		const RE::NiPoint3& a_position,
		float a_heading,
		bool a_moving)
	{
		return F4T::Replication::MovementSnapshot{
			F4T::Replication::Vec3{ a_position.x, a_position.y, a_position.z },
			a_heading,
			a_moving
		};
	}
}

namespace F4T::ProxyActorController::detail
{
	bool ShouldSnapRemoteMovement(std::string_view a_movementType)
	{
		return a_movementType == "cell_change" || a_movementType == "worldspace_change" || a_movementType == "teleport";
	}

	void ResetRemoteMovementStateTracking()
	{
		g_lastObservedSneakPlayerId.reset();
		g_lastObservedSneakState.reset();
	}

	void ApplyRemoteMovementStateToProxy(RE::Actor& a_proxy, const RemotePlayer& a_remotePlayer)
	{
		(void)a_proxy;

		if (g_lastObservedSneakPlayerId != a_remotePlayer.playerId) {
			g_lastObservedSneakPlayerId = a_remotePlayer.playerId;
			g_lastObservedSneakState = a_remotePlayer.isSneaking;
			return;
		}

		if (!g_lastObservedSneakState) {
			g_lastObservedSneakState = a_remotePlayer.isSneaking;
			return;
		}

		if (*g_lastObservedSneakState == a_remotePlayer.isSneaking) {
			return;
		}

		g_lastObservedSneakState = a_remotePlayer.isSneaking;
		LogInfoWithLocalPlayerPrefix(std::format(
			"Remote player {} sneak state changed: isSneaking={}",
			a_remotePlayer.playerId,
			a_remotePlayer.isSneaking));
	}

	void LogProxyMovementDiagnostic(
		const RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		ActiveProxyKind a_proxyKind,
		bool a_sameCellPassed,
		ProxyMovementDiagnosticMode a_movementMode)
	{
		RE::NiPoint3 targetPosition{};
		targetPosition.x = a_remotePlayer.x;
		targetPosition.y = a_remotePlayer.y;
		targetPosition.z = a_remotePlayer.z;

		const auto proxyPosition = a_proxy.GetPosition();
		const auto localPlayerPosition = a_player.GetPosition();
		const auto distanceFromLocalPlayer = GetDistance(proxyPosition, localPlayerPosition);
		const auto movementModeName = GetProxyMovementDiagnosticModeName(a_movementMode);

		LogThrottledInfo(
			std::format(
				"proxy_movement_diagnostic_{}_{}_{}",
				GetActiveProxySourceForLog(a_proxyKind),
				a_remotePlayer.playerId,
				movementModeName),
			std::format(
				"Proxy movement diagnostic: source={}, proxy={:08X}, assignedRemotePlayerId={}, targetRemotePosition={}, proxyPositionAfterMovement={}, localPlayerPosition={}, distanceFromLocalPlayer={:.2f}, sameCellPassed={}, movementMode={}.",
				GetActiveProxySourceForLog(a_proxyKind),
				a_proxy.GetFormID(),
				a_remotePlayer.playerId,
				FormatPosition(targetPosition),
				FormatPosition(proxyPosition),
				FormatPosition(localPlayerPosition),
				distanceFromLocalPlayer,
				a_sameCellPassed,
				movementModeName),
			kProxyMovementDiagnosticLogInterval);
	}

	void RestoreProxyForRemotePlayer(
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		ActiveProxyKind a_proxyKind,
		bool a_sameCellPassed)
	{
		RE::NiPoint3 targetPosition{};
		targetPosition.x = a_remotePlayer.x;
		targetPosition.y = a_remotePlayer.y;
		targetPosition.z = a_remotePlayer.z;

		F4T::ProxyGameAPI::PositionRemoteActor(
			a_proxy,
			targetPosition,
			a_remotePlayer.angleZ,
			true);
		g_proxyLifecycleState = ProxyLifecycleState::kActive;
		g_proxyHoldReason.reset();
		g_proxyHoldPlayerId.reset();
		g_proxyHoldKind.reset();

		LogInfoWithLocalPlayerPrefix(std::format(
			"{} restored for remote player {} at X={:.2f}, Y={:.2f}, Z={:.2f}.",
			GetActiveProxyLogName(a_proxyKind),
			a_remotePlayer.playerId,
			a_remotePlayer.x,
			a_remotePlayer.y,
			a_remotePlayer.z));
		LogProxyMovementDiagnostic(
			a_proxy,
			a_remotePlayer,
			a_player,
			a_proxyKind,
			a_sameCellPassed,
			ProxyMovementDiagnosticMode::kRestore);
	}

	void MoveProxyToRemotePlayer(
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		ActiveProxyKind a_proxyKind,
		bool a_sameCellPassed,
		ProxyActorSlot& a_slot)
	{
		RE::NiPoint3 targetPosition = a_proxy.GetPosition();
		targetPosition.x = a_remotePlayer.x;
		targetPosition.y = a_remotePlayer.y;
		targetPosition.z = a_remotePlayer.z;

		const auto shouldSnap = ShouldSnapRemoteMovement(a_remotePlayer.movementType);
		const auto approachAlpha = F4T::ProxyMovementSmoothing::SmoothingAlpha(
			F4T::ProxyMovementSmoothing::FrameDeltaSeconds(
				a_slot.lastApproachSampleSeconds,
				SteadySeconds(std::chrono::steady_clock::now())),
			F4T::ProxyMovementSmoothing::kPositionSmoothingRatePerSecond);
		const auto nextPosition =
			shouldSnap ? targetPosition : LerpPosition(a_proxy.GetPosition(), targetPosition, approachAlpha);

		ApplyRuntimeProxyTransform(a_proxy, nextPosition, a_remotePlayer.angleZ, a_remotePlayer, shouldSnap);

		if (shouldSnap) {
			LogThrottledInfo(
				"proxy_snap_" + std::to_string(a_remotePlayer.playerId) + "_" + a_remotePlayer.movementType,
				std::format(
					"Snapped {} to remote player {} because movementType={} at X={:.2f}, Y={:.2f}, Z={:.2f}.",
					GetActiveProxyLogName(a_proxyKind),
					a_remotePlayer.playerId,
					a_remotePlayer.movementType,
					a_remotePlayer.x,
					a_remotePlayer.y,
					a_remotePlayer.z));
		} else if (!g_firstSmoothedMovementLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"First smoothed {} movement for remote player {}: current moves toward target X={:.2f}, Y={:.2f}, Z={:.2f} with smoothing alpha {:.3f}.",
				GetActiveProxyDescription(a_proxyKind),
				a_remotePlayer.playerId,
				a_remotePlayer.x,
				a_remotePlayer.y,
				a_remotePlayer.z,
				approachAlpha));
			g_firstSmoothedMovementLogged = true;
		}

		if (!g_remoteStateMovedLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Moved {} from remote player {} state for the first time: X={:.2f}, Y={:.2f}, Z={:.2f}, AngleZ={:.2f}.",
				GetActiveProxyLogName(a_proxyKind),
				a_remotePlayer.playerId,
				a_remotePlayer.x,
				a_remotePlayer.y,
				a_remotePlayer.z,
				a_remotePlayer.angleZ));
			g_remoteStateMovedLogged = true;
		}

		LogProxyMovementDiagnostic(
			a_proxy,
			a_remotePlayer,
			a_player,
			a_proxyKind,
			a_sameCellPassed,
			shouldSnap ? ProxyMovementDiagnosticMode::kSnap : ProxyMovementDiagnosticMode::kSmooth);
	}

	void MoveSlotProxyToRemotePlayer(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed)
	{
		if (!a_proxy.currentProcess) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"MoveSlotProxyToRemotePlayer: Proxy {:08X} has NO AIProcess! Movement may be jerky.",
				a_proxy.GetFormID()));
		}

		const auto targetPosition = GetRuntimeProxyVisibleTargetPosition(a_slot, a_remotePlayer, a_player);

		a_slot.targetPosition = targetPosition;
		a_slot.targetHeading = a_remotePlayer.angleZ;
		a_slot.puppetDirection = a_remotePlayer.angleZ;
		a_slot.lastRemotePlayerSnapshot = a_remotePlayer;

		const bool remoteLocomoting =
			a_remotePlayer.isMoving && a_remotePlayer.movementSpeed >= 1.0F;
		const auto smoothingNow = SteadySeconds(std::chrono::steady_clock::now());
		F4T::ProxyMovementSmoothing::NoteRemoteSample(
			a_slot.movementSmoothing,
			smoothingNow,
			a_remotePlayer.movementSpeed,
			a_remotePlayer.isSprinting,
			remoteLocomoting,
			a_remotePlayer.animationGraphSpeed);
		const auto smoothingDecision =
			F4T::ProxyMovementSmoothing::Update(
				a_slot.movementSmoothing,
				smoothingNow,
				F4T::ProxyMovementSmoothing::kNoMeasuredSpeed);

		if (smoothingDecision.engaging) {
			a_slot.puppetGraphSpeed = F4T::ProxyAnimationSync::ResolveLocomotionGraphSpeed(
				smoothingDecision.gaitSpeed,
				a_remotePlayer.animationGraphSpeed,
				true,
				a_remotePlayer.isSprinting);
		} else {
			a_slot.puppetGraphSpeed = -1.0F;
		}

		F4T::ProxyPuppet::SetProxyLocomotionState(
			a_proxy.GetFormID(),
			a_slot.puppetGraphSpeed,
			a_slot.puppetDirection);

		const auto shouldSnap =
			ShouldSnapRemoteMovement(a_remotePlayer.movementType) || !smoothingDecision.engaging;
		F4T::ProxyMovementDiagnostics::NotePacket(
			a_slot.snapshotMovement.diagnostics,
			a_remotePlayer.lastReceivedLocalTime,
			a_remotePlayer.movementSpeed);
		const bool recorded = F4T::ProxySnapshotMovement::Record(
			a_slot.snapshotMovement,
			a_remotePlayer.playerId,
			MakeMovementSnapshot(a_slot.targetPosition, a_slot.targetHeading, a_remotePlayer.isMoving),
			shouldSnap,
			smoothingNow,
			a_remotePlayer.clientTime.value_or(0.0));
		if (recorded && shouldSnap) {
			F4T::ProxyMovementDiagnostics::NoteSnap(
				a_slot.snapshotMovement.diagnostics,
				ShouldSnapRemoteMovement(a_remotePlayer.movementType)
					? F4T::ProxyMovementDiagnostics::SnapReason::kMovementType
					: F4T::ProxyMovementDiagnostics::SnapReason::kIdle);
		}
		if (shouldSnap) {
			ApplyRuntimeProxyTransform(a_proxy, targetPosition, a_remotePlayer.angleZ, a_remotePlayer, true, &a_slot);
			a_slot.skipSmoothFrameOnce = true;
		}

		a_slot.lifecycleState = ProxyLifecycleState::kActive;
		a_slot.holdReason.reset();
		a_slot.heldSinceTime = {};
		a_slot.reusableLogged = false;
		ApplyProxyDisplayNameFromRemoteState(a_slot, a_proxy, a_remotePlayer);
		ApplyProxyEquipmentFromRemoteState(a_slot, a_proxy, a_remotePlayer);
		ApplyProxyPowerArmorFromRemoteState(a_slot, a_proxy, a_remotePlayer);
		UpdateProxyAnimationStateDebug(a_slot, a_remotePlayer, a_proxy);

		if (shouldSnap) {
			LogThrottledInfo(
				"stage4_proxy_snap_" + std::to_string(a_remotePlayer.playerId) + "_" + a_remotePlayer.movementType,
				std::format(
					"Snapped runtime proxy actor {:08X} to remote player {} because movementType={} at X={:.2f}, Y={:.2f}, Z={:.2f}.",
					a_proxy.GetFormID(),
					a_remotePlayer.playerId,
					a_remotePlayer.movementType,
					a_remotePlayer.x,
					a_remotePlayer.y,
					a_remotePlayer.z));
		} else if (!a_slot.firstSmoothedMovementLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"First smoothed runtime proxy movement for remote player {}: actor={:08X}, smoothingRate={:.2f}/s.",
				a_remotePlayer.playerId,
				a_proxy.GetFormID(),
				F4T::ProxyMovementSmoothing::kFrameApproachRatePerSecond));
			a_slot.firstSmoothedMovementLogged = true;
		}

		if (!a_slot.remoteStateMovedLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Moved runtime proxy actor {:08X} from remote player {} state for the first time: X={:.2f}, Y={:.2f}, Z={:.2f}, AngleZ={:.2f}.",
				a_proxy.GetFormID(),
				a_remotePlayer.playerId,
				a_remotePlayer.x,
				a_remotePlayer.y,
				a_remotePlayer.z,
				a_remotePlayer.angleZ));
			a_slot.remoteStateMovedLogged = true;
		}

		LogProxyMovementDiagnostic(
			a_proxy,
			a_remotePlayer,
			a_player,
			ActiveProxyKind::kRuntimeSpawned,
			a_sameCellPassed,
			shouldSnap ? ProxyMovementDiagnosticMode::kSnap : ProxyMovementDiagnosticMode::kSmooth);
		if (const auto window = F4T::ProxyMovementDiagnostics::TakeIfDue(
				a_slot.snapshotMovement.diagnostics,
				std::chrono::steady_clock::now())) {
			LogInfoWithLocalPlayerPrefix(F4T::ProxyMovementDiagnostics::FormatProxyWindow(a_remotePlayer.playerId, *window));
		}
	}

	void ApplySmoothFrameMovement()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		auto* parentCell = player->GetParentCell();
		if (!parentCell) {
			return;
		}

		const auto frameNow = std::chrono::steady_clock::now();
		const double frameNowSeconds = SteadySeconds(frameNow);

		for (auto& [playerId, slot] : g_proxySlots) {
			if (slot.lifecycleState != ProxyLifecycleState::kActive) {
				continue;
			}

			if (slot.skipSmoothFrameOnce) {
				slot.skipSmoothFrameOnce = false;
				continue;
			}

			auto* proxy = TryResolveSlotProxy(slot, *parentCell, *player);
			if (!proxy || !proxy->currentProcess) {
				continue;
			}

			if (!ProxyHasLoaded3D(*proxy) || slot.proxyPending3DRecovery) {
				continue;
			}

			const auto rendered = F4T::ProxySnapshotMovement::Render(slot.snapshotMovement, frameNowSeconds);
			if (!rendered.hasValue) {
				continue;
			}

			const RE::NiPoint3 currentPosition = proxy->GetPosition();
			const RE::NiPoint3 nextPosition{
				rendered.snapshot.position.x,
				rendered.snapshot.position.y,
				rendered.snapshot.position.z
			};
			const float headingToApply = rendered.snapshot.heading;

			const float frameDeltaSeconds = FrameDeltaSeconds(slot.lastSmoothMoveTime, frameNow);
			const RE::NiPoint3 frameDelta{
				nextPosition.x - currentPosition.x,
				nextPosition.y - currentPosition.y,
				nextPosition.z - currentPosition.z
			};

			const float horizontalDelta =
				std::sqrt((frameDelta.x * frameDelta.x) + (frameDelta.y * frameDelta.y));
			const float measuredSpeed =
				frameDeltaSeconds > 0.0F ? horizontalDelta / frameDeltaSeconds : 0.0F;
			F4T::ProxyMovementDiagnostics::NoteRender(
				slot.snapshotMovement.diagnostics,
				rendered.status,
				measuredSpeed,
				slot.snapshotMovement.buffer.Size(),
				slot.snapshotMovement.delay.Delay());

			proxy->SetHeading(headingToApply);

			const auto smoothingDecision = F4T::ProxyMovementSmoothing::Update(
				slot.movementSmoothing,
				frameNowSeconds,
				measuredSpeed);

			if (!smoothingDecision.engaging) {
				F4T::ProxyGameAPI::PositionRemoteActor(
					*proxy,
					nextPosition,
					headingToApply,
					true);
				slot.puppetGraphSpeed = -1.0F;
				F4T::ProxyPuppet::SetProxyLocomotionState(
					proxy->GetFormID(),
					-1.0F,
					headingToApply);
				F4T::ProxyPuppet::ApplyProxyLocomotionFrame(
					*proxy,
					RE::NiPoint3{},
					frameDeltaSeconds,
					-1.0F,
					headingToApply);
				SyncProxyAnimationFromMotion(slot, *proxy, 0.0F);
				continue;
			}

			const bool remoteSprinting =
				slot.lastRemotePlayerSnapshot && slot.lastRemotePlayerSnapshot->isSprinting;
			slot.puppetGraphSpeed = F4T::ProxyAnimationSync::MapMovementSpeedToGraphSpeed(
				smoothingDecision.gaitSpeed,
				remoteSprinting);

			F4T::ProxyGameAPI::PositionRemoteActor(
				*proxy,
				nextPosition,
				headingToApply,
				true);
			F4T::ProxyPuppet::ApplyProxyLocomotionFrame(
				*proxy,
				frameDelta,
				frameDeltaSeconds,
				slot.puppetGraphSpeed,
				headingToApply);

			SyncProxyAnimationFromMotion(slot, *proxy, measuredSpeed);
		}
	}
}
