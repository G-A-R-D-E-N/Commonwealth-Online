#pragma once

#include <nlohmann/json.hpp>

#include <optional>

namespace F4T::NpcCoopValidation
{
	[[nodiscard]] std::optional<nlohmann::json> NormalizeNpcLoot(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeNpcLootTake(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeNpcLootResult(const nlohmann::json& a_packet);
}
