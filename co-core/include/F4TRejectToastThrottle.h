#pragma once

#include <chrono>
#include <optional>

namespace F4T::RejectToastThrottle
{
	inline constexpr std::chrono::seconds kBurstWindow{ 2 };

	struct Gate
	{
		[[nodiscard]] bool ShouldShow(std::chrono::steady_clock::time_point a_now) noexcept
		{
			if (lastShown && a_now - *lastShown < kBurstWindow) {
				return false;
			}
			lastShown = a_now;
			return true;
		}

		std::optional<std::chrono::steady_clock::time_point> lastShown;
	};
}
