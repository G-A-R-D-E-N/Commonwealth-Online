#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace F4T::InventoryWire
{
	inline constexpr std::size_t kMaximumTradeLines = 16;

	[[nodiscard]] std::optional<nlohmann::json> NormalizeContainerRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeContainerResult(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeTradeRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeTradeResult(const nlohmann::json& a_packet);
}
