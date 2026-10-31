#pragma once

#include <nlohmann/json.hpp>

#include <optional>

namespace F4T::PlayerStateValidation
{
	[[nodiscard]] std::optional<nlohmann::json> NormalizePlayerState(const nlohmann::json& a_packet);
}
