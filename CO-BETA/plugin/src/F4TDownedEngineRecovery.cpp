#include "pch.h"

#include "F4TDownedEngine.h"

#include "F4TDebugLogging.h"
#include "F4TDownedScript.h"
#include "F4TEquipmentInstance.h"
#include "F4TThrowableSync.h"

#include "RE/A/ActorEquipManager.h"
#include "RE/A/AIProcess.h"
#include "RE/B/BGSEquipIndex.h"
#include "RE/B/BGSInventoryItem.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/E/ExtraInstanceData.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESForm.h"

namespace
{
	constexpr std::string_view kLogComponent{ "Downed" };

	bool g_sessionFlagsApplied = false;
	bool g_sessionEndGiveUpCleared = false;
	F4T::Downed::BleedoutRecoveryRunner g_bleedoutRecovery;

	constexpr std::uint32_t kMaximumReequipAttempts = 5;
	constexpr double kReequipRetrySeconds = 0.5;
	inline constexpr REL::ID kEssentialDown{ 513258, 2230033, 2230033 } /* E:/RE-work/F4RR-main-20261023/RE-Work/subsystems/commonwealth-online-re/downed-persist-co309/README.md */;

	struct HeldWeapon
	{
		std::uint32_t formId{ 0 };
		std::uint32_t stackId{ 0 };
	};

	struct PendingReequip
	{
		HeldWeapon weapon;
		std::uint32_t attempts{ 0 };
		double nextSeconds{ 0.0 };
	};

	std::optional<PendingReequip> g_reequip;

	std::optional<HeldWeapon> FindRightHandWeapon(RE::Actor& a_actor)
	{
		RE::BGSEquipIndex rightHand;
		rightHand.index = F4T::ThrowableSync::kRightHandEquipIndex;
		RE::BGSObjectInstance equipped{ nullptr, nullptr };
		const auto* item = a_actor.GetEquippedItem(&equipped, rightHand);
		if (!item || !item->object || !item->object->Is(RE::ENUM_FORM_ID::kWEAP)) {
			return std::nullopt;
		}
		const auto formId = item->object->GetFormID();
		const auto found = F4T::EquipmentInstance::FindEquippedStack(a_actor, formId);
		if (!found.stack) {
			return std::nullopt;
		}
		return HeldWeapon{ formId, found.stackId };
	}

	bool ResolveWeaponInstance(RE::Actor& a_actor, const HeldWeapon& a_weapon, RE::TBO_InstanceData*& a_instance)
	{
		const auto found = F4T::EquipmentInstance::FindStackIndexed(
			a_actor,
			a_weapon.formId,
			[&](const RE::BGSInventoryItem::Stack&, std::uint32_t a_id) { return a_id == a_weapon.stackId; });
		if (!found.stack) {
			return false;
		}
		a_instance = nullptr;
		if (found.stack->extra) {
			if (auto* extra = found.stack->extra->GetByType<RE::ExtraInstanceData>()) {
				a_instance = extra->data.get();
			}
		}
		return true;
	}

	bool EquipWeaponStack(RE::Actor& a_actor, const HeldWeapon& a_weapon)
	{
		auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(a_weapon.formId);
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		RE::TBO_InstanceData* instance = nullptr;
		if (!object || !equipManager || !ResolveWeaponInstance(a_actor, a_weapon, instance)) {
			return false;
		}
		const RE::BGSObjectInstance objectInstance{ object, instance };
		return equipManager->EquipObject(&a_actor, objectInstance, a_weapon.stackId, 1, nullptr, false, false, true, true, false);
	}

	// Invariant: an unequip can merge stacks, so a failed equip re-finds an unequipped stack of the same form before giving up.
	bool EquipWeapon(RE::Actor& a_actor, HeldWeapon& a_weapon)
	{
		if (EquipWeaponStack(a_actor, a_weapon)) {
			return true;
		}
		const auto refound = F4T::EquipmentInstance::FindStackIndexed(
			a_actor,
			a_weapon.formId,
			[](const RE::BGSInventoryItem::Stack& a_stack, std::uint32_t) { return !a_stack.IsEquipped(); });
		if (!refound.stack || refound.stackId == a_weapon.stackId) {
			return false;
		}
		a_weapon.stackId = refound.stackId;
		return EquipWeaponStack(a_actor, a_weapon);
	}

	bool UnequipWeapon(RE::Actor& a_actor, const HeldWeapon& a_weapon)
	{
		auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(a_weapon.formId);
		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		RE::TBO_InstanceData* instance = nullptr;
		if (!object || !equipManager || !ResolveWeaponInstance(a_actor, a_weapon, instance)) {
			return false;
		}
		const RE::BGSObjectInstance objectInstance{ object, instance };
		return equipManager->UnequipObject(&a_actor, &objectInstance, 1, nullptr, a_weapon.stackId, false, false, true, true, nullptr);
	}
}

