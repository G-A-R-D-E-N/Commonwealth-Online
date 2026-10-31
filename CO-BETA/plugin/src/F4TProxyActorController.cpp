#include "pch.h"

#include <mutex>

#include "F4TProxyActorController.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxySaveSafety.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	static std::optional<std::uint32_t> g_lastLocalCellId;
	static bool g_globalControllerStartedLogged = false;
	constexpr std::size_t kMaxRuntimeProxyActors = 15;

	bool g_remoteStateMovementStartedLogged = false;
	bool g_proxySmoothingEnabledLogged = false;
	std::uint32_t g_nextProxyHoldingPositionIndex = 0;
	bool g_stage4StartedLogged = false;
	std::mutex g_proxyFormIdIndexLock;
	std::unordered_map<std::uint32_t, std::uint32_t> g_proxyFormIdIndex;

	ProxyActorSlot* FindSlot(std::uint32_t a_remotePlayerId)
	{
		const auto slot = g_proxySlots.find(a_remotePlayerId);
		return slot != g_proxySlots.end() ? std::addressof(slot->second) : nullptr;
	}

	std::size_t CountSlotsInState(ProxyLifecycleState a_state)
	{
		return static_cast<std::size_t>(std::ranges::count_if(
			g_proxySlots,
			[a_state](const auto& a_entry) {
				return a_entry.second.lifecycleState == a_state;
			}));
	}

	std::optional<std::uint32_t> FindReusableSlot()
	{
		std::optional<std::uint32_t> reusablePlayerId;
		for (const auto& [playerId, slot] : g_proxySlots) {
			if (slot.lifecycleState != ProxyLifecycleState::kReusable) {
				continue;
			}

			if (!reusablePlayerId || playerId < *reusablePlayerId) {
				reusablePlayerId = playerId;
			}
		}
		return reusablePlayerId;
	}

	ProxyActorSlot* AssignSlotToRemotePlayer(
		std::uint32_t a_reusablePlayerId,
		const RemotePlayer& a_remotePlayer,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed)
	{
		auto node = g_proxySlots.extract(a_reusablePlayerId);
		if (node.empty()) {
			return nullptr;
		}

		auto& slotValue = node.mapped();
		const auto previousRemotePlayerId = slotValue.remotePlayerId;
		if (auto* proxy = TryResolveSlotProxy(slotValue, a_parentCell, a_player)) {
			ClearSlotProxyAnimationIfApplied(slotValue, *proxy, "reassignment");
			ClearSlotEquipmentIfApplied(slotValue, *proxy, "reassignment");
		} else {
			ResetSlotAppliedAnimationState(slotValue);
			F4T::DebugLogging::Log("Equipment", "ledger forget requested for remote player {}", slotValue.remotePlayerId);
			ResetSlotEquipmentTracking(slotValue, true);
			ResetSlotAppearanceTracking(slotValue);
		}
		ResetSlotDisplayNameTracking(slotValue);
		slotValue.previousRemotePlayerId = previousRemotePlayerId;
		slotValue.remotePlayerId = a_remotePlayer.playerId;
		slotValue.lifecycleState = ProxyLifecycleState::kHeldNoRemote;
		slotValue.holdReason.reset();
		slotValue.heldSinceTime = {};
		slotValue.reusableLogged = false;
		slotValue.visibilityRefreshPending = true;
		slotValue.visibilityRefreshLogged = false;
		SetSlotInitialRemoteSpawnTransform(slotValue, a_remotePlayer);
		slotValue.pendingSpawnHandle.reset();
		slotValue.spawnAttemptInProgress = false;
		slotValue.spawnFailed = false;
		if (slotValue.proxyHandle) {
			slotValue.spawnState = PlaceAtMeBackedProxySpawnState::kPromoted;
			slotValue.spawnSucceeded = true;
		} else {
			slotValue.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
			slotValue.spawnAttempted = false;
			slotValue.spawnSucceeded = false;
			slotValue.preexistingCandidateRefs.clear();
		}
		ResetSlotMovementState(slotValue);
		ResetSlotObservationState(slotValue);
		if (g_runtimeProxySpawnAttemptPlayerId == previousRemotePlayerId) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=slot-reassigned thread={}",
				previousRemotePlayerId,
				REX::W32::GetCurrentThreadId());
			g_runtimeProxySpawnAttemptPlayerId.reset();
		}
		node.key() = a_remotePlayer.playerId;

		auto insertResult = g_proxySlots.insert(std::move(node));
		auto& slot = insertResult.position->second;
		LogInfoWithLocalPlayerPrefix(std::format(
			"Reusable runtime proxy slot reassigned from disconnected remote player {} to remote player {}.",
			previousRemotePlayerId,
			a_remotePlayer.playerId));

		if (auto* proxy = TryResolveSlotProxy(slot, a_parentCell, a_player)) {
			NeutralizeProxyActor(slot, *proxy, a_player, ProxyNeutralizationReason::kSlotReassigned);
			RestoreSlotProxyForRemotePlayer(slot, *proxy, a_remotePlayer, a_player, a_sameCellPassed);
		}

		return std::addressof(slot);
	}

	ProxyActorSlot* GetOrCreateSlot(
		const RemotePlayer& a_remotePlayer,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed)
	{
		if (auto* existingSlot = FindSlot(a_remotePlayer.playerId)) {
			return existingSlot;
		}

		if (g_proxySlots.size() < kMaxRuntimeProxyActors) {
			auto [slot, inserted] = g_proxySlots.emplace(a_remotePlayer.playerId, ProxyActorSlot{});
			slot->second.remotePlayerId = a_remotePlayer.playerId;
			CaptureSlotInitialRemoteSpawnTransform(slot->second, a_remotePlayer);
			if (inserted) {
				slot->second.holdingPositionIndex = g_nextProxyHoldingPositionIndex++;
				ResetSlotObservationState(slot->second);
				LogInfoWithLocalPlayerPrefix(std::format(
					"Runtime proxy slot created for remote player {} with holding index {}.",
					a_remotePlayer.playerId,
					slot->second.holdingPositionIndex));
			}
			return std::addressof(slot->second);
		}

		if (auto reusablePlayerId = FindReusableSlot()) {
			return AssignSlotToRemotePlayer(*reusablePlayerId, a_remotePlayer, a_parentCell, a_player, a_sameCellPassed);
		}

		LogThrottledWarning(
			"stage4_max_runtime_proxy_limit",
			std::format(
				"Runtime proxy max limit reached: max={}, totalSlots={}, active={}, heldLeftCell={}, heldDisconnectedWaiting={}, reusable={}. Remote player {} will not receive a visual proxy yet.",
				kMaxRuntimeProxyActors,
				g_proxySlots.size(),
				CountSlotsInState(ProxyLifecycleState::kActive),
				CountSlotsInState(ProxyLifecycleState::kHeldLeftCell),
				CountSlotsInState(ProxyLifecycleState::kHeldDisconnected),
				CountSlotsInState(ProxyLifecycleState::kReusable),
				a_remotePlayer.playerId));
		return nullptr;
	}

	bool RequestSlotProxyRespawnForSexMismatch(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer)
	{
		const auto desiredFemale = GetDesiredProxyIsFemale(a_remotePlayer);
		if (!desiredFemale.has_value()) {
			return false;
		}

		const auto* baseActor = a_proxy.GetNPC();
		if (!baseActor) {
			return false;
		}

		const bool proxyFemale =
			const_cast<RE::TESNPC*>(baseActor)->GetSex() == RE::SEX::kFemale;
		if (proxyFemale == *desiredFemale) {
			return false;
		}

		LogInfoWithLocalPlayerPrefix(std::format(
			"Runtime proxy sex mismatch for remote player {}: proxyBase={:08X} isFemale={}, desiredIsFemale={}. Deleting and respawning with {}.",
			a_slot.remotePlayerId,
			baseActor->GetFormID(),
			proxyFemale,
			*desiredFemale,
			*desiredFemale ? kFemaleProxyBaseActorEditorId : kProxyBaseActorEditorId));

		F4T::ProxySaveSafety::DestroyProxyActor(a_proxy);
		ResetSlotForRespawn(a_slot);
		if (g_runtimeProxySpawnAttemptPlayerId == a_slot.remotePlayerId) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=sex-mismatch-respawn thread={}",
				a_slot.remotePlayerId,
				REX::W32::GetCurrentThreadId());
			g_runtimeProxySpawnAttemptPlayerId.reset();
		}
		return true;
	}

	bool UpdateRuntimeSlotForRemotePlayer(
		ProxyActorSlot& a_slot,
		const RemotePlayer& a_remotePlayer,
		RE::TESObjectCELL& a_parentCell,
		const RE::PlayerCharacter& a_player,
		bool a_sameCellPassed)
	{
		CaptureSlotInitialRemoteSpawnTransform(a_slot, a_remotePlayer);
		auto* proxy = TryResolveSlotProxy(a_slot, a_parentCell, a_player);
		if (proxy && RequestSlotProxyRespawnForSexMismatch(a_slot, *proxy, a_remotePlayer)) {
			proxy = nullptr;
		}
		if (!proxy) {
			MaintainSlotPlaceAtMeSpawn(a_slot, a_parentCell, a_player, std::addressof(a_remotePlayer));
			if (a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kSettling) {
				return false;
			}
			proxy = TryResolveSlotProxy(a_slot, a_parentCell, a_player);
			if (!proxy) {
				return false;
			}
		}

		if (ShouldLog("runtime_proxy_active_neutralization_check_" + std::to_string(a_slot.remotePlayerId), kProxyNeutralizationInterval)) {
			NeutralizeProxyActor(a_slot, *proxy, a_player, ProxyNeutralizationReason::kActiveMaintenance);
		}

		if (a_slot.visibilityRefreshPending &&
			ShouldLog(
				"runtime_proxy_visibility_refresh_" + std::to_string(a_slot.remotePlayerId),
				kRuntimeProxyVisibilityDiagnosticLogInterval)) {
			EnsureRuntimeProxyVisibility(a_slot, *proxy, "active-refresh");
		}

		MaintainRuntimeProxyFrustumVisibility(a_slot, *proxy, a_player);
		MaintainRuntimeProxy3DState(a_slot, *proxy, a_remotePlayer, a_player, a_sameCellPassed);

		if (a_slot.proxyPending3DRecovery) {
			return true;
		}

		if (!ProxyHasLoaded3D(*proxy)) {
			return true;
		}

		const bool remoteCellChanged =
			a_slot.lastObservedRemoteCellId.has_value() &&
			*a_slot.lastObservedRemoteCellId != a_remotePlayer.cellId;
		a_slot.lastObservedRemoteCellId = a_remotePlayer.cellId;

		if (ProxyHeadNodesMissing(*proxy)) {
			if (!a_slot.headMeshRefreshAttempted) {
				EnsureProxyHeadMesh(a_slot, *proxy);
			}
			return true;
		}

		if (a_slot.headMeshPendingStable) {
			a_slot.headMeshPendingStable = false;
			F4T::ProxyAnimationSync::ResetAppliedAnimationState(a_slot.appliedAnimationState);
			a_slot.hasLoggedInitialAnimationState = false;

			ApplyProxyDisplayNameFromRemoteState(a_slot, *proxy, a_remotePlayer);

			const bool needsSnapRestoreWhilePending =
				ShouldSnapRemoteMovement(a_remotePlayer.movementType) || remoteCellChanged;
			if (!a_slot.restoredForCurrentState ||
				a_slot.lifecycleState != ProxyLifecycleState::kActive ||
				needsSnapRestoreWhilePending) {
				RestoreSlotProxyForRemotePlayer(a_slot, *proxy, a_remotePlayer, a_player, a_sameCellPassed);
			}
			return true;
		}

		ApplyProxyDisplayNameFromRemoteState(a_slot, *proxy, a_remotePlayer);

		const bool needsSnapRestore =
			ShouldSnapRemoteMovement(a_remotePlayer.movementType) || remoteCellChanged;
		const bool needsFirstOrSnapRestore =
			!a_slot.restoredForCurrentState ||
			a_slot.lifecycleState != ProxyLifecycleState::kActive ||
			needsSnapRestore;

		if (needsFirstOrSnapRestore) {
			RestoreSlotProxyForRemotePlayer(a_slot, *proxy, a_remotePlayer, a_player, a_sameCellPassed);
			return true;
		}

		ApplyProxyAppearanceFromRemoteState(a_slot, *proxy, a_remotePlayer);
		if (a_slot.headMeshPendingStable || ProxyHeadNodesMissing(*proxy)) {
			return true;
		}

		MoveSlotProxyToRemotePlayer(a_slot, *proxy, a_remotePlayer, a_player, a_sameCellPassed);
		return true;
	}

	void RebuildProxyFormIdIndex()
	{
		std::unordered_map<std::uint32_t, std::uint32_t> index;
		const auto indexSlot = [&index](std::uint32_t a_playerId, const ProxyActorSlot& a_slot) {
			if (a_slot.cachedProxyFormId) {
				index.emplace(*a_slot.cachedProxyFormId, a_playerId);
			}
			if (const auto proxy = a_slot.proxyHandle.get()) {
				index.emplace(proxy->GetFormID(), a_playerId);
			}
		};

		for (const auto& [playerId, slot] : g_proxySlots) {
			if (slot.lifecycleState == ProxyLifecycleState::kActive) {
				indexSlot(playerId, slot);
			}
		}
		for (const auto& [playerId, slot] : g_proxySlots) {
			if (slot.lifecycleState != ProxyLifecycleState::kActive) {
				indexSlot(playerId, slot);
			}
		}

		const std::scoped_lock lock(g_proxyFormIdIndexLock);
		g_proxyFormIdIndex = std::move(index);
	}
}

