#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::GnsProtocolAdapter::Detail
{
	std::optional<std::string> ReadString(const nlohmann::json& a_packet, const char* a_field);
	std::optional<bool> ReadBoolean(const nlohmann::json& a_packet, const char* a_field);
	std::optional<std::uint64_t> ReadUnsigned64(const nlohmann::json& a_packet, const char* a_field);
	std::optional<std::uint32_t> ReadUnsigned32(const nlohmann::json& a_packet, const char* a_field);
	std::optional<double> ReadPositiveFiniteDouble(const nlohmann::json& a_packet, const char* a_field);
}
