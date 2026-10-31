// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
// source:CO-BETA/plugin/src/F4TProxyActorController.cpp moved verbatim (RM-10)
#include "pch.h"

#include "F4TEquipmentInstance.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxyEquipmentApply.h"
#include "F4TProxyEquipmentStacks.h"
#include "F4TThrowableSync.h"

#include "RE/T/TESObjectWEAP.h"
// TODO-verify: concept-level wire and plan checks, engine facts unverified; source:docs/code-notes/power-armor-sync.md.

using namespace F4T::ProxyActorController::detail;

namespace
{
	using F4T::RemotePlayerState::RemoteItemMod;
	using F4T::ProxyEquipmentStacks::StackRemoval;
	using F4T::ProxyEquipmentStacks::TrackedStack;

	constexpr auto kProxyWeaponGraphSettleDuration = 300ms;

	struct SyncedEquipmentSlot
	{
		std::string_view name;
		RE::BIPED_OBJECT bipedObject;
	};

	constexpr std::array<SyncedEquipmentSlot, 16> kSyncedEquipmentSlots{
		{
			{ "hairTop", RE::BIPED_OBJECT::kHairTop },
			{ "hairLong", RE::BIPED_OBJECT::kHairLong },
			{ "body", RE::BIPED_OBJECT::kBody },
			{ "underTorso", RE::BIPED_OBJECT::kUnderTorso },
			{ "underLeftArm", RE::BIPED_OBJECT::kUnderLeftArm },
			{ "underRightArm", RE::BIPED_OBJECT::kUnderRightArm },
			{ "underLeftLeg", RE::BIPED_OBJECT::kUnderLeftLeg },
			{ "underRightLeg", RE::BIPED_OBJECT::kUnderRightLeg },
			{ "aboveTorso", RE::BIPED_OBJECT::kAboveTorso },
			{ "aboveLeftArm", RE::BIPED_OBJECT::kAboveLeftArm },
			{ "aboveRightArm", RE::BIPED_OBJECT::kAboveRightArm },
			{ "aboveLeftLeg", RE::BIPED_OBJECT::kAboveLeftLeg },
			{ "aboveRightLeg", RE::BIPED_OBJECT::kAboveRightLeg },
			{ "headband", RE::BIPED_OBJECT::kHeadband },
			{ "eyes", RE::BIPED_OBJECT::kEyes },
			{ "scalp", RE::BIPED_OBJECT::kScalp }
		}
	};

	std::optional<RE::BIPED_OBJECT> GetSyncedBipedObjectForSlot(std::string_view a_slotName)
	{
		for (const auto& slot : kSyncedEquipmentSlots) {
			if (slot.name == a_slotName) {
				return slot.bipedObject;
			}
		}

		return std::nullopt;
	}

	bool AreEquipmentStatesEqual(
		const std::vector<RemoteEquippedItem>& a_left,
		const std::vector<RemoteEquippedItem>& a_right)
	{
		return a_left == a_right;
	}

	RE::BIPOBJECT* GetEquippedArmorBipedObject(RE::Actor& a_actor, RE::BIPED_OBJECT a_bipedObject)
	{
		if (!a_actor.biped) {
			return nullptr;
		}

		auto* bipedObject = a_actor.biped->GetBipObject(a_bipedObject);
		if (!bipedObject || !bipedObject->parent.object) {
			return nullptr;
		}

		if (!bipedObject->parent.object->Is(RE::ENUM_FORM_ID::kARMO)) {
			return nullptr;
		}

		return bipedObject;
	}

	bool UnequipSyncedArmor(
		RE::ActorEquipManager& a_equipManager,
		RE::Actor& a_proxy,
		RE::BIPOBJECT& a_bipedObject)
	{
		// TODO-verify: equipped armor stack id via FindEquippedStack is unverified for armor; see docs/code-notes/equipment-state.md
		const auto stackId = a_bipedObject.parent.object
			? F4T::EquipmentInstance::FindEquippedStack(a_proxy, a_bipedObject.parent.object->GetFormID()).stackId
			: 0U;

		return a_equipManager.UnequipObject(
			std::addressof(a_proxy),
			std::addressof(a_bipedObject.parent),
			1,
			nullptr,
			stackId,
			false,
			true,
			false,
			true,
			nullptr);
	}

