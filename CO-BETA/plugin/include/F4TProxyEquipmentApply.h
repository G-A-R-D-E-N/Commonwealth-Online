#pragma once

// Constraint: only stacks this client added are ever tracked or removed; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).

#include "F4TEquipmentInstance.h"
#include "F4TProxyEquipmentStacks.h"

#include "RE/A/Actor.h"
#include "RE/T/TESObjectARMO.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ProxyActorController::detail
{
	struct ArmorTarget
	{
		std::uint32_t formId{};
		const std::vector<F4T::RemotePlayerState::RemoteItemMod>* mods{ nullptr };
	};

	struct AddedArmorStack
	{
		std::uint32_t stackId{ 0 };
		RE::TBO_InstanceData* instanceData{ nullptr };
	};

	struct StackCensus
	{
		std::uint32_t total{ 0 };
		std::uint32_t nullExtra{ 0 };
		std::string stackIds;
	};

	[[nodiscard]] const ArmorTarget* FindArmorTarget(const std::vector<ArmorTarget>& a_targets, std::uint32_t a_formId);

	[[nodiscard]] std::string InstanceDataFailureKey(std::uint32_t a_formId, const std::vector<F4T::RemotePlayerState::RemoteItemMod>& a_mods);

	[[nodiscard]] const std::vector<F4T::RemotePlayerState::RemoteItemMod>* ResolveTargetMods(
		const F4T::RemotePlayerState::RemoteEquippedItem& a_item);

	[[nodiscard]] F4T::EquipmentInstance::FoundStack FindTrackedStack(
		RE::Actor& a_proxy,
		const F4T::ProxyEquipmentStacks::TrackedStack& a_tracked);

	[[nodiscard]] const F4T::ProxyEquipmentStacks::TrackedStack* FindReusableTrackedStack(
		const F4T::ProxyEquipmentStacks::Ledger& a_ledger,
		RE::Actor& a_proxy,
		std::uint32_t a_formId,
		const std::vector<F4T::RemotePlayerState::RemoteItemMod>* a_mods);

	[[nodiscard]] F4T::ProxyEquipmentStacks::StackRemovalResult RemoveTrackedStack(
		F4T::ProxyEquipmentStacks::Ledger& a_ledger,
		RE::Actor& a_proxy,
		const F4T::ProxyEquipmentStacks::TrackedStack& a_tracked,
		std::string_view a_logKey);

	void RemoveTrackedStacks(F4T::ProxyEquipmentStacks::Ledger& a_ledger, RE::Actor& a_proxy, std::string_view a_logKey);

	void RetainSyncedStacks(F4T::ProxyEquipmentStacks::Ledger& a_ledger, RE::Actor& a_proxy, const std::vector<ArmorTarget>& a_wantedArmor, const std::vector<F4T::RemotePlayerState::RemoteItemMod>* a_rightHandMods, std::uint32_t a_rightHandWeaponFormId, std::string_view a_logKey);

	std::optional<AddedArmorStack> AddTrackedArmorCopy(
		F4T::ProxyEquipmentStacks::Ledger& a_ledger,
		RE::Actor& a_proxy,
		RE::TESObjectARMO& a_armor,
		const std::vector<F4T::RemotePlayerState::RemoteItemMod>* a_mods,
		std::string_view a_logKey);

	StackCensus CensusStacks(RE::Actor& a_proxy, std::uint32_t a_formId);
	std::size_t CountStacks(RE::Actor& a_proxy, std::uint32_t a_formId);
}
