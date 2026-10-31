#include "pch.h"

#include "F4TProxyActorControllerInternal.h"
#include "F4TProxySaveSafety.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr auto kRuntimeProxyVisibilityNudgeInterval = 250ms;
	constexpr float kRuntimeProxyForceVisibilityRange = 8192.0F;

	std::uint32_t GetProxyHumanoidUpdate3DFlags()
	{
		return static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kModel) |
		       static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kSkin) |
		       static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kHead) |
		       static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kFace) |
		       static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kSkeleton) |
		       static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kInitDefault);
	}

	std::uint32_t GetProxyHeadUpdate3DFlags()
	{
		return static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kHead) |
		       static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kFace);
	}

	void RefreshRuntimeProxyVisibility(RE::Actor& a_proxy)
	{
		if (a_proxy.IsDisabled()) {
			a_proxy.Enable(false);
		}

		a_proxy.SetAlpha(1.0F);
		a_proxy.UpdateAlpha();
		a_proxy.SetDisplayGeometry(true);
		a_proxy.Update3DPosition(true);
		if (ProxyHasLoaded3D(a_proxy)) {
			a_proxy.MoveHavok(true);
		}

		QueueProxyUpdate3D(a_proxy, GetProxyVisibilityUpdate3DFlags());
	}

	void LogRuntimeProxyVisibilityState(RE::Actor& a_proxy, std::string_view a_context)
	{
		LogInfoWithLocalPlayerPrefix(std::format(
			"Runtime proxy visibility ({}): actor={:08X}, IsDisabled={}, QAvailableInGame={}, Get3D={}, GetFullyLoaded3D={}, GetCurrent3D={}, IsVisible={}.",
			a_context,
			a_proxy.GetFormID(),
			a_proxy.IsDisabled(),
			a_proxy.QAvailableInGame(),
			a_proxy.Get3D() != nullptr,
			a_proxy.GetFullyLoaded3D() != nullptr,
			a_proxy.GetCurrent3D() != nullptr,
			a_proxy.IsVisible()));
	}

	void NudgeProxyFrustumVisibilityIfNearLocalPlayer(RE::Actor& a_proxy, const RE::PlayerCharacter& a_player)
	{
		const auto distance = GetDistance(a_proxy.GetPosition(), a_player.GetPosition());
		if (distance > kRuntimeProxyForceVisibilityRange) {
			return;
		}

		constexpr auto frustrum = std::to_underlying(RE::ACTOR_VISIBILITY_MASK::kFrustrum);
		a_proxy.visFlags = static_cast<std::uint32_t>(a_proxy.visFlags | frustrum);
	}

	void RecoverRuntimeProxyAfter3DReload(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed)
	{
		if (!ProxyHasLoaded3D(a_proxy)) {
			return;
		}

		if (!a_slot.proxy3DResetQueued) {
			EnsureProxyHeadMesh(a_slot, a_proxy);
			a_slot.proxy3DResetQueued = true;
			LogInfoWithLocalPlayerPrefix(std::format(
				"Runtime proxy actor {:08X} for remote player {} queued 3D recovery after mesh reload (head/face refresh).",
				a_proxy.GetFormID(),
				a_slot.remotePlayerId));
			return;
		}

		if (!Proxy3DReadyForPresentation(a_proxy)) {
			return;
		}

		if (!a_slot.proxy3DRecoverySettling) {
			a_slot.proxy3DRecoverySettling = true;
			a_slot.proxy3DRecoverySettleStartTime = std::chrono::steady_clock::now();
			F4T::ProxyAnimationSync::ResetAppliedAnimationState(a_slot.appliedAnimationState);
			F4T::ProxyPuppet::SetProxyLocomotionState(
				a_proxy.GetFormID(),
				-1.0F,
				a_slot.puppetDirection);
			LogInfoWithLocalPlayerPrefix(std::format(
				"Runtime proxy actor {:08X} for remote player {} entered 3D recovery settle; deferring movement for {:.1f}s.",
				a_proxy.GetFormID(),
				a_slot.remotePlayerId,
				kPlaceAtMeProxySettleSeconds));
			return;
		}

		const auto elapsedSeconds = std::chrono::duration<float>(
			std::chrono::steady_clock::now() - a_slot.proxy3DRecoverySettleStartTime).count();
		if (elapsedSeconds < kPlaceAtMeProxySettleSeconds) {
			return;
		}

		a_slot.proxyPending3DRecovery = false;
		a_slot.proxy3DResetQueued = false;
		a_slot.proxy3DRecoverySettling = false;
		a_slot.visibilityRefreshPending = false;
		a_slot.restoredForCurrentState = false;
		a_slot.hasLoggedInitialAnimationState = false;

		RestoreSlotProxyForRemotePlayer(a_slot, a_proxy, a_remotePlayer, a_player, a_sameCellPassed);

		LogInfoWithLocalPlayerPrefix(std::format(
			"Runtime proxy actor {:08X} for remote player {} finished 3D recovery: Get3D={}, GetFullyLoaded3D={}, IsVisible={}.",
			a_proxy.GetFormID(),
			a_slot.remotePlayerId,
			a_proxy.Get3D() != nullptr,
			a_proxy.GetFullyLoaded3D() != nullptr,
			a_proxy.IsVisible()));
	}
}

