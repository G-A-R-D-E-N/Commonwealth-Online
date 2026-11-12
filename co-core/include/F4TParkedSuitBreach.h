#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace F4T::ParkedSuit::Breach
{
	inline constexpr std::size_t kGridSize = 6;
	inline constexpr std::size_t kMaximumLength = 4;
	inline constexpr std::size_t kMaximumPicks = kMaximumLength + 1;

	struct Pick
	{
		std::size_t row{};
		std::size_t col{};
	};

	struct Grid
	{
		std::array<std::array<std::string, kGridSize>, kGridSize> cells;
		std::vector<std::string> target;
	};

	[[nodiscard]] Grid Generate(std::uint32_t a_seed, std::size_t a_length);
	[[nodiscard]] bool Solves(const Grid& a_grid, std::size_t a_length, const std::vector<Pick>& a_picks);
}