	bool UnequipSyncedWeapon(
		RE::ActorEquipManager& a_equipManager,
		RE::Actor& a_proxy)
	{
		RE::BGSEquipIndex rightHandIndex;
		rightHandIndex.index = 0;
		RE::BGSObjectInstance equippedItem{ nullptr, nullptr };
		const auto* item = a_proxy.GetEquippedItem(&equippedItem, rightHandIndex);
		if (!item || !item->object) {
			return false;
		}

		const auto stackId = F4T::EquipmentInstance::FindEquippedStack(a_proxy, item->object->GetFormID()).stackId;

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

	bool EquipSyncedWeapon(
		RE::ActorEquipManager& a_equipManager,
		RE::Actor& a_proxy,
		RE::TESObjectWEAP& a_weapon,
		bool a_addCopy = true)
	{
		if (a_addCopy) {
			a_proxy.AddObjectToContainer(
				&a_weapon,
				RE::BSTSmartPointer<RE::ExtraDataList>{},
				1,
				nullptr,
				RE::ITEM_REMOVE_REASON::kNone);
		}

		RE::BGSObjectInstance objectInstance{ &a_weapon, nullptr };
		const bool equipSuccess = a_equipManager.EquipObject(
			std::addressof(a_proxy),
			objectInstance,
			0,
			1,
			nullptr,
			false,
			true,
			false,
			true,
			false);

		return equipSuccess;
	}

}

namespace F4T::ProxyActorController::detail
{
	void RefreshProxyAfterEquipmentChange(ProxyActorSlot& a_slot, RE::Actor& a_proxy)
	{
		QueueProxyUpdate3D(a_proxy, GetProxyVisibilityUpdate3DFlags());
		F4T::ProxyAnimationSync::ResetAppliedAnimationState(a_slot.appliedAnimationState);
		a_slot.hasLoggedInitialAnimationState = false;
	}

	// Constraint: the modded weapon stack is tracked on the slot, never in the ledger; source:docs/code-notes/equipment-state.md (weapon mod sync).
	void RemoveTrackedModdedStack(ProxyActorSlot& a_slot, RE::Actor& a_proxy)
	{
		const TrackedStack tracked{ a_slot.syncedModdedWeaponFormId, 0, a_slot.syncedModdedWeaponExtra };
		if (!tracked.extra || tracked.formId == 0) {
			return;
		}
		// Constraint: a blocked removal keeps the slot pointer so a later pass retries it; source:docs/code-notes/equipment-state.md (weapon mod sync).
		const auto removal = RemoveTrackedStack(a_slot.syncedEquipmentStacks, a_proxy, tracked, std::to_string(a_slot.remotePlayerId));
		if (!F4T::ProxyEquipmentStacks::RetryTrackedStack(removal.outcome)) {
			a_slot.syncedModdedWeaponExtra = nullptr;
			a_slot.syncedModdedWeaponFormId = 0;
		}
	}

	// Constraint: a same-proxy clear keeps blocked ledger entries for retry; a proxy-gone reset forgets the ledger so stale null-extra stack indexes cannot match the next actor; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	void ResetSlotEquipmentTracking(ProxyActorSlot& a_slot, bool a_forgetTrackedStacks)
	{
		F4T::PowerArmorSync::DestroyPlacedFrame(a_slot.placedPowerArmorFrame, a_slot.proxyHandle.get().get());
		a_slot.placedPowerArmorFrame.placements = 0;
		a_slot.hasAppliedEquipmentState = false;
		a_slot.lastAppliedEquipmentItems.clear();
		if (a_forgetTrackedStacks) {
			const auto forgotten = a_slot.syncedEquipmentStacks.ForgetAll().size();
			F4T::DebugLogging::Log("Equipment", "ledger forget on reset remote player {}: {} tracked stacks dropped", a_slot.remotePlayerId, forgotten);
		}
		a_slot.syncedEquipmentStacks.ForgetInstanceDataFailures();
		F4T::DebugLogging::Log("Equipment", "instance data failure memo cleared on reset remote player {} forget={}", a_slot.remotePlayerId, a_forgetTrackedStacks);
		a_slot.syncedModdedWeaponExtra = nullptr;
		a_slot.syncedModdedWeaponFormId = 0;
		a_slot.lastAppliedPowerArmorState = {};
		a_slot.appliedPowerArmorFrameFormId = 0;
	}

