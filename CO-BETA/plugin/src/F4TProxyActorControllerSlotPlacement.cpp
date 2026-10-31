// source:CO-BETA/plugin/src/F4TProxyActorController.cpp moved verbatim (RM-10)
#include "pch.h"

#include "F4TConsoleGuard.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxySaveSafety.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	std::size_t g_skippedNonCreatedSlotSpawnCandidates = 0;

	std::string FormatSlotCandidateSnapshot(const ProxyActorSlot& a_slot)
	{
		if (a_slot.preexistingCandidateRefs.empty()) {
			return "[]";
		}

		std::vector<std::uint32_t> sortedCandidateRefs(
			a_slot.preexistingCandidateRefs.begin(),
			a_slot.preexistingCandidateRefs.end());
		std::ranges::sort(sortedCandidateRefs);

		std::string candidates = "[";
		for (std::size_t index = 0; index < sortedCandidateRefs.size(); ++index) {
			if (index > 0) {
				candidates += ", ";
			}
			candidates += std::format("{:08X}", sortedCandidateRefs[index]);
		}
		candidates += "]";
		return candidates;
	}

	bool TryClaimSlotWindowCandidateLog(std::uint32_t a_formId, bool a_acceptedAsNew)
	{
		if (g_slotWindowCandidateLogLines >= F4T::ProxySaveSafety::kMaximumWindowCandidateLogLines) {
			return false;
		}
		const auto logged = std::ranges::find(g_slotWindowLoggedCandidates, a_formId, &std::pair<std::uint32_t, bool>::first);
		if (logged != g_slotWindowLoggedCandidates.end()) {
			if (logged->second == a_acceptedAsNew) {
				return false;
			}
			logged->second = a_acceptedAsNew;
		} else {
			g_slotWindowLoggedCandidates.emplace_back(a_formId, a_acceptedAsNew);
		}
		++g_slotWindowCandidateLogLines;
		return true;
	}

	RE::NiPoint3 GetRemotePlayerPosition(const RemotePlayer& a_remotePlayer)
	{
		return RE::NiPoint3{
			a_remotePlayer.x,
			a_remotePlayer.y,
			a_remotePlayer.z
		};
	}

	bool IsSlotSpawnCandidateActor(
		const ProxyActorSlot& a_slot,
		const RE::Actor& a_actor,
		const RE::PlayerCharacter& a_player)
	{
		if (std::addressof(a_actor) == std::addressof(a_player)) {
			return false;
		}

		if (const_cast<RE::Actor&>(a_actor).IsDeleted()) {
			return false;
		}

		const auto* baseActor = a_actor.GetNPC();
		if (!baseActor || baseActor->GetFormID() != a_slot.spawnBaseFormId) {
			return false;
		}

		if (!a_actor.IsCreated()) {
			++g_skippedNonCreatedSlotSpawnCandidates;
			if (F4T::ProxySaveSafety::ShouldLogPeriodic(g_skippedNonCreatedSlotSpawnCandidates)) {
				F4T::DebugLogging::Log(
					"ProxySaveSafety",
					"[RT-VERIFY 408-C] slot spawn candidate skipped: actor={:08X} base={:08X} isCreated=false player={} occurrence={}",
					a_actor.GetFormID(),
					baseActor->GetFormID(),
					a_slot.remotePlayerId,
					g_skippedNonCreatedSlotSpawnCandidates);
			}
			return false;
		}

		return true;
	}

	bool IsStaleSavedRuntimeProxyCandidate(
		const ProxyActorSlot& a_slot,
		const RE::TESObjectREFR& a_ref,
		const RE::Actor& a_actor,
		const RE::PlayerCharacter& a_player)
	{
		if (a_slot.spawnSource != RuntimeActorBaseSource::kCustomProxy ||
			!a_actor.IsCreated() ||
			IsRuntimeProxyRef(a_ref) ||
			IsProxyPoolActorAssigned(a_actor) ||
			const_cast<RE::Actor&>(a_actor).IsDeleted()) {
			return false;
		}

		if (IsSlotSpawnCandidateActor(a_slot, a_actor, a_player)) {
			return true;
		}

		return IsCustomPlayerProxyActor(a_actor);
	}

	void SnapshotPreexistingSlotCandidates(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player)
	{
		a_slot.preexistingCandidateRefs.clear();
		const auto playerPosition = a_player.GetPosition();
		a_currentCell.ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (!a_ref) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			auto* actor = a_ref->As<RE::Actor>();
			if (!actor || std::addressof(*actor) == std::addressof(a_player) ||
				const_cast<RE::Actor&>(*actor).IsDeleted()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const auto isSpawnBaseCandidate = IsSlotSpawnCandidateActor(a_slot, *actor, a_player);
			const auto isAnyCustomProxy = IsCustomPlayerProxyActor(*actor);
			if (!isSpawnBaseCandidate && !isAnyCustomProxy) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			if (IsStaleSavedRuntimeProxyCandidate(a_slot, *a_ref, *actor, a_player)) {
				const auto staleFormId = actor->GetFormID();
				const auto position = actor->GetPosition();
				LogInfoWithLocalPlayerPrefix(std::format(
					"Stage 4 deleting stale saved runtime proxy candidate before spawning fresh proxy for remote player {}: actor={:08X}, base={:08X}, position={}, distanceFromLocalPlayer={:.2f}.",
					a_slot.remotePlayerId,
					staleFormId,
					actor->GetNPC() ? actor->GetNPC()->GetFormID() : 0,
					FormatPosition(position),
					GetDistance(position, playerPosition)));
				F4T::ProxySaveSafety::DestroyProxyActor(*actor);
				return RE::BSContainer::ForEachResult::kContinue;
			}

			if (!isSpawnBaseCandidate) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const auto position = actor->GetPosition();
			a_slot.preexistingCandidateRefs.insert(actor->GetFormID());
			LogInfoWithLocalPlayerPrefix(std::format(
				"Stage 4 pre-PlaceAtMe proxy candidate for remote player {}: actor={:08X}, base={:08X}, position={}, distanceFromLocalPlayer={:.2f}, Get3D={}, IsVisible={}.",
				a_slot.remotePlayerId,
				actor->GetFormID(),
				actor->GetNPC() ? actor->GetNPC()->GetFormID() : 0,
				FormatPosition(position),
				GetDistance(position, playerPosition),
				actor->Get3D() != nullptr,
				actor->IsVisible()));
			return RE::BSContainer::ForEachResult::kContinue;
		});

		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 4 pre-PlaceAtMe proxy candidate snapshot for remote player {}: base={:08X}, count={}, candidates={}.",
			a_slot.remotePlayerId,
			a_slot.spawnBaseFormId,
			a_slot.preexistingCandidateRefs.size(),
			FormatSlotCandidateSnapshot(a_slot)));
	}

	void BeginSlotPlaceAtMeSpawnAttempt(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		std::uint32_t a_baseFormId,
		RuntimeActorBaseSource a_source)
	{
		a_slot.spawnBaseFormId = a_baseFormId;
		a_slot.spawnSource = a_source;
		a_slot.spawnAttempted = true;
		a_slot.spawnAttemptInProgress = true;
		a_slot.spawnFailed = false;
		a_slot.spawnState =
			a_source == RuntimeActorBaseSource::kCustomProxy ?
				PlaceAtMeBackedProxySpawnState::kCustomProxyAttempting :
				PlaceAtMeBackedProxySpawnState::kCodsworthFallbackAttempting;
		g_runtimeProxySpawnAttemptPlayerId = a_slot.remotePlayerId;
		g_runtimeProxyActorBaseSource = a_source;

		SnapshotPreexistingSlotCandidates(a_slot, a_currentCell, a_player);
		if (g_slotWindowScans != 0 || g_slotWindowSuppressedLogs != 0 || !g_slotWindowLoggedCandidates.empty()) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window totals of the previous window before re-OPEN newPlayer={} scans={} suppressed={} candidate ids recorded={} thread={}",
				a_slot.remotePlayerId,
				g_slotWindowScans,
				g_slotWindowSuppressedLogs,
				g_slotWindowLoggedCandidates.size(),
				REX::W32::GetCurrentThreadId());
		}
		g_slotWindowScans = 0;
		g_slotWindowSuppressedLogs = 0;
		g_slotWindowLoggedCandidates.clear();
		g_slotWindowCandidateLogLines = 0;
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"[RT-VERIFY 408-C] slot PlaceAtMe window OPEN player={} base={:08X} source={} preexisting={} thread={}",
			a_slot.remotePlayerId,
			a_baseFormId,
			GetPlaceAtMeBackedProxySourceLabel(a_source),
			a_slot.preexistingCandidateRefs.size(),
			REX::W32::GetCurrentThreadId());
		const auto command = std::format("player.placeatme {:08X} 1", a_baseFormId);
		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 4 executing PlaceAtMe-backed proxy spawn for remote player {}: {}.",
			a_slot.remotePlayerId,
			command));
		// TODO-verify(#407): the placeatme ref is visible to the window scan as soon as ExecuteCommand returns source:rt-briefs/thread-408.md (comment 20722)
		F4T::ConsoleGuard::ExecutePluginCommand(command.c_str());
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"runtime-unknown [RT-VERIFY 408-C] slot PlaceAtMe command returned player={} base={:08X} scans={} thread={}",
			a_slot.remotePlayerId,
			a_baseFormId,
			g_slotWindowScans,
			REX::W32::GetCurrentThreadId());
		a_slot.candidateSearchStartTime = std::chrono::steady_clock::now();
	}

	void DiscardSupersededSlotProxyActor(ProxyActorSlot& a_slot, const RE::Actor& a_newActor)
	{
		const auto discardHandle = [&](RE::ActorHandle& a_handle) {
			const auto actorRef = a_handle.get();
			auto* actor = actorRef.get();
			if (!actor || actor->GetFormID() == a_newActor.GetFormID()) {
				return;
			}

			LogWarningWithLocalPlayerPrefix(std::format(
				"Discarding superseded runtime proxy actor {:08X} for remote player {} because {:08X} was promoted.",
				actor->GetFormID(),
				a_slot.remotePlayerId,
				a_newActor.GetFormID()));
			F4T::ProxySaveSafety::DestroyProxyActor(*actor);
		};

		discardHandle(a_slot.proxyHandle);
		auto* pending = a_slot.pendingSpawnHandle.get().get();
		if (pending && pending->GetFormID() != a_newActor.GetFormID()) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Discarding superseded pending runtime proxy actor {:08X} for remote player {} because {:08X} was promoted.",
				pending->GetFormID(),
				a_slot.remotePlayerId,
				a_newActor.GetFormID()));
			F4T::ProxySaveSafety::DestroyProxyActor(*pending);
			a_slot.pendingSpawnHandle.reset();
		}
	}
}

