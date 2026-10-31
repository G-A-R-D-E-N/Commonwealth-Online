#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncInternal.h"

#include "F4TNpcSyncLootInternal.h"

#include "F4TNetworking.h"
#include "F4TNpcSyncRegistry.h"

#include "RE/B/BGSInventoryItem.h"
#include "RE/B/BGSInventoryList.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectREFR.h"

#include <chrono>
#include <utility>
#include <vector>

namespace F4T::NpcSync::Loot
{
	void Tick([[maybe_unused]] bool a_isHost, [[maybe_unused]] std::uint64_t a_authorityEpoch, [[maybe_unused]] std::string_view a_cellId, [[maybe_unused]] std::string_view a_worldspaceId)
	{
		Detail::InitializeCapture();
		const Detail::AuthorityScope scope{
			a_isHost,
			a_authorityEpoch,
			std::string{ a_cellId },
			std::string{ a_worldspaceId }
		};
		Detail::CaptureTick(scope);
		Detail::TakeTick(scope);
		if (!a_isHost) {
			Detail::MirrorTick(scope);
		}
		Networking::FlushNpcLootOutbound();
	}

	void Reset()
	{
		Detail::ResetCapture();
		Detail::ResetMirror();
		Detail::ResetTake();
		Detail::ResetQueues();
	}
}

namespace F4T::NpcSync::Loot::Detail
{
	namespace
	{
		constexpr std::size_t kMaximumQueuedPackets = 64;

		std::mutex g_inboundMutex;
		std::vector<NpcLootSync::RemoteLoot> g_inboundLoot;
		std::vector<NpcLootSync::TakeRequest> g_inboundTakes;
		std::vector<NpcLootSync::TakeResult> g_inboundResults;

		std::mutex g_outboundMutex;
		std::vector<NpcLootSync::RemoteLoot> g_outboundLoot;
		std::vector<NpcLootSync::TakeResult> g_outboundResults;

		std::mutex g_ledgerMutex;
		NpcLootSync::EchoLedger g_ledger;

		NpcLootSync::RevisionCounter g_revisions;
	}

	std::uint32_t CountLootItem(RE::TESObjectREFR& a_container, std::uint32_t a_itemId)
	{
		std::uint32_t total = 0;
		for (const auto& [itemId, count] : ReadContainerLoot(a_container)) {
			if (itemId == a_itemId) {
				total += count;
			}
		}
		return total;
	}

	bool IsLootableContainer(RE::TESObjectREFR& a_container)
	{
		return a_container.IsDead(true);
	}

	bool IsContainerInScope(RE::TESObjectREFR& a_container, const AuthorityScope& a_scope)
	{
		const auto* cell = a_container.GetParentCell();
		if (!cell || NpcSync::Detail::FormatFormId(cell->GetFormID()) != a_scope.cellId) {
			return false;
		}
		const auto worldspaceId = cell->IsExterior() && cell->worldSpace ?
		                              NpcSync::Detail::FormatFormId(cell->worldSpace->GetFormID()) :
		                              std::string{};
		return worldspaceId == a_scope.worldspaceId;
	}

	bool EnqueueRemoteLoot(NpcLootSync::RemoteLoot a_loot)
	{
		const std::scoped_lock lock(g_inboundMutex);
		if (g_inboundLoot.size() >= kMaximumQueuedPackets) {
			return false;
		}
		g_inboundLoot.push_back(std::move(a_loot));
		return true;
	}

	bool EnqueueTakeRequest(NpcLootSync::TakeRequest a_request)
	{
		const std::scoped_lock lock(g_inboundMutex);
		if (g_inboundTakes.size() >= kMaximumQueuedPackets) {
			return false;
		}
		g_inboundTakes.push_back(std::move(a_request));
		return true;
	}

	bool EnqueueTakeResult(NpcLootSync::TakeResult a_result)
	{
		const std::scoped_lock lock(g_inboundMutex);
		if (g_inboundResults.size() >= kMaximumQueuedPackets) {
			return false;
		}
		g_inboundResults.push_back(std::move(a_result));
		return true;
	}

	bool EnqueueOutboundLoot(NpcLootSync::RemoteLoot a_loot)
	{
		const std::scoped_lock lock(g_outboundMutex);
		if (g_outboundLoot.size() >= kMaximumQueuedPackets) {
			return false;
		}
		g_outboundLoot.push_back(std::move(a_loot));
		auto& queued = g_outboundLoot.back();
		if (queued.revision == 0) {
			queued.revision = Revisions().Next(queued.identity.npcId);
		}
		return true;
	}

	bool EnqueueOutboundTakeResult(NpcLootSync::TakeResult a_result)
	{
		const std::scoped_lock lock(g_outboundMutex);
		if (g_outboundResults.size() >= kMaximumQueuedPackets) {
			return false;
		}
		g_outboundResults.push_back(std::move(a_result));
		return true;
	}

	std::vector<NpcLootSync::RemoteLoot> DrainRemoteLoot()
	{
		const std::scoped_lock lock(g_inboundMutex);
		return std::exchange(g_inboundLoot, {});
	}

