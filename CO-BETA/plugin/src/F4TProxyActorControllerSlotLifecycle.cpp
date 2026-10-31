#include "pch.h"

#include "F4TProxyActorControllerInternal.h"
#include "F4TProxySaveSafety.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr float kDisconnectedProxyReuseDelaySeconds = 30.0F;
	constexpr float kProxyHoldingOffsetZ = -10000.0F;
	constexpr float kProxyHiddenHoldingPositionSpacing = 100.0F;
	constexpr float kHeldProxyPositionCorrectionDistance = 25.0F;
	bool g_fallbackRuntimeUnavailableLogged = false;

	bool IsProxyHeldFor(
		ProxyLifecycleState a_reason,
		std::optional<std::uint32_t> a_playerId,
		ActiveProxyKind a_proxyKind)
	{
		return g_proxyLifecycleState == a_reason &&
		       g_proxyHoldReason == a_reason &&
		       g_proxyHoldPlayerId == a_playerId &&
		       g_proxyHoldKind == a_proxyKind;
	}

	void LogProxyStillHeld(ProxyLifecycleState a_reason, std::optional<std::uint32_t> a_playerId)
	{
		switch (a_reason) {
		case ProxyLifecycleState::kHeldNoRemote:
			LogThrottledInfo(
				"proxy_held_no_remote_player",
				"No suitable remote player is available; proxy actor is already at the holding position.");
			break;
		case ProxyLifecycleState::kHeldDisconnected:
			if (a_playerId) {
				LogThrottledInfo(
					"proxy_held_disconnected_" + std::to_string(*a_playerId),
					std::format(
						"Remote player {} is still absent from state; proxy actor remains at the holding position.",
						*a_playerId));
			}
			break;
		case ProxyLifecycleState::kHeldLeftCell:
			if (a_playerId) {
				LogThrottledInfo(
					"proxy_held_left_cell_" + std::to_string(*a_playerId),
					std::format(
						"Remote player {} is still outside the local cell; proxy actor remains at the holding position.",
						*a_playerId));
			}
			break;
		default:
			break;
		}
	}

	bool HasSlotHeldSinceTime(const ProxyActorSlot& a_slot)
	{
		return a_slot.heldSinceTime.time_since_epoch().count() != 0;
	}

	bool IsHeldSlotState(ProxyLifecycleState a_state)
	{
		return a_state == ProxyLifecycleState::kHeldNoRemote ||
		       a_state == ProxyLifecycleState::kHeldLeftCell ||
		       a_state == ProxyLifecycleState::kHeldDisconnected ||
		       a_state == ProxyLifecycleState::kReusable;
	}

	RE::NiPoint3 GetSlotHoldingPosition(
		const ProxyActorSlot& a_slot,
		const RE::PlayerCharacter& a_player)
	{
		auto holdingPosition = a_player.GetPosition();
		holdingPosition.x += static_cast<float>(a_slot.holdingPositionIndex) * kProxyHiddenHoldingPositionSpacing;
		holdingPosition.z += kProxyHoldingOffsetZ;
		return holdingPosition;
	}

	void CorrectHeldSlotPositionIfNeeded(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RE::PlayerCharacter& a_player)
	{
		if (!IsHeldSlotState(a_slot.lifecycleState)) {
			return;
		}

		const auto holdingPosition = GetSlotHoldingPosition(a_slot, a_player);
		const auto currentPosition = a_proxy.GetPosition();
		if (GetDistance(currentPosition, holdingPosition) <= kHeldProxyPositionCorrectionDistance) {
			return;
		}

		F4T::ProxyGameAPI::PositionRemoteActor(a_proxy, holdingPosition, a_proxy.GetHeading(), true);
		LogThrottledInfo(
			"held_runtime_proxy_position_corrected_" + std::to_string(a_slot.remotePlayerId),
			std::format(
				"Held runtime proxy actor {:08X} for remote player {} drifted from holding position; correcting.",
				a_proxy.GetFormID(),
				a_slot.remotePlayerId));
	}
}

