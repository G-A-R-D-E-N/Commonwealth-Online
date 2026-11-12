#include "F4TWorkshopMaterials.h"

#include <array>
#include <cstddef>

namespace F4T::WorkshopMaterials
{
	namespace
	{
		constexpr float kDoorHpScale{ 0.75F };

		constexpr std::array<float, 4> kTierMaxHp{ 300.0F, 600.0F, 1000.0F, 1600.0F };

		constexpr std::array<std::array<float, 5>, 4> kMaterialMultipliers{ {
			{ 1.0F, 0.6F, 0.6F, 0.6F, 1.5F },
			{ 0.2F, 0.4F, 0.4F, 0.5F, 1.2F },
			{ 0.05F, 0.1F, 0.15F, 0.2F, 1.0F },
			{ 0.0F, 0.05F, 0.1F, 0.1F, 0.8F },
		} };

		[[nodiscard]] std::size_t TierIndex(MaterialTier a_tier) noexcept
		{
			return static_cast<std::size_t>(a_tier);
		}

		[[nodiscard]] std::size_t WeaponIndex(WeaponKind a_weaponKind) noexcept
		{
			return static_cast<std::size_t>(a_weaponKind);
		}
	}

	float MaxHpForTier(MaterialTier a_tier) noexcept
	{
		return kTierMaxHp[TierIndex(a_tier)];
	}

	float MaxHpForDoor(MaterialTier a_tier) noexcept
	{
		return MaxHpForTier(a_tier) * kDoorHpScale;
	}

	float MaterialMultiplier(MaterialTier a_tier, WeaponKind a_weaponKind) noexcept
	{
		return kMaterialMultipliers[TierIndex(a_tier)][WeaponIndex(a_weaponKind)];
	}

	float DamageForHit(MaterialTier a_tier, WeaponKind a_weaponKind, float a_baseDamage) noexcept
	{
		if (a_baseDamage <= 0.0F) {
			return 0.0F;
		}
		return a_baseDamage * MaterialMultiplier(a_tier, a_weaponKind);
	}
}
