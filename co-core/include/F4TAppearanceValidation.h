#pragma once

#include <nlohmann/json.hpp>

#include <optional>

namespace F4T::AppearanceValidation
{
	[[nodiscard]] std::optional<nlohmann::json> NormalizeAppearance(const nlohmann::json& a_value);
}
