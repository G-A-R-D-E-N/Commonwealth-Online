#include "F4TGnsProtocolAdapterJson.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <limits>

namespace F4T::GnsProtocolAdapter::Detail
{
	std::optional<std::string> ReadString(const nlohmann::json& a_packet, const char* a_field)
	{
		const auto iterator = a_packet.find(a_field);
		if (iterator == a_packet.end() || !iterator->is_string()) {
			return std::nullopt;
		}
		return iterator->get<std::string>();
	}

	std::optional<bool> ReadBoolean(const nlohmann::json& a_packet, const char* a_field)
	{
		const auto iterator = a_packet.find(a_field);
		if (iterator == a_packet.end() || !iterator->is_boolean()) {
			return std::nullopt;
		}
		return iterator->get<bool>();
	}

	std::optional<std::uint64_t> ReadUnsigned64(const nlohmann::json& a_packet, const char* a_field)
	{
		const auto iterator = a_packet.find(a_field);
		if (iterator == a_packet.end()) {
			return std::nullopt;
		}

		try {
			if (iterator->is_number_unsigned()) {
				return iterator->get<std::uint64_t>();
			}
			if (iterator->is_number_integer()) {
				const auto value = iterator->get<std::int64_t>();
				if (value >= 0) {
					return static_cast<std::uint64_t>(value);
				}
			}
			if (iterator->is_string()) {
				const auto text = iterator->get<std::string>();
				std::uint64_t value = 0;
				const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
				if (error == std::errc{} && end == text.data() + text.size()) {
					return value;
				}
			}
		} catch (...) {
		}
		return std::nullopt;
	}

	std::optional<std::uint32_t> ReadUnsigned32(const nlohmann::json& a_packet, const char* a_field)
	{
		const auto value = ReadUnsigned64(a_packet, a_field);
		if (!value || *value > (std::numeric_limits<std::uint32_t>::max)()) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>(*value);
	}

	std::optional<double> ReadPositiveFiniteDouble(const nlohmann::json& a_packet, const char* a_field)
	{
		const auto iterator = a_packet.find(a_field);
		if (iterator == a_packet.end() || iterator->is_boolean() || !iterator->is_number()) {
			return std::nullopt;
		}
		try {
			const auto value = iterator->get<double>();
			if (std::isfinite(value) && value > 0.0) {
				return value;
			}
		} catch (...) {
		}
		return std::nullopt;
	}
}
