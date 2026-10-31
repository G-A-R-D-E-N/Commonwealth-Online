#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ServerConfiguration::JsonRead
{
	using Json = nlohmann::json;

	std::string Trim(std::string_view a_value);
	std::string Lower(std::string a_value);
	const Json* Find(const Json& a_root, std::string_view a_primary, std::string_view a_alias = {});
	std::optional<std::string> ReadString(const Json* a_node);
	std::string ReadRequiredString(const Json* a_node, std::string_view a_field);
	std::optional<std::string> ReadOptionalString(const Json* a_node, std::string_view a_field);
	std::int64_t ReadInteger(const Json* a_node, std::int64_t a_default, std::string_view a_field);
	const Json* Section(const Json& a_root, std::string_view a_name);
	const Json* Member(const Json* a_section, std::string_view a_key);
	const Json* Pick(const Json* a_first, const Json* a_fallback);
	double ReadNumber(const Json* a_node, double a_default, std::string_view a_field);
	std::uint16_t ReadPort(const Json* a_node, std::uint16_t a_default, std::string_view a_field);
	bool ReadBoolean(const Json* a_node, bool a_default, std::string_view a_field);
	std::vector<std::string> ReadStringArray(const Json* a_node, std::string_view a_field);
}
