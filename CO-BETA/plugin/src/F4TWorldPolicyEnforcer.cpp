// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-gaps2/README.md
#include "pch.h"

#include "F4TWorldPolicyEnforcer.h"

#include "F4TDebugLogging.h"
#include "F4TObjectSync.h"
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
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace F4T::WorldPolicyEnforcer
{
	namespace
	{
		using F4T::WorldPolicy::Policy;
		using F4T::WorldPolicyClient::QuestAction;
		using F4T::WorldPolicyClient::RefAction;
		using F4T::WorldPolicyClient::RefClass;
		using F4T::WorldPolicyClient::RefState;

		constexpr std::size_t kMaximumTrackedDisabledRefs = F4T::WorldPolicy::kMaximumTotalDisableEntries;
		constexpr std::uint64_t kWorldSweepIntervalMs = 30000;
		constexpr std::uint64_t kQuestSweepIntervalMs = 1000;
		constexpr std::uint16_t kQuestFlagsToClear =
			static_cast<std::uint16_t>(RE::QuestFlag::kActive) |
			static_cast<std::uint16_t>(RE::QuestFlag::kDisplayedInHUD);
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
		std::unordered_map<std::uint32_t, std::uint16_t> g_stoppedQuestFlags;
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
		F4T::VatsPolicy::Restore();

		g_applied = Policy{};
			g_policyInForce = true;
			g_disableIds.clear();
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
				const auto formId = dataHandler->LookupFormID(key.localId, key.plugin);
				if (formId == 0) {
					F4T::DebugLogging::Log("WorldPolicy", "unresolved disable key {}", F4T::WorldPolicy::FormatFormKey(key));
					continue;
				}
				g_disableIds.insert(formId);
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

			F4T::DebugLogging::Log(
				"WorldPolicy",
				"applied mode={} version={} doors={} suppressQuests={} disableKeys={}",
				F4T::WorldPolicy::ModeName(policy.mode),
				policy.version,
				F4T::WorldPolicy::DoorsName(policy.doors),
				policy.suppressQuests,
				policy.disable.size());
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
			if (g_applied.suppressQuests) {
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

		void StopQuest(RE::TESQuest& a_quest)
		{
			g_stoppedQuestFlags.try_emplace(a_quest.GetFormID(), a_quest.data.flags);
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-gaps2/README.md
			F4T::DebugLogging::Log("WorldPolicy", "[RT-VERIFY 385-B] quest {:08X} flags {:04X} clear mask 0x820: bit 0x800 read {} bit 0x20 read {} before clear", a_quest.GetFormID(), a_quest.data.flags, (a_quest.data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kActive)) != 0, (a_quest.data.flags & static_cast<std::uint16_t>(RE::QuestFlag::kDisplayedInHUD)) != 0);
			a_quest.data.flags = static_cast<std::uint16_t>(a_quest.data.flags & ~kQuestFlagsToClear);
			F4T::DebugLogging::Log(
				"WorldPolicy",
				"stopped quest {:08X} type {}",
				a_quest.GetFormID(),
				static_cast<std::int32_t>(a_quest.data.questType));
		}

		void RestoreQuestFlags()
		{
			for (const auto& [questId, flags] : g_stoppedQuestFlags) {
				auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(questId);
				if (quest == nullptr) {
					continue;
				}
				const auto current = quest->data.flags;
				if ((current & static_cast<std::uint16_t>(RE::QuestFlag::kEnabled)) == 0 || (current & kQuestFlagsEnded) != 0) {
					continue;
				}
				quest->data.flags = static_cast<std::uint16_t>(quest->data.flags | (flags & kQuestFlagsToClear));
				F4T::DebugLogging::Log("WorldPolicy", "restored quest {:08X} flags {:04X}", questId, flags);
			}
			g_stoppedQuestFlags.clear();
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

				const auto action = F4T::WorldPolicyClient::DecideForQuest(
					g_applied,
					true,
					static_cast<std::int32_t>(quest->data.questType));
				if (action == QuestAction::Stop) {
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
	}

	void ReapplyAfterSave()
	{
		if (!g_policyAdopted.load()) {
			return;
		}

		F4T::VatsPolicy::Reassert();
		g_nextWorldSweepMs = 0;
		g_nextQuestSweepMs = 0;
		SweepAttachedCells();
		SweepQueuedCells();
		if (g_applied.suppressQuests) {
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

		if (g_applied.suppressQuests && a_nowMs >= g_nextQuestSweepMs) {
			g_nextQuestSweepMs = a_nowMs + kQuestSweepIntervalMs;
			SweepQuests();
		}
	}
}
