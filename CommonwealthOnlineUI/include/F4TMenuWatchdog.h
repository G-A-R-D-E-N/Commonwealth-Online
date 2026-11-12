#pragma once

#include <chrono>

namespace F4T::PrismaUI::Internal
{
	inline constexpr std::chrono::seconds kVanillaMenuUnfocusedLimit{ 2 };

	inline bool VanillaMenuUnfocusedTooLong(bool a_unfocused, std::chrono::steady_clock::time_point& a_since, std::chrono::steady_clock::time_point a_now)
	{
		if (!a_unfocused) {
			a_since = {};
			return false;
		}
		if (a_since == std::chrono::steady_clock::time_point{}) {
			a_since = a_now;
		}
		return a_now - a_since >= kVanillaMenuUnfocusedLimit;
	}
}
