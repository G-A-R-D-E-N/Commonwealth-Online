#include "pch.h"

#include "F4TProxyActorControllerInternal.h"
#include "F4TConsoleGuard.h"
#include "F4TLocalAnimationGraphDebug.h"
#include "F4TProxySneakAction.h"
#include "F4TProxyWeaponDraw.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr auto kProxyWeaponAlertReassertInterval = 100ms;

	const char* GetProxyAnimationSpeedBucketLabel(ProxyAnimationSpeedBucket a_bucket)
	{
		switch (a_bucket) {
		case ProxyAnimationSpeedBucket::kIdle:
			return "idle";
		case ProxyAnimationSpeedBucket::kSlow:
			return "slow";
		case ProxyAnimationSpeedBucket::kWalk:
			return "walk";
		case ProxyAnimationSpeedBucket::kRun:
			return "run";
		case ProxyAnimationSpeedBucket::kSprint:
			return "sprint";
		default:
			return "unknown";
		}
	}

	ProxyAnimationSpeedBucket GetProxyAnimationSpeedBucket(const RemotePlayer& a_remotePlayer)
	{
		if (!a_remotePlayer.isMoving || a_remotePlayer.movementSpeed < 1.0F) {
			return ProxyAnimationSpeedBucket::kIdle;
		}
		if (a_remotePlayer.isSprinting) {
			return ProxyAnimationSpeedBucket::kSprint;
		}

		if (a_remotePlayer.movementSpeed < 75.0F) {
			return ProxyAnimationSpeedBucket::kSlow;
		}
		if (a_remotePlayer.movementSpeed < 150.0F) {
			return ProxyAnimationSpeedBucket::kWalk;
		}
		if (a_remotePlayer.movementSpeed < 300.0F) {
			return ProxyAnimationSpeedBucket::kRun;
		}
		return ProxyAnimationSpeedBucket::kSprint;
	}

	void ApplyProxyWeaponAlert(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer)
	{
		const auto weaponArchetype = F4T::WeaponClassification::ClassifyEquippedRightHandWeapon(a_proxy);
		const bool usesRangedBehavior = F4T::WeaponClassification::UseWeaponBehavior(weaponArchetype);
		const bool usesMeleeBehavior = F4T::WeaponClassification::UseMeleeBehavior(weaponArchetype);
		if (!usesRangedBehavior && !usesMeleeBehavior) {
			return;
		}

		const auto actorId = a_proxy.GetFormID();
		const auto playerId = a_remotePlayer.playerId;
		const auto alertValue = a_remotePlayer.weaponDrawn ? 1 : 0;

		if (a_remotePlayer.weaponDrawn && usesRangedBehavior) {
			auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
			graphHolder->SetGraphVariableInt(
				RE::BSFixedString{ F4T::ProxyAnimationSync::GraphVar::kSyncGunDown },
				0);
		}

		const auto alertStateChanged =
			!a_slot.lastObservedWeaponDrawnState ||
			*a_slot.lastObservedWeaponDrawnState != a_remotePlayer.weaponDrawn;

		const auto commandKey = std::format(
			"runtime_proxy_weapon_alert_command_{}_{}",
			playerId,
			actorId);
		const auto shouldReassert =
			a_remotePlayer.weaponDrawn &&
			ShouldLog(commandKey, kProxyWeaponAlertReassertInterval);

		if (!alertStateChanged && !shouldReassert) {
			return;
		}

		const auto command = std::format("{:08X}.setalert {}", actorId, alertValue);
		F4T::ConsoleGuard::ExecutePluginCommand(command.c_str());

		LogThrottledInfo(
			"runtime_proxy_weapon_alert_experiment_" + std::to_string(playerId) + "_" + std::to_string(actorId),
			std::format(
				"Runtime proxy weapon alert experiment for remote player {}: actor={:08X}, weaponDrawn={}, command='{}'.",
				playerId,
				actorId,
				a_remotePlayer.weaponDrawn,
				command),
			kProxyNeutralizationInterval);
	}

	bool IsProxyWeaponGraphSettling(ProxyActorSlot& a_slot)
	{
		if (a_slot.weaponGraphSettleUntil.time_since_epoch().count() == 0) {
			return false;
		}

		if (std::chrono::steady_clock::now() < a_slot.weaponGraphSettleUntil) {
			return true;
		}

		a_slot.weaponGraphSettleUntil = {};
		return false;
	}

	void ApplyRemoteActionEventsFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer)
	{
		for (const auto& event : a_remotePlayer.actionEvents) {
			if (event.eventName.empty() || event.sequence == 0 ||
				!F4T::GnsSnapshotEnvelope::IsNewer(event.sequence, a_slot.lastProcessedActionSequence)) {
				continue;
			}

			F4T::RemoteActionComponent::RemoteActionSnapshot snapshot{};
			snapshot.actionType = event.type;
			snapshot.eventName = event.eventName;
			snapshot.captureTime = event.captureTime;
			a_slot.actionQueue.Enqueue(snapshot);
			a_slot.lastProcessedActionSequence = event.sequence;

			LogThrottledInfo(
				"runtime_proxy_action_replayed_" + std::to_string(a_remotePlayer.playerId) + "_" + event.eventName,
				std::format(
					"Runtime proxy queued action '{}' (sequence={}, type={}) for remote player {} on actor {:08X}.",
					event.eventName,
					event.sequence,
					event.type,
					a_remotePlayer.playerId,
					a_proxy.GetFormID()),
				kProxyNeutralizationInterval);
		}
		a_slot.actionQueue.ReplayNextAction(a_proxy);
	}

	constexpr float kProxyMotionSpeedThreshold = 8.0F;
}

