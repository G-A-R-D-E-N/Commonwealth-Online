#pragma once

#include <nlohmann/json.hpp>

#include <optional>

namespace F4T::TransformValidation
{
	[[nodiscard]] std::optional<nlohmann::json> NormalizeTransform(const nlohmann::json& a_packet);
}
