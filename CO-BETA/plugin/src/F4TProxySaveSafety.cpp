#include "pch.h"

#include <atomic>

#include "F4TProxySaveSafety.h"

#include "F4TAnimationGraphOverride.h"
#include "F4TNetworking.h"
#include "F4TNpcSyncInternal.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxyPuppet.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	constexpr auto kProxyTickGateLogInterval = 2s;
	constexpr std::size_t kMaxSweepCandidates = 512;
	constexpr std::size_t kMaxSweepPasses = 8;
	constexpr std::size_t kMaxUnresolvedProxyHandles = 64;
	constexpr auto kProxySaveSafetyComponent = "ProxySaveSafety";

	std::uint64_t g_lastObservedGeneration = F4T::ReplicationRuntime::kNoSession;
	bool g_saveLoadThreadWasBusy = false;
	bool g_sessionGenerationBumped = false;
	std::atomic<std::uint32_t> g_gameThreadId{};
	std::atomic<bool> g_pendingSaveTeardown{};
	std::atomic<bool> g_gameThreadCorrectionLogged{};
	std::atomic<bool> g_teardownQueueLogged{};
	std::vector<RE::ObjectRefHandle> g_unresolvedProxyHandles;

	void NoteGameThread() noexcept
	{
		const auto currentThreadId = REX::W32::GetCurrentThreadId();
		auto expected = std::uint32_t{ 0 };
		g_gameThreadId.compare_exchange_strong(expected, currentThreadId, std::memory_order_acq_rel);
	}

	void RecordGameThreadFromTick()
	{
		const auto currentThreadId = REX::W32::GetCurrentThreadId();
		const auto recorded = g_gameThreadId.load(std::memory_order_acquire);
		if (recorded == currentThreadId) {
			return;
		}

		g_gameThreadId.store(currentThreadId, std::memory_order_release);
		if (recorded != 0 && !g_gameThreadCorrectionLogged.exchange(true, std::memory_order_acq_rel)) {
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"game thread id corrected: recorded {} is not the tick thread {}; the queued save teardown now runs inline",
				recorded,
				currentThreadId);
		}
	}

	// TODO-verify(#407): deleted stand-in stays out of the save source:E:/GardenEngine/re-answers/2026-10-22-static-residue-407-408-385.md (GARDEN/CommonwealthOnline-Matrix issue #408 comment 21113, partial)
	void LogDisableAndDeleteRefState(const char* a_stage, RE::TESObjectREFR& a_ref)
	{
		// TODO-verify(#407): flag bit 10 decides whether a created deleted ref is saved source:E:/GardenEngine/re-answers/2026-10-22-static-residue-407-408-385.md
		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"PARTIAL [RT-VERIFY 408-D] DisableAndDeleteRef {} actor={:08X} isCreated={} isDeleted={} formFlags={:08X} thread={}",
			a_stage,
			a_ref.GetFormID(),
			a_ref.IsCreated(),
			a_ref.IsDeleted(),
			a_ref.GetFormFlags(),
			REX::W32::GetCurrentThreadId());
	}

	void DisableAndDeleteRef(RE::TESObjectREFR& a_ref)
	{
		const auto* actor = a_ref.As<RE::Actor>();
		const auto* npc = actor ? actor->GetNPC() : nullptr;
		const auto isCodsworthBase = npc && npc->GetFormID() == kDebugCodsworthBaseFormId;
		if (F4T::ProxySaveSafety::IsProtectedPlacedCodsworth(a_ref.IsCreated(), isCodsworthBase)) {
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"WARN [RT-VERIFY 408-D] DisableAndDeleteRef refused actor={:08X} isCreated=false base={:08X}: a placed Codsworth base is never deleted",
				a_ref.GetFormID(),
				kDebugCodsworthBaseFormId);
			return;
		}
		if (a_ref.IsDeleted()) {
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"[RT-VERIFY 408-D] DisableAndDeleteRef early return actor={:08X} isDeleted=true, Disable and MarkAsDeleted skipped",
				a_ref.GetFormID());
			F4T::ProxySaveSafety::ForgetStandIn(a_ref.GetFormID());
			return;
		}
		LogDisableAndDeleteRefState("before", a_ref);
		a_ref.Disable();
		a_ref.MarkAsDeleted();
		LogDisableAndDeleteRefState("after", a_ref);
		if (a_ref.IsDeleted()) {
			F4T::ProxySaveSafety::ForgetStandIn(a_ref.GetFormID());
			return;
		}
		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"WARN [RT-VERIFY 408-D] DisableAndDeleteRef left actor={:08X} not deleted after MarkAsDeleted; the id stays recorded as a stand-in",
			a_ref.GetFormID());
	}

	bool IsLoadingMenuOpen()
	{
		const auto* ui = RE::UI::GetSingleton();
		return ui && ui->GetMenuOpen<RE::LoadingMenu>();
	}

	bool IsPlacedFallbackProxyActor(const RE::Actor& a_actor) noexcept
	{
		return F4T::ProxySaveSafety::IsPlacedFallbackProxy(IsCkPlacedProxyReference(a_actor, a_actor, std::nullopt), a_actor.IsCreated());
	}

	bool IsCreatedFallbackBaseProxyActor(const RE::Actor& a_actor, bool a_standInsAllowed)
	{
		const auto* baseActor = a_actor.GetNPC();
		return F4T::ProxySaveSafety::IsCreatedFallbackBaseProxy(
			a_actor.IsCreated(),
			baseActor && baseActor->GetFormID() == kDebugCodsworthBaseFormId,
			F4T::NpcSync::Detail::IsPuppetActor(a_actor.GetFormID()) ||
				F4T::NpcSync::Detail::IsRecordedCreatedPuppet(a_actor.GetFormID()),
			a_standInsAllowed && F4T::ProxySaveSafety::IsStandIn(a_actor.GetFormID()));
	}

	void HoldPlacedFallbackProxy(RE::Actor& a_actor)
	{
		MoveProxyToHoldingPosition(
			a_actor,
			ProxyLifecycleState::kHeldNoRemote,
			std::nullopt,
			ActiveProxyKind::kPlacedFallback);
	}

	void RetireProxyActorForSave(RE::TESObjectREFR& a_ref, std::size_t& a_removed)
	{
		if (a_ref.IsDeleted()) {
			return;
		}

		auto* actor = a_ref.As<RE::Actor>();
		if (actor) {
			F4T::AnimationGraphOverride::ClearProxy(actor);
		}
		switch (F4T::ProxySaveSafety::DecideProxyRetire(
			actor && IsPlacedFallbackProxyActor(*actor),
			actor && IsCustomPlayerProxyActor(*actor),
			IsRuntimeProxyRef(a_ref))) {
		case F4T::ProxySaveSafety::ProxyRetireAction::kHoldPlacedFallback:
			HoldPlacedFallbackProxy(*actor);
			break;
		case F4T::ProxySaveSafety::ProxyRetireAction::kSkip:
			break;
		case F4T::ProxySaveSafety::ProxyRetireAction::kDelete:
			DisableAndDeleteRef(a_ref);
			++a_removed;
			break;
		}
	}

	void RememberUnresolvedProxyHandle(const RE::ObjectRefHandle& a_handle)
	{
		if (std::ranges::contains(g_unresolvedProxyHandles, a_handle)) {
			return;
		}
		if (g_unresolvedProxyHandles.size() >= kMaxUnresolvedProxyHandles) {
			g_unresolvedProxyHandles.erase(g_unresolvedProxyHandles.begin());
		}
		g_unresolvedProxyHandles.push_back(a_handle);
	}

	template <class THandle>
	void RetireProxyHandleForSave(THandle& a_handle, std::size_t& a_removed)
	{
		if (const auto ref = a_handle.get()) {
			RetireProxyActorForSave(*ref, a_removed);
		} else if (a_handle) {
			RememberUnresolvedProxyHandle(RE::ObjectRefHandle{ a_handle });
		}
		a_handle.reset();
	}

	bool IsTrackedByLiveSlot(const RE::Actor& a_actor)
	{
		const auto actorId = a_actor.GetFormID();
		for (const auto& [_, slot] : g_proxySlots) {
			if (slot.cachedProxyFormId && *slot.cachedProxyFormId == actorId) {
				return true;
			}
			if (const auto proxy = slot.proxyHandle.get()) {
				if (proxy->GetFormID() == actorId) {
					return true;
				}
			}
			if (const auto pending = slot.pendingSpawnHandle.get()) {
				if (pending->GetFormID() == actorId) {
					return true;
				}
			}
		}
		return false;
	}

	RE::NiPointer<RE::Actor> ToActorPointer(RE::TESObjectREFR* a_ref)
	{
		auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
		return actor ? RE::NiPointer<RE::Actor>{ actor } : RE::NiPointer<RE::Actor>{};
	}

	bool AppendSweepCandidate(
		std::vector<RE::NiPointer<RE::Actor>>& a_candidates,
		std::vector<std::uint32_t>& a_seenIds,
		const RE::NiPointer<RE::Actor>& a_actor)
	{
		if (!a_actor) {
			return false;
		}

		if (a_candidates.size() >= kMaxSweepCandidates) {
			return true;
		}

		const auto actorId = a_actor->GetFormID();
		if (actorId == 0 || std::ranges::contains(a_seenIds, actorId)) {
			return false;
		}

		a_seenIds.push_back(actorId);
		a_candidates.push_back(a_actor);
		return false;
	}

	bool CollectPlayerCellCandidates(
		std::vector<RE::NiPointer<RE::Actor>>& a_candidates,
		std::vector<std::uint32_t>& a_seenIds)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			return false;
		}

		bool capped = false;
		cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			capped = AppendSweepCandidate(a_candidates, a_seenIds, ToActorPointer(a_ref)) || capped;
			return RE::BSContainer::ForEachResult::kContinue;
		});
		return capped;
	}

	bool CollectProcessListCandidates(
		std::vector<RE::NiPointer<RE::Actor>>& a_candidates,
		std::vector<std::uint32_t>& a_seenIds)
	{
		const auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			return false;
		}

		const RE::BSTArray<RE::ActorHandle>* handleArrays[] = {
			std::addressof(processLists->highActorHandles),
			std::addressof(processLists->middleHighActorHandles),
			std::addressof(processLists->middleLowActorHandles),
			std::addressof(processLists->lowActorHandles)
		};

		bool capped = false;
		for (const auto* handles : handleArrays) {
			for (const auto& handle : *handles) {
				capped = AppendSweepCandidate(a_candidates, a_seenIds, handle.get()) || capped;
			}
		}
		return capped;
	}

	bool CollectSweepCandidates(
		std::vector<RE::NiPointer<RE::Actor>>& a_candidates,
		std::vector<std::uint32_t>& a_seenIds)
	{
		bool capped = false;
		F4T::ProxySaveSafety::VisitSweepSourcesInPriorityOrder([&](F4T::ProxySaveSafety::SweepSource a_source) {
			switch (a_source) {
			case F4T::ProxySaveSafety::SweepSource::kPlayerCell:
				capped = CollectPlayerCellCandidates(a_candidates, a_seenIds) || capped;
				break;
			case F4T::ProxySaveSafety::SweepSource::kProcessLists:
				capped = CollectProcessListCandidates(a_candidates, a_seenIds) || capped;
				break;
			}
		});
		return capped;
	}

	void CollectUnresolvedProxyCandidates(std::vector<RE::NiPointer<RE::Actor>>& a_candidates)
	{
		std::erase_if(g_unresolvedProxyHandles, [&a_candidates](const RE::ObjectRefHandle& a_handle) {
			const auto ref = a_handle.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				return false;
			}
			a_candidates.push_back(RE::NiPointer<RE::Actor>{ actor });
			return true;
		});
	}

	std::size_t SweepCandidatePage(const std::vector<RE::NiPointer<RE::Actor>>& a_candidates, bool a_standInsAllowed)
	{
		std::size_t deleted = 0;
		std::size_t logged = 0;
		for (const auto& actor : a_candidates) {
			if (!actor) {
				continue;
			}
			const auto isCustomProxy = IsCustomPlayerProxyActor(*actor);
			const auto isDeleted = actor->IsDeleted();
			const auto isOwnedByLiveSlot = IsTrackedByLiveSlot(*actor);
			const auto isPlacedFallback = IsPlacedFallbackProxyActor(*actor);
			const auto isCreatedFallbackBase = IsCreatedFallbackBaseProxyActor(*actor, a_standInsAllowed);
			const auto isRecorded = F4T::ProxySaveSafety::IsStandIn(actor->GetFormID());
			const auto decision = F4T::ProxySaveSafety::DecideSavedProxySweep(
				isCustomProxy,
				isDeleted,
				isOwnedByLiveSlot,
				isPlacedFallback,
				isCreatedFallbackBase);
			if (decision == F4T::ProxySaveSafety::SavedProxySweep::kDelete || isRecorded) {
				++logged;
				F4T::DebugLogging::Log(
					kProxySaveSafetyComponent,
					"sweep candidate actor={:08X} base={:08X} cell={} customProxy={} deleted={} liveSlot={} placedFallback={} createdFallbackBase={} recorded={} standInsAllowed={} -> {}",
					actor->GetFormID(),
					actor->GetNPC() ? actor->GetNPC()->GetFormID() : 0,
					actor->GetParentCell() ? std::format("{:08X}", actor->GetParentCell()->GetFormID()) : std::string{ "<none>" },
					isCustomProxy,
					isDeleted,
					isOwnedByLiveSlot,
					isPlacedFallback,
					isCreatedFallbackBase,
					isRecorded,
					a_standInsAllowed,
					decision == F4T::ProxySaveSafety::SavedProxySweep::kDelete ? "delete" : "keep");
			}
			if (decision != F4T::ProxySaveSafety::SavedProxySweep::kDelete) {
				continue;
			}

			DisableAndDeleteRef(*actor);
			++deleted;
		}
		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"sweep page finished: candidates={} logged={} deleted={} standInsAllowed={}",
			a_candidates.size(),
			logged,
			deleted,
			a_standInsAllowed);
		return deleted;
	}

	void SweepSavedProxyActors(bool a_standInsAllowed)
	{
		std::vector<std::uint32_t> seenIds;
		seenIds.reserve(kMaxSweepCandidates);

		std::size_t candidates = 0;
		std::size_t deleted = 0;
		std::size_t passes = 0;
		bool capped = false;

		for (;;) {
			std::vector<RE::NiPointer<RE::Actor>> page;
			page.reserve(kMaxSweepCandidates);
			if (passes == 0) {
				CollectUnresolvedProxyCandidates(page);
			}
			capped = CollectSweepCandidates(page, seenIds);
			candidates += page.size();
			deleted += SweepCandidatePage(page, a_standInsAllowed);
			++passes;

			if (!F4T::ProxySaveSafety::ShouldRunAnotherSweepPass(page.size(), capped) || passes >= kMaxSweepPasses) {
				break;
			}
		}

		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"saved proxy sweep finished: candidates={}, deleted={}, liveSlots={}, passes={}, capped={}, unresolvedHandles={}",
			candidates,
			deleted,
			g_proxySlots.size(),
			passes,
			capped,
			g_unresolvedProxyHandles.size());
	}

	std::size_t RemoveProxyActorsForSave()
	{
		std::size_t removed = 0;

		for (auto& [playerId, slot] : g_proxySlots) {
			const auto liveRef = slot.proxyHandle.get();
			auto* liveProxy = liveRef ? liveRef->As<RE::Actor>() : nullptr;
			const auto pendingRef = slot.pendingSpawnHandle.get();
			auto* pendingProxy = pendingRef ? pendingRef->As<RE::Actor>() : nullptr;

			switch (F4T::ProxySaveSafety::DecideSlotSaveCleanup(liveProxy != nullptr, pendingProxy != nullptr)) {
			case F4T::ProxySaveSafety::SlotSaveCleanup::kDropLiveProxy:
				if (pendingProxy && pendingProxy != liveProxy) {
					RetireProxyActorForSave(*pendingProxy, removed);
				}
				RetireProxyActorForSave(*liveProxy, removed);
				break;
			case F4T::ProxySaveSafety::SlotSaveCleanup::kDropPendingSpawn:
				RetireProxyActorForSave(*pendingProxy, removed);
				break;
			case F4T::ProxySaveSafety::SlotSaveCleanup::kNone:
				break;
			}

			if (slot.proxyHandle && !slot.proxyHandle.get()) {
				RememberUnresolvedProxyHandle(RE::ObjectRefHandle{ slot.proxyHandle });
			}
			if (slot.pendingSpawnHandle && !slot.pendingSpawnHandle.get()) {
				RememberUnresolvedProxyHandle(RE::ObjectRefHandle{ slot.pendingSpawnHandle });
			}

			if (g_runtimeProxySpawnAttemptPlayerId == playerId) {
				F4T::DebugLogging::Log(
					"ProxySaveSafety",
					"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=save-teardown scans={} suppressed={} candidate ids recorded={} thread={}",
					playerId,
					g_slotWindowScans,
					g_slotWindowSuppressedLogs,
					g_slotWindowLoggedCandidates.size(),
					REX::W32::GetCurrentThreadId());
				g_runtimeProxySpawnAttemptPlayerId.reset();
			}
			ResetSlotForRespawn(slot);
		}

		RetireProxyHandleForSave(g_runtimeProxyHandle, removed);
		RetireProxyHandleForSave(g_proxyHandle, removed);

		g_remoteStateMovedLogged = false;
		g_firstSmoothedMovementLogged = false;
		g_runtimeProxyActorBaseSource = RuntimeActorBaseSource::kCustomProxy;
		return removed;
	}

	void ClearProxySessionState()
	{
		RemoveProxyActorsForSave();
		g_proxySlots.clear();
		g_representedPlayerId.reset();
		g_proxyLifecycleState = ProxyLifecycleState::kHeldNoRemote;
		g_proxyHoldReason.reset();
		g_proxyHoldPlayerId.reset();
		g_proxyHoldKind.reset();
		if (g_runtimeProxySpawnAttemptPlayerId) {
			F4T::DebugLogging::Log(
				"ProxySaveSafety",
				"[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE reset player={} reason=session-clear scans={} suppressed={} candidate ids recorded={} thread={}",
				*g_runtimeProxySpawnAttemptPlayerId,
				g_slotWindowScans,
				g_slotWindowSuppressedLogs,
				g_slotWindowLoggedCandidates.size(),
				REX::W32::GetCurrentThreadId());
			g_runtimeProxySpawnAttemptPlayerId.reset();
		}
		F4T::ProxyPuppet::SetActiveProxyFormIDs({});
	}

	void RunSaveTeardown()
	{
		auto removed = RemoveProxyActorsForSave();
		F4T::ProxyPuppet::SetActiveProxyFormIDs({});

		std::vector<RE::NiPointer<RE::Actor>> unresolved;
		CollectUnresolvedProxyCandidates(unresolved);
		removed += SweepCandidatePage(unresolved, false);

		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"removed {} proxy actor(s) before the save was written; slots keep their remote player and respawn after the save",
			removed);
	}
}

