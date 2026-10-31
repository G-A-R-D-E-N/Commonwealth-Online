#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncLootInternal.h"

#include "F4TNpcSyncInternal.h"
#include "F4TNetworking.h"
#include "F4TRemotePlayerState.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESContainerChangedEvent.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectREFR.h"

#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace F4T::NpcSync::Loot::Detail
{
	namespace
	{
		struct PendingContainerChanges
		{
			std::unordered_map<std::uint64_t, std::uint32_t> removals;
			std::unordered_map<std::uint64_t, std::uint32_t> takes;
			std::unordered_set<std::uint32_t> additions;
			bool overflow{ false };
		};

		constexpr std::size_t kMaximumPendingContainerChanges = 256;

		std::uint64_t RemovalKey(std::uint32_t a_containerFormId, std::uint32_t a_baseObjectFormId)
		{
			return (static_cast<std::uint64_t>(a_containerFormId) << 32) | a_baseObjectFormId;
		}

		std::mutex g_pendingMutex;
		PendingContainerChanges g_pendingChanges;
		bool g_nonHostTick{};
		std::unordered_map<std::uint32_t, std::uint32_t> g_dirtyContainers;
		std::unordered_set<std::uint32_t> g_publishedContainers;
		bool g_sinkRegistered = false;

		class ContainerChangeSink : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESContainerChangedEvent& a_event,
				RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (a_event.itemCount <= 0) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::scoped_lock lock(g_pendingMutex);
				if (g_pendingChanges.overflow) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto key = RemovalKey(a_event.oldContainerFormID, a_event.baseObjectFormID);
				const auto newRemoval = a_event.oldContainerFormID != 0 && !g_pendingChanges.removals.contains(key);
				const auto newAddition = a_event.newContainerFormID != 0 && !g_pendingChanges.additions.contains(a_event.newContainerFormID);
				if ((newRemoval || newAddition) &&
					g_pendingChanges.removals.size() + g_pendingChanges.additions.size() >= kMaximumPendingContainerChanges) {
					g_pendingChanges.overflow = true;
					return RE::BSEventNotifyControl::kContinue;
				}
				if (a_event.oldContainerFormID != 0) {
					const auto itemCount = static_cast<std::uint32_t>(a_event.itemCount);
					auto& pending = g_pendingChanges.removals[key];
					pending = NpcLootSync::ClampToAvailable(pending + itemCount, NpcLootSync::MaximumLootCount);
				}
				if (a_event.newContainerFormID != 0) {
					g_pendingChanges.additions.insert(a_event.newContainerFormID);
				}
				const auto* player = RE::PlayerCharacter::GetSingleton();
				if (player && a_event.oldContainerFormID != 0 && a_event.newContainerFormID == player->GetFormID() &&
					(g_pendingChanges.takes.contains(key) || g_pendingChanges.takes.size() < kMaximumPendingContainerChanges)) {
					auto& pending = g_pendingChanges.takes[key];
					pending = NpcLootSync::ClampToAvailable(pending + static_cast<std::uint32_t>(a_event.itemCount), NpcLootSync::MaximumLootCount);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		ContainerChangeSink g_containerChangeSink;

		PendingContainerChanges DrainPendingContainerChanges()
		{
			const std::scoped_lock lock(g_pendingMutex);
			return std::exchange(g_pendingChanges, PendingContainerChanges{});
		}

		bool ConsumeOwnRemoval(std::uint32_t a_containerFormId, std::uint32_t a_formId, std::uint32_t a_count)
		{
			const std::scoped_lock lock(LedgerMutex());
			return Ledger().Consume(a_containerFormId, a_formId, a_count, NowMilliseconds());
		}
	}

	void InitializeCapture()
	{
		if (g_sinkRegistered) {
			return;
		}
		if (auto* eventSource = RE::TESContainerChangedEvent::GetEventSource()) {
			eventSource->RegisterSink(&g_containerChangeSink);
			g_sinkRegistered = true;
		}
	}

	void CaptureTick(const AuthorityScope& a_scope)
	{
		const auto changes = DrainPendingContainerChanges();
		g_nonHostTick = !a_scope.isHost;
		if (!a_scope.isHost) {
			g_dirtyContainers.clear();
			for (const auto& [key, count] : changes.takes) {
				SendTake(static_cast<std::uint32_t>(key >> 32), static_cast<std::uint32_t>(key & 0xFFFFFFFFULL), count, a_scope);
			}
			return;
		}
		const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		const auto authority = F4T::Networking::GetNpcAuthorityForScope(a_scope.cellId, a_scope.worldspaceId);
		if (!localPlayerId || !authority || authority->playerId != *localPlayerId || authority->epoch != a_scope.epoch ||
			a_scope.epoch == 0 || a_scope.epoch > (std::numeric_limits<std::uint32_t>::max)()) {
			g_dirtyContainers.clear();
			return;
		}
		if (changes.overflow) {
			for (const auto& [containerFormId, npcId] : NpcSync::Detail::g_hostNpcIdsByActorFormId) {
				g_dirtyContainers.try_emplace(containerFormId, npcId);
			}
		} else {
			for (const auto& [key, count] : changes.removals) {
				const auto containerFormId = static_cast<std::uint32_t>(key >> 32);
				const auto baseObjectFormId = static_cast<std::uint32_t>(key & 0xFFFFFFFFULL);
				if (const auto removedFrom = NpcIdForLootContainer(containerFormId)) {
					if (!ConsumeOwnRemoval(containerFormId, baseObjectFormId, count)) {
						g_dirtyContainers.try_emplace(containerFormId, *removedFrom);
					}
				}
			}
			for (const auto containerFormId : changes.additions) {
				if (const auto addedTo = NpcIdForLootContainer(containerFormId)) {
					g_dirtyContainers.try_emplace(containerFormId, *addedTo);
				}
			}
		}
		for (const auto& [containerFormId, npcId] : NpcSync::Detail::g_hostNpcIdsByActorFormId) {
			if (g_publishedContainers.contains(containerFormId)) {
				continue;
			}
			auto* trackedForm = RE::TESForm::GetFormByID(containerFormId);
			auto* tracked = trackedForm ? trackedForm->As<RE::TESObjectREFR>() : nullptr;
			if (tracked && IsLootableContainer(*tracked) && IsContainerInScope(*tracked, a_scope)) {
				g_dirtyContainers.try_emplace(containerFormId, npcId);
			}
		}
		for (auto entry = g_dirtyContainers.begin(); entry != g_dirtyContainers.end();) {
			const auto containerFormId = entry->first;
			const auto npcId = entry->second;
			auto* form = RE::TESForm::GetFormByID(containerFormId);
			auto* container = form ? form->As<RE::TESObjectREFR>() : nullptr;
			if (!container || !IsContainerInScope(*container, a_scope)) {
				entry = g_dirtyContainers.erase(entry);
				continue;
			}
			if (!IsLootableContainer(*container)) {
				++entry;
				continue;
			}
			auto published = NpcLootSync::BuildPublishList(ReadContainerLoot(*container));
			if (published.truncated) {
				g_publishedContainers.insert(containerFormId);
				entry = g_dirtyContainers.erase(entry);
				continue;
			}
			const auto* cell = container->GetParentCell();
			const auto worldspaceId = cell->IsExterior() && cell->worldSpace ? cell->worldSpace->GetFormID() : 0U;
			NpcLootSync::RemoteLoot loot{
				NpcLootSync::LootIdentity{
					npcId,
					containerFormId,
					cell->GetFormID(),
					worldspaceId,
					static_cast<std::uint32_t>(a_scope.epoch) },
				0U,
				*localPlayerId,
				std::move(published.items)
			};
			if (!EnqueueOutboundLoot(std::move(loot))) {
				++entry;
				continue;
			}
			g_publishedContainers.insert(containerFormId);
			entry = g_dirtyContainers.erase(entry);
		}
	}

	void MarkLootDirty(std::uint32_t a_containerFormId)
	{
		if (const auto npcId = NpcIdForLootContainer(a_containerFormId)) {
			g_dirtyContainers.try_emplace(a_containerFormId, *npcId);
		}
	}

	void ResetCapture()
	{
		const auto changes = DrainPendingContainerChanges();
		if (g_nonHostTick) {
			for (const auto& [key, count] : changes.takes) {
				RollBackTake(static_cast<std::uint32_t>(key >> 32), static_cast<std::uint32_t>(key & 0xFFFFFFFFULL), count, "capture reset", true);
			}
		}
		g_nonHostTick = false;
		g_dirtyContainers.clear();
		g_publishedContainers.clear();
		Revisions().Clear();
	}
}
