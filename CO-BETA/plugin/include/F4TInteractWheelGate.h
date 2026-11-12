#pragma once

namespace F4T::InteractWheelGate
{
	// Constraint: an open interact wheel yields to a trade window, which takes the same input.
	[[nodiscard]] constexpr bool ShouldCloseWheelForTrade(bool a_wheelOpen, bool a_tradeVisible) noexcept
	{
		return a_wheelOpen && a_tradeVisible;
	}

	[[nodiscard]] constexpr bool CanOpenWheel(bool a_menuBlocked, bool a_tradeVisible) noexcept
	{
		return !a_menuBlocked && !a_tradeVisible;
	}
}
