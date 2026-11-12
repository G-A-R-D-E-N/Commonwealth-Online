#include "pch.h"

#include "F4TWeaponClassification.h"

#include "RE/A/Actor.h"
#include "RE/B/BGSEquipIndex.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/W/WEAPON_TYPE.h"

namespace F4T::WeaponClassification
{
	WeaponAnimationType ClassifyWeapon(const RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return WeaponAnimationType::kUnarmed;
		}

		switch (a_weapon->weaponData.type.get()) {
		case RE::WEAPON_TYPE::kHandToHand:
			return WeaponAnimationType::kHandToHand;
		case RE::WEAPON_TYPE::kOneHandDagger:
			return WeaponAnimationType::kMeleeShort;
		case RE::WEAPON_TYPE::kOneHandSword:
		case RE::WEAPON_TYPE::kOneHandAxe:
		case RE::WEAPON_TYPE::kOneHandMace:
			return WeaponAnimationType::kMeleeOneHanded;
		case RE::WEAPON_TYPE::kTwoHandSword:
		case RE::WEAPON_TYPE::kTwoHandAxe:
			return WeaponAnimationType::kMeleeTwoHanded;
		case RE::WEAPON_TYPE::kGun:
			return WeaponAnimationType::kRangedGun;
		case RE::WEAPON_TYPE::kBow:
			return WeaponAnimationType::kRangedBow;
		case RE::WEAPON_TYPE::kStaff:
		case RE::WEAPON_TYPE::kGrenade:
		case RE::WEAPON_TYPE::kMine:
			return WeaponAnimationType::kRangedOther;
		default:
			return WeaponAnimationType::kUnarmed;
		}
	}

	bool IsMeleeArchetype(WeaponAnimationType a_type)
	{
		return a_type == WeaponAnimationType::kMeleeOneHanded ||
		       a_type == WeaponAnimationType::kMeleeTwoHanded ||
		       a_type == WeaponAnimationType::kMeleeShort ||
		       a_type == WeaponAnimationType::kHandToHand ||
		       a_type == WeaponAnimationType::kUnarmed;
	}

	bool IsRangedArchetype(WeaponAnimationType a_type)
	{
		return a_type == WeaponAnimationType::kRangedGun ||
		       a_type == WeaponAnimationType::kRangedBow ||
		       a_type == WeaponAnimationType::kRangedOther;
	}

	bool UseMeleeBehavior(WeaponAnimationType a_type)
	{
		return a_type == WeaponAnimationType::kMeleeOneHanded ||
		       a_type == WeaponAnimationType::kMeleeTwoHanded ||
		       a_type == WeaponAnimationType::kMeleeShort ||
		       a_type == WeaponAnimationType::kHandToHand;
	}

	bool UseWeaponBehavior(WeaponAnimationType a_type)
	{
		return IsRangedArchetype(a_type);
	}

	const char* GetArchetypeLabel(WeaponAnimationType a_type)
	{
		switch (a_type) {
		case WeaponAnimationType::kUnarmed:
			return "unarmed";
		case WeaponAnimationType::kMeleeOneHanded:
			return "melee-1h";
		case WeaponAnimationType::kMeleeTwoHanded:
			return "melee-2h";
		case WeaponAnimationType::kMeleeShort:
			return "melee-short";
		case WeaponAnimationType::kHandToHand:
			return "hand-to-hand";
		case WeaponAnimationType::kRangedGun:
			return "gun";
		case WeaponAnimationType::kRangedBow:
			return "bow";
		case WeaponAnimationType::kRangedOther:
			return "ranged-other";
		default:
			return "unknown";
		}
	}

	WeaponAnimationType ClassifyEquippedRightHandWeapon(RE::Actor& a_actor)
	{
		RE::BGSEquipIndex rightHandIndex;
		rightHandIndex.index = 0;
		RE::BGSObjectInstance equippedItem{ nullptr, nullptr };
		const auto* item = a_actor.GetEquippedItem(&equippedItem, rightHandIndex);
		if (!item || !item->object || !item->object->Is(RE::ENUM_FORM_ID::kWEAP)) {
			return WeaponAnimationType::kUnarmed;
		}

		return ClassifyWeapon(static_cast<const RE::TESObjectWEAP*>(item->object));
	}
}