namespace F4T::ProxySaveSafety
{
	void NoteGameThreadId() noexcept
	{
		NoteGameThread();
	}

	void DestroyProxyActor(RE::TESObjectREFR& a_ref)
	{
		DisableAndDeleteRef(a_ref);
	}

	bool IsProxyTickBlocked()
	{
		const auto* ui = RE::UI::GetSingleton();
		const auto* saveLoad = RE::BGSSaveLoadManager::GetSingleton();

		const TickGateInputs inputs{
			IsLoadingMenuOpen(),
			ui && ui->GetMenuOpen<RE::MainMenu>(),
			saveLoad && (saveLoad->saveLoadTasksThread.busy || saveLoad->queuedTasks != 0)
		};

		const auto gate = EvaluateTickGate(inputs);
		if (gate == TickGate::kRun) {
			return false;
		}

		LogThrottledInfo(
			"proxy_tick_gated",
			std::format(
				"Proxy actor tick skipped: {}. Slot state is preserved and the tick resumes once the game is back in gameplay.",
				GetTickGateLabel(gate)),
			kProxyTickGateLogInterval);
		return true;
	}

	void Tick()
	{
		RecordGameThreadFromTick();

		if (g_pendingSaveTeardown.exchange(false, std::memory_order_acq_rel)) {
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"running the queued proxy teardown on the game thread before any further work");
			RunSaveTeardown();
		}