namespace F4T::ProxyActorController::detail
{
	void UpdateProxyAnimationStateDebug(
		ProxyActorSlot& a_slot,
		const RemotePlayer& a_remotePlayer,
		RE::Actor& a_proxy)
	{
		const auto speedBucket = GetProxyAnimationSpeedBucket(a_remotePlayer);
		const auto shouldLogInitial = !a_slot.hasLoggedInitialAnimationState;
		const auto now = std::chrono::steady_clock::now();
		const auto sneakReady = ProxyHasLoaded3D(a_proxy) && !a_slot.proxyPending3DRecovery;
		const auto changed =
			!a_slot.lastObservedMovingState || *a_slot.lastObservedMovingState != a_remotePlayer.isMoving ||
			!a_slot.lastObservedSprintingState || *a_slot.lastObservedSprintingState != a_remotePlayer.isSprinting ||
			((!a_slot.lastObservedSneakingState || *a_slot.lastObservedSneakingState != a_remotePlayer.isSneaking) && sneakReady && now >= a_slot.sneakRequest.nextAttemptTime && !IsProxyWeaponGraphSettling(a_slot) && (!a_slot.sneakRequest.lastOutcome || *a_slot.sneakRequest.lastOutcome != F4T::ProxySneakAction::SneakRequestOutcome::kGiveUp)) ||
			!a_slot.lastObservedJumpingState || *a_slot.lastObservedJumpingState != a_remotePlayer.isJumping ||
			!a_slot.lastObservedWeaponDrawnState || *a_slot.lastObservedWeaponDrawnState != a_remotePlayer.weaponDrawn ||
			!a_slot.lastObservedMovementType || *a_slot.lastObservedMovementType != a_remotePlayer.movementType ||
			!a_slot.lastObservedMovementSpeedBucket || *a_slot.lastObservedMovementSpeedBucket != speedBucket;

		if (shouldLogInitial || changed) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Runtime proxy animation state {} for remote player {}: actor={:08X}, holdingIndex={}, moving={}, speed={:.1f}, speedBucket={}, sprinting={}, sneaking={}, jumping={}, weaponDrawn={}, movementType={}.",
				shouldLogInitial ? "initial" : "changed",
				a_remotePlayer.playerId,
				a_proxy.GetFormID(),
				a_slot.holdingPositionIndex,
				a_remotePlayer.isMoving,
				a_remotePlayer.movementSpeed,
				GetProxyAnimationSpeedBucketLabel(speedBucket),
				a_remotePlayer.isSprinting,
				a_remotePlayer.isSneaking,
				a_remotePlayer.isJumping,
				a_remotePlayer.weaponDrawn,
				a_remotePlayer.movementType));
		}

		ApplyProxyWeaponAlert(a_slot, a_proxy, a_remotePlayer);
		F4T::ProxyWeaponDraw::ReconcileProxyWeaponDraw(
			a_remotePlayer.playerId,
			a_proxy,
			a_remotePlayer,
			IsProxyWeaponGraphSettling(a_slot));
		a_slot.hasLoggedInitialAnimationState = true;
		a_slot.lastObservedMovingState = a_remotePlayer.isMoving;
		a_slot.lastObservedSprintingState = a_remotePlayer.isSprinting;
		a_slot.lastObservedJumpingState = a_remotePlayer.isJumping;
		a_slot.lastObservedWeaponDrawnState = a_remotePlayer.weaponDrawn;
		a_slot.lastObservedMovementType = a_remotePlayer.movementType;
		a_slot.lastObservedMovementSpeedBucket = speedBucket;
		if (!sneakReady && (!a_slot.lastObservedSneakingState || *a_slot.lastObservedSneakingState != a_remotePlayer.isSneaking) &&
			F4T::ProxySneakAction::ShouldTraceCount(++a_slot.sneakRequest.deferredCount)) {
			F4T::DebugLogging::Log(
				"ProxySneak",
				"[RT-VERIFY 364-B] sneak request deferred: proxy 3D not ready for remote player {} actor={:08X}: sneaking={}, loaded3D={}, pending3DRecovery={}, deferredCount={}.",
				a_remotePlayer.playerId,
				a_proxy.GetFormID(),
				a_remotePlayer.isSneaking,
				ProxyHasLoaded3D(a_proxy),
				a_slot.proxyPending3DRecovery,
				a_slot.sneakRequest.deferredCount);
		}
		if (IsProxyWeaponGraphSettling(a_slot)) {
			if (sneakReady) {
				F4T::ProxySneakAction::RequestProxySneakState(a_slot.sneakRequest, a_proxy, a_remotePlayer.isSneaking, false);
			}
			return;
		}
		ApplyRemoteActionEventsFromRemoteState(a_slot, a_proxy, a_remotePlayer);
		const auto sneakCommitted = sneakReady && F4T::ProxySneakAction::RequestProxySneakState(
			a_slot.sneakRequest,
			a_proxy,
			a_remotePlayer.isSneaking,
			true);

		if (sneakCommitted) {
			if (!a_slot.lastObservedSneakingState || *a_slot.lastObservedSneakingState != a_remotePlayer.isSneaking) {
				F4T::DebugLogging::Log(
					"ProxySneak",
					"[RT-VERIFY 364-B] sneak committed for remote player {} actor={:08X}: sneaking={}.",
					a_remotePlayer.playerId,
					a_proxy.GetFormID(),
					a_remotePlayer.isSneaking);
			}
			a_slot.lastObservedSneakingState = a_remotePlayer.isSneaking;
		}

		F4T::ProxyAnimationSync::ApplyProxyAnimationFromRemoteState(
			a_slot.appliedAnimationState,
			a_proxy,
			a_remotePlayer,
			a_slot.movementSmoothing.gait);

		F4T::LocalAnimationGraphDebug::UpdateProxyAnimationGraphReadOnlyDebug(
			a_proxy,
			a_remotePlayer.playerId);
	}

	void SyncProxyAnimationFromMotion(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		float a_measuredSpeed)
	{
		if (!ProxyHasLoaded3D(a_proxy) || a_slot.proxyPending3DRecovery) {
			return;
		}

		if (!a_slot.lastRemotePlayerSnapshot) {
			return;
		}

		auto effectiveRemote = *a_slot.lastRemotePlayerSnapshot;
		ApplyProxyWeaponAlert(a_slot, a_proxy, effectiveRemote);
		if (IsProxyWeaponGraphSettling(a_slot)) {
			return;
		}

		if (effectiveRemote.isJumping) {
			a_slot.puppetGraphSpeed = -1.0F;
			F4T::ProxyPuppet::SetProxyLocomotionState(a_proxy.GetFormID(), -1.0F, a_slot.puppetDirection);
			F4T::ProxyAnimationSync::ApplyProxyAnimationFromRemoteState(
				a_slot.appliedAnimationState,
				a_proxy,
				effectiveRemote,
				a_slot.movementSmoothing.gait);
			return;
		}

		const bool remoteLocomoting =
			effectiveRemote.isMoving && effectiveRemote.movementSpeed >= 1.0F;

		if (!remoteLocomoting) {
			a_slot.puppetGraphSpeed = -1.0F;
			effectiveRemote.isMoving = false;
			effectiveRemote.movementSpeed = 0.0F;
			effectiveRemote.animationGraphSpeed = -1.0F;
			F4T::ProxyPuppet::SetProxyLocomotionState(a_proxy.GetFormID(), -1.0F, a_slot.puppetDirection);
		} else if (a_measuredSpeed >= kProxyMotionSpeedThreshold) {
			effectiveRemote.movementSpeed = std::max(effectiveRemote.movementSpeed, a_measuredSpeed);
			a_slot.puppetGraphSpeed = F4T::ProxyAnimationSync::ResolveLocomotionGraphSpeed(
				effectiveRemote.movementSpeed,
				effectiveRemote.animationGraphSpeed,
				true,
				effectiveRemote.isSprinting);
		}

		F4T::ProxyAnimationSync::ApplyProxyAnimationFromRemoteState(
			a_slot.appliedAnimationState,
			a_proxy,
			effectiveRemote,
			a_slot.movementSmoothing.gait);
	}
}