namespace F4T::ProxyActorController::detail
{
	void MarkRuntimeProxySpawnFailed(std::string_view a_reason)
	{
		g_runtimeProxySpawnFailed = true;
		g_runtimeProxySpawnAttemptInProgress = false;
		LogWarningWithLocalPlayerPrefix(std::format(
			"Runtime proxy spawn failed: {}",
			a_reason));
	}

	bool ProxyHasLoaded3D(const RE::Actor& a_proxy)
	{
		return a_proxy.Get3D() != nullptr ||
		       a_proxy.GetFullyLoaded3D() != nullptr ||
		       a_proxy.GetCurrent3D() != nullptr;
	}

	bool Proxy3DReadyForPresentation(const RE::Actor& a_proxy)
	{
		return ProxyHasLoaded3D(a_proxy);
	}

	bool ShouldRetainProxyInPriorLocalCell(
		std::uint32_t a_playerId,
		const RE::TESObjectCELL& a_proxyCell,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player)
	{
		if (!a_currentCell.IsExterior() || !a_proxyCell.IsExterior()) {
			return false;
		}

		for (const auto& remotePlayer : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
			if (remotePlayer.playerId != a_playerId || !HasValidRemoteTransform(remotePlayer)) {
				continue;
			}

			if (!IsRemotePlayerInSameLocation(remotePlayer, a_player, a_currentCell)) {
				continue;
			}

			const auto remoteCellId = ParseHexFormId(remotePlayer.cellId);
			if (remoteCellId && *remoteCellId == a_proxyCell.GetFormID()) {
				return true;
			}

			return true;
		}

		return false;
	}

