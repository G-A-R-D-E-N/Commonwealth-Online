#include "F4TServerConfigJson.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace F4T::ServerConfiguration::JsonRead
{
	std::string Trim(std::string_view a_value)
	{
		std::size_t begin = 0;
		std::size_t end = a_value.size();
		while (begin < end && std::isspace(static_cast<unsigned char>(a_value[begin])) != 0) {
			++begin;
		}
		while (end > begin && std::isspace(static_cast<unsigned char>(a_value[end - 1])) != 0) {
			end -= 1;
		}
		return std::string{ a_value.substr(begin, end - begin) };
	}

	std::string Lower(std::string a_value)
	{
		std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return a_value;
	}

	const Json* Find(const Json& a_root, std::string_view a_primary, std::string_view a_alias)
	{
		const auto primary = a_root.find(a_primary);
		if (primary != a_root.end() && !primary->is_null()) {
			return std::addressof(*primary);
		}
		if (!a_alias.empty()) {
			const auto alias = a_root.find(a_alias);
			if (alias != a_root.end() && !alias->is_null()) {
				return std::addressof(*alias);
			}
		}
		return nullptr;
	}

	std::optional<std::string> ReadString(const Json* a_node)
	{
		if (a_node == nullptr || !a_node->is_string()) {
			return std::nullopt;
		}
		return a_node->get<std::string>();
	}

	std::string ReadRequiredString(const Json* a_node, std::string_view a_field)
	{
		const auto value = ReadString(a_node);
		if (!value) {
			throw std::invalid_argument(std::string{ a_field } + " must be a string");
		}
		return *value;
	}

	std::optional<std::string> ReadOptionalString(const Json* a_node, std::string_view a_field)
	{
		if (a_node == nullptr) {
			return std::nullopt;
		}
		const auto value = ReadString(a_node);
		if (!value) {
			throw std::invalid_argument(std::string{ a_field } + " must be a string");
		}
		const auto normalized = Trim(*value);
		return normalized.empty() ? std::nullopt : std::optional{ normalized };
	}

	std::int64_t ReadInteger(const Json* a_node, std::int64_t a_default, std::string_view a_field)
	{
		if (a_node == nullptr) {
			return a_default;
		}
		if (a_node->is_boolean()) {
			throw std::invalid_argument(std::string{ a_field } + " must be numeric");
		}
		try {
			if (a_node->is_number_integer()) {
				return a_node->get<std::int64_t>();
			}
			if (a_node->is_number_unsigned()) {
				const auto value = a_node->get<std::uint64_t>();
				if (value <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
					return static_cast<std::int64_t>(value);
				}
			}
			if (a_node->is_string()) {
				const auto text = Trim(a_node->get_ref<const std::string&>());
				std::int64_t parsed = 0;
				const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
				if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
					return parsed;
				}
			}
		} catch (...) {
		}
		throw std::invalid_argument(std::string{ a_field } + " must be numeric");
	}

	const Json* Section(const Json& a_root, std::string_view a_name)
	{
		const auto* node = Find(a_root, a_name);
		if (node != nullptr && !node->is_object()) {
			throw std::invalid_argument(std::string{ a_name } + " must be an object");
		}
		return node;
	}

	const Json* Member(const Json* a_section, std::string_view a_key)
	{
		return a_section == nullptr ? nullptr : Find(*a_section, a_key);
	}

	const Json* Pick(const Json* a_first, const Json* a_fallback)
	{
		return a_first != nullptr ? a_first : a_fallback;
	}

	double ReadNumber(const Json* a_node, double a_default, std::string_view a_field)
	{
		if (a_node == nullptr) {
			return a_default;
		}
		if (!a_node->is_number()) {
			throw std::invalid_argument(std::string{ a_field } + " must be numeric");
		}
		return a_node->get<double>();
	}

	std::uint16_t ReadPort(const Json* a_node, std::uint16_t a_default, std::string_view a_field)
	{
		const auto value = ReadInteger(a_node, a_default, a_field);
		return value >= 1 && value <= 65535 ? static_cast<std::uint16_t>(value) : 0;
	}

	bool ReadBoolean(const Json* a_node, bool a_default, std::string_view a_field)
	{
		if (a_node == nullptr) {
			return a_default;
		}
		if (a_node->is_boolean()) {
			return a_node->get<bool>();
		}
		if (a_node->is_number_integer()) {
			const auto value = a_node->get<std::int64_t>();
			if (value == 0 || value == 1) {
				return value == 1;
			}
		}
		if (a_node->is_number_unsigned()) {
			const auto value = a_node->get<std::uint64_t>();
			if (value == 0 || value == 1) {
				return value == 1;
			}
		}
		if (a_node->is_string()) {
			const auto value = Lower(Trim(a_node->get_ref<const std::string&>()));
			if (value == "1" || value == "true" || value == "yes" || value == "on") {
				return true;
			}
			if (value.empty() || value == "0" || value == "false" || value == "no" || value == "off") {
				return false;
			}
		}
		throw std::invalid_argument(std::string{ a_field } + " must be a boolean or one of true/false, yes/no, on/off, 1/0");
	}

	std::vector<std::string> ReadStringArray(const Json* a_node, std::string_view a_field)
	{
		if (a_node == nullptr) {
			return {};
		}
		if (!a_node->is_array()) {
			throw std::invalid_argument(std::string{ a_field } + " must be an array of strings");
		}
		std::vector<std::string> values;
		values.reserve(a_node->size());
		for (const auto& item : *a_node) {
			if (!item.is_string()) {
				throw std::invalid_argument(std::string{ a_field } + " must contain only strings");
			}
			values.push_back(Trim(item.get_ref<const std::string&>()));
		}
		return values;
	}
}
