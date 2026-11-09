#pragma once

#include <cstdint>

namespace F4T::WorkshopMaterials
{
	enum class MaterialTier : std::uint8_t
	{
		Wood,
		ScrapMetal,
		Concrete,
		Reinforced
	};

	enum class WeaponKind : std::uint8_t
	{
		Melee,
		Light,
		Rifle,
		Heavy,
		Explosive
	};

	[[nodiscard]] float MaxHpForTier(MaterialTier a_tier) noexcept;
	[[nodiscard]] float MaxHpForDoor(MaterialTier a_tier) noexcept;
	[[nodiscard]] float MaterialMultiplier(MaterialTier a_tier, WeaponKind a_weaponKind) noexcept;
	[[nodiscard]] float DamageForHit(MaterialTier a_tier, WeaponKind a_weaponKind, float a_baseDamage) noexcept;
}
