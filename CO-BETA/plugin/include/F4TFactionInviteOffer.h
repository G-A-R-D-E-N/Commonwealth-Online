#pragma once

#include <cstdint>

namespace F4T::FactionInviteOffer
{
	inline constexpr float kActivationRange = 200.0F;
	inline constexpr float kActivationFacingCosine = 0.5F;

	[[nodiscard]] constexpr bool IsActivationAim(float a_distance, float a_facingCosine) noexcept
	{
		return a_distance <= kActivationRange && a_facingCosine >= kActivationFacingCosine;
	}

	[[nodiscard]] constexpr bool ShouldOffer(
		bool a_localCanInvite,
		std::uint32_t a_localPlayerId,
		std::uint32_t a_targetPlayerId,
		bool a_targetInFaction,
		bool a_targetDowned) noexcept
	{
		return a_localCanInvite && a_targetPlayerId != 0 && a_targetPlayerId != a_localPlayerId && !a_targetInFaction && !a_targetDowned;
	}

	void Tick();
}
