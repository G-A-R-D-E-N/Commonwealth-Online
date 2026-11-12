#pragma once

#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace F4T::WorkshopNoBuild
{
	struct Region
	{
		Workshop::FormKey space;
		float minX{};
		float minY{};
		float maxX{};
		float maxY{};
		bool hasBounds{};
		std::optional<Workshop::FormKey> cell;
		std::optional<Workshop::FormKey> location;
	};

	class Table
	{
	public:
		[[nodiscard]] bool Empty() const noexcept { return m_regions.empty(); }
		[[nodiscard]] std::size_t Size() const noexcept { return m_regions.size(); }
		[[nodiscard]] std::vector<Workshop::FormKey> Spaces() const
		{
			std::vector<Workshop::FormKey> spaces;
			spaces.reserve(m_regions.size());
			for (const auto& region : m_regions) {
				spaces.push_back(region.space);
			}
			return spaces;
		}
		[[nodiscard]] bool Contains(
			const Workshop::FormKey& a_space,
			float a_x,
			float a_y,
			float a_z,
			const std::optional<Workshop::FormKey>& a_cell = std::nullopt,
			const std::optional<Workshop::FormKey>& a_location = std::nullopt) const noexcept;
		[[nodiscard]] bool LoadFromJson(const nlohmann::json& a_root);

	private:
		std::vector<Region> m_regions;
	};
}
