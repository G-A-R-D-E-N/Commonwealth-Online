#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace F4T::ProxyCellRelease
{
	template <class Slots>
	[[nodiscard]] std::vector<std::uint32_t> PendingCells(Slots& a_slots, std::uint32_t a_currentCell)
	{
		std::vector<std::uint32_t> cells;
		for (auto& [_, slot] : a_slots) {
			if (!slot.pendingCellReleaseFrom) {
				continue;
			}
			if (*slot.pendingCellReleaseFrom == a_currentCell) {
				slot.pendingCellReleaseFrom.reset();
				continue;
			}
			if (std::ranges::find(cells, *slot.pendingCellReleaseFrom) == cells.end()) {
				cells.push_back(*slot.pendingCellReleaseFrom);
			}
		}
		return cells;
	}
}