	std::vector<NpcLootSync::TakeRequest> DrainTakeRequests()
	{
		const std::scoped_lock lock(g_inboundMutex);
		return std::exchange(g_inboundTakes, {});
	}

	std::vector<NpcLootSync::TakeResult> DrainTakeResults()
	{
		const std::scoped_lock lock(g_inboundMutex);
		return std::exchange(g_inboundResults, {});
	}

	std::vector<NpcLootSync::RemoteLoot> DrainOutboundLoot()
	{
		const std::scoped_lock lock(g_outboundMutex);
		return std::exchange(g_outboundLoot, {});
	}

	std::vector<NpcLootSync::TakeResult> DrainOutboundTakeResults()
	{
		const std::scoped_lock lock(g_outboundMutex);
		return std::exchange(g_outboundResults, {});
	}

	std::uint64_t NowMilliseconds()
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
				.count());
	}

	std::optional<std::uint32_t> NpcIdForLootContainer(std::uint32_t a_containerFormId)
	{
		if (const auto host = TryGetHostNpcByActorFormId(a_containerFormId)) {
			return host->npcId;
		}
		if (const auto puppet = TryGetPuppetByActorFormId(a_containerFormId)) {
			return puppet->npcId;
		}
		return std::nullopt;
	}

	RE::TESObjectREFR* FindLootContainer(std::uint32_t a_npcId)
	{
		auto actorFormId = TryGetHostActorFormIdByNpcId(a_npcId);
		if (!actorFormId) {
			actorFormId = TryGetPuppetActorFormIdByNpcId(a_npcId);
		}
		if (!actorFormId) {
			return nullptr;
		}
		auto* form = RE::TESForm::GetFormByID(*actorFormId);
		return form ? form->As<RE::TESObjectREFR>() : nullptr;
	}

	std::vector<NpcLootSync::LootItem> ReadContainerLoot(RE::TESObjectREFR& a_container)
	{
		std::vector<NpcLootSync::LootItem> items;
		auto* list = a_container.inventoryList;
		if (!list) {
			return items;
		}
		const RE::BSAutoReadLock lock{ list->rwLock };
		for (const auto& item : list->data) {
			if (!item.object) {
				continue;
			}
			std::uint32_t total = 0;
			for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
				if (stack->IsEquipped()) {
					continue;
				}
				total += stack->GetCount();
			}
			if (total == 0) {
				continue;
			}
			items.push_back(NpcLootSync::LootItem{ item.object->GetFormID(), total });
		}
		return items;
	}

	std::uint32_t RemoveLootFromContainer(RE::TESObjectREFR& a_container, std::uint32_t a_itemId, std::uint32_t a_count)
	{
		if (a_count == 0 || a_itemId == 0) {
			return 0;
		}
		auto* list = a_container.inventoryList;
		if (!list) {
			return 0;
		}
		std::vector<std::pair<std::uint32_t, std::uint32_t>> plan;
		std::uint32_t remaining = a_count;
		{
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (remaining == 0) {
					break;
				}
				if (!item.object || item.object->GetFormID() != a_itemId) {
					continue;
				}
				std::uint32_t stackId = 0;
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackId) {
					if (remaining == 0) {
						break;
					}
					if (stack->IsEquipped()) {
						continue;
					}
					const auto taken = NpcLootSync::ClampToAvailable(remaining, stack->GetCount());
					if (taken == 0) {
						continue;
					}
					plan.emplace_back(stackId, taken);
					remaining -= taken;
				}
			}
		}
		if (plan.empty()) {
			return 0;
		}
		auto* form = RE::TESForm::GetFormByID(a_itemId);
		auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
		if (!object) {
			return 0;
		}
		const auto before = CountLootItem(a_container, a_itemId);
		for (auto entry = plan.rbegin(); entry != plan.rend(); ++entry) {
			RE::TESObjectREFR::RemoveItemData removeData{ object, static_cast<std::int32_t>(entry->second) };
			removeData.stackData.push_back(entry->first);
			a_container.RemoveItem(removeData);
		}
		const auto after = CountLootItem(a_container, a_itemId);
		return before > after ? before - after : 0;
	}

	void ResetQueues()
	{
		{
			const std::scoped_lock lock(g_inboundMutex);
			g_inboundLoot.clear();
			g_inboundTakes.clear();
			g_inboundResults.clear();
		}
		{
			const std::scoped_lock lock(g_outboundMutex);
			g_outboundLoot.clear();
			g_outboundResults.clear();
		}
		{
			const std::scoped_lock lock(g_ledgerMutex);
			g_ledger = NpcLootSync::EchoLedger{};
		}
	}

	NpcLootSync::EchoLedger& Ledger()
	{
		return g_ledger;
	}

	std::mutex& LedgerMutex()
	{
		return g_ledgerMutex;
	}

	NpcLootSync::RevisionCounter& Revisions()
	{
		return g_revisions;
	}
}
