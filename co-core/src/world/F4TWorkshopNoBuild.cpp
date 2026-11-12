#include "F4TWorkshopNoBuild.h"
#include "F4TJsonFloat.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>

namespace F4T::WorkshopNoBuild
{
	namespace
	{
		using Json = nlohmann::json;
		using F4T::JsonFloat::ReadFloat;

		std::optional<std::uint32_t> ReadU32(const Json& a_value)
		{
			if (a_value.is_number_unsigned()) {
				const auto value = a_value.get<std::uint64_t>();
				if (value <= (std::numeric_limits<std::uint32_t>::max)()) {
					return static_cast<std::uint32_t>(value);
				}
			} else if (a_value.is_number_integer()) {
				const auto value = a_value.get<std::int64_t>();
				if (value >= 0 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
					return static_cast<std::uint32_t>(value);
				}
			}
			return std::nullopt;
		}

		std::optional<Workshop::FormKey> ReadForm(const Json& a_value)
		{
			if (!a_value.is_object()) {
				return std::nullopt;
			}
			const auto plugin = a_value.find("plugin");
			const auto localFormId = a_value.find("localFormId");
			if (plugin == a_value.end() || localFormId == a_value.end() || !plugin->is_string()) {
				return std::nullopt;
			}
			auto name = plugin->get<std::string>();
			const auto id = ReadU32(*localFormId);
			if (name.empty() || name.size() > Workshop::MaximumPluginNameBytes || !id || *id == 0) {
				return std::nullopt;
			}
			return Workshop::FormKey{ std::move(name), *id };
		}
	}

	bool Table::Contains(
		const Workshop::FormKey& a_space,
		float a_x,
		float a_y,
		float a_z,
		const std::optional<Workshop::FormKey>& a_cell,
		const std::optional<Workshop::FormKey>& a_location) const noexcept
	{
		(void)a_z;
		if (!std::isfinite(a_x) || !std::isfinite(a_y)) {
			return false;
		}
		for (const auto& region : m_regions) {
			if (!SameFormKeyIgnoringCase(region.space, a_space)) {
				continue;
			}
			if (region.cell) {
				if (!a_cell || !SameFormKeyIgnoringCase(*a_cell, *region.cell)) {
					continue;
				}
			}
			if (region.location) {
				if (!a_location || !SameFormKeyIgnoringCase(*a_location, *region.location)) {
					continue;
				}
			}
			if (region.hasBounds) {
				if (a_x < region.minX || a_x > region.maxX || a_y < region.minY || a_y > region.maxY) {
					continue;
				}
			}
			return true;
		}
		return false;
	}

	bool Table::LoadFromJson(const Json& a_root)
	{
		m_regions.clear();
		if (a_root.is_null()) {
			return true;
		}
		const auto* regions = &a_root;
		if (a_root.is_object()) {
			const auto found = a_root.find("regions");
			if (found == a_root.end()) {
				return false;
			}
			regions = &(*found);
		}
		if (!regions->is_array()) {
			return false;
		}
		std::vector<Region> loaded;
		for (const auto& entry : *regions) {
			if (!entry.is_object()) {
				return false;
			}
			const auto space = entry.find("space");
			if (space == entry.end()) {
				return false;
			}
			const auto form = ReadForm(*space);
			if (!form) {
				return false;
			}
			Region region;
			region.space = *form;
			if (const auto cell = entry.find("cell"); cell != entry.end()) {
				const auto value = ReadForm(*cell);
				if (!value) {
					return false;
				}
				region.cell = *value;
			}
			if (const auto location = entry.find("location"); location != entry.end()) {
				const auto value = ReadForm(*location);
				if (!value) {
					return false;
				}
				region.location = *value;
			}
			const auto minX = entry.find("minX");
			const auto minY = entry.find("minY");
			const auto maxX = entry.find("maxX");
			const auto maxY = entry.find("maxY");
			const bool anyBound = minX != entry.end() || minY != entry.end() || maxX != entry.end() || maxY != entry.end();
			if (anyBound) {
				if (minX == entry.end() || minY == entry.end() || maxX == entry.end() || maxY == entry.end()) {
					return false;
				}
				const auto x0 = ReadFloat(*minX);
				const auto y0 = ReadFloat(*minY);
				const auto x1 = ReadFloat(*maxX);
				const auto y1 = ReadFloat(*maxY);
				if (!x0 || !y0 || !x1 || !y1 || *x1 < *x0 || *y1 < *y0) {
					return false;
				}
				region.minX = *x0;
				region.minY = *y0;
				region.maxX = *x1;
				region.maxY = *y1;
				region.hasBounds = true;
			}
			if (!region.hasBounds && !region.cell && !region.location) {
				return false;
			}
			loaded.push_back(std::move(region));
		}
		m_regions = std::move(loaded);
		return true;
	}
}
