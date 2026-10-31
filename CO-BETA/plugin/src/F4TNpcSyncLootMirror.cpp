#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncLootInternal.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TNetworking.h"
#include "F4TNpcSyncRegistry.h"

#include "RE/E/ExtraDataList.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectREFR.h"

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace F4T::NpcSync::Loot::Detail
{
	namespace
	{
		struct AppliedLoot
		{
			std::uint32_t authorityEpoch{};
			std::uint32_t revision{};
		};

		struct PendingLoot
		{
			NpcLootSync::RemoteLoot loot;
			std::uint64_t tick{};
		};

		constexpr std::uint64_t kPendingLootLifetimeTicks = 600;

		std::unordered_map<std::uint32_t, AppliedLoot> g_appliedLoot;
		std::unordered_map<std::uint32_t, PendingLoot> g_pendingLoot;
		std::uint64_t g_mirrorTicks{};
	}

	void AddLootToContainer(RE::TESObjectREFR& a_container, std::uint32_t a_itemId, std::uint32_t a_count)
	{
		if (a_count == 0 || a_itemId == 0) {
			return;
		}
		auto* form = RE::TESForm::GetFormByID(a_itemId);
		auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
		if (!object) {
			return;
		}
		a_container.AddObjectToContainer(
			object,
			RE::BSTSmartPointer<RE::ExtraDataList>{},
			static_cast<std::int32_t>(a_count),
			nullptr,
			RE::ITEM_REMOVE_REASON::kNone);
	}

	void MirrorTick(const AuthorityScope& a_scope)
	{
		++g_mirrorTicks;
		const auto loots = DrainRemoteLoot();
		if (a_scope.epoch == 0 || a_scope.epoch > (std::numeric_limits<std::uint32_t>::max)()) {
			g_pendingLoot.clear();
			return;
		}
		const auto applyLoot = [&a_scope](const NpcLootSync::RemoteLoot& a_loot) {
			auto* container = FindLootContainer(a_loot.identity.npcId);
			if (!container || !IsLootableContainer(*container) || !IsContainerInScope(*container, a_scope)) {
				return false;
			}
			auto sourceFormId = container->GetFormID();
			if (const auto puppet = TryGetPuppetByActorFormId(sourceFormId)) {
				sourceFormId = puppet->sourceFormId;
			}
			if (sourceFormId != a_loot.identity.sourceFormId) {
				return false;
			}
			const auto applied = g_appliedLoot.find(a_loot.identity.npcId);
			if (applied != g_appliedLoot.end() && applied->second.authorityEpoch == a_loot.identity.authorityEpoch &&
				!F4T::GnsSnapshotEnvelope::IsNewer(a_loot.revision, applied->second.revision)) {
				return true;
			}
			g_appliedLoot[a_loot.identity.npcId] = AppliedLoot{ a_loot.identity.authorityEpoch, a_loot.revision };
			const auto diff = NpcLootSync::ComputeMirrorDiff(ReadContainerLoot(*container), a_loot.items);
			for (const auto& [itemId, count] : diff.removals) {
				RemoveLootFromContainer(*container, itemId, count);
			}
			for (const auto& [itemId, count] : diff.additions) {
				AddLootToContainer(*container, itemId, count);
			}
			return true;
		};
		if (!loots.empty()) {
			const auto authority = F4T::Networking::GetNpcAuthorityForScope(a_scope.cellId, a_scope.worldspaceId);
			const auto authorityPlayerId = authority && authority->epoch == a_scope.epoch ? authority->playerId : 0U;
			for (const auto& loot : loots) {
				if (loot.revision == 0 || loot.identity.npcId == 0 || loot.identity.sourceFormId == 0) {
					continue;
				}
				if (authorityPlayerId == 0 || loot.identity.authorityEpoch != static_cast<std::uint32_t>(a_scope.epoch) ||
					loot.authorityPlayerId != authorityPlayerId) {
					continue;
				}
				g_pendingLoot[loot.identity.npcId] = PendingLoot{ loot, g_mirrorTicks };
			}
		}
		for (auto entry = g_pendingLoot.begin(); entry != g_pendingLoot.end();) {
			if (g_mirrorTicks - entry->second.tick > kPendingLootLifetimeTicks ||
				entry->second.loot.identity.authorityEpoch != static_cast<std::uint32_t>(a_scope.epoch)) {
				entry = g_pendingLoot.erase(entry);
				continue;
			}
			if (applyLoot(entry->second.loot)) {
				entry = g_pendingLoot.erase(entry);
				continue;
			}
			++entry;
		}
	}

	void ResetMirror()
	{
		g_appliedLoot.clear();
		g_pendingLoot.clear();
	}
}