	void ClearSlotEquipmentIfApplied(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		std::string_view a_reason)
	{
		if (!a_slot.hasAppliedEquipmentState && a_slot.appliedPowerArmorFrameFormId == 0 &&
			a_slot.lastAppliedPowerArmorState.pieces.empty() && a_slot.placedPowerArmorFrame.baseFormId == 0 &&
			a_slot.syncedEquipmentStacks.Size() == 0 && !a_slot.syncedModdedWeaponExtra) {
			ResetSlotEquipmentTracking(a_slot);
			return;
		}

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			LogThrottledWarning(
				"proxy_equipment_clear_manager_missing_" + std::to_string(a_slot.remotePlayerId),
				std::format(
					"Could not clear synced equipment for remote player {} during {}; ActorEquipManager unavailable.",
					a_slot.remotePlayerId,
					a_reason));
			ResetSlotEquipmentTracking(a_slot);
			return;
		}

		bool changed = false;
		for (const auto& slot : kSyncedEquipmentSlots) {
			auto* bipedObject = GetEquippedArmorBipedObject(a_proxy, slot.bipedObject);
			if (!bipedObject) {
				continue;
			}

			changed = UnequipSyncedArmor(*equipManager, a_proxy, *bipedObject) || changed;
		}

		if (UnequipSyncedWeapon(*equipManager, a_proxy)) {
			changed = true;
		}
		RemoveTrackedModdedStack(a_slot, a_proxy);
		RemoveTrackedStacks(a_slot.syncedEquipmentStacks, a_proxy, std::to_string(a_slot.remotePlayerId));
		const auto ledgerLeft = a_slot.syncedEquipmentStacks.Size();
		const auto logState = static_cast<std::size_t>(changed) | (ledgerLeft << 1);
		++a_slot.equipmentCleanupLogCount;
		if (F4T::DebugLogging::IsEnabled() &&
			(a_slot.equipmentCleanupLogCount <= 5 || a_slot.equipmentCleanupLogCount % 300 == 0 || logState != a_slot.equipmentCleanupLogState)) {
			F4T::DebugLogging::Log("Equipment", "cleanup remote player {} reason={} changed={} ledgerLeft={} count={}", a_slot.remotePlayerId, a_reason, changed, ledgerLeft, a_slot.equipmentCleanupLogCount);
		}
		a_slot.equipmentCleanupLogState = logState;

		if (a_slot.placedPowerArmorFrame.baseFormId != 0) {
			changed = F4T::PowerArmorSync::UnequipProxyPowerArmorFrame(
						  a_proxy,
						  a_slot.placedPowerArmorFrame,
						  std::to_string(a_slot.remotePlayerId)) ||
					  changed;
		}
		a_slot.appliedPowerArmorFrameFormId = 0;

		if (!a_slot.lastAppliedPowerArmorState.pieces.empty()) {
			std::vector<F4T::PowerArmorSync::PowerArmorOp> powerArmorUnequips;
			powerArmorUnequips.reserve(a_slot.lastAppliedPowerArmorState.pieces.size());
			for (const auto& piece : a_slot.lastAppliedPowerArmorState.pieces) {
				powerArmorUnequips.push_back({ F4T::PowerArmorSync::PowerArmorOpKind::kUnequip, piece });
			}
			const auto cleared = F4T::PowerArmorSync::ApplyProxyPowerArmorPlan(
				a_proxy,
				a_slot.lastAppliedPowerArmorState,
				F4T::PowerArmorSync::PowerArmorState{},
				powerArmorUnequips,
				std::to_string(a_slot.remotePlayerId));
			changed = cleared.changed || changed;
		}

		if (changed) {
			RefreshProxyAfterEquipmentChange(a_slot, a_proxy);
			LogInfoWithLocalPlayerPrefix(std::format(
				"Cleared synced equipment from runtime proxy actor {:08X} for remote player {} during {}.",
				a_proxy.GetFormID(),
				a_slot.remotePlayerId,
				a_reason));
		}

		ResetSlotEquipmentTracking(a_slot);
	}