	std::uint32_t GetProxyVisibilityUpdate3DFlags()
	{
		return static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kModel) |
		       static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kSkin) |
		       GetProxyHeadUpdate3DFlags();
	}

	void QueueProxyUpdate3D(RE::Actor& a_proxy, std::uint32_t a_flags)
	{
		a_proxy.Set3DUpdateFlag(RE::RESET_3D_FLAGS::kModel);
		a_proxy.Set3DUpdateFlag(RE::RESET_3D_FLAGS::kSkin);
		a_proxy.Set3DUpdateFlag(RE::RESET_3D_FLAGS::kHead);
		a_proxy.Set3DUpdateFlag(RE::RESET_3D_FLAGS::kFace);

		if ((a_flags & static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kSkeleton)) != 0) {
			a_proxy.Set3DUpdateFlag(RE::RESET_3D_FLAGS::kSkeleton);
		}
		if ((a_flags & static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kInitDefault)) != 0) {
			a_proxy.Set3DUpdateFlag(RE::RESET_3D_FLAGS::kInitDefault);
		}

		if (auto* taskQueue = RE::TaskQueueInterface::GetSingleton()) {
			taskQueue->QueueUpdate3D(std::addressof(a_proxy), a_flags);
		}
	}

	bool ProxyHeadNodesMissing(const RE::Actor& a_proxy)
	{
		const auto* npc = a_proxy.GetNPC();
		if (!npc || npc->numHeadParts <= 0) {
			return false;
		}

		if (const auto* process = a_proxy.currentProcess) {
			if (const auto* middleHigh = process->middleHigh) {
				return middleHigh->headNode == nullptr && middleHigh->faceNodeSkinned == nullptr;
			}
		}

		return true;
	}

	void EnsureProxyHeadMesh(ProxyActorSlot& a_slot, RE::Actor& a_proxy)
	{
		if (auto* process = a_proxy.currentProcess) {
			if (auto* middleHigh = process->middleHigh) {
				if (middleHigh->headDeferredHideLimb) {
					middleHigh->headDeferredHideLimb = nullptr;
				}
			}
		}

		QueueProxyUpdate3D(a_proxy, GetProxyHeadUpdate3DFlags());

		a_slot.headMeshRefreshAttempted = true;
		a_slot.headMeshPendingStable = true;
		F4T::ProxyAnimationSync::ResetAppliedAnimationState(a_slot.appliedAnimationState);
		a_slot.hasLoggedInitialAnimationState = false;

		const auto* process = a_proxy.currentProcess;
		const auto* middleHigh = process ? process->middleHigh : nullptr;
		LogThrottledInfo(
			"runtime_proxy_head_mesh_refresh_" + std::to_string(a_slot.remotePlayerId),
			std::format(
				"Runtime proxy head mesh refresh for remote player {}: actor={:08X}, headNode={}, faceNodeSkinned={}, Get3D={}, IsVisible={}.",
				a_slot.remotePlayerId,
				a_proxy.GetFormID(),
				middleHigh && middleHigh->headNode != nullptr,
				middleHigh && middleHigh->faceNodeSkinned != nullptr,
				a_proxy.Get3D() != nullptr,
				a_proxy.IsVisible()),
			kRuntimeProxyVisibilityDiagnosticLogInterval);
	}

	void EnsureRuntimeProxyVisibility(ProxyActorSlot& a_slot, RE::Actor& a_proxy, std::string_view a_context)
	{
		if (ProxyHasLoaded3D(a_proxy) && a_proxy.IsVisible()) {
			a_slot.visibilityRefreshPending = false;
			return;
		}

		RefreshRuntimeProxyVisibility(a_proxy);

		if (!a_slot.visibilityRefreshLogged) {
			LogRuntimeProxyVisibilityState(a_proxy, a_context);
			a_slot.visibilityRefreshLogged = true;
		}
	}

	void MaintainRuntimeProxyFrustumVisibility(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RE::PlayerCharacter& a_player)
	{
		if (!ProxyHasLoaded3D(a_proxy)) {
			a_slot.lastObservedProxyVisible = false;
			return;
		}

		const bool isVisible = a_proxy.IsVisible();
		const bool wasVisible = a_slot.lastObservedProxyVisible.value_or(false);

		if (!isVisible) {
			if (ShouldLog(
					"runtime_proxy_visibility_nudge_" + std::to_string(a_slot.remotePlayerId),
					kRuntimeProxyVisibilityNudgeInterval)) {
				RefreshRuntimeProxyVisibility(a_proxy);
				NudgeProxyFrustumVisibilityIfNearLocalPlayer(a_proxy, a_player);
			}
			a_slot.visibilityRefreshPending = true;
		} else {
			if (a_slot.visibilityRefreshPending) {
				a_slot.visibilityRefreshPending = false;
				a_slot.visibilityRefreshLogged = false;
			}
			if (a_slot.lastObservedProxyVisible.has_value() && !wasVisible) {
				a_slot.restoredForCurrentState = false;
				LogThrottledInfo(
					"runtime_proxy_visibility_regained_" + std::to_string(a_slot.remotePlayerId),
					std::format(
						"Runtime proxy actor {:08X} for remote player {} regained visibility after frustum cull.",
						a_proxy.GetFormID(),
						a_slot.remotePlayerId),
					kRuntimeProxyVisibilityDiagnosticLogInterval);
			}
		}

		a_slot.lastObservedProxyVisible = isVisible;
	}

	void MaintainRuntimeProxy3DState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed)
	{
		const bool has3D = ProxyHasLoaded3D(a_proxy);

		if (a_slot.proxyObservedLoaded3D && !has3D) {
			a_slot.proxyPending3DRecovery = true;
			a_slot.proxy3DResetQueued = false;
			a_slot.proxy3DRecoverySettling = false;
			a_slot.restoredForCurrentState = false;
			a_slot.headMeshRefreshAttempted = false;
			a_slot.headMeshPendingStable = false;
			a_slot.visibilityRefreshPending = true;
			a_slot.visibilityRefreshLogged = false;
			F4T::ProxyAnimationSync::ResetAppliedAnimationState(a_slot.appliedAnimationState);
			F4T::ProxyPuppet::SetProxyLocomotionState(
				a_proxy.GetFormID(),
				-1.0F,
				a_slot.puppetDirection);
			LogThrottledInfo(
				"runtime_proxy_3d_unloaded_" + std::to_string(a_slot.remotePlayerId),
				std::format(
					"Runtime proxy actor {:08X} for remote player {} lost loaded 3D (likely out of load range); deferring puppet/animation until reload.",
					a_proxy.GetFormID(),
					a_slot.remotePlayerId),
				kRuntimeProxyVisibilityDiagnosticLogInterval);
		}

		if (has3D && a_slot.proxyPending3DRecovery) {
			RecoverRuntimeProxyAfter3DReload(a_slot, a_proxy, a_remotePlayer, a_player, a_sameCellPassed);
		}

		a_slot.proxyObservedLoaded3D = has3D;
	}
}
