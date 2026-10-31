#pragma once

#include <nlohmann/json.hpp>

#include <optional>

namespace F4T::PowerArmorValidation
{
	[[nodiscard]] std::optional<nlohmann::json> NormalizePowerArmor(const nlohmann::json& a_value);
}
