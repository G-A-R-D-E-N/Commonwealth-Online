#include "pch.h"

#include "F4TThrowableSync.h"

#include "F4TEquipmentInstance.h"
#include "F4TProxyActorControllerInternal.h"

#include "RE/A/Actor.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/B/BGSEquipIndex.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/B/BSTSmartPointer.h"
#include "RE/E/ExtraDataList.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectWEAP.h"

#include <format>
#include <string>

namespace F4T::ThrowableSync
{
	using ProxyActorController::detail::EquipSyncedWeaponWithMods;
	using ProxyActorController::detail::LogThrottledWarning;
	using ProxyActorController::detail::ModdedWeaponEquip;
	using ProxyActorController::detail::ProxyActorSlot;
	using ProxyActorController::detail::RemoveTrackedModdedStack;

	namespace
	{
		std::uint32_t GetEquippedRightHandFormId(RE::Actor& a_actor)
		{
			RE::BGSEquipIndex rightHandIndex;
			rightHandIndex.index = kRightHandEquipIndex;
			RE::BGSObjectInstance equippedItem{ nullptr, nullptr };
			const auto* item = a_actor.GetEquippedItem(&equippedItem, rightHandIndex);
			if (!item || !item->object || !item->object->Is(RE::ENUM_FORM_ID::kWEAP)) {
				return 0;
			}
			return item->object->GetFormID();
		}

		bool UnequipRightHand(RE::ActorEquipManager& a_equipManager, RE::Actor& a_proxy)
		{
			RE::BGSEquipIndex rightHandIndex;
			rightHandIndex.index = kRightHandEquipIndex;
			RE::BGSObjectInstance equippedItem{ nullptr, nullptr };
			if (!a_proxy.GetEquippedItem(&equippedItem, rightHandIndex) || !equippedItem.object) {
				return false;
			}

			const std::uint32_t stackId = F4T::EquipmentInstance::FindEquippedStack(a_proxy, equippedItem.object->GetFormID()).stackId;

			return a_equipManager.UnequipObject(
				std::addressof(a_proxy),
				&equippedItem,
				1,
				nullptr,
				stackId,
				false,
				true,
				false,
				true,
				nullptr);
		}

		ModdedWeaponEquip EquipRightHand(
			RE::ActorEquipManager& a_equipManager,
			RE::Actor& a_proxy,
			RE::TESObjectWEAP& a_weapon,
			const std::vector<RemoteItemMod>* a_mods)
		{
			if (a_mods && !a_mods->empty()) {
				return EquipSyncedWeaponWithMods(a_equipManager, a_proxy, a_weapon, *a_mods, "throwable");
			}

			const auto isPlainStack = [](const RE::BGSInventoryItem::Stack& a_stack) {
				return !F4T::EquipmentInstance::ReadStackMods(a_stack);
			};

			auto existing = F4T::EquipmentInstance::FindStack(a_proxy, a_weapon.GetFormID(), isPlainStack);
			if (!existing.stack) {
				a_proxy.AddObjectToContainer(
					&a_weapon,
					RE::BSTSmartPointer<RE::ExtraDataList>{},
					1,
					nullptr,
					RE::ITEM_REMOVE_REASON::kNone);
				existing = F4T::EquipmentInstance::FindStack(a_proxy, a_weapon.GetFormID(), isPlainStack);
			}

			RE::BGSObjectInstance objectInstance{ &a_weapon, nullptr };
			ModdedWeaponEquip result;
			result.equipped = a_equipManager.EquipObject(
				std::addressof(a_proxy),
				objectInstance,
				existing.stackId,
				1,
				nullptr,
				false,
				true,
				false,
				true,
				false);
			return result;
		}
	}

