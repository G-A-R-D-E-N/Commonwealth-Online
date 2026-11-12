#pragma once

#include <chrono>

namespace F4T::ProxyTransformFreshness
{
	inline constexpr auto kTimeout = std::chrono::seconds{ 10 };

	[[nodiscard]] inline bool IsFresh(
		std::chrono::steady_clock::time_point a_received,
		std::chrono::steady_clock::time_point a_now) noexcept
	{
		return a_received != std::chrono::steady_clock::time_point{} &&
			a_now >= a_received && a_now - a_received <= kTimeout;
	}
}