namespace F4T::ProxyActorController::detail
{
	bool HasValidRemoteTransform(const RemotePlayer& a_remotePlayer)
	{
		if (!std::isfinite(a_remotePlayer.x) ||
			!std::isfinite(a_remotePlayer.y) ||
			!std::isfinite(a_remotePlayer.z) ||
			!std::isfinite(a_remotePlayer.angleZ)) {
			LogThrottledWarning(
				"remote_transform_invalid_" + std::to_string(a_remotePlayer.playerId),
				std::format(
					"Remote player {} has invalid transform data; runtime proxy spawn/update is skipped.",
					a_remotePlayer.playerId));
			return false;
		}

		if (!ParseHexFormId(a_remotePlayer.cellId)) {
			LogThrottledWarning(
				"remote_transform_cell_missing_" + std::to_string(a_remotePlayer.playerId),
				std::format(
					"Remote player {} has no valid cellId; runtime proxy spawn/update is skipped.",
					a_remotePlayer.playerId));
			return false;
		}

		return true;
	}

	std::vector<RemotePlayer> GetSortedRemotePlayerSnapshot()
	{
		auto remotePlayers = F4T::RemotePlayerState::GetRemotePlayerSnapshot();
		std::ranges::sort(
			remotePlayers,
			[](const RemotePlayer& a_left, const RemotePlayer& a_right) {
				return a_left.playerId < a_right.playerId;
			});
		return remotePlayers;
	}

