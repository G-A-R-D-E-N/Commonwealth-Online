// source:CO-BETA/plugin/src/F4TProxyActorController.cpp moved verbatim (RM-10)
#include "pch.h"

#include "F4TProxyActorControllerInternal.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr auto kPromotedProxyResolveTimeout = 3s;

	RE::Actor* TryResolveFromCachedProxyActor(
		ProxyActorSlot& a_slot,
		const RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player)
	{
		if (a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kSettling ||
			a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kCustomProxyAttempting ||
			a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kCodsworthFallbackAttempting) {
			return nullptr;
		}

		if (!a_slot.cachedProxyActor) {
			return nullptr;
		}

		auto* actor = a_slot.cachedProxyActor.get();
		if (!actor) {
			ClearSlotCachedProxyActor(a_slot);
			return nullptr;
		}

		if (a_slot.cachedProxyFormId && actor->GetFormID() != *a_slot.cachedProxyFormId) {
			ClearSlotCachedProxyActor(a_slot);
			return nullptr;
		}

		if (!IsValidProxyActor(*actor, a_currentCell, a_player)) {
			// Match the sibling failure branches above: release the strong
			// cachedProxyActor reference instead of pinning a now-invalid actor
			// alive for the slot's lifetime.
			ClearSlotCachedProxyActor(a_slot);
			return nullptr;
		}

		a_slot.proxyHandle = GetProxyActorHandle(*actor);
		a_slot.cachedProxyFormId = actor->GetFormID();
		return actor;
	}

	RE::Actor* TryRecoverSlotProxyActor(
		ProxyActorSlot& a_slot,
		const RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player)
	{
		if (a_slot.spawnState != PlaceAtMeBackedProxySpawnState::kPromoted && !a_slot.spawnSucceeded) {
			return nullptr;
		}

		const auto adoptRecoveredActor = [&](RE::Actor& a_actor) -> RE::Actor* {
			if (!IsValidProxyActor(a_actor, a_currentCell, a_player)) {
				return nullptr;
			}

			a_slot.proxyHandle = GetProxyActorHandle(a_actor);
			a_slot.cachedProxyFormId = a_actor.GetFormID();
			a_slot.cachedProxyActor = std::addressof(a_actor);
			return std::addressof(a_actor);
		};

		if (a_slot.cachedProxyFormId) {
			if (auto* form = RE::TESForm::GetFormByID(*a_slot.cachedProxyFormId)) {
				if (auto* actor = form->As<RE::Actor>()) {
					if (auto* recovered = adoptRecoveredActor(*actor)) {
						return recovered;
					}

					const auto* proxyCell = actor->GetParentCell();
					LogThrottledInfo(
						"runtime_proxy_handle_recovery_rejected_" + std::to_string(a_slot.remotePlayerId),
						std::format(
							"Runtime proxy recovery rejected for remote player {}: actor={:08X}, proxyCell={}, localCell={}, samePlaySpace={}.",
							a_slot.remotePlayerId,
							actor->GetFormID(),
							proxyCell ? std::format("{:08X}", proxyCell->GetFormID()) : "unavailable",
							std::format("{:08X}", a_currentCell.GetFormID()),
							proxyCell ? IsProxyInSamePlaySpace(*proxyCell, a_currentCell) : false),
						kRuntimeProxyVisibilityDiagnosticLogInterval);
				}
			}

			if (auto* processListActor = FindActorInProcessListsByFormId(*a_slot.cachedProxyFormId)) {
				if (auto* recovered = adoptRecoveredActor(*processListActor)) {
					LogThrottledInfo(
						"runtime_proxy_handle_recovered_processlists_" + std::to_string(a_slot.remotePlayerId),
						std::format(
							"Runtime proxy for remote player {} recovered from process lists by FormID {:08X}.",
							a_slot.remotePlayerId,
							recovered->GetFormID()),
						kRuntimeProxyVisibilityDiagnosticLogInterval);
					return recovered;
				}
			}

			RE::Actor* cellMatch = nullptr;
			const_cast<RE::TESObjectCELL&>(a_currentCell).ForEachReference([&](RE::TESObjectREFR* a_ref) {
				if (cellMatch || !a_ref) {
					return RE::BSContainer::ForEachResult::kContinue;
				}

				auto* actor = a_ref->As<RE::Actor>();
				if (!actor || actor->GetFormID() != *a_slot.cachedProxyFormId) {
					return RE::BSContainer::ForEachResult::kContinue;
				}

				cellMatch = adoptRecoveredActor(*actor);
				return cellMatch ?
					RE::BSContainer::ForEachResult::kStop :
					RE::BSContainer::ForEachResult::kContinue;
			});
			if (cellMatch) {
				return cellMatch;
			}
		}

		return nullptr;
	}

	void ResetSlotActionReplayState(ProxyActorSlot& a_slot)
	{
		a_slot.actionQueue.Clear();
		a_slot.lastProcessedActionSequence = 0;
	}
}

