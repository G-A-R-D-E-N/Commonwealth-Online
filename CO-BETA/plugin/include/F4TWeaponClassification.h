#pragma once

#include "RE/T/TESObjectWEAP.h"
#include "RE/W/WEAPON_TYPE.h"

#include <cstdint>
#include <optional>

namespace F4T::WeaponClassification
{
	enum class WeaponAnimationType
	{
		kUnarmed,
		kMeleeOneHanded,
		kMeleeTwoHanded,
		kMeleeShort,
		kHandToHand,
		kRangedGun,
		kRangedBow,
		kRangedOther
	};

	WeaponAnimationType ClassifyWeapon(const RE::TESObjectWEAP* a_weapon);

	bool IsMeleeArchetype(WeaponAnimationType a_type);

	bool IsRangedArchetype(WeaponAnimationType a_type);

	bool UseMeleeBehavior(WeaponAnimationType a_type);

	bool UseWeaponBehavior(WeaponAnimationType a_type);

	WeaponAnimationType ClassifyEquippedRightHandWeapon(RE::Actor& a_actor);

	const char* GetArchetypeLabel(WeaponAnimationType a_type);
}