namespace F4T::ProxyActorController
{
	std::optional<std::uint32_t> TryGetRemotePlayerIdForProxyFormId(std::uint32_t a_proxyFormId)
	{
		if (a_proxyFormId == 0) {
			return std::nullopt;
		}

		// Slot map is main-thread only; event sinks read the index.
		const std::scoped_lock lock(g_proxyFormIdIndexLock);
		const auto entry = g_proxyFormIdIndex.find(a_proxyFormId);
		if (entry == g_proxyFormIdIndex.end()) {
			return std::nullopt;
		}
		return entry->second;
	}

	RE::Actor* TryGetProxyActorForRemotePlayerId(std::uint32_t a_remotePlayerId)
	{
		if (a_remotePlayerId == 0) {
			return nullptr;
		}

		const auto slot = g_proxySlots.find(a_remotePlayerId);
		if (slot == g_proxySlots.end()) {
			return nullptr;
		}

		// A returned raw pointer must be backed by the slot's persistent strong ref.
		if (slot->second.cachedProxyActor) {
			return slot->second.cachedProxyActor.get();
		}
		if (auto proxy = slot->second.proxyHandle.get()) {
			slot->second.cachedProxyActor = proxy;
			return slot->second.cachedProxyActor.get();
		}

		return nullptr;
	}

