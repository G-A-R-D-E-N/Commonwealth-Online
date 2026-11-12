#pragma once

#include <cstdint>
#include <span>

namespace F4T::Authority
{
	[[nodiscard]] inline std::uint32_t ElectSticky(std::uint32_t a_incumbent, std::span<const std::uint32_t> a_eligible) noexcept
	{
		if (a_incumbent != 0) {
			for (const auto candidate : a_eligible) {
				if (candidate == a_incumbent) {
					return a_incumbent;
				}
			}
		}

		std::uint32_t best = 0;
		for (const auto candidate : a_eligible) {
			if (candidate != 0 && (best == 0 || candidate < best)) {
				best = candidate;
			}
		}
		return best;
	}
}