namespace F4T::ProxyActorController::detail
{
	void MoveProxyToHoldingPosition(
		RE::Actor& a_proxy,
		ProxyLifecycleState a_reason,
		std::optional<std::uint32_t> a_playerId,
		ActiveProxyKind a_proxyKind)
	{
		if (IsProxyHeldFor(a_reason, a_playerId, a_proxyKind)) {
			LogProxyStillHeld(a_reason, a_playerId);
			return;
		}

		auto holdingPosition = a_proxy.GetPosition();
		if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
			holdingPosition = player->GetPosition();
		}
		holdingPosition.z += kProxyHoldingOffsetZ;
		F4T::ProxyGameAPI::PositionRemoteActor(
			a_proxy,
			holdingPosition,
			a_proxy.GetHeading(),
			true);
		g_proxyLifecycleState = a_reason;
		g_proxyHoldReason = a_reason;
		g_proxyHoldPlayerId = a_playerId;
		g_proxyHoldKind = a_proxyKind;
		g_remoteStateMovedLogged = false;
		g_firstSmoothedMovementLogged = false;
		ResetRemoteMovementStateTracking();

		switch (a_reason) {
		case ProxyLifecycleState::kHeldNoRemote:
			LogInfoWithLocalPlayerPrefix(std::format(
				"No suitable remote player is available; moving {} to holding position.",
				GetActiveProxyLogName(a_proxyKind)));
			break;
		case ProxyLifecycleState::kHeldDisconnected:
			if (a_playerId) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Remote player {} disappeared; moving {} to holding position.",
					*a_playerId,
					GetActiveProxyLogName(a_proxyKind)));
			}
			break;
		case ProxyLifecycleState::kHeldLeftCell:
			if (a_playerId) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Remote player {} is not in the same cell; moving {} to holding position.",
					*a_playerId,
					GetActiveProxyLogName(a_proxyKind)));
			}
			break;
		default:
			break;
		}
	}

	void ReleaseAllSlotProxiesInOldCell(
		std::uint32_t a_oldCellId,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player)
	{
		for (auto& [playerId, slot] : g_proxySlots) {
			if (!slot.proxyHandle) {
				if (auto* pending = slot.pendingSpawnHandle.get().get()) {
					F4T::ProxySaveSafety::DestroyProxyActor(*pending);
				}
				slot.pendingSpawnHandle.reset();
				ClearSlotCachedProxyActor(slot);
				slot.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
				slot.spawnAttemptInProgress = false;
				slot.spawnAttempted = false;
				slot.spawnSucceeded = false;
				slot.spawnFailed = false;
				slot.restoredForCurrentState = false;
				slot.unresolvedHandleSinceTime = {};
				if (g_runtimeProxySpawnAttemptPlayerId == playerId) {
					F4T::DebugLogging::Log(
						"ProxySaveSafety",
						"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=cell-change-no-proxy-handle thread={}",
						playerId,
						REX::W32::GetCurrentThreadId());
					g_runtimeProxySpawnAttemptPlayerId.reset();
				}
				continue;
			}

			const auto proxyRef = slot.proxyHandle.get();
			auto* proxy = proxyRef.get();
			if (!proxy) {
				ResetSlotSneakObservationState(slot, "proxy handle expired");
				slot.proxyHandle.reset();
				slot.pendingSpawnHandle.reset();
				ClearSlotCachedProxyActor(slot);
				slot.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
				slot.spawnAttemptInProgress = false;
				slot.spawnAttempted = false;
				slot.spawnSucceeded = false;
				slot.spawnFailed = false;
				slot.restoredForCurrentState = false;
				slot.unresolvedHandleSinceTime = {};
				if (g_runtimeProxySpawnAttemptPlayerId == playerId) {
					F4T::DebugLogging::Log(
						"ProxySaveSafety",
						"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=cell-change-proxy-null thread={}",
						playerId,
						REX::W32::GetCurrentThreadId());
					g_runtimeProxySpawnAttemptPlayerId.reset();
				}
				continue;
			}

			const auto* proxyCell = proxy->GetParentCell();
			if (!proxyCell || proxyCell->GetFormID() != a_oldCellId) {
				continue;
			}

			if (ShouldRetainProxyInPriorLocalCell(playerId, *proxyCell, a_currentCell, a_player)) {
				slot.restoredForCurrentState = false;
				slot.visibilityRefreshPending = true;
				slot.visibilityRefreshLogged = false;
				slot.proxyObservedLoaded3D = ProxyHasLoaded3D(*proxy);
				if (!Proxy3DReadyForPresentation(*proxy)) {
					slot.proxyPending3DRecovery = true;
					slot.proxy3DResetQueued = false;
					slot.proxy3DRecoverySettling = false;
				}
				LogInfoWithLocalPlayerPrefix(std::format(
					"Retained runtime proxy {:08X} for remote player {} in prior local cell {:08X} after local player moved to cell {:08X}; proxy stays at remote location for same-worldspace exterior play.",
					proxy->GetFormID(),
					playerId,
					a_oldCellId,
					a_currentCell.GetFormID()));
				continue;
			}

			const auto releasedFormId = proxy->GetFormID();
			ClearSlotProxyAnimationIfApplied(slot, *proxy, "local cell change");
			ClearSlotEquipmentIfApplied(slot, *proxy, "local cell change");
			ResetSlotSneakObservationState(slot, "local cell change");
			NeutralizeProxyActor(slot, *proxy, a_player, ProxyNeutralizationReason::kHolding);
			if (F4T::ProxySaveSafety::IsPlacedFallbackProxy(
					IsCkPlacedProxyReference(*proxy, *proxy, std::nullopt),
					proxy->IsCreated())) {
				const auto holdBefore = FormatPosition(proxy->GetPosition());
				MoveProxyToHoldingPosition(
					*proxy,
					ProxyLifecycleState::kHeldLeftCell,
					slot.remotePlayerId,
					ActiveProxyKind::kPlacedFallback);
				F4T::DebugLogging::Log(
					"ProxySaveSafety",
					"cell-change hold proxy={:08X} player={} oldCell={:08X} newCell={:08X} before={} after={} created={}",
					proxy->GetFormID(),
					slot.remotePlayerId,
					a_oldCellId,
					a_currentCell.GetFormID(),
					holdBefore,
					FormatPosition(proxy->GetPosition()),
					proxy->IsCreated());
			} else {
				F4T::DebugLogging::Log(
					"ProxySaveSafety",
					"cell-change destroy proxy={:08X} player={} oldCell={:08X} created={}",
					proxy->GetFormID(),
					slot.remotePlayerId,
					a_oldCellId,
					proxy->IsCreated());
				F4T::ProxySaveSafety::DestroyProxyActor(*proxy);
			}

			slot.proxyHandle.reset();
			slot.cachedProxyActor = nullptr;
			slot.cachedProxyFormId.reset();
			slot.pendingSpawnHandle.reset();
			slot.restoredForCurrentState = false;
			slot.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
			slot.spawnAttemptInProgress = false;
			slot.spawnAttempted = false;
			slot.spawnSucceeded = false;
			slot.spawnFailed = false;
			slot.unresolvedHandleSinceTime = {};
			slot.lifecycleState = ProxyLifecycleState::kHeldNoRemote;
			slot.holdReason.reset();
			slot.proxyObservedLoaded3D = false;
			slot.proxyPending3DRecovery = false;
			slot.proxy3DResetQueued = false;
			slot.proxy3DRecoverySettling = false;
			slot.headMeshRefreshAttempted = false;
			slot.headMeshPendingStable = false;
			slot.visibilityRefreshPending = true;
			slot.visibilityRefreshLogged = false;

			LogInfoWithLocalPlayerPrefix(std::format(
				"Released stale runtime proxy {:08X} for remote player {} from prior cell {:08X} after local player moved to cell {:08X}.",
				releasedFormId,
				playerId,
				a_oldCellId,
				a_currentCell.GetFormID()));
		}
	}

	void MarkSlotHeld(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		ProxyLifecycleState a_reason)
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (a_slot.lifecycleState == a_reason && a_slot.holdReason == a_reason) {
			if (player) {
				CorrectHeldSlotPositionIfNeeded(a_slot, a_proxy, *player);
			}
			if (ShouldLog("runtime_proxy_holding_neutralization_check_" + std::to_string(a_slot.remotePlayerId), kProxyNeutralizationInterval)) {
				if (player) {
					NeutralizeProxyActor(a_slot, a_proxy, *player, ProxyNeutralizationReason::kHolding);
				}
			}
			LogProxyStillHeld(a_reason, a_slot.remotePlayerId);
			return;
		}

		ClearSlotProxyAnimationIfApplied(
			a_slot,
			a_proxy,
			a_reason == ProxyLifecycleState::kHeldDisconnected ? "disconnect hold" : "hold");
		ClearSlotEquipmentIfApplied(
			a_slot,
			a_proxy,
			a_reason == ProxyLifecycleState::kHeldDisconnected ? "disconnect hold" : "hold");
		if (player) {
			F4T::ProxyGameAPI::PositionRemoteActor(
				a_proxy,
				GetSlotHoldingPosition(a_slot, *player),
				a_proxy.GetHeading(),
				true);
			NeutralizeProxyActor(a_slot, a_proxy, *player, ProxyNeutralizationReason::kHolding);
		}
		a_slot.lifecycleState = a_reason;
		a_slot.holdReason = a_reason;
		ResetSlotMovementState(a_slot);
		ResetSlotObservationState(a_slot);
		if (a_reason == ProxyLifecycleState::kHeldDisconnected) {
			if (!HasSlotHeldSinceTime(a_slot)) {
				a_slot.heldSinceTime = std::chrono::steady_clock::now();
			}
			a_slot.reusableLogged = false;
		} else {
			a_slot.heldSinceTime = {};
			a_slot.reusableLogged = false;
		}

		switch (a_reason) {
		case ProxyLifecycleState::kHeldDisconnected:
			LogInfoWithLocalPlayerPrefix(std::format(
				"Remote player {} disappeared; moving runtime proxy actor {:08X} to holding position.",
				a_slot.remotePlayerId,
				a_proxy.GetFormID()));
			break;
		case ProxyLifecycleState::kHeldLeftCell:
			LogInfoWithLocalPlayerPrefix(std::format(
				"Remote player {} is not in the same cell; moving runtime proxy actor {:08X} to holding position.",
				a_slot.remotePlayerId,
				a_proxy.GetFormID()));
			break;
		default:
			LogInfoWithLocalPlayerPrefix(std::format(
				"Remote player {} is not currently represented; moving runtime proxy actor {:08X} to holding position.",
				a_slot.remotePlayerId,
				a_proxy.GetFormID()));
			break;
		}
	}

	void CorrectHeldSlotPositions(RE::TESObjectCELL& a_parentCell, const RE::PlayerCharacter& a_player)
	{
		for (auto& [_, slot] : g_proxySlots) {
			if (!IsHeldSlotState(slot.lifecycleState)) {
				continue;
			}

			if (auto* proxy = TryResolveSlotProxy(slot, a_parentCell, a_player)) {
				CorrectHeldSlotPositionIfNeeded(slot, *proxy, a_player);
			}
		}
	}

	void MarkReusableSlots(
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player)
	{
		const auto now = std::chrono::steady_clock::now();
		for (auto& [playerId, slot] : g_proxySlots) {
			if (slot.lifecycleState != ProxyLifecycleState::kHeldDisconnected || !HasSlotHeldSinceTime(slot)) {
				continue;
			}

			const auto heldSeconds = std::chrono::duration<float>(now - slot.heldSinceTime).count();
			if (heldSeconds < kDisconnectedProxyReuseDelaySeconds) {
				continue;
			}

			if (auto* proxy = TryResolveSlotProxy(slot, a_parentCell, a_player)) {
				ClearSlotProxyAnimationIfApplied(slot, *proxy, "reuse");
				ClearSlotEquipmentIfApplied(slot, *proxy, "reuse");
			} else {
				ResetSlotAppliedAnimationState(slot);
				F4T::DebugLogging::Log("Equipment", "ledger forget requested for remote player {}", slot.remotePlayerId);
				ResetSlotEquipmentTracking(slot, true);
				ResetSlotAppearanceTracking(slot);
				ResetSlotDisplayNameTracking(slot);
			}
			slot.lifecycleState = ProxyLifecycleState::kReusable;
			slot.holdReason.reset();
			slot.previousRemotePlayerId = playerId;
			if (!slot.reusableLogged) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Runtime proxy slot for disconnected remote player {} became reusable after {:.1f}s held.",
					playerId,
					heldSeconds));
				slot.reusableLogged = true;
			}
		}
	}

	void RestoreSlotProxyForRemotePlayer(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed)
	{
		const auto targetPosition = GetRuntimeProxyVisibleTargetPosition(a_slot, a_remotePlayer, a_player);

		a_slot.targetPosition = targetPosition;
		a_slot.targetHeading = a_remotePlayer.angleZ;
		a_slot.puppetDirection = a_remotePlayer.angleZ;
		a_slot.lastRemotePlayerSnapshot = a_remotePlayer;
		a_slot.puppetGraphSpeed = -1.0F;
		if (a_remotePlayer.isMoving && a_remotePlayer.movementSpeed >= 1.0F) {
			a_slot.puppetGraphSpeed = F4T::ProxyAnimationSync::ResolveLocomotionGraphSpeed(
				a_remotePlayer.movementSpeed,
				a_remotePlayer.animationGraphSpeed,
				a_remotePlayer.isMoving,
				a_remotePlayer.isSprinting);
		}

		F4T::ProxyPuppet::SetProxyLocomotionState(
			a_proxy.GetFormID(),
			a_slot.puppetGraphSpeed,
			a_slot.puppetDirection);

		F4T::ProxyGameAPI::PositionRemoteActor(
			a_proxy,
			targetPosition,
			a_remotePlayer.angleZ,
			true);
		a_slot.skipSmoothFrameOnce = true;

		a_slot.lifecycleState = ProxyLifecycleState::kActive;
		a_slot.holdReason.reset();
		a_slot.heldSinceTime = {};
		a_slot.reusableLogged = false;
		F4T::ProxySnapshotMovement::Reset(a_slot.snapshotMovement);
		a_slot.restoredForCurrentState = true;
		a_slot.firstSmoothedMovementLogged = false;
		a_slot.remoteStateMovedLogged = false;
		a_slot.lastRemotePlayerSnapshot = a_remotePlayer;
		a_slot.puppetGraphSpeed = -1.0F;

		LogInfoWithLocalPlayerPrefix(std::format(
			"Runtime proxy actor {:08X} restored/reused for remote player {} at visible X={:.2f}, Y={:.2f}, Z={:.2f} from remote X={:.2f}, Y={:.2f}, Z={:.2f}.",
			a_proxy.GetFormID(),
			a_remotePlayer.playerId,
			targetPosition.x,
			targetPosition.y,
			targetPosition.z,
			a_remotePlayer.x,
			a_remotePlayer.y,
			a_remotePlayer.z));
		ApplyProxyDisplayNameFromRemoteState(a_slot, a_proxy, a_remotePlayer);
		ApplyProxyEquipmentFromRemoteState(a_slot, a_proxy, a_remotePlayer);
		ApplyProxyPowerArmorFromRemoteState(a_slot, a_proxy, a_remotePlayer);
		UpdateProxyAnimationStateDebug(a_slot, a_remotePlayer, a_proxy);
		LogProxyMovementDiagnostic(
			a_proxy,
			a_remotePlayer,
			a_player,
			ActiveProxyKind::kRuntimeSpawned,
			a_sameCellPassed,
			ProxyMovementDiagnosticMode::kRestore);
	}

	void HoldDisconnectedSlots(
		const std::unordered_set<std::uint32_t>& a_seenRemotePlayerIds,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player)
	{
		for (auto& [playerId, slot] : g_proxySlots) {
			if (a_seenRemotePlayerIds.contains(playerId)) {
				continue;
			}
			if (slot.lifecycleState == ProxyLifecycleState::kReusable) {
				continue;
			}

			if (slot.spawnAttemptInProgress || slot.spawnState == PlaceAtMeBackedProxySpawnState::kSettling) {
				MaintainSlotPlaceAtMeSpawn(slot, a_parentCell, a_player);
			}

			if (auto* proxy = TryResolveSlotProxy(slot, a_parentCell, a_player)) {
				MarkSlotHeld(slot, *proxy, ProxyLifecycleState::kHeldDisconnected);
			} else if (slot.lifecycleState != ProxyLifecycleState::kHeldDisconnected || !HasSlotHeldSinceTime(slot)) {
				const auto newlyHeldDisconnected = slot.lifecycleState != ProxyLifecycleState::kHeldDisconnected;
				slot.lifecycleState = ProxyLifecycleState::kHeldDisconnected;
				slot.holdReason = ProxyLifecycleState::kHeldDisconnected;
				if (!HasSlotHeldSinceTime(slot)) {
					slot.heldSinceTime = std::chrono::steady_clock::now();
				}
				slot.reusableLogged = false;
				ResetSlotMovementState(slot);
				ResetSlotObservationState(slot);
				if (newlyHeldDisconnected) {
					LogInfoWithLocalPlayerPrefix(std::format(
						"Remote player {} disappeared; runtime proxy slot is held without a valid proxy actor and can become reusable after the grace period.",
						playerId));
				}
			}
		}
	}

	void HoldFallbackIfRepresentedPlayerDisconnected(
		const std::unordered_set<std::uint32_t>& a_seenRemotePlayerIds,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player)
	{
		if (!g_representedPlayerId || a_seenRemotePlayerIds.contains(*g_representedPlayerId)) {
			return;
		}

		if (auto* fallbackProxy = ResolveProxy(a_parentCell, a_player)) {
			MoveProxyToHoldingPosition(
				*fallbackProxy,
				ProxyLifecycleState::kHeldDisconnected,
				g_representedPlayerId,
				ActiveProxyKind::kPlacedFallback);
		}
		g_representedPlayerId.reset();
	}

	bool TryUpdatePlacedFallbackForRemotePlayer(
		const RemotePlayer& a_remotePlayer,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed,
		ProxyActorSlot& a_slot)
	{
		auto* fallbackProxy = ResolveProxy(a_parentCell, a_player);
		if (!fallbackProxy) {
			return false;
		}

		if (!g_fallbackRuntimeUnavailableLogged || g_representedPlayerId != a_remotePlayer.playerId) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Placed fallback proxy active for remote player {} because runtime proxy spawning is unavailable for that player. Fallback remains single-player only.",
				a_remotePlayer.playerId));
			g_fallbackRuntimeUnavailableLogged = true;
		}

		const auto representedPlayerChanged = g_representedPlayerId != a_remotePlayer.playerId;
		g_representedPlayerId = a_remotePlayer.playerId;
		if (representedPlayerChanged ||
			g_proxyLifecycleState != ProxyLifecycleState::kActive ||
			ShouldSnapRemoteMovement(a_remotePlayer.movementType)) {
			RestoreProxyForRemotePlayer(
				*fallbackProxy,
				a_remotePlayer,
				a_player,
				ActiveProxyKind::kPlacedFallback,
				a_sameCellPassed);
			return true;
		}

		ApplyRemoteMovementStateToProxy(*fallbackProxy, a_remotePlayer);
		MoveProxyToRemotePlayer(
			*fallbackProxy,
			a_remotePlayer,
			a_player,
			ActiveProxyKind::kPlacedFallback,
			a_sameCellPassed,
			a_slot);
		return true;
	}
}