	std::vector<ActiveProxyActor> GetActiveProxyActors()
	{
		std::vector<ActiveProxyActor> proxies;
		for (const auto& [playerId, slot] : g_proxySlots) {
			if (slot.lifecycleState != ProxyLifecycleState::kActive) {
				continue;
			}
			if (auto* actor = TryGetProxyActorForRemotePlayerId(playerId)) {
				proxies.push_back(ActiveProxyActor{ playerId, actor });
			}
		}
		return proxies;
	}

	bool IsPlayerProxyActor(const RE::Actor& a_actor)
	{
		return IsCustomPlayerProxyActor(a_actor);
	}

	void PublishActiveProxyFormIds()
	{
		std::vector<std::uint32_t> proxyFormIds;
		proxyFormIds.reserve(g_proxySlots.size());

		const auto appendActorHandleFormId = [&proxyFormIds](const RE::ActorHandle& a_handle) {
			if (!a_handle) {
				return;
			}
			if (const auto actorRef = a_handle.get()) {
				proxyFormIds.push_back(actorRef->GetFormID());
			}
		};

		const auto appendHandleFormId = [&proxyFormIds](const RE::ObjectRefHandle& a_handle) {
			if (!a_handle) {
				return;
			}
			if (const auto ref = a_handle.get()) {
				proxyFormIds.push_back(ref->GetFormID());
			}
		};

		for (const auto& [_, slot] : g_proxySlots) {
			const bool puppetBlocked =
				slot.proxyPending3DRecovery ||
				slot.proxy3DRecoverySettling ||
				slot.spawnState == PlaceAtMeBackedProxySpawnState::kSettling ||
				IsPlaceAtMeBackedProxyAttempting(slot.spawnState);
			if (puppetBlocked) {
				continue;
			}

			appendActorHandleFormId(slot.proxyHandle);
			appendActorHandleFormId(slot.pendingSpawnHandle);
			if (slot.cachedProxyFormId &&
				!std::ranges::contains(proxyFormIds, *slot.cachedProxyFormId)) {
				proxyFormIds.push_back(*slot.cachedProxyFormId);
			}
		}

		appendHandleFormId(g_runtimeProxyHandle);
		appendHandleFormId(g_proxyHandle);

		std::string formIdList{ "[" };
		for (std::size_t i = 0; i < proxyFormIds.size(); ++i) {
			if (i != 0) {
				formIdList += ", ";
			}
			formIdList += std::format("{:08X}", proxyFormIds[i]);
		}
		formIdList += "]";

		std::uint64_t hookCalls = 0;
		std::uint64_t hookSkips = 0;
		F4T::ProxyPuppet::GetHookStats(hookCalls, hookSkips);

		LogThrottledInfo(
			"proxy_puppet_stats",
			std::format(
				"Proxy puppet diagnostics: publishedProxyFormIds={}, feedbackHookTotalCalls={}, feedbackHookProxySkips={}.",
				formIdList,
				hookCalls,
				hookSkips),
			1s);

		F4T::ProxyPuppet::SetActiveProxyFormIDs(std::move(proxyFormIds));
	}

