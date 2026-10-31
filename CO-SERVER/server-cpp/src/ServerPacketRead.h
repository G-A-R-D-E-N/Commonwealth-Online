#pragma once

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server
{
	inline std::optional<std::string> ReadString(const nlohmann::json& a_packet, std::string_view a_name)
	{
		const auto found = a_packet.find(a_name);
		if (found == a_packet.end() || !found->is_string()) {
			return std::nullopt;
		}
		return found->get<std::string>();
	}

	inline std::optional<std::uint32_t> ReadUInt32(const nlohmann::json& a_packet, std::string_view a_name)
	{
		const auto found = a_packet.find(a_name);
		if (found == a_packet.end() || found->is_boolean()) {
			return std::nullopt;
		}
		try {
			if (found->is_number_unsigned()) {
				const auto value = found->get<std::uint64_t>();
				if (value <= (std::numeric_limits<std::uint32_t>::max)()) {
					return static_cast<std::uint32_t>(value);
				}
			}
			if (found->is_number_integer()) {
				const auto value = found->get<std::int64_t>();
				if (value >= 0 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
					return static_cast<std::uint32_t>(value);
				}
			}
		} catch (...) {
		}
		return std::nullopt;
	}

	inline std::optional<double> ReadDouble(const nlohmann::json& a_packet, std::string_view a_name)
	{
		const auto found = a_packet.find(a_name);
		if (found == a_packet.end() || found->is_boolean() || !found->is_number()) {
			return std::nullopt;
		}
		try {
			const auto value = found->get<double>();
			return std::isfinite(value) ? std::optional{ value } : std::nullopt;
		} catch (...) {
			return std::nullopt;
		}
	}
}
