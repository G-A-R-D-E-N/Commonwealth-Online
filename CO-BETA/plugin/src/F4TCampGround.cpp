#include "pch.h"

#include "F4TCampGround.h"

#include "RE/B/bhkPickData.h"
#include "RE/C/COL_LAYER.h"
#include "RE/N/NiPoint3.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectCELL.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace F4T::CampGround
{
	namespace
	{
		constexpr float kProbeAboveUnits = 128.0F;
		constexpr float kProbeBelowUnits = 512.0F;
		constexpr float kMaxRaiseUnits = 32.0F;
	}

	std::optional<Surface> SurfaceAt(float a_x, float a_y, float a_z)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!cell || !cell->IsAttached()) {
			return std::nullopt;
		}
		RE::bhkPickData pick;
		pick.SetStartEnd(
			RE::NiPoint3{ a_x, a_y, a_z + kProbeAboveUnits },
			RE::NiPoint3{ a_x, a_y, a_z - kProbeBelowUnits });
		pick.castQuery.m_filterData.m_collisionFilterInfo = static_cast<std::uint32_t>(RE::COL_LAYER::kDroppingPick);
		if (!cell->Pick(pick) || !pick.HasHit()) {
			return std::nullopt;
		}
		const float surface = a_z + kProbeAboveUnits - (kProbeAboveUnits + kProbeBelowUnits) * pick.GetHitFraction();
		if (surface > a_z + kMaxRaiseUnits) {
			return std::nullopt;
		}
		const float upward = std::clamp(std::fabs(pick.result.normal[2]), 0.0F, 1.0F);
		return Surface{ surface, std::acos(upward) * 180.0F / std::numbers::pi_v<float> };
	}
}