	void Update()
	{
		F4T::ProxySaveSafety::Tick();
		if (F4T::ProxySaveSafety::IsProxyTickBlocked()) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			LogThrottledWarning("player_missing", "Proxy actor controller skipped: local player is not available yet.");
			return;
		}

		auto* parentCell = player->GetParentCell();
		if (!parentCell) {
			LogThrottledWarning("cell_missing", "Proxy actor controller skipped: local player parent cell is unavailable.");
			return;
		}

		if (!g_globalControllerStartedLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Global proxy actor controller active: placeAtMeSpawn=true, currentCell={:08X}.",
				parentCell->GetFormID()));
			g_globalControllerStartedLogged = true;
		}

		const auto currentCellId = parentCell->GetFormID();
		if (g_lastLocalCellId && *g_lastLocalCellId != currentCellId) {
			ReleaseAllSlotProxiesInOldCell(*g_lastLocalCellId, *parentCell, *player);
		}
		g_lastLocalCellId = currentCellId;

		if (!g_stage4StartedLogged) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Proxy actor manager active (PlaceAtMe, MoveRefrToPosition positioning), max slots={}.",
				kMaxRuntimeProxyActors));
			g_stage4StartedLogged = true;
		}

	const auto remotePlayers = GetSortedRemotePlayerSnapshot();

	MarkReusableSlots(*parentCell, *player);
		std::unordered_set<std::uint32_t> seenRemotePlayerIds;
		seenRemotePlayerIds.reserve(remotePlayers.size());
		[[maybe_unused]] std::optional<RemotePlayer> fallbackCandidate;
		[[maybe_unused]] bool fallbackCandidateSameCellPassed = false;
		[[maybe_unused]] ProxyActorSlot* fallbackCandidateSlot = nullptr;

		for (const auto& remotePlayer : remotePlayers) {
			seenRemotePlayerIds.insert(remotePlayer.playerId);

			if (!HasValidRemoteTransform(remotePlayer)) {
				if (auto* slot = FindSlot(remotePlayer.playerId)) {
					if (auto* proxy = TryResolveSlotProxy(*slot, *parentCell, *player)) {
						MarkSlotHeld(*slot, *proxy, ProxyLifecycleState::kHeldNoRemote);
					}
				}
				continue;
			}

			const auto sameCellPassed = IsRemotePlayerInSameLocation(remotePlayer, *player, *parentCell);
			if (!sameCellPassed) {
				if (auto* slot = FindSlot(remotePlayer.playerId)) {
					if (auto* proxy = TryResolveSlotProxy(*slot, *parentCell, *player)) {
						MarkSlotHeld(*slot, *proxy, ProxyLifecycleState::kHeldLeftCell);
					}
				}
				continue;
			}

			auto* slot = GetOrCreateSlot(remotePlayer, *parentCell, *player, sameCellPassed);
			if (!slot) {
				continue;
			}

			const auto runtimeUpdated = UpdateRuntimeSlotForRemotePlayer(
				*slot,
				remotePlayer,
				*parentCell,
				*player,
				sameCellPassed);
			if (runtimeUpdated && g_representedPlayerId == remotePlayer.playerId) {
				if (auto* fallbackProxy = ResolveProxy(*parentCell, *player)) {
					MoveProxyToHoldingPosition(
						*fallbackProxy,
						ProxyLifecycleState::kHeldNoRemote,
						g_representedPlayerId,
						ActiveProxyKind::kPlacedFallback);
				}
				g_representedPlayerId.reset();
			}

			if (!runtimeUpdated && slot->spawnFailed && !fallbackCandidate) {
				fallbackCandidate = remotePlayer;
				fallbackCandidateSameCellPassed = sameCellPassed;
				fallbackCandidateSlot = slot;
			}
		}

		HoldDisconnectedSlots(seenRemotePlayerIds, *parentCell, *player);
		MarkReusableSlots(*parentCell, *player);
		CorrectHeldSlotPositions(*parentCell, *player);
		HoldFallbackIfRepresentedPlayerDisconnected(seenRemotePlayerIds, *parentCell, *player);

		if (fallbackCandidate && fallbackCandidateSlot) {
			TryUpdatePlacedFallbackForRemotePlayer(
				*fallbackCandidate,
				*parentCell,
				*player,
				fallbackCandidateSameCellPassed,
				*fallbackCandidateSlot);
		}

		ApplySmoothFrameMovement();

		PublishActiveProxyFormIds();
		RebuildProxyFormIdIndex();
	}

	void UpdateSafetyTest()
	{
		Update();
	}

	void NoteKnownSetChanged()
	{
		for (auto& [_, slot] : g_proxySlots) {
			ResetSlotDisplayNameTracking(slot);
		}
	}
}
