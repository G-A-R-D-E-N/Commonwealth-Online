#include "F4TParkedSuitBreach.h"

namespace F4T::ParkedSuit::Breach
{
	namespace
	{
		constexpr std::array<const char*, 8> kCodes{ "1C", "55", "BD", "E9", "7A", "FF", "04", "C3" };
	}

	Grid Generate(std::uint32_t a_seed, std::size_t a_length)
	{
		std::uint32_t state = a_seed;
		const auto draw = [&state](std::size_t a_bound) {
			state = state * 1103515245U + 12345U;
			return static_cast<std::size_t>((state >> 16) % a_bound);
		};

		Grid grid;
		for (auto& row : grid.cells) {
			for (auto& cell : row) {
				cell = kCodes[draw(kCodes.size())];
			}
		}

		std::vector<Pick> path{ Pick{ 0, draw(kGridSize) } };
		while (path.size() < a_length) {
			const auto last = path.back();
			if (path.size() % 2 == 1) {
				auto stepRow = draw(kGridSize - 1);
				if (stepRow >= last.row) ++stepRow;
				path.push_back(Pick{ stepRow, last.col });
			} else {
				auto stepCol = draw(kGridSize - 1);
				if (stepCol >= last.col) ++stepCol;
				path.push_back(Pick{ last.row, stepCol });
			}
		}

		for (const auto& point : path) {
			grid.target.push_back(grid.cells[point.row][point.col]);
		}
		return grid;
	}

	bool Solves(const Grid& a_grid, std::size_t a_length, const std::vector<Pick>& a_picks)
	{
		if (a_picks.empty() || a_picks.size() > a_length + 1 || a_grid.target.size() != a_length) {
			return false;
		}

		std::array<std::array<bool, kGridSize>, kGridSize> used{};
		bool rowPhase = true;
		std::size_t line = 0;
		std::size_t matched = 0;
		for (std::size_t index = 0; index < a_picks.size(); ++index) {
			const auto& pick = a_picks[index];
			if (pick.row >= kGridSize || pick.col >= kGridSize || used[pick.row][pick.col] || (rowPhase ? pick.row : pick.col) != line) {
				return false;
			}
			used[pick.row][pick.col] = true;
			if (matched < a_length && a_grid.cells[pick.row][pick.col] == a_grid.target[matched]) {
				++matched;
			}
			if (matched == a_length) {
				return index + 1 == a_picks.size();
			}
			line = rowPhase ? pick.col : pick.row;
			rowPhase = !rowPhase;
		}
		return false;
	}
}