		const auto generation = F4T::ReplicationRuntime::CurrentGeneration();
		if (generation != g_lastObservedGeneration && !IsLoadingMenuOpen()) {
			const bool sessionStarted = generation != F4T::ReplicationRuntime::kNoSession;
			g_lastObservedGeneration = generation;
			g_sessionGenerationBumped = false;
			if (sessionStarted) {
				F4T::DebugLogging::Log(
					kProxySaveSafetyComponent,
					"session generation {} started; clearing proxy session state and sweeping saved proxies",
					generation);
				ClearProxySessionState();
				SweepSavedProxyActors(false);
			}
		}

		const auto* saveLoad = RE::BGSSaveLoadManager::GetSingleton();
		const bool saveLoadThreadBusy = saveLoad && saveLoad->saveLoadTasksThread.busy;
		const bool loadInProgress = saveLoad && saveLoad->queuedEntryToLoad != nullptr;
		if (!g_saveLoadThreadWasBusy && IsSaveStartFallback(saveLoadThreadBusy, IsLoadingMenuOpen(), loadInProgress)) {
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"save-load thread became busy outside a loading menu or queued load; treating it as a save start");
			RunSaveTeardown();
		}
		g_saveLoadThreadWasBusy = saveLoadThreadBusy;
	}

	void PrepareForSave()
	{
		const auto gameThreadId = g_gameThreadId.load(std::memory_order_acquire);
		const auto currentThreadId = REX::W32::GetCurrentThreadId();
		switch (DecideSaveTeardownRoute(currentThreadId, gameThreadId)) {
		case SaveTeardownRoute::kRunInline:
			RunSaveTeardown();
			break;
		case SaveTeardownRoute::kQueueGameThreadTask:
			g_pendingSaveTeardown.store(true, std::memory_order_release);
			if (!g_teardownQueueLogged.exchange(true, std::memory_order_acq_rel)) {
				F4T::DebugLogging::Log(
					kProxySaveSafetyComponent,
					"proxy teardown queued: kPreSaveGame arrived on thread {} while the game thread is {}; the teardown runs on the next game-thread task instead of mutating proxy refs off-thread",
					currentThreadId,
					gameThreadId);
			}
			if (const auto* taskInterface = F4SE::GetTaskInterface()) {
				taskInterface->AddTask([]() {
					if (g_pendingSaveTeardown.exchange(false, std::memory_order_acq_rel)) {
						RunSaveTeardown();
					}
				});
			}
			break;
		}
	}

	void AfterLoadSweep(bool a_loadSucceeded, bool a_clearStandIns)
	{
		g_unresolvedProxyHandles.clear();
		ClearProxySessionState();
		if (a_clearStandIns) {
			ClearStandInFormIds();
		}
		SweepSavedProxyActors(a_loadSucceeded);
		if (a_loadSucceeded) {
			PruneStandInFormIds();
		}

		const bool connected =
			F4T::Networking::IsConnectedToServer() && F4T::RemotePlayerState::GetAssignedPlayerId().has_value();
		if (ShouldBumpSessionGeneration(connected, g_sessionGenerationBumped)) {
			g_sessionGenerationBumped = true;
			F4T::ReplicationRuntime::NoteConnectionEstablished();
			F4T::DebugLogging::Log(
				kProxySaveSafetyComponent,
				"forced a new replication session generation after the load so no pre-load slot data survives it");
		}
		g_lastObservedGeneration = F4T::ReplicationRuntime::CurrentGeneration();

		F4T::DebugLogging::Log(
			kProxySaveSafetyComponent,
			"post-load proxy sweep complete: proxy slots cleared and the session generation reset");
	}

	bool IsSaveTeardownPending()
	{
		return g_pendingSaveTeardown.load(std::memory_order_acquire);
	}
}
