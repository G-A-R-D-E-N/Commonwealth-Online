// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-gaps2/README.md
#include "pch.h"

#include "F4TWorldPolicyEnforcer.h"

#include "F4TDebugLogging.h"
#include "F4TObjectSync.h"
#include "F4TPolicyNoInteract.h"
#include "F4TProxyPuppet.h"
#include "F4TReplicationRuntime.h"
#include "F4TUIClient.h"
#include "F4TVatsPolicy.h"
#include "F4TWorldPolicy.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace F4T::WorldPolicyEnforcer
{
	namespace
	{
		using F4T::WorldPolicy::Policy;
		using F4T::WorldPolicyClient::QuestAction;
		using F4T::WorldPolicyClient::QuestEndKind;
		using F4T::WorldPolicyClient::QuestRestore;
		using F4T::WorldPolicyClient::RefAction;
		using F4T::WorldPolicyClient::RefClass;
		using F4T::WorldPolicyClient::RefState;
		using F4T::WorldPolicyClient::SuppressedQuest;

		constexpr std::size_t kMaximumTrackedDisabledRefs = F4T::WorldPolicy::kMaximumTotalDisableEntries;
		constexpr std::uint64_t kWorldSweepIntervalMs = 30000;
		constexpr std::uint64_t kQuestSweepIntervalMs = 1000;
		constexpr std::uint16_t kQuestFlagsToClear =
			static_cast<std::uint16_t>(RE::QuestFlag::kActive) |
			static_cast<std::uint16_t>(RE::QuestFlag::kDisplayedInHUD);
		// address source:RE-Work/subsystems/commonwealth-online-re/mp-saves-worldpolicy-co309/README.md (start/stop core, QUEST_DATA bit 0; false stops)
		inline constexpr REL::ID kSetQuestRunning{ 1276838, 2207727, 2207727 };
		using SetQuestRunningFn = void (*)(RE::TESQuest*, bool);
		constexpr std::uint16_t kQuestFlagsEnded =
			static_cast<std::uint16_t>(RE::QuestFlag::kCompleted) |
			static_cast<std::uint16_t>(RE::QuestFlag::kFailed);

		std::mutex g_mutex;
		F4T::WorldPolicyClient::InboundPolicyState g_inbound;
		F4T::WorldPolicyClient::PendingCellQueue g_pendingCells;
		std::optional<Policy> g_delivered;

		Policy g_applied;
		bool g_policyInForce{ false };
		std::unordered_set<std::uint32_t> g_disableIds;
		std::unordered_set<std::uint32_t> g_disabledByUs;
		bool g_trackingFullLogged{ false };
		std::unordered_map<std::uint32_t, SuppressedQuest> g_suppressedQuests;
		std::unordered_set<std::uint32_t> g_blockedQuestIds;
		std::unordered_map<std::uint32_t, std::uint16_t> g_stageIssued;
		std::unordered_set<std::uint32_t> g_hideWarned;
		std::atomic_bool g_policyAdopted{ false };
		std::uint64_t g_lastGeneration{ kNoSessionGeneration };
		std::uint64_t g_nextWorldSweepMs{ 0 };
		std::uint64_t g_nextQuestSweepMs{ 0 };
		bool g_cellSinkInstalled{ false };
		bool g_cellSinkUnavailable{ false };
		bool g_sharedDoorsPushed{ false };
		bool g_sharedDoorsValue{ true };
		std::string g_pushedMode;

		struct SweepBatch
		{
			std::vector<std::uint32_t> toApply;
			std::vector<std::uint32_t> toRelease;
		};

		void QueueCellSweep(std::uint32_t a_cellFormId);
		void SweepAttachedCells();
		void SweepQueuedCells();
		void SweepQuests();
		void ReleaseRef(std::uint32_t a_refId);
		void ReleaseDisabledRefs();
		void RestoreQuestFlags();
		void SyncDoorReplication();
		void ApplyAdoptedPolicy(std::uint64_t a_generation);

		class CellLoadSink final : public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESCellFullyLoadedEvent& a_event,
				RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
			{
				if (a_event.cell != nullptr) {
					QueueCellSweep(a_event.cell->GetFormID());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		CellLoadSink g_cellLoadSink;

		void QueueCellSweep(std::uint32_t a_cellFormId)
		{
			std::scoped_lock lock(g_mutex);
			g_pendingCells.Push(a_cellFormId);
		}

		void EnsureCellSink()
		{
			if (g_cellSinkInstalled || g_cellSinkUnavailable) {
				return;
			}

			auto* source = RE::TESCellFullyLoadedEvent::GetEventSource();
			if (source == nullptr) {
				g_cellSinkUnavailable = true;
				F4T::DebugLogging::Log("WorldPolicy", "cell fully loaded event source unavailable; using the periodic world sweep only");
				return;
			}

			source->RegisterSink(&g_cellLoadSink);
			g_cellSinkInstalled = true;
			F4T::DebugLogging::Log("WorldPolicy", "cell fully loaded sink registered");
		}

		void ClearSessionPolicy(std::uint64_t a_generation)
		{
		ReleaseDisabledRefs();
		RestoreQuestFlags();
		F4T::PolicyNoInteract::Clear();
		F4T::VatsPolicy::Restore();

		g_applied = Policy{};
			g_policyInForce = true;
			g_disableIds.clear();
			g_blockedQuestIds.clear();
			g_stageIssued.clear();
			g_suppressedQuests.clear();
			g_hideWarned.clear();
			g_nextWorldSweepMs = 0;
			g_nextQuestSweepMs = 0;
			g_pushedMode.clear();
			g_policyAdopted.store(false);
			g_sharedDoorsPushed = false;
			SyncDoorReplication();

			std::scoped_lock lock(g_mutex);
			g_inbound.OnSessionGeneration(a_generation);
			g_pendingCells.Clear();
			g_delivered.reset();
		}

		void PushModeToUi()
		{
			const std::string mode{ F4T::WorldPolicy::ModeName(g_applied.mode) };
			if (mode == g_pushedMode) {
				return;
			}
			g_pushedMode = mode;
			F4T::UIClient::PushWorldMode(g_pushedMode.c_str());
		}

		void ResolveIds(const Policy& a_policy)
		{
			g_disableIds.clear();

			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (dataHandler == nullptr) {
				F4T::DebugLogging::Log("WorldPolicy", "no data handler; policy form keys cannot be resolved");
				return;
			}

			for (const auto& key : a_policy.disable) {
				const auto resolvedId = dataHandler->LookupFormID(key.localId, key.plugin);
				if (resolvedId == 0) {
					F4T::DebugLogging::Log("WorldPolicy", "unresolved disable key {}", F4T::WorldPolicy::FormatFormKey(key));
					continue;
				}
				g_disableIds.insert(resolvedId);
			}

			g_blockedQuestIds.clear();
			for (const auto& key : a_policy.blockedQuests) {
				const auto resolvedId = dataHandler->LookupFormID(key.localId, key.plugin);
				if (resolvedId == 0) {
					F4T::DebugLogging::Log("WorldPolicy", "unresolved blocked quest key {}", F4T::WorldPolicy::FormatFormKey(key));
					continue;
				}
				g_blockedQuestIds.insert(resolvedId);
			}

		}

		bool AdoptInboundPolicy(std::uint64_t a_generation)
		{
			Policy policy;
			{
				std::scoped_lock lock(g_mutex);
				if (!g_inbound.TakeAdoptable(a_generation, policy)) {
					return false;
				}
			}

			g_applied = policy;
			g_policyInForce = true;
			ResolveIds(policy);
			F4T::PolicyNoInteract::Apply(policy.noInteract);

			F4T::DebugLogging::Log(
				"WorldPolicy",
				"applied mode={} version={} doors={} suppressQuests={} disableKeys={} noInteractKeys={} blockedQuestKeys={}",
				F4T::WorldPolicy::ModeName(policy.mode),
				policy.version,
				F4T::WorldPolicy::DoorsName(policy.doors),
				policy.suppressQuests,
				policy.disable.size(),
				policy.noInteract.size(),
				policy.blockedQuests.size());
			return true;
		}

		void ApplyAdoptedPolicy(std::uint64_t a_generation)
		{
			if (!AdoptInboundPolicy(a_generation)) {
				return;
			}

			g_nextWorldSweepMs = 0;
			g_nextQuestSweepMs = 0;
		g_policyAdopted.store(true);
		SyncDoorReplication();
		F4T::VatsPolicy::ApplyForPolicy(g_applied);
			if (!g_applied.suppressQuests) {
				RestoreQuestFlags();
			}

			SweepAttachedCells();
			SweepQueuedCells();
			if (F4T::WorldPolicyClient::WantsQuestSweep(g_applied)) {
				SweepQuests();
			}
		}

		void SyncDoorReplication()
		{
			const bool shared = g_applied.doors == F4T::WorldPolicy::Doors::Shared;
			if (g_sharedDoorsPushed && shared == g_sharedDoorsValue) {
				return;
			}
			g_sharedDoorsPushed = true;
			g_sharedDoorsValue = shared;
			F4T::ObjectSync::SetSharedDoors(shared);
			F4T::ObjectSync::RefreshTrackedLocks();
		}

		RefClass ClassifyRef(RE::TESObjectREFR& a_ref)
		{
			if (a_ref.GetFormType() != RE::ENUM_FORM_ID::kACHR) {
				return RefClass::World;
			}
			if (a_ref.IsPlayerRef()) {
				return RefClass::Player;
			}
			if (F4T::ProxyPuppet::IsProxyFormID(a_ref.GetFormID())) {
				return RefClass::ProxyActor;
			}

			const auto* actor = a_ref.As<RE::Actor>();
			if (actor != nullptr && actor->vendorFaction != nullptr) {
				return RefClass::VendorActor;
			}
			return RefClass::Actor;
		}

		bool IsTarget(RE::TESObjectREFR& a_ref)
		{
			const auto refId = a_ref.GetFormID();
			const bool refListed = g_disableIds.contains(refId);
			const auto* base = a_ref.GetObjectReference();
			const bool baseListed = base != nullptr && g_disableIds.contains(base->GetFormID());
			if (!refListed && !baseListed) {
				return false;
			}

			return F4T::WorldPolicyClient::IsRemovalTarget(g_applied, ClassifyRef(a_ref), baseListed, refListed);
		}

		RefState ReadRefState(RE::TESObjectREFR& a_ref)
		{
			if (a_ref.IsDeleted()) {
				return RefState::Deleted;
			}
			return a_ref.IsDisabled() ? RefState::DisabledNotDeleted : RefState::Live;
		}

		bool HasTrackingRoom(std::uint32_t a_refId)
		{
			if (F4T::WorldPolicyClient::CanTrackDisabledRef(g_disabledByUs.size(), kMaximumTrackedDisabledRefs, g_disabledByUs.contains(a_refId))) {
				return true;
			}
			if (!g_trackingFullLogged) {
				g_trackingFullLogged = true;
				F4T::DebugLogging::Log("WorldPolicy", "disabled reference tracking is full; further references stay enabled");
			}
			return false;
		}

		void ConsiderRef(RE::TESObjectREFR& a_ref, SweepBatch& a_batch)
		{
			const auto refId = a_ref.GetFormID();
			if (!IsTarget(a_ref)) {
				if (g_disabledByUs.contains(refId) && !a_ref.IsDeleted()) {
					a_batch.toRelease.push_back(refId);
				}
				return;
			}

			if (F4T::WorldPolicyClient::DecideRefAction(ReadRefState(a_ref)) == RefAction::Skip) {
				return;
			}
			a_batch.toApply.push_back(refId);
		}

		void ReleaseRef(std::uint32_t a_refId)
		{
			g_disabledByUs.erase(a_refId);

			auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_refId);
			if (ref == nullptr || ref->IsDeleted()) {
				return;
			}

			ref->Enable(false);
			F4T::DebugLogging::Log("WorldPolicy", "restored reference {:08X}", a_refId);
		}

		void ApplyRef(std::uint32_t a_refId)
		{
			auto* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_refId);
			if (ref == nullptr) {
				return;
			}

			if (F4T::WorldPolicyClient::DecideRefAction(ReadRefState(*ref)) == RefAction::Skip) {
				return;
			}

			if (!HasTrackingRoom(a_refId)) {
				return;
			}

			ref->Disable();
			g_disabledByUs.insert(a_refId);
			F4T::DebugLogging::Log("WorldPolicy", "disabled reference {:08X}", a_refId);
		}

		void ReleaseDisabledRefs()
		{
			const std::vector<std::uint32_t> tracked{ g_disabledByUs.begin(), g_disabledByUs.end() };
			for (const auto refId : tracked) {
				ReleaseRef(refId);
			}
			g_trackingFullLogged = false;
		}

		void ApplyBatch(const SweepBatch& a_batch)
		{
			for (const auto refId : a_batch.toRelease) {
				ReleaseRef(refId);
			}
			for (const auto refId : a_batch.toApply) {
				ApplyRef(refId);
			}
		}

		void SweepCell(RE::TESObjectCELL& a_cell, SweepBatch& a_batch)
		{
			a_cell.ForEachReference([&a_batch](RE::TESObjectREFR* a_ref) {
				if (a_ref != nullptr) {
					ConsiderRef(*a_ref, a_batch);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void SweepQueuedCells()
		{
			std::vector<std::uint32_t> cells;
			{
				std::scoped_lock lock(g_mutex);
				cells = g_pendingCells.Drain();
			}
			if (cells.empty()) {
				return;
			}

			SweepBatch batch;
			for (const auto cellId : cells) {
				auto* cell = RE::TESForm::GetFormByID<RE::TESObjectCELL>(cellId);
				if (cell == nullptr || !cell->IsAttached()) {
					continue;
				}
				SweepCell(*cell, batch);
			}
			ApplyBatch(batch);
			SyncDoorReplication();
		}

		void SweepAttachedCells()
		{
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (dataHandler == nullptr) {
				return;
			}

			SweepBatch batch;
			for (auto* cell : dataHandler->GetFormArray<RE::TESObjectCELL>()) {
				if (cell == nullptr || !cell->IsAttached()) {
					continue;
				}
				SweepCell(*cell, batch);
			}
			ApplyBatch(batch);
		}

		std::vector<F4T::WorldPolicyClient::StageSummary> StageSummaries(RE::TESQuest& a_quest)
		{
			std::vector<F4T::WorldPolicyClient::StageSummary> summaries;
			for (const auto* stage : a_quest.stages) {
				if (stage != nullptr) {
					summaries.push_back(F4T::WorldPolicyClient::StageSummary{ stage->data.index, stage->data.flags.any(RE::QUEST_STAGE_DATA::Flags::kShutDown) });
				}
			}
			return summaries;
		}

		bool TrySetShutdownStage(RE::TESQuest& a_quest, std::uint16_t a_stage)
		{
			const auto resolvedId = a_quest.GetFormID();
			const auto flags = a_quest.data.flags;
			const auto before = a_quest.currentStage;
			g_stageIssued.insert_or_assign(resolvedId, a_stage);
			g_hideWarned.erase(resolvedId);
			if (!a_quest.SetStage(a_stage)) {
				return false;
			}
			g_suppressedQuests.insert_or_assign(resolvedId, SuppressedQuest{ QuestEndKind::Ended, flags, before });
			F4T::DebugLogging::Log("WorldPolicy", "ended quest {:08X} on shutdown stage {} from stage {}", resolvedId, a_stage, before);
			return true;
		}

		void SetQuestRunning(RE::TESQuest& a_quest, bool a_running)
		{
			REL::Relocation<SetQuestRunningFn> setRunning{ kSetQuestRunning };
			setRunning(&a_quest, a_running);
		}

		void HideQuest(RE::TESQuest& a_quest, std::string_view a_reason)
		{
			const auto resolvedId = a_quest.GetFormID();
			const bool running = (a_quest.data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kEnabled)) != 0;
			auto& record = g_suppressedQuests.try_emplace(resolvedId, SuppressedQuest{ QuestEndKind::Hidden, a_quest.data.flags, a_quest.currentStage }).first->second;
			if (g_hideWarned.insert(resolvedId).second) {
				REX::WARN("world policy hid quest {:08X} by stopping it and clearing its journal flags ({}); script shutdown is not measured (TODO-verify(#669))", resolvedId, a_reason);
			}
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-gaps2/README.md
			F4T::DebugLogging::Log("WorldPolicy", "[RT-VERIFY 385-B] quest {:08X} flags {:04X} clear mask 0x820: bit 0x800 read {} bit 0x20 read {} before clear", resolvedId, a_quest.data.flags, (a_quest.data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kActive)) != 0, (a_quest.data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kDisplayedInHUD)) != 0);
			a_quest.data.flags = static_cast<std::uint16_t>(a_quest.data.flags & ~kQuestFlagsToClear);
			if (running) {
				record.stopped = true;
				SetQuestRunning(a_quest, false);
			}
			F4T::DebugLogging::Log(
				"WorldPolicy",
				"hid quest {:08X} type {}",
				resolvedId,
				static_cast<std::int32_t>(a_quest.data.questType));
		}

		// TODO-verify(#669) assumes landing on the shutdown stage halts the quest's fragments and releases its aliases; not measured in engine
		void StopQuest(RE::TESQuest& a_quest)
		{
			const auto issued = g_stageIssued.find(a_quest.GetFormID());
			if (issued != g_stageIssued.end() && issued->second == a_quest.currentStage) {
				HideQuest(a_quest, "still active after its shutdown stage");
				return;
			}

			const auto stage = F4T::WorldPolicyClient::SelectShutdownStage(StageSummaries(a_quest));
			if (!stage) {
				HideQuest(a_quest, "no shutdown stage");
				return;
			}
			if (!TrySetShutdownStage(a_quest, *stage)) {
				HideQuest(a_quest, "SetStage did not take");
			}
		}

		void RestoreQuestFlags()
		{
			for (const auto& [questId, record] : g_suppressedQuests) {
				auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(questId);
				if (quest == nullptr) {
					REX::WARN("world policy could not resolve quest {:08X} to restore it (TODO-verify(#669))", questId);
					continue;
				}
				bool enabled = (quest->data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kEnabled)) != 0;
				const bool finished = (quest->data.flags & kQuestFlagsEnded) != 0;
				if (record.stopped && !enabled && !finished) {
					SetQuestRunning(*quest, true);
					enabled = true;
					F4T::DebugLogging::Log("WorldPolicy", "restarted quest {:08X} stopped while hidden", questId);
				}
				switch (F4T::WorldPolicyClient::DecideQuestRestore(record.kind, enabled, finished)) {
				case QuestRestore::Restore:
					quest->data.flags = static_cast<std::uint16_t>(quest->data.flags | (record.flags & kQuestFlagsToClear));
					F4T::DebugLogging::Log("WorldPolicy", "restored quest {:08X} flags {:04X}", questId, record.flags);
					break;
				case QuestRestore::StaysEnded:
					REX::WARN("world policy quest {:08X} stays ended in the save (shutdown stage; was stage {}); it is not un-ended on release (TODO-verify(#669))", questId, record.stage);
					break;
				case QuestRestore::FinishedWhileSuppressed:
					F4T::DebugLogging::Log("WorldPolicy", "quest {:08X} completed or failed while suppressed; not restored", questId);
					break;
				case QuestRestore::DisabledWhileSuppressed:
					F4T::DebugLogging::Log("WorldPolicy", "quest {:08X} disabled while suppressed; not restored", questId);
					break;
				}
			}
			std::erase_if(g_suppressedQuests, [](const auto& a_entry) { return a_entry.second.kind == QuestEndKind::Hidden && RE::TESForm::GetFormByID<RE::TESQuest>(a_entry.first) != nullptr; });
		}

		void SweepQuests()
		{
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (dataHandler == nullptr) {
				return;
			}

			for (auto* quest : dataHandler->GetFormArray<RE::TESQuest>()) {
				if (quest == nullptr || !quest->GetActive()) {
					continue;
				}

				const F4T::WorldPolicyClient::QuestState state{
					static_cast<std::int32_t>(quest->data.questType),
					true,
					(quest->data.flags & kQuestFlagsEnded) != 0,
					g_blockedQuestIds.contains(quest->GetFormID())
				};
				if (F4T::WorldPolicyClient::DecideForQuest(g_applied, state) == QuestAction::Stop) {
					StopQuest(*quest);
				}
			}
		}
	}

	void AcceptPolicy(const Policy& a_policy)
	{
		std::uint64_t generation = kNoSessionGeneration;
		bool stored = false;
		{
			std::scoped_lock lock(g_mutex);
			if (g_delivered && *g_delivered == a_policy) {
				return;
			}
			g_delivered = a_policy;
			generation = F4T::ReplicationRuntime::CurrentGeneration();
			stored = g_inbound.Store(a_policy, generation);
		}

		if (!stored) {
			F4T::DebugLogging::Log(
				"WorldPolicy",
				"ignored out-of-order worldPolicy version {} for session generation {}",
				a_policy.version,
				generation);
			return;
		}

		F4T::DebugLogging::Log(
			"WorldPolicy",
			"received worldPolicy version {} mode {} doors {} suppressQuests {} disableKeys {}",
			a_policy.version,
			F4T::WorldPolicy::ModeName(a_policy.mode),
			F4T::WorldPolicy::DoorsName(a_policy.doors),
			a_policy.suppressQuests,
			a_policy.disable.size());
	}

	bool HasAdoptedPolicy() noexcept
	{
		return g_policyAdopted.load();
	}

	void ReleaseForSave()
	{
		ReleaseDisabledRefs();
		RestoreQuestFlags();
		F4T::PolicyNoInteract::Clear();
	}

	void ReapplyAfterSave()
	{
		if (!g_policyAdopted.load()) {
			return;
		}

		F4T::PolicyNoInteract::Apply(g_applied.noInteract);
		F4T::VatsPolicy::Reassert();
		g_nextWorldSweepMs = 0;
		g_nextQuestSweepMs = 0;
		SweepAttachedCells();
		SweepQueuedCells();
		if (F4T::WorldPolicyClient::WantsQuestSweep(g_applied)) {
			SweepQuests();
		}
	}

	void Tick(std::uint64_t a_nowMs, std::uint64_t a_generation)
	{
		EnsureCellSink();

		if (a_generation != g_lastGeneration) {
			F4T::DebugLogging::Log("WorldPolicy", "session generation {} -> {}", g_lastGeneration, a_generation);
			g_lastGeneration = a_generation;
			ClearSessionPolicy(a_generation);
		}

		if (a_generation == kNoSessionGeneration) {
			return;
		}

		ApplyAdoptedPolicy(a_generation);

		if (!g_policyInForce) {
			return;
		}

		if (g_policyAdopted.load()) {
			PushModeToUi();
		}

		if (!g_disableIds.empty() || !g_disabledByUs.empty()) {
			SweepQueuedCells();
			if (a_nowMs >= g_nextWorldSweepMs) {
				g_nextWorldSweepMs = a_nowMs + kWorldSweepIntervalMs;
				SweepAttachedCells();
			}
		}

		if (F4T::WorldPolicyClient::WantsQuestSweep(g_applied) && a_nowMs >= g_nextQuestSweepMs) {
			g_nextQuestSweepMs = a_nowMs + kQuestSweepIntervalMs;
			SweepQuests();
		}
	}
}
