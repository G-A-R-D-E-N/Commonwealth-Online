#pragma once

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <optional>

namespace F4T::JsonFloat
{
	inline std::optional<float> ReadFloat(const nlohmann::json& a_value)
	{
		if (!a_value.is_number()) {
			return std::nullopt;
		}
		const auto value = a_value.get<double>();
		if (!std::isfinite(value) || value < -(std::numeric_limits<float>::max)() || value > (std::numeric_limits<float>::max)()) {
			return std::nullopt;
		}
		return static_cast<float>(value);
	}
}