	// TODO(RE): concept-level/unverified; the equipped throwable is read from the right-hand index only.
	std::optional<RemoteEquippedItem> CaptureEquippedThrowable(RE::Actor& a_actor)
	{
		RE::BGSEquipIndex equipIndex;
		equipIndex.index = kRightHandEquipIndex;
		RE::BGSObjectInstance equippedItem{ nullptr, nullptr };
		const auto* item = a_actor.GetEquippedItem(&equippedItem, equipIndex);
		if (!item || !item->object || !item->object->Is(RE::ENUM_FORM_ID::kWEAP)) {
			return std::nullopt;
		}

		const auto* weapon = static_cast<const RE::TESObjectWEAP*>(item->object);
		auto captured = CaptureFromThrownWeapon(weapon->GetFormID(), weapon->IsThrownWeapon());
		if (!captured) {
			return std::nullopt;
		}

		captured->mods = F4T::EquipmentInstance::CaptureEquippedItemMods(a_actor, captured->formId);
		return captured;
	}

	// TODO(RE): concept-level/unverified; the visual attach calls mirror the rightHand path in F4TProxyActorControllerEquipment.cpp.
	ApplyOutcome ApplyThrowablePlan(
		RE::Actor& a_proxy,
		ProxyActorSlot& a_slot,
		const ApplyPlan& a_plan,
		bool a_weaponDrawn,
		const std::vector<RemoteItemMod>* a_mods)
	{
		if (a_plan.action == ApplyAction::kSkip) {
			return ApplyOutcome::kUnchanged;
		}

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			LogThrottledWarning(
				"throwable_apply_manager_missing_" + std::to_string(a_proxy.GetFormID()),
				"Could not apply the synced throwable; ActorEquipManager unavailable.");
			return ApplyOutcome::kFailed;
		}

		if (a_plan.action == ApplyAction::kClear) {
			if (GetEquippedRightHandFormId(a_proxy) != a_plan.formId) {
				return ApplyOutcome::kUnchanged;
			}
			if (!UnequipRightHand(*equipManager, a_proxy)) {
				LogThrottledWarning(
					"throwable_clear_failed_" + std::to_string(a_plan.formId),
					std::format("Could not clear throwable {:08X} from a proxy.", a_plan.formId));
				return ApplyOutcome::kFailed;
			}
			RemoveTrackedModdedStack(a_slot, a_proxy);
			return ApplyOutcome::kApplied;
		}

		const auto proxyRightHandFormId = GetEquippedRightHandFormId(a_proxy);
		if (proxyRightHandFormId == a_plan.formId) {
			return ApplyOutcome::kUnchanged;
		}

		auto* weapon = RE::TESForm::GetFormByID<RE::TESObjectWEAP>(a_plan.formId);
		if (!weapon) {
			LogThrottledWarning(
				"throwable_form_missing_" + std::to_string(a_plan.formId),
				std::format("Could not apply throwable form {:08X}; no local WEAP form resolved.", a_plan.formId));
			return ApplyOutcome::kFailed;
		}

		if (proxyRightHandFormId != 0 && !UnequipRightHand(*equipManager, a_proxy)) {
			LogThrottledWarning(
				"throwable_unequip_failed_" + std::to_string(a_plan.formId),
				std::format(
					"Could not clear weapon {:08X} before equipping throwable {:08X} on a proxy.",
					proxyRightHandFormId,
					a_plan.formId));
		}
		RemoveTrackedModdedStack(a_slot, a_proxy);

		const auto equipped = EquipRightHand(*equipManager, a_proxy, *weapon, a_mods);
		if (!equipped.equipped) {
			LogThrottledWarning(
				"throwable_equip_failed_" + std::to_string(a_plan.formId),
				std::format("ActorEquipManager failed to equip throwable form {:08X} on a proxy.", a_plan.formId));
			return ApplyOutcome::kFailed;
		}
		if (equipped.extra) {
			a_slot.syncedModdedWeaponExtra = equipped.extra;
			a_slot.syncedModdedWeaponFormId = a_plan.formId;
		}

		RE::BGSEquipIndex rightHandIndex;
		rightHandIndex.index = kRightHandEquipIndex;
		RE::BGSObjectInstanceT<RE::TESObjectWEAP> weaponInstance{ weapon, equipped.instanceData };
		a_proxy.HandleItemEquip(false);
		a_proxy.AttachWeapon(weaponInstance, rightHandIndex);
		a_proxy.DoReparentWeapon(weapon, rightHandIndex, a_weaponDrawn);
		a_proxy.DrawWeaponMagicHands(a_weaponDrawn);
		a_proxy.Update3DPosition(true);
		return ApplyOutcome::kApplied;
	}
}
