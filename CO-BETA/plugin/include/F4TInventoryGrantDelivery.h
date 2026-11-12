#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace F4T::InventoryGrantDelivery
{
	template <class Resolve, class Add>
	bool Apply(const nlohmann::json& a_items, Resolve&& a_resolve, Add&& a_add)
	{
		using Object = std::invoke_result_t<Resolve, const std::string&>;
		std::vector<std::pair<Object, std::uint32_t>> resolved;
		for (const auto& [key, item] : a_items.items()) {
			auto object = a_resolve(key);
			if (!object) return false;
			resolved.emplace_back(object, item.at("count").get<std::uint32_t>());
		}
		for (const auto& [object, count] : resolved) a_add(object, count);
		return true;
	}
}