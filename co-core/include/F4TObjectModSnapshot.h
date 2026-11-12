#pragma once

#include "F4TInventoryContract.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace F4T::ObjectModSnapshot
{
	inline constexpr std::size_t MaximumEntries = 2048;
	struct Entry
	{
		std::string itemKey;
		std::uint8_t index{};
		std::uint8_t rank{};
		std::uint8_t disabled{};
		bool operator==(const Entry&) const = default;
	};

	template <class Source, std::size_t Extent, class Resolve>
	[[nodiscard]] std::optional<std::vector<Entry>> Capture(std::span<Source, Extent> a_source, Resolve&& a_resolve)
	{
		if (a_source.size() > MaximumEntries) return std::nullopt;
		std::vector<Entry> result;
		result.reserve(a_source.size());
		for (const auto& source : a_source) {
			auto key = a_resolve(source.objectID);
			if (!key || !F4T::InventoryContract::ParseItemKey(*key)) return std::nullopt;
			result.push_back(Entry{ std::move(*key), source.index, source.rank, source.disabled });
		}
		return result;
	}
}
