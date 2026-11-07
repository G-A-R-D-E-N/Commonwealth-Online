#pragma once

#include <nlohmann/json.hpp>

#include <optional>

namespace F4T::InventorySnapshotRead
{
	[[nodiscard]] std::optional<nlohmann::json> ReadLocalItems();
}
