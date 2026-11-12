#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
namespace F4T::ItemHealthSnapshot
{
	[[nodiscard]] inline std::optional<nlohmann::json> Encode(float a_health)
	{
		if (!std::isfinite(a_health)) return std::nullopt;
		return nlohmann::json{ { "version", 1u }, { "healthBits", std::bit_cast<std::uint32_t>(a_health) } };
	}
	[[nodiscard]] inline std::optional<float> Decode(const nlohmann::json& a_value)
	{
		if (!a_value.is_object() || a_value.size() != 2 || !a_value.contains("version") || a_value.at("version") != 1 ||
			!a_value.contains("healthBits") || !a_value.at("healthBits").is_number_unsigned()) return std::nullopt;
		const auto bits = a_value.at("healthBits").get<std::uint64_t>();
		if (bits > (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
		const auto value = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
		return std::isfinite(value) ? std::optional<float>{ value } : std::nullopt;
	}
}
