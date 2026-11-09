#include "pch.h"

#include "F4TInventorySnapshotRead.h"

#include "F4TInventoryContract.h"
#include "F4TInventoryDropGate.h"
#include "F4TInventorySnapshot.h"
#include "F4TNetworkingInternal.h"

#include <cstdint>
#include <format>
#include <set>
#include <vector>

namespace F4T::InventorySnapshotRead
{
	std::optional<nlohmann::json> ReadLocalItems()
	{
		// RE offset: BGSInventoryList data 0x58, rwLock 0x78; GetInventoryObjectCount 0x3FB480; source:RE-Work/naming/crossref/crossref_163_disagreements_vs_155.csv source:RE-Work/Runtime/03-FINDINGS.md
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->inventoryList) {
			return std::nullopt;
		}
		std::set<std::uint32_t> formIds;
		{
			const RE::BSAutoReadLock lock{ player->inventoryList->rwLock };
			for (const auto& item : player->inventoryList->data) {
				if (item.object) {
					formIds.insert(item.object->GetFormID());
				}
			}
		}

		nlohmann::json items = nlohmann::json::array();
		for (const auto formId : formIds) {
			auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(formId);
			if (!object) {
				continue;
			}
			const auto rawCount = static_cast<std::int64_t>(player->GetInventoryObjectCount(object));
			const auto key = F4T::InventoryDropGate::TranslateItemKey(object);
			if (!key || rawCount <= 0) {
				continue;
			}
			const auto parts = F4T::InventoryContract::ParseItemKey(*key);
			if (!parts) {
				continue;
			}
			const auto entry = nlohmann::json{ { "plugin", parts->plugin }, { "localFormId", std::format("{:08X}", parts->localId) },
				{ "count", static_cast<std::uint32_t>(rawCount) } };
			if (!F4T::InventorySnapshot::NormalizeSnapshotItems(nlohmann::json::array({ entry }))) {
				F4T::Networking::Detail::LogThrottledWarning("inventory_snapshot_entry", "Left one owned item out of the inventory snapshot: the server would refuse its name, form id or count.");
				continue;
			}
			items.push_back(entry);
		}
		return items;
	}
}