namespace F4T::ProxyActorController::detail
{
	void ClearSlotCachedProxyActor(ProxyActorSlot& a_slot)
	{
		a_slot.cachedProxyActor = nullptr;
		a_slot.cachedProxyFormId.reset();
	}

	RE::Actor* TryResolveSlotProxy(
		ProxyActorSlot& a_slot,
		const RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player)
	{
		if (auto* cached = TryResolveFromCachedProxyActor(a_slot, a_currentCell, a_player)) {
			a_slot.unresolvedHandleSinceTime = {};
			LogThrottledInfo(
				"runtime_proxy_cached_actor_resolved_" + std::to_string(a_slot.remotePlayerId),
				std::format(
					"Runtime proxy for remote player {} resolved from cached actor pointer {:08X}.",
					a_slot.remotePlayerId,
					cached->GetFormID()),
				kRuntimeProxyVisibilityDiagnosticLogInterval);
			return cached;
		}

		if (!a_slot.proxyHandle) {
			if (auto* recovered = TryRecoverSlotProxyActor(a_slot, a_currentCell, a_player)) {
				a_slot.unresolvedHandleSinceTime = {};
				LogThrottledInfo(
					"runtime_proxy_handle_recovered_" + std::to_string(a_slot.remotePlayerId),
					std::format(
						"Runtime proxy for remote player {} recovered by FormID {:08X} after handle lookup failed.",
						a_slot.remotePlayerId,
						recovered->GetFormID()),
					kRuntimeProxyVisibilityDiagnosticLogInterval);
				return recovered;
			}
			return nullptr;
		}

		const auto proxyRef = a_slot.proxyHandle.get();
		auto* proxy = proxyRef.get();
		if (!proxy) {
			if (auto* recovered = TryRecoverSlotProxyActor(a_slot, a_currentCell, a_player)) {
				a_slot.unresolvedHandleSinceTime = {};
				LogThrottledInfo(
					"runtime_proxy_handle_recovered_" + std::to_string(a_slot.remotePlayerId),
					std::format(
						"Runtime proxy for remote player {} recovered by FormID {:08X} after stale handle lookup.",
						a_slot.remotePlayerId,
						recovered->GetFormID()),
					kRuntimeProxyVisibilityDiagnosticLogInterval);
				return recovered;
			}

			if (a_slot.spawnSucceeded ||
				a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kPromoted) {
				const auto now = std::chrono::steady_clock::now();
				if (a_slot.unresolvedHandleSinceTime == std::chrono::steady_clock::time_point{}) {
					a_slot.unresolvedHandleSinceTime = now;
				} else if (now - a_slot.unresolvedHandleSinceTime >= kPromotedProxyResolveTimeout) {
					LogWarningWithLocalPlayerPrefix(std::format(
						"Runtime proxy handle for remote player {} remained unresolved for {:.1f}s; resetting the slot for a controlled respawn.",
						a_slot.remotePlayerId,
						std::chrono::duration<float>(now - a_slot.unresolvedHandleSinceTime).count()));
					a_slot.proxyHandle.reset();
					a_slot.pendingSpawnHandle.reset();
					ClearSlotCachedProxyActor(a_slot);
					a_slot.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
					a_slot.spawnAttemptInProgress = false;
					a_slot.spawnAttempted = false;
					a_slot.spawnSucceeded = false;
					a_slot.spawnFailed = false;
					a_slot.restoredForCurrentState = false;
					a_slot.unresolvedHandleSinceTime = {};
					a_slot.visibilityRefreshPending = true;
					a_slot.visibilityRefreshLogged = false;
					if (g_runtimeProxySpawnAttemptPlayerId == a_slot.remotePlayerId) {
						F4T::DebugLogging::Log(
							"ProxySaveSafety",
							"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=proxy-handle-unresolved-timeout thread={}",
							a_slot.remotePlayerId,
							REX::W32::GetCurrentThreadId());
						g_runtimeProxySpawnAttemptPlayerId.reset();
					}
					return nullptr;
				}

				LogThrottledInfo(
					"runtime_proxy_handle_pending_resolve_" + std::to_string(a_slot.remotePlayerId),
					std::format(
						"Runtime proxy handle for remote player {} is not resolvable yet; keeping promoted slot state.",
						a_slot.remotePlayerId),
					kRuntimeProxyVisibilityDiagnosticLogInterval);
				return nullptr;
			}

			if (a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kSettling) {
				return nullptr;
			}
		}

		if (proxy && IsValidProxyActor(*proxy, a_currentCell, a_player)) {
			a_slot.unresolvedHandleSinceTime = {};
			a_slot.cachedProxyFormId = proxy->GetFormID();
			a_slot.cachedProxyActor = proxy;
			return proxy;
		}

		if (!proxy) {
			return nullptr;
		}

		LogThrottledWarning(
			"stage4_runtime_proxy_handle_invalid_" + std::to_string(a_slot.remotePlayerId),
			std::format(
				"Runtime proxy handle for remote player {} is invalid or failed validation; controlled respawn may be attempted.",
				a_slot.remotePlayerId));
		a_slot.proxyHandle.reset();
		ClearSlotCachedProxyActor(a_slot);
		a_slot.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
		a_slot.spawnAttemptInProgress = false;
		a_slot.spawnSucceeded = false;
		a_slot.restoredForCurrentState = false;
		F4T::DebugLogging::Log("Equipment", "ledger forget requested for remote player {}", a_slot.remotePlayerId);
		ResetSlotEquipmentTracking(a_slot, true);
		ResetSlotAppearanceTracking(a_slot);
		ResetSlotDisplayNameTracking(a_slot);
		if (a_slot.lifecycleState != ProxyLifecycleState::kHeldDisconnected) {
			a_slot.heldSinceTime = {};
			a_slot.reusableLogged = false;
		}
		if (g_runtimeProxySpawnAttemptPlayerId == a_slot.remotePlayerId) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=proxy-handle-invalid thread={}",
				a_slot.remotePlayerId,
				REX::W32::GetCurrentThreadId());
			g_runtimeProxySpawnAttemptPlayerId.reset();
		}
		return nullptr;
	}

	void ResetSlotMovementState(ProxyActorSlot& a_slot)
	{
		a_slot.restoredForCurrentState = false;
		a_slot.firstSmoothedMovementLogged = false;
		a_slot.remoteStateMovedLogged = false;
		F4T::ProxyMovementSmoothing::Reset(a_slot.movementSmoothing);
		F4T::ProxySnapshotMovement::Reset(a_slot.snapshotMovement);
		a_slot.lastSmoothMoveTime = {};
		a_slot.lastApproachSampleSeconds = 0.0;
		a_slot.proxyObservedLoaded3D = false;
		a_slot.proxyPending3DRecovery = false;
		a_slot.proxy3DResetQueued = false;
		a_slot.proxy3DRecoverySettling = false;
		a_slot.lastObservedRemoteCellId.reset();
		a_slot.headMeshRefreshAttempted = false;
		a_slot.headMeshPendingStable = false;
		a_slot.lastObservedProxyVisible.reset();
	}

	void ResetSlotAppliedAnimationState(ProxyActorSlot& a_slot)
	{
		F4T::ProxyAnimationSync::ResetAppliedAnimationState(a_slot.appliedAnimationState);
	}

	void ClearSlotProxyAnimationIfApplied(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		std::string_view a_reason)
	{
		F4T::ProxyAnimationSync::ClearProxyAnimationState(
			a_slot.appliedAnimationState,
			a_proxy,
			a_slot.remotePlayerId,
			a_reason);
	}

	void ResetSlotObservationState(ProxyActorSlot& a_slot)
	{
		a_slot.hasLoggedInitialAnimationState = false;
		a_slot.lastObservedMovingState.reset();
		a_slot.lastObservedSprintingState.reset();
		a_slot.lastObservedSneakingState.reset();
		a_slot.sneakRequest = {};
		a_slot.lastObservedJumpingState.reset();
		a_slot.lastObservedWeaponDrawnState.reset();
		a_slot.lastObservedMovementType.reset();
		a_slot.lastObservedMovementSpeedBucket.reset();
		ResetSlotAppliedAnimationState(a_slot);
		ResetSlotActionReplayState(a_slot);
	}

	void ResetSlotSneakObservationState(ProxyActorSlot& a_slot, std::string_view a_reason)
	{
		a_slot.lastObservedSneakingState.reset();
		a_slot.sneakRequest = {};
		ResetSlotAppliedAnimationState(a_slot);
		F4T::DebugLogging::Log(
			"ProxySneak",
			"[RT-VERIFY 364-B] sneak observation reset for remote player {} ({}): observed sneaking state, sneak request and applied animation state cleared.",
			a_slot.remotePlayerId,
			a_reason);
	}

	void ResetSlotForRespawn(ProxyActorSlot& a_slot)
	{
		a_slot.proxyHandle.reset();
		a_slot.pendingSpawnHandle.reset();
		ClearSlotCachedProxyActor(a_slot);
		a_slot.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
		a_slot.spawnSource = RuntimeActorBaseSource::kCustomProxy;
		a_slot.spawnBaseFormId = 0;
		a_slot.spawnAttemptInProgress = false;
		a_slot.spawnAttempted = false;
		a_slot.spawnSucceeded = false;
		a_slot.spawnFailed = false;
		a_slot.restoredForCurrentState = false;
		a_slot.unresolvedHandleSinceTime = {};
		a_slot.preexistingCandidateRefs.clear();
		a_slot.initialRemoteSpawnPosition.reset();
		a_slot.lastPositionSyncTime = {};
		a_slot.visibilityRefreshPending = true;
		a_slot.visibilityRefreshLogged = false;
		a_slot.lifecycleState = ProxyLifecycleState::kHeldNoRemote;
		a_slot.holdReason.reset();
		a_slot.heldSinceTime = {};
		a_slot.reusableLogged = false;
		ResetSlotMovementState(a_slot);
		ResetSlotObservationState(a_slot);
		F4T::DebugLogging::Log("Equipment", "respawn reset: forgetting {} tracked stacks", a_slot.syncedEquipmentStacks.Size());
		F4T::DebugLogging::Log("Equipment", "ledger forget requested for remote player {}", a_slot.remotePlayerId);
		ResetSlotEquipmentTracking(a_slot, true);
		ResetSlotAppearanceTracking(a_slot);
		ResetSlotDisplayNameTracking(a_slot);
	}
}