	void ApplyProxyEquipmentFromRemoteState(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RemotePlayer& a_remotePlayer)
	{
		if (!a_remotePlayer.hasEquipmentUpdate) {
			return;
		}
		const auto now = F4T::ProxyEquipmentDebounce::Clock::now();
		if (!AreEquipmentStatesEqual(a_slot.lastSeenEquipmentItems, a_remotePlayer.equippedItems)) {
			a_slot.lastSeenEquipmentItems = a_remotePlayer.equippedItems;
			a_slot.equipmentDebounce.wantChangedAt = now;
		}
		if (a_slot.hasAppliedEquipmentState &&
			AreEquipmentStatesEqual(a_slot.lastAppliedEquipmentItems, a_remotePlayer.equippedItems)) {
			return;
		}
		if (!F4T::ProxyEquipmentDebounce::ShouldApply(a_slot.equipmentDebounce, !a_slot.hasAppliedEquipmentState, now)) {
			return;
		}
		a_slot.equipmentDebounce.appliedAt = now;

	auto* equipManager = RE::ActorEquipManager::GetSingleton();
	if (!equipManager) {
		LogThrottledWarning(
			"proxy_equipment_manager_missing_" + std::to_string(a_remotePlayer.playerId),
			std::format(
				"Could not apply synced equipment for remote player {}; ActorEquipManager unavailable.",
				a_remotePlayer.playerId));
		return;
	}

	LogReceivedEquipment(a_remotePlayer);

	bool changed = false;
	bool allApplied = true;
	bool weaponVisualAttached = false;
	// Constraint: wanted armor is deduplicated by form id before any stack work; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
	std::vector<ArmorTarget> wantedArmor;
	std::vector<ArmorTarget> armorToEquip;
	for (const auto& item : a_remotePlayer.equippedItems) {
		if (item.slot == "rightHand" || F4T::ThrowableSync::IsThrowableSlot(item.slot)) {
			continue;
		}

		const auto bipedObject = GetSyncedBipedObjectForSlot(item.slot);
		if (!bipedObject) {
			LogThrottledWarning(
				"proxy_equipment_unknown_slot_" + item.slot,
				std::format(
					"Ignoring equipment slot '{}' for remote player {}; slot is not tracked.",
					item.slot,
					a_remotePlayer.playerId));
			continue;
		}

		const auto* targetMods = ResolveTargetMods(item);
		if (item.formId != 0 && FindArmorTarget(wantedArmor, item.formId) == nullptr) {
			wantedArmor.push_back({ item.formId, targetMods });
		}

		auto* currentBipedObject = GetEquippedArmorBipedObject(a_proxy, *bipedObject);
		const auto currentFormId =
			currentBipedObject && currentBipedObject->parent.object ? currentBipedObject->parent.object->GetFormID() : 0U;

		bool slotNeedsWork = currentFormId != item.formId;
		if (!slotNeedsWork && currentFormId != 0) {
			const auto currentMods = F4T::EquipmentInstance::CaptureEquippedItemMods(a_proxy, currentFormId);
			slotNeedsWork = F4T::ProxyEquipmentStacks::SlotNeedsWork(
				currentFormId,
				item.formId,
				F4T::EquipmentInstance::SameMods(
					currentMods.value_or(std::vector<RemoteItemMod>{}),
					targetMods ? *targetMods : std::vector<RemoteItemMod>{}));
		}
		if (!slotNeedsWork) {
			continue;
		}

		if (currentFormId != 0 && !F4T::PowerArmorSync::IsPowerArmorPieceForm(currentFormId)) {
			changed = UnequipSyncedArmor(*equipManager, a_proxy, *currentBipedObject) || changed;
		}
		const auto* wanted = FindArmorTarget(wantedArmor, item.formId);
		if (wanted && FindArmorTarget(armorToEquip, item.formId) == nullptr) {
			armorToEquip.push_back(*wanted);
		}
	}

	for (const auto& target : armorToEquip) {
		if (F4T::PowerArmorSync::IsPowerArmorPieceForm(target.formId)) {
			continue;
		}

		auto* armor = RE::TESForm::GetFormByID<RE::TESObjectARMO>(target.formId);
		if (!armor) {
			LogThrottledWarning(
				"proxy_equipment_form_missing_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
				std::format(
					"Could not apply equipment form {:08X} for remote player {}; no local ARMO form resolved.",
					target.formId,
					a_remotePlayer.playerId));
			allApplied = false;
			continue;
		}

		std::uint32_t stackId = 0;
		RE::TBO_InstanceData* instanceData = nullptr;
		bool haveStack = false;
		bool reuseFailed = false;
		// Constraint: a tracked stack is reused only when its identity and recipe match the target; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
		// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
		const auto* tracked = FindReusableTrackedStack(a_slot.syncedEquipmentStacks, a_proxy, target.formId, target.mods);
		if (tracked) {
			const auto found = FindTrackedStack(a_proxy, *tracked);
			const bool recipeMatches = found.stack && F4T::EquipmentInstance::SameMods(
				F4T::EquipmentInstance::ReadStackMods(*found.stack).value_or(std::vector<RemoteItemMod>{}),
				target.mods ? *target.mods : std::vector<RemoteItemMod>{});
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
			if (F4T::DebugLogging::IsEnabled() && !found.stack) {
				LogThrottledInfo(
					"proxy_361_apply_missing_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
					std::format("[RT-VERIFY 361-G] apply formId={:08X} tracked stack not found by stack id and extra pointer; re-adding.", target.formId),
					1s);
			} else if (F4T::DebugLogging::IsEnabled() && (!found.stack->extra || !recipeMatches)) {
				LogThrottledInfo(
					"proxy_361_apply_readd_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
					std::format("[RT-VERIFY 361-D] apply formId={:08X} stackId={} extraNull={} recipeMatch={}; re-adding.", target.formId, found.stackId, !found.stack->extra, recipeMatches),
					1s);
			}
			if (found.stack && found.stack->extra && recipeMatches) {
				stackId = found.stackId;
				haveStack = true;
				if (target.mods) {
					// TODO-verify(#407)
					instanceData = found.stack->extra->CreateInstanceData(armor, false);
					haveStack = instanceData != nullptr;
					// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co367-armormods/README.md
					if (F4T::DebugLogging::IsEnabled()) {
						LogThrottledInfo(
							"proxy_361_apply_reuse_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
							std::format("[RT-VERIFY 361-D] reuse formId={:08X} stackId={} instanceData={}", target.formId, stackId, haveStack),
							1s);
					}
					reuseFailed = !haveStack;
				}
			}
		}
		if (reuseFailed) {
			LogThrottledWarning(
				"proxy_equipment_instance_data_failed_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
				std::format(
					"Could not build instance data for armor form {:08X} on remote player {}; removing the stale tracked stack.",
					target.formId,
					a_remotePlayer.playerId));
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co361-nullextra/README.md
			a_slot.syncedEquipmentStacks.RememberInstanceDataFailure(InstanceDataFailureKey(target.formId, *target.mods));
			if (F4T::DebugLogging::IsEnabled()) {
				LogThrottledInfo(
					"proxy_361_reuse_memo_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
					std::format("[RT-VERIFY 361-H] reuse instance data failed formId={:08X} stackId={} mods={}; recipe remembered so the next add falls back to the base copy.",
						target.formId, tracked->stackId, F4T::EquipmentState::DescribeMods(*target.mods)),
					1s);
			}
			const auto removal = RemoveTrackedStack(a_slot.syncedEquipmentStacks, a_proxy, *tracked, std::to_string(a_remotePlayer.playerId));
			if (removal.outcome == StackRemoval::kRemoved) {
				allApplied = false;
				continue;
			}
			LogThrottledInfo(
				"proxy_equipment_stale_stack_kept_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
				std::format(
					"[RT-VERIFY 361-F] stale tracked stack for armor form {:08X} on remote player {} was not removed (outcome={}); adding a fresh copy in this pass.",
					target.formId,
					a_remotePlayer.playerId,
					removal.outcome == StackRemoval::kBlocked ? "blocked" : "missing"));
		}
		if (!haveStack) {
			const auto added = AddTrackedArmorCopy(
				a_slot.syncedEquipmentStacks,
				a_proxy,
				*armor,
				target.mods,
				std::to_string(a_remotePlayer.playerId));
			auto chosen = added;
			if (!chosen && target.mods) {
				chosen = AddTrackedArmorCopy(
					a_slot.syncedEquipmentStacks,
					a_proxy,
					*armor,
					nullptr,
					std::to_string(a_remotePlayer.playerId));
			}
			if (!chosen) {
				allApplied = false;
				LogThrottledWarning(
					"proxy_equipment_recipe_failed_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
					std::format(
						"Could not add armor form {:08X} for remote player {}; base copy fallback also failed; this armor is not equipped in this pass.",
						target.formId,
						a_remotePlayer.playerId));
				continue;
			}
			stackId = chosen->stackId;
			instanceData = chosen->instanceData;
		}

		// TODO-verify(#407)
		RE::BGSObjectInstance objectInstance{ armor, instanceData };
		if (F4T::DebugLogging::IsEnabled()) {
			LogThrottledInfo(
				"proxy_361_apply_equip_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
				std::format("[RT-VERIFY 361-E] apply equip formId={:08X} stackId={} instanceData={} mods={} remainingStackIds=[{}]", target.formId, stackId, instanceData != nullptr, target.mods ? F4T::EquipmentState::DescribeMods(*target.mods) : std::string{ "none" }, CensusStacks(a_proxy, target.formId).stackIds),
				1s);
		}
		if (equipManager->EquipObject(
				std::addressof(a_proxy),
				objectInstance,
				stackId,
				1,
				nullptr,
				false,
				true,
				false,
				true,
				false)) {
			changed = true;
		} else {
			allApplied = false;
			LogThrottledWarning(
				"proxy_equipment_equip_failed_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(target.formId),
				std::format(
					"ActorEquipManager failed to equip form {:08X} on proxy {:08X} for remote player {}.",
					target.formId,
					a_proxy.GetFormID(),
					a_remotePlayer.playerId));
		}
	}

	// source:RE-Work/subsystems/commonwealth-online-re/co245-inventory-equipment/README.md (Review corrections for section 9 (G3), R-A, R-G)
	std::uint32_t rightHandWeaponFormId = 0;
	const std::vector<RemoteItemMod>* rightHandMods = nullptr;
	for (const auto& item : a_remotePlayer.equippedItems) {
		if (item.slot == "rightHand") {
			rightHandWeaponFormId = item.formId;
			if (item.mods && !item.mods->empty()) {
				rightHandMods = std::addressof(*item.mods);
			}
			break;
		}
	}

	// TODO(RE): concept-level/unverified; the throwable is assumed to share equip index 0 with the right-hand weapon.
	const auto throwableEntry = F4T::ThrowableSync::FindThrowableEntry(a_remotePlayer.equippedItems);
	const auto* throwableWeapon = throwableEntry ?
		RE::TESForm::GetFormByID<RE::TESObjectWEAP>(throwableEntry->formId) :
		nullptr;
	const bool throwableResolves = throwableWeapon && throwableWeapon->IsThrownWeapon();
	const bool throwableOwnsRightHand = F4T::ThrowableSync::ThrowableOwnsRightHand(throwableEntry, rightHandWeaponFormId, throwableResolves);
	// TODO(RE): concept-level/unverified; an unresolved throwable leaf leaves the proxy hand untouched for this call.
	const bool throwableUnresolved = F4T::ThrowableSync::ThrowableUnresolved(throwableEntry, rightHandWeaponFormId, throwableResolves);
	if (throwableUnresolved) {
		LogThrottledWarning(
			"proxy_weapon_form_missing_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(throwableEntry->formId),
			std::format(
				"Could not apply throwable form {:08X} for remote player {}; no local WEAP form resolved, leaving the proxy weapon untouched.",
				throwableEntry->formId,
				a_remotePlayer.playerId));
	}
	const auto* throwableMods = throwableEntry && throwableEntry->mods ?
		std::addressof(*throwableEntry->mods) : nullptr;

	RE::BGSEquipIndex rightHandIndex;
	rightHandIndex.index = 0;
	RE::BGSObjectInstance currentEquippedItem{ nullptr, nullptr };
	const auto* currentItem = a_proxy.GetEquippedItem(&currentEquippedItem, rightHandIndex);
	const auto currentWeaponFormId =
		currentItem && currentItem->object && currentItem->object->Is(RE::ENUM_FORM_ID::kWEAP)
			? currentItem->object->GetFormID()
			: 0U;

	bool weaponModsStale = false;
	if (rightHandWeaponFormId != 0 && currentWeaponFormId == rightHandWeaponFormId) {
		const auto equipped = F4T::EquipmentInstance::FindEquippedStack(a_proxy, currentWeaponFormId);
		const auto currentMods = equipped.stack ? F4T::EquipmentInstance::ReadStackMods(*equipped.stack) : std::nullopt;
		weaponModsStale = !F4T::EquipmentInstance::SameMods(
			currentMods.value_or(std::vector<RemoteItemMod>{}),
			rightHandMods ? *rightHandMods : std::vector<RemoteItemMod>{});
	}
	const bool weaponDiffers =
		!throwableOwnsRightHand && !throwableUnresolved && (currentWeaponFormId != rightHandWeaponFormId || weaponModsStale);

	if (currentWeaponFormId != 0 && weaponDiffers) {
		if (UnequipSyncedWeapon(*equipManager, a_proxy)) {
			changed = true;
			if (a_proxy.currentProcess) {
				a_proxy.currentProcess->RequestLoadAnimationsForWeaponChange(a_proxy);
			}
		} else {
			allApplied = false;
			LogThrottledWarning(
				"proxy_weapon_unequip_failed_" + std::to_string(a_remotePlayer.playerId),
				std::format(
					"ActorEquipManager failed to unequip weapon {:08X} on proxy {:08X} for remote player {}.",
					currentWeaponFormId,
					a_proxy.GetFormID(),
					a_remotePlayer.playerId));
		}
	}

	if (weaponDiffers) {
		RemoveTrackedModdedStack(a_slot, a_proxy);
	}

	if (rightHandWeaponFormId != 0 && weaponDiffers) {
		auto* weapon = RE::TESForm::GetFormByID<RE::TESObjectWEAP>(rightHandWeaponFormId);
		if (!weapon) {
			allApplied = false;
			LogThrottledWarning(
				"proxy_weapon_form_missing_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(rightHandWeaponFormId),
				std::format(
					"Could not apply weapon form {:08X} for remote player {}; no local WEAP form resolved.",
					rightHandWeaponFormId,
					a_remotePlayer.playerId));
		} else {
			RE::TBO_InstanceData* weaponInstanceData = nullptr;
			bool equipSuccess = false;
			// Constraint: a base weapon copy is added and tracked only when the proxy holds no unmodded stack of that form; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
			bool addBaseCopy = !F4T::EquipmentInstance::FindStack(a_proxy, rightHandWeaponFormId,
				[](const RE::BGSInventoryItem::Stack& a_stack) { return !F4T::EquipmentInstance::ReadStackMods(a_stack); }).stack;
			if (rightHandMods) {
				const auto modded = EquipSyncedWeaponWithMods(
					*equipManager, a_proxy, *weapon, *rightHandMods, std::to_string(a_remotePlayer.playerId));
				equipSuccess = modded.equipped;
				weaponInstanceData = equipSuccess ? modded.instanceData : nullptr;
				if (modded.extra) {
					a_slot.syncedModdedWeaponExtra = modded.extra;
					a_slot.syncedModdedWeaponFormId = rightHandWeaponFormId;
				}
				if (!equipSuccess) {
					RemoveTrackedModdedStack(a_slot, a_proxy);
				}
			}
			if (!equipSuccess) {
				equipSuccess = EquipSyncedWeapon(*equipManager, a_proxy, *weapon, addBaseCopy);
				if (addBaseCopy) {
					const auto added = F4T::EquipmentInstance::FindStack(a_proxy, rightHandWeaponFormId,
						[](const RE::BGSInventoryItem::Stack& a_stack) { return !F4T::EquipmentInstance::ReadStackMods(a_stack); });
					if (added.stack) {
						// Constraint: a tracked stack is identified by form id, stack id and extra pointer; source:docs/code-notes/equipment-state.md (proxy equipment stack ledger).
						a_slot.syncedEquipmentStacks.Track(
							TrackedStack{ rightHandWeaponFormId, added.stackId, added.stack->extra.get() });
						// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-gaps1/README.md
						F4T::DebugLogging::Log("Equipment", "[RT-VERIFY 361-F] weapon base copy tracked formId={:08X} stackId={} extra={:p}", rightHandWeaponFormId, added.stackId, static_cast<const void*>(added.stack->extra.get()));
					}
				}
			}
			LogThrottledInfo(
				"proxy_weapon_equip_attempt_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(rightHandWeaponFormId),
				std::format(
					"EquipSyncedWeapon for remote player {}: weaponFormId={:08X}, proxyActor={:08X}, result={}.",
					a_remotePlayer.playerId,
					rightHandWeaponFormId,
					a_proxy.GetFormID(),
					equipSuccess),
				1s);
		if (equipSuccess) {
			changed = true;
			RE::BGSEquipIndex equipIndex;
			equipIndex.index = 0;
			RE::BGSObjectInstanceT<RE::TESObjectWEAP> weaponInstance{ weapon, weaponInstanceData };
			a_proxy.HandleItemEquip(false);
			a_proxy.AttachWeapon(weaponInstance, equipIndex);
			a_proxy.DoReparentWeapon(weapon, equipIndex, a_remotePlayer.weaponDrawn);
			a_proxy.DrawWeaponMagicHands(a_remotePlayer.weaponDrawn);
			a_proxy.Update3DPosition(true);

			if (a_proxy.currentProcess) {
				a_proxy.currentProcess->RequestLoadAnimationsForWeaponChange(a_proxy);
			}

			const auto weaponArchetype = F4T::WeaponClassification::ClassifyWeapon(weapon);
			LogThrottledInfo(
				"proxy_weapon_equipped_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(rightHandWeaponFormId),
				std::format(
					"Equipped weapon on proxy for remote player {}: weaponFormId={:08X}, archetype={}, proxyActor={:08X}.",
					a_remotePlayer.playerId,
					rightHandWeaponFormId,
					F4T::WeaponClassification::GetArchetypeLabel(weaponArchetype),
					a_proxy.GetFormID()),
				1s);

			weaponVisualAttached = true;
			} else {
				allApplied = false;
				LogThrottledWarning(
					"proxy_weapon_equip_failed_" + std::to_string(a_remotePlayer.playerId) + "_" + std::to_string(rightHandWeaponFormId),
					std::format(
						"ActorEquipManager failed to equip weapon form {:08X} on proxy {:08X} for remote player {}.",
						rightHandWeaponFormId,
						a_proxy.GetFormID(),
						a_remotePlayer.playerId));
			}
		}
	}

	RetainSyncedStacks(a_slot.syncedEquipmentStacks, a_proxy, wantedArmor, rightHandMods, rightHandWeaponFormId, std::to_string(a_remotePlayer.playerId));

	const auto throwablePlan = F4T::ThrowableSync::ResolveApplyPlan(
		throwableEntry,
		rightHandWeaponFormId,
		throwableResolves);
	auto throwableOutcome = F4T::ThrowableSync::ApplyOutcome::kUnchanged;
	if (throwablePlan.action != F4T::ThrowableSync::ApplyAction::kSkip) {
		throwableOutcome = F4T::ThrowableSync::ApplyThrowablePlan(a_proxy, a_slot, throwablePlan, a_remotePlayer.weaponDrawn, throwableMods);
		if (throwableOutcome == F4T::ThrowableSync::ApplyOutcome::kApplied) {
			changed = true;
			if (a_proxy.currentProcess) {
				a_proxy.currentProcess->RequestLoadAnimationsForWeaponChange(a_proxy);
			}
			weaponVisualAttached = true;
		}
	}

	if (allApplied && !throwableUnresolved && throwableOutcome != F4T::ThrowableSync::ApplyOutcome::kFailed) {
		a_slot.hasAppliedEquipmentState = true;
		a_slot.lastAppliedEquipmentItems = a_remotePlayer.equippedItems;
		const auto clearedFailures = a_slot.syncedEquipmentStacks.ForgetInstanceDataFailures();
		if (clearedFailures > 0 && F4T::DebugLogging::IsEnabled()) {
			F4T::DebugLogging::Log("Equipment", "[RT-VERIFY 361-H] instance data failure memo cleared after a successful apply for remote player {}: {} recipes forgotten", a_remotePlayer.playerId, clearedFailures);
		}
	}

	if (changed) {
		if (weaponVisualAttached) {
			F4T::ProxyAnimationSync::ResetAppliedAnimationState(a_slot.appliedAnimationState);
			a_slot.hasLoggedInitialAnimationState = false;
			a_slot.weaponGraphSettleUntil = std::chrono::steady_clock::now() + kProxyWeaponGraphSettleDuration;
		} else {
			RefreshProxyAfterEquipmentChange(a_slot, a_proxy);
		}
		LogInfoWithLocalPlayerPrefix(std::format(
			"Applied synced equipment to runtime proxy actor {:08X} for remote player {}: slots={}, equips={}.",
			a_proxy.GetFormID(),
			a_remotePlayer.playerId,
			a_remotePlayer.equippedItems.size(),
			armorToEquip.size()));
	}
	}
}
