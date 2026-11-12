#pragma once

#include <nlohmann/json.hpp>

#include <optional>

namespace F4T::AuthoritativePacketValidation
{
	[[nodiscard]] std::optional<nlohmann::json> NormalizeWorldState(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeNpcState(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeCombatHit(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeNpcHit(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeNpcDamage(const nlohmann::json& a_packet);
}
