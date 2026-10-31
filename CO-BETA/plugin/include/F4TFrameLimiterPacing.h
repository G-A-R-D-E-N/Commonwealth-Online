#pragma once

#include <cstdint>

namespace F4T::FrameLimiterPacing
{
	inline std::int64_t BlockNs(std::int64_t a_remainingNs, std::int64_t a_spinNs) noexcept
	{
		return a_remainingNs > a_spinNs ? a_remainingNs - a_spinNs : 0;
	}

	inline std::int64_t NextAnchorNs(std::int64_t a_anchorNs, std::int64_t a_periodNs, std::int64_t a_nowNs) noexcept
	{
		const auto deadlineNs = a_anchorNs + a_periodNs;
		return a_nowNs - deadlineNs >= a_periodNs ? a_nowNs : deadlineNs;
	}

	inline bool ShouldCap(bool a_playerPresent, bool a_loadingMenuOpen, bool a_mainMenuOpen) noexcept
	{
		return a_playerPresent && !a_loadingMenuOpen && !a_mainMenuOpen;
	}
}
