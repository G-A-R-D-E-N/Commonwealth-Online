#include "F4TAdminItemValidation.h"

#include "F4TNetworkValidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::AdminItemValidation
{
	namespace
	{
		using Json = nlohmann::json;

		const Json* Field(const Json& a_object, std::string_view a_name)
		{
			const auto found = a_object.find(a_name);
			return found == a_object.end() ? nullptr : std::addressof(*found);
		}

		std::optional<std::uint64_t> ReadInteger(const Json* a_node)
		{
			if (a_node == nullptr || a_node->is_boolean() || !a_node->is_number_integer()) {
				return std::nullopt;
			}
			if (a_node->is_number_unsigned()) {
				return a_node->get<std::uint64_t>();
			}
			const auto value = a_node->get<std::int64_t>();
			if (value < 0) {
				return std::nullopt;
			}
			return static_cast<std::uint64_t>(value);
		}

		std::optional<std::uint32_t> ReadUInt32Between(const Json* a_node, std::uint32_t a_minimum)
		{
			const auto value = ReadInteger(a_node);
			if (!value || *value < a_minimum || *value > (std::numeric_limits<std::uint32_t>::max)()) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(*value);
		}

		std::optional<std::string> ReadBoundedText(const Json* a_node, std::size_t a_maximumBytes)
		{
			if (a_node == nullptr || !a_node->is_string()) {
				return std::nullopt;
			}
			auto text = a_node->get<std::string>();
			if (text.size() > a_maximumBytes) {
				return std::nullopt;
			}
			for (const unsigned char ch : text) {
				if (ch < 32 || ch == 127) {
					return std::nullopt;
				}
			}
			return text;
		}

		std::optional<std::string> ReadItemId(const Json* a_node)
		{
			if (a_node == nullptr || !a_node->is_string()) {
				return std::nullopt;
			}
			auto text = a_node->get<std::string>();
			if (text.size() != 8 || !F4T::NetworkValidation::IsValidHexFormId(text, false, false)) {
				return std::nullopt;
			}
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
				return static_cast<char>(std::toupper(ch));
			});
			return text;
		}

		std::optional<Json> NormalizeItem(const Json& a_item)
		{
			if (!a_item.is_object()) {
				return std::nullopt;
			}
			const auto itemId = ReadItemId(Field(a_item, "itemId"));
			const auto name = ReadBoundedText(Field(a_item, "name"), MaximumItemNameBytes);
			const auto count = ReadUInt32Between(Field(a_item, "count"), 1);
			if (!itemId || !name || !count) {
				return std::nullopt;
			}
			return Json{ { "itemId", *itemId }, { "name", *name }, { "count", *count } };
		}
	}

	std::optional<Json> NormalizeInventoryReport(const Json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.dump().size() > MaximumInventoryReportBytes) {
			return std::nullopt;
		}
		const auto opId = ReadUInt32Between(Field(a_packet, "opId"), 1);
		const auto* truncated = Field(a_packet, "truncated");
		const auto* items = Field(a_packet, "items");
		if (!opId || truncated == nullptr || !truncated->is_boolean() || items == nullptr || !items->is_array() ||
			items->size() > MaximumInventoryItems) {
			return std::nullopt;
		}

		Json normalizedItems = Json::array();
		for (const auto& item : *items) {
			auto normalized = NormalizeItem(item);
			if (!normalized) {
				return std::nullopt;
			}
			normalizedItems.push_back(std::move(*normalized));
		}
		return Json{ { "opId", *opId }, { "truncated", truncated->get<bool>() }, { "items", std::move(normalizedItems) } };
	}

	std::optional<Json> NormalizeAdminItemResult(const Json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto opId = ReadUInt32Between(Field(a_packet, "opId"), 1);
		const auto* ok = Field(a_packet, "ok");
		const auto applied = ReadUInt32Between(Field(a_packet, "applied"), 0);
		const auto error = ReadBoundedText(Field(a_packet, "error"), MaximumAdminResultErrorBytes);
		if (!opId || ok == nullptr || !ok->is_boolean() || !applied || !error) {
			return std::nullopt;
		}
		if (!ok->get<bool>() && error->empty()) {
			return std::nullopt;
		}
		return Json{ { "opId", *opId }, { "ok", ok->get<bool>() }, { "applied", *applied }, { "error", *error } };
	}
}
