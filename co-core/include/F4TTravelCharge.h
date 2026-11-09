#pragma once

#include <cstdint>

namespace F4T::TravelCharge
{
	[[nodiscard]] constexpr std::int32_t ResolveCost(bool a_isAdmin, std::int32_t a_cost) noexcept
	{
		return a_isAdmin ? 0 : a_cost;
	}
}