namespace F4T::Downed::Engine
{
	void ForgetPendingReequip()
	{
		if (!g_reequip) {
			return;
		}
		auto* player = LocalPlayer();
		if (player && CanTouchWorld()) {
			const auto current = FindRightHandWeapon(*player);
			const bool back = current && current->formId == g_reequip->weapon.formId;
			const bool requested = !back && EquipWeapon(*player, g_reequip->weapon);
			F4T::DebugLogging::Log(
				kLogComponent,
				"weapon re-equip dropped with the session: item={:08X} alreadyBack={} finalRequest={}",
				g_reequip->weapon.formId,
				back,
				requested);
		}
		g_reequip.reset();
	}

	void ZeroEssentialDownTimer(RE::Actor& a_actor)
	{
		if (a_actor.currentProcess) {
			a_actor.currentProcess->essentialDownTimer = 0.0F;
		}
	}

	void ExitEssentialDown(RE::Actor& a_actor)
	{
		static REL::Relocation<void (*)(RE::Actor*, bool)> exitEssentialDown{ kEssentialDown };
		exitEssentialDown(&a_actor, false);
		LogLocalBleedoutState("essential-down exit");
	}

	void ResetHeldWeaponState(RE::Actor& a_actor)
	{
		LogLocalBleedoutState("weapon reset before");
		LogWeaponState(a_actor, "weapon reset before");
		if (g_reequip) {
			LogInfo("weapon reset skipped: a re-equip is still pending.");
			return;
		}
		const auto held = FindRightHandWeapon(a_actor);
		if (!held) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"weapon reset skipped: no weapon in the right hand (weaponDrawn={} weaponState={})",
				a_actor.GetWeaponMagicDrawn(),
				static_cast<std::int32_t>(a_actor.weaponState));
			return;
		}
		const auto unequipped = UnequipWeapon(a_actor, *held);
		if (!unequipped) {
			F4T::DebugLogging::Log(kLogComponent, "weapon reset skipped: item={:08X} could not be unequipped", held->formId);
			return;
		}
		g_reequip = PendingReequip{ *held, 1, NowSeconds() + kReequipRetrySeconds };
		const auto equipped = EquipWeapon(a_actor, g_reequip->weapon);
		F4T::DebugLogging::Log(
			kLogComponent,
			"weapon reset after the forced exit: item={:08X} stack={} unequipped=true reequipRequested={} weaponDrawn={}",
			held->formId,
			g_reequip->weapon.stackId,
			equipped,
			a_actor.GetWeaponMagicDrawn());
		LogWeaponState(a_actor, "weapon reset requested");
	}

	void RetryWeaponReequip(RE::Actor& a_actor, double a_nowSeconds)
	{
		if (!g_reequip || a_nowSeconds < g_reequip->nextSeconds) {
			return;
		}
		const auto current = FindRightHandWeapon(a_actor);
		if (current && current->formId == g_reequip->weapon.formId) {
			F4T::DebugLogging::Log(kLogComponent, "weapon re-equip confirmed: item={:08X}", current->formId);
			LogWeaponState(a_actor, "weapon re-equip confirmed");
			g_reequip.reset();
			return;
		}
		if (g_reequip->attempts >= kMaximumReequipAttempts) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"weapon re-equip gave up after {} attempts: item={:08X} is not in the right hand",
				g_reequip->attempts,
				g_reequip->weapon.formId);
			g_reequip.reset();
			return;
		}
		++g_reequip->attempts;
		g_reequip->nextSeconds = a_nowSeconds + kReequipRetrySeconds;
		const auto equipped = EquipWeapon(a_actor, g_reequip->weapon);
		F4T::DebugLogging::Log(
			kLogComponent,
			"weapon re-equip retry {}: item={:08X} requested={}",
			g_reequip->attempts,
			g_reequip->weapon.formId,
			equipped);
	}

	void ApplySessionFlags(bool a_apply)
	{
		if (g_sessionFlagsApplied == a_apply) {
			return;
		}
		auto* player = LocalPlayer();
		if (!player) {
			return;
		}
		if (a_apply) {
			g_sessionEndGiveUpCleared = false;
			player->boolFlags.reset(
				RE::Actor::BOOL_FLAGS::kMovementBlocked,
				RE::Actor::BOOL_FLAGS::kAttackingDisabled);
			if (!SetDownedFlags(*player, true)) {
				return;
			}
		} else {
			if (!CanTouchWorld()) {
				return;
			}
			SetControlBlocked(*player, false);
			if (IsEssentialDown(*player)) {
				if (!g_sessionEndGiveUpCleared) {
					ClearBleedoutGiveUp();
					g_sessionEndGiveUpCleared = true;
				}
				RequestBleedoutRecovery();
				UpdateBleedoutRecovery(NowSeconds());
				return;
			}
			if (!SetPose(*player, false)) {
				return;
			}
			if (!SetDownedFlags(*player, false)) {
				return;
			}
			g_bleedoutRecovery.Reset();
			g_sessionEndGiveUpCleared = false;
		}
		g_sessionFlagsApplied = a_apply;
		F4T::DebugLogging::Log(kLogComponent, "session essential/bleedout flags applied={}", a_apply);
	}

	void RequestBleedoutRecovery()
	{
		g_bleedoutRecovery.Request();
	}

	void RequestCameraRestore()
	{
		g_bleedoutRecovery.RequestCameraRestore();
	}

	void RequestWeaponReset()
	{
		g_bleedoutRecovery.RequestWeaponReset();
	}

	void ClearPostRecoveryState()
	{
		g_bleedoutRecovery.ClearPostRecovery();
		ForgetCamera();
		ForgetPendingReequip();
	}

	void CancelBleedoutRecovery()
	{
		g_bleedoutRecovery.ClearPostRecovery();
		g_bleedoutRecovery.ClearGaveUp();
		auto* player = LocalPlayer();
		g_bleedoutRecovery.Cancel(NowSeconds(), player && CanTouchWorld(), [player](bool a_noRecovery) {
			return F4T::Downed::Script::CallActorBool(*player, "SetNoBleedoutRecovery", a_noRecovery);
		});
	}

	void ClearBleedoutGiveUp()
	{
		g_bleedoutRecovery.ClearGaveUp();
	}

	void UpdateBleedoutRecovery(double a_nowSeconds)
	{
		auto* player = LocalPlayer();
		const bool canTouch = player && CanTouchWorld();
		if (canTouch && player->IsDead(true)) {
			CancelBleedoutRecovery();
			return;
		}
		const bool essentialDown = canTouch && IsEssentialDown(*player);
		const auto attemptsBefore = g_bleedoutRecovery.Attempts();
		const auto step = g_bleedoutRecovery.Update(
			a_nowSeconds, canTouch, essentialDown, g_sessionFlagsApplied, [player](bool a_noRecovery) {
				return F4T::Downed::Script::CallActorBool(*player, "SetNoBleedoutRecovery", a_noRecovery);
			});
		switch (step) {
		case F4T::Downed::RecoveryStep::kStoodUp:
			SetPose(*player, false);
			break;
		case F4T::Downed::RecoveryStep::kGaveUp:
			LogLocalBleedoutState("gave up");
			if (IsEssentialDown(*player)) {
				ExitEssentialDown(*player);
				if (IsEssentialDown(*player)) {
					player->Resurrect(false, true);
					ReseatLocalPlayer();
				}
				RequestWeaponReset();
			}
			SetPose(*player, false);
			LogInfo("essential-down recovery reached the dispatch cap; gave up.");
			LogLocalBleedoutState("gave up after exit");
			break;
		case F4T::Downed::RecoveryStep::kDispatched:
			ZeroEssentialDownTimer(*player);
			if (attemptsBefore == 0) {
				F4T::DebugLogging::Log(kLogComponent, "essential-down local player: NoBleedoutRecovery clear queued attempt=1");
			}
			break;
		default:
			break;
		}
		if (!canTouch) {
			return;
		}
		const bool settledDown = IsEssentialDown(*player);
		const bool inBleedoutAnimation = IsInBleedoutAnimation(*player);
		if (g_bleedoutRecovery.ConsumeWeaponReset(a_nowSeconds, canTouch, settledDown, inBleedoutAnimation)) {
			ResetHeldWeaponState(*player);
		}
		if (!settledDown && !inBleedoutAnimation) {
			RetryWeaponReequip(*player, a_nowSeconds);
		}
		switch (g_bleedoutRecovery.ConsumeCameraRestore(
			a_nowSeconds, canTouch, settledDown, inBleedoutAnimation, IsCameraInBleedout())) {
		case F4T::Downed::CameraRestoreStep::kRestore:
			RestoreBleedoutCamera();
			break;
		case F4T::Downed::CameraRestoreStep::kNotNeeded:
			LogLocalBleedoutState("camera restore not needed");
			break;
		default:
			break;
		}
	}
}