	RE::NiPoint3 GetRuntimeProxyVisibleTargetPosition(
		ProxyActorSlot& a_slot,
		const RemotePlayer& a_remotePlayer,
		const RE::PlayerCharacter& a_player)
	{
		return GetRemotePlayerPosition(a_remotePlayer);
	}

	void SetSlotInitialRemoteSpawnTransform(ProxyActorSlot& a_slot, const RemotePlayer& a_remotePlayer)
	{
		a_slot.initialRemoteSpawnPosition = GetRemotePlayerPosition(a_remotePlayer);
	}

	void CaptureSlotInitialRemoteSpawnTransform(ProxyActorSlot& a_slot, const RemotePlayer& a_remotePlayer)
	{
		if (a_slot.proxyHandle || a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kPromoted) {
			return;
		}

		SetSlotInitialRemoteSpawnTransform(a_slot, a_remotePlayer);
	}

	bool TryBeginSlotCodsworthFallback(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player)
	{
		if (!ShouldFallbackToCodsworthForPlaceAtMeBackedSpawn()) {
			return false;
		}

		auto* codsworthBase = ResolveCodsworthBaseByAbsoluteFormId();
		if (!codsworthBase) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Stage 4 Codsworth fallback unavailable for remote player {}: diagnostic base 000179FF did not resolve as TESNPC.",
				a_slot.remotePlayerId));
			return false;
		}

		if (a_slot.spawnState == PlaceAtMeBackedProxySpawnState::kCustomProxyAttempting) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window custom attempt abandoned for Codsworth fallback player={} base={:08X} thread={}",
				a_slot.remotePlayerId,
				codsworthBase->GetFormID(),
				REX::W32::GetCurrentThreadId());
			a_slot.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;
		}
		BeginSlotPlaceAtMeSpawnAttempt(
			a_slot,
			a_currentCell,
			a_player,
			codsworthBase->GetFormID(),
			RuntimeActorBaseSource::kVanillaDiagnosticNpc);
		return true;
	}

	bool TryBeginSlotPlaceAtMeSpawn(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		const RemotePlayer& a_remotePlayer)
	{
		if (g_runtimeProxySpawnAttemptPlayerId && *g_runtimeProxySpawnAttemptPlayerId != a_slot.remotePlayerId) {
			LogThrottledInfo(
				"stage4_spawn_waiting_" + std::to_string(a_slot.remotePlayerId),
				std::format(
					"Runtime proxy spawn for remote player {} is waiting; PlaceAtMe spawn is already in progress for remote player {}.",
					a_slot.remotePlayerId,
					*g_runtimeProxySpawnAttemptPlayerId),
				kRuntimeProxyVisibilityDiagnosticLogInterval);
			return false;
		}

		if (ShouldUseCustomProxyBaseForPlaceAtMeBackedSpawn()) {
			const auto wantFemale = GetDesiredProxyIsFemale(a_remotePlayer).value_or(false);
			if (auto* customProxyBase = ResolveProxyActorBaseForSex(wantFemale)) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Stage 4 PlaceAtMe-backed proxy spawn base resolved for remote player {}: custom {}, formId={:08X}, isFemale={}.",
					a_slot.remotePlayerId,
					wantFemale ? kFemaleProxyBaseActorEditorId : kProxyBaseActorEditorId,
					customProxyBase->GetFormID(),
					wantFemale));
				BeginSlotPlaceAtMeSpawnAttempt(
					a_slot,
					a_currentCell,
					a_player,
					customProxyBase->GetFormID(),
					RuntimeActorBaseSource::kCustomProxy);
				return true;
			}
		}

		if (ShouldFallbackToCodsworthForPlaceAtMeBackedSpawn()) {
			if (ShouldUseCustomProxyBaseForPlaceAtMeBackedSpawn()) {
				LogWarningWithLocalPlayerPrefix(std::format(
					"Stage 4 custom proxy base unavailable for remote player {}; falling back to Codsworth diagnostic base 000179FF.",
					a_slot.remotePlayerId));
			} else {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Stage 4 PlaceAtMe-backed proxy spawn base for remote player {}: vanilla Codsworth diagnostic NPC 000179FF.",
					a_slot.remotePlayerId));
			}
			return TryBeginSlotCodsworthFallback(a_slot, a_currentCell, a_player);
		}

		LogWarningWithLocalPlayerPrefix(std::format(
			"Stage 4 PlaceAtMe-backed proxy spawn failed for remote player {}: custom proxy base unavailable and Codsworth fallback is disabled.",
			a_slot.remotePlayerId));
		return false;
	}

	RE::Actor* TryIsolateSlotPlaceAtMeCandidate(
		ProxyActorSlot& a_slot,
		RE::TESObjectCELL& a_currentCell,
		const RE::PlayerCharacter& a_player,
		std::size_t& a_newCandidateCount)
	{
		++g_slotWindowScans;
		RE::Actor* nearestNewCandidate = nullptr;
		auto nearestNewDistance = std::numeric_limits<float>::max();
		const auto playerPosition = a_player.GetPosition();

		a_currentCell.ForEachReference([&](RE::TESObjectREFR* a_ref) {
			if (!a_ref) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			auto* actor = a_ref->As<RE::Actor>();
			if (!actor || !IsSlotSpawnCandidateActor(a_slot, *actor, a_player)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const auto position = actor->GetPosition();
			const auto distanceFromLocalPlayer = GetDistance(position, playerPosition);
			const auto inPreSpawnSnapshot = a_slot.preexistingCandidateRefs.find(actor->GetFormID()) != a_slot.preexistingCandidateRefs.end();
			const auto acceptedAsNew =
				!inPreSpawnSnapshot &&
				distanceFromLocalPlayer <= kDebugPlaceAtMeSpawnDetectionRadius;

			if (!TryClaimSlotWindowCandidateLog(actor->GetFormID(), acceptedAsNew)) {
				++g_slotWindowSuppressedLogs;
			} else {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Stage 4 PlaceAtMe-backed proxy candidate for remote player {} considered: actor={:08X}, base={:08X}, inPreSpawnSnapshot={}, acceptedAsNew={}, position={}, distanceFromLocalPlayer={:.2f}, Get3D={}, IsVisible={}.",
					a_slot.remotePlayerId,
					actor->GetFormID(),
					actor->GetNPC() ? actor->GetNPC()->GetFormID() : 0,
					inPreSpawnSnapshot,
					acceptedAsNew,
					FormatPosition(position),
					distanceFromLocalPlayer,
					actor->Get3D() != nullptr,
					actor->IsVisible()));
				F4T::DebugLogging::Log(
					"ProxySaveSafety",
					"[RT-VERIFY 408-C] slot PlaceAtMe window candidate player={} actor={:08X} inPreSpawnSnapshot={} acceptedAsNew={} scan={}",
					a_slot.remotePlayerId,
					actor->GetFormID(),
					inPreSpawnSnapshot,
					acceptedAsNew,
					g_slotWindowScans);
			}
			if (acceptedAsNew) {
				++a_newCandidateCount;
			}
			if (acceptedAsNew && distanceFromLocalPlayer < nearestNewDistance) {
				nearestNewDistance = distanceFromLocalPlayer;
				nearestNewCandidate = actor;
			}

			return RE::BSContainer::ForEachResult::kContinue;
		});

		if (F4T::ProxySaveSafety::ShouldLogPeriodic(g_slotWindowScans)) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window scan={} player={} newCandidates={} nearest={:08X} thread={}",
				g_slotWindowScans,
				a_slot.remotePlayerId,
				a_newCandidateCount,
				nearestNewCandidate ? nearestNewCandidate->GetFormID() : 0u,
				REX::W32::GetCurrentThreadId());
		}
		return nearestNewCandidate;
	}

	void MarkSlotSpawnFailed(ProxyActorSlot& a_slot)
	{
		a_slot.spawnState = PlaceAtMeBackedProxySpawnState::kFailed;
		a_slot.spawnAttemptInProgress = false;
		a_slot.spawnFailed = true;
		a_slot.lifecycleState = ProxyLifecycleState::kSpawnFailed;
		a_slot.pendingSpawnHandle.reset();
		a_slot.cachedProxyActor = nullptr;
		a_slot.cachedProxyFormId.reset();
		if (g_runtimeProxySpawnAttemptPlayerId == a_slot.remotePlayerId) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=spawn-failed thread={}",
				a_slot.remotePlayerId,
				REX::W32::GetCurrentThreadId());
			g_runtimeProxySpawnAttemptPlayerId.reset();
		}
	}

	void StartSlotPlaceAtMeSettle(ProxyActorSlot& a_slot, RE::Actor& a_actor, std::size_t a_newCandidateCount)
	{
		// TODO-verify(#407): lone foreign 0x179FF candidate source:rt-briefs/thread-408.md (comment 20722)
		if (F4T::ProxySaveSafety::IsUniqueStandInCandidate(a_newCandidateCount)) {
			F4T::ProxySaveSafety::RecordStandIn(a_actor.GetFormID());
		} else {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Stage 4 PlaceAtMe-backed proxy actor={:08X} for remote player {} was not recorded as a CO stand-in: {} new base candidates appeared in the window.",
				a_actor.GetFormID(),
				a_slot.remotePlayerId,
				a_newCandidateCount));
		}
		F4T::DebugLogging::Log(
			"ProxySaveSafety",
			"runtime-unknown [RT-VERIFY 408-C] slot PlaceAtMe window CLOSE settle player={} actor={:08X} base={:08X} newCandidates={} scans={} suppressed={} candidate ids recorded={} recordedAsStandIn={} thread={}",
			a_slot.remotePlayerId,
			a_actor.GetFormID(),
			a_slot.spawnBaseFormId,
			a_newCandidateCount,
			g_slotWindowScans,
			g_slotWindowSuppressedLogs,
			g_slotWindowLoggedCandidates.size(),
			F4T::ProxySaveSafety::IsUniqueStandInCandidate(a_newCandidateCount),
			REX::W32::GetCurrentThreadId());
		a_slot.cachedProxyFormId = a_actor.GetFormID();
		a_slot.cachedProxyActor = std::addressof(a_actor);
		a_slot.pendingSpawnHandle = GetProxyActorHandle(a_actor);
		a_slot.unresolvedHandleSinceTime = {};
		a_slot.settleStartTime = std::chrono::steady_clock::now();
		a_slot.spawnState = PlaceAtMeBackedProxySpawnState::kSettling;
		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 4 new PlaceAtMe-backed proxy isolated for remote player {}: actor={:08X}, base={:08X}, source={}. Waiting {:.1f} seconds before remote assignment.",
			a_slot.remotePlayerId,
			a_actor.GetFormID(),
			a_slot.spawnBaseFormId,
			GetPlaceAtMeBackedProxySourceLabel(a_slot.spawnSource),
			kPlaceAtMeProxySettleSeconds));
	}

	void PromoteSlotPlaceAtMeProxy(ProxyActorSlot& a_slot, RE::Actor& a_actor)
	{
		const auto has3D = a_actor.Get3D() != nullptr;
		const auto isVisible = a_actor.IsVisible();
		if (!Proxy3DReadyForPresentation(a_actor)) {
			LogWarningWithLocalPlayerPrefix(std::format(
				"Stage 4 runtime proxy for remote player {} promoted with partial visibility: Get3D={}, GetFullyLoaded3D={}, IsVisible={}. Continuing to remote movement.",
				a_slot.remotePlayerId,
				has3D,
				a_actor.GetFullyLoaded3D() != nullptr,
				isVisible));
		}

		for (const auto& remotePlayer : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
			if (remotePlayer.playerId == a_slot.remotePlayerId && HasValidRemoteTransform(remotePlayer)) {
				SetSlotInitialRemoteSpawnTransform(a_slot, remotePlayer);
				break;
			}
		}

		DiscardSupersededSlotProxyActor(a_slot, a_actor);
		F4T::DebugLogging::Log("Equipment", "ledger forget requested for remote player {}", a_slot.remotePlayerId);
		ResetSlotEquipmentTracking(a_slot, true);
		ResetSlotAppearanceTracking(a_slot);
		ResetSlotDisplayNameTracking(a_slot);
		a_slot.proxyHandle = GetProxyActorHandle(a_actor);
		a_slot.cachedProxyFormId = a_actor.GetFormID();
		a_slot.cachedProxyActor = std::addressof(a_actor);
		a_slot.pendingSpawnHandle.reset();
		a_slot.spawnSucceeded = true;
		a_slot.spawnAttemptInProgress = false;
		a_slot.spawnFailed = false;
		a_slot.spawnState = PlaceAtMeBackedProxySpawnState::kPromoted;
		a_slot.unresolvedHandleSinceTime = {};
		a_slot.lifecycleState = ProxyLifecycleState::kHeldNoRemote;
		a_slot.holdReason = ProxyLifecycleState::kHeldNoRemote;
		a_slot.heldSinceTime = {};
		a_slot.reusableLogged = false;
		a_slot.restoredForCurrentState = false;
		a_slot.visibilityRefreshPending = true;
		a_slot.visibilityRefreshLogged = false;
		if (g_runtimeProxySpawnAttemptPlayerId == a_slot.remotePlayerId) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=spawn-promoted thread={}",
				a_slot.remotePlayerId,
				REX::W32::GetCurrentThreadId());
			g_runtimeProxySpawnAttemptPlayerId.reset();
		}

		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			NeutralizeProxyActor(a_slot, a_actor, *player, ProxyNeutralizationReason::kSpawnPromoted);
			if (ProxyHeadNodesMissing(a_actor)) {
				EnsureProxyHeadMesh(a_slot, a_actor);
			}
			EnsureRuntimeProxyVisibility(a_slot, a_actor, "placeatme-promoted");
		}
		LogInfoWithLocalPlayerPrefix(std::format(
			"Stage 4 runtime proxy spawned for remote player {}: actor={:08X}, handle={:08X}, source={}.",
			a_slot.remotePlayerId,
			a_actor.GetFormID(),
			a_slot.proxyHandle.get_handle(),
			GetPlaceAtMeBackedProxySourceLabel(a_slot.spawnSource)));
	}
}
