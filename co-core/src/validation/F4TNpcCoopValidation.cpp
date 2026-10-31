#include "F4TNpcCoopValidation.h"

#include "F4TNetworkValidation.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace F4T::NpcCoopValidation
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kMaximumLootItems = 64;
		constexpr std::uint32_t kMaximumLootCount = 65535;
		// source:CO-BETA/docs/protocol/packets.md
		constexpr std::uint32_t kRuntimeFormIdBase = 0xFF000000;
		constexpr std::size_t kMaximumOperationIdLength = 64;
		constexpr std::size_t kMaximumReasonLength = 64;

		std::optional<std::uint32_t> ReadUInt32(const Json& a_value)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			try {
				if (a_value.is_number_unsigned()) {
					const auto value = a_value.get<std::uint64_t>();
					if (value <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_integer()) {
					const auto value = a_value.get<std::int64_t>();
					if (value >= 0 && static_cast<std::uint64_t>(value) <= (std::numeric_limits<std::uint32_t>::max)()) {
						return static_cast<std::uint32_t>(value);
					}
				}
				if (a_value.is_number_float()) {
					const auto value = a_value.get<double>();
					if (std::isfinite(value) && value == std::trunc(value) && value >= 0.0 &&
						value <= static_cast<double>((std::numeric_limits<std::uint32_t>::max)())) {
						return static_cast<std::uint32_t>(value);
					}
				}
			} catch (...) {
			}
			return std::nullopt;
		}

		std::optional<std::uint32_t> ReadUInt32Field(const Json& a_object, std::string_view a_name)
		{
			const auto found = a_object.find(a_name);
			return found == a_object.end() ? std::nullopt : ReadUInt32(*found);
		}

		std::string NormalizeFormId(std::string_view a_value)
		{
			std::uint32_t parsed = 0;
			const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed, 16);
			if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size()) {
				return {};
			}
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", parsed);
			return buffer;
		}

		std::optional<std::string> ReadLootFormId(const Json& a_object)
		{
			const auto found = a_object.find("formId");
			if (found == a_object.end() || !found->is_string()) {
				return std::nullopt;
			}
			const auto value = found->get<std::string>();
			if (value.size() != 8 || !F4T::NetworkValidation::IsValidHexFormId(value, false, false)) {
				return std::nullopt;
			}
			std::uint32_t parsed = 0;
			const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 16);
			if (result.ec != std::errc{} || parsed >= kRuntimeFormIdBase) {
				return std::nullopt;
			}
			return NormalizeFormId(value);
		}

		std::optional<std::uint32_t> ReadLootCount(const Json& a_object)
		{
			const auto count = ReadUInt32Field(a_object, "count");
			if (!count || *count == 0 || *count > kMaximumLootCount) {
				return std::nullopt;
			}
			return count;
		}

		std::optional<std::string> ReadOperationId(const Json& a_object)
		{
			const auto found = a_object.find("operationId");
			if (found == a_object.end() || !found->is_string()) {
				return std::nullopt;
			}
			auto value = found->get<std::string>();
			if (value.empty() || value.size() > kMaximumOperationIdLength) {
				return std::nullopt;
			}
			return value;
		}

		bool NormalizeNpcIdentity(const Json& a_packet, Json& a_normalized)
		{
			const auto npcIt = a_packet.find("npcId");
			const auto cellIt = a_packet.find("cellId");
			const auto sourceIt = a_packet.find("sourceFormId");
			const auto epochIt = a_packet.find("authorityEpoch");
			if (npcIt == a_packet.end() || cellIt == a_packet.end() || sourceIt == a_packet.end() || epochIt == a_packet.end()) {
				return false;
			}
			const auto npcId = ReadUInt32(*npcIt);
			const auto authorityEpoch = ReadUInt32(*epochIt);
			if (!npcId || *npcId == 0 || !authorityEpoch || *authorityEpoch == 0) {
				return false;
			}
			if (!cellIt->is_string() || !sourceIt->is_string()) {
				return false;
			}
			const auto cell = cellIt->get<std::string>();
			const auto source = sourceIt->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(cell, false, false) ||
				!F4T::NetworkValidation::IsValidHexFormId(source, false, false)) {
				return false;
			}
			std::string world;
			if (const auto worldIt = a_packet.find("worldspaceId"); worldIt != a_packet.end() && worldIt->is_string()) {
				world = worldIt->get<std::string>();
			}
			if (!F4T::NetworkValidation::IsValidHexFormId(world, true, true)) {
				return false;
			}

			a_normalized["npcId"] = *npcId;
			a_normalized["cellId"] = NormalizeFormId(cell);
			a_normalized["worldspaceId"] = world.empty() ? std::string{} : NormalizeFormId(world);
			a_normalized["sourceFormId"] = NormalizeFormId(source);
			a_normalized["authorityEpoch"] = *authorityEpoch;
			return true;
		}
	}

	std::optional<nlohmann::json> NormalizeNpcLoot(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto itemsIt = a_packet.find("items");
		if (itemsIt == a_packet.end() || !itemsIt->is_array() || itemsIt->size() > kMaximumLootItems) {
			return std::nullopt;
		}
		const auto revision = ReadUInt32Field(a_packet, "revision");
		if (!revision || *revision == 0) {
			return std::nullopt;
		}

		std::map<std::string, std::uint32_t> counts;
		for (const auto& item : *itemsIt) {
			if (!item.is_object()) {
				return std::nullopt;
			}
			const auto formId = ReadLootFormId(item);
			const auto count = ReadLootCount(item);
			if (!formId || !count) {
				return std::nullopt;
			}
			auto& total = counts[*formId];
			total += *count;
			if (total > kMaximumLootCount) {
				return std::nullopt;
			}
		}

		Json items = Json::array();
		for (const auto& [formId, count] : counts) {
			items.push_back(Json{ { "formId", formId }, { "count", count } });
		}
		Json normalized = Json::object();
		normalized["type"] = "npcLoot";
		if (!NormalizeNpcIdentity(a_packet, normalized)) {
			return std::nullopt;
		}
		normalized["revision"] = *revision;
		normalized["items"] = std::move(items);
		return normalized;
	}

	std::optional<nlohmann::json> NormalizeNpcLootTake(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto operationId = ReadOperationId(a_packet);
		const auto formId = ReadLootFormId(a_packet);
		const auto count = ReadLootCount(a_packet);
		if (!operationId || !formId || !count) {
			return std::nullopt;
		}
		Json normalized = Json::object();
		normalized["type"] = "npcLootTake";
		if (!NormalizeNpcIdentity(a_packet, normalized)) {
			return std::nullopt;
		}
		normalized["operationId"] = *operationId;
		normalized["formId"] = *formId;
		normalized["count"] = *count;
		return normalized;
	}

	std::optional<nlohmann::json> NormalizeNpcLootResult(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto operationId = ReadOperationId(a_packet);
		const auto formId = ReadLootFormId(a_packet);
		const auto count = ReadLootCount(a_packet);
		const auto takerPlayerId = ReadUInt32Field(a_packet, "takerPlayerId");
		const auto statusIt = a_packet.find("status");
		const auto revisionIt = a_packet.find("revision");
		if (!operationId || !formId || !count || !takerPlayerId || *takerPlayerId == 0 || statusIt == a_packet.end() ||
			!statusIt->is_string() || revisionIt == a_packet.end()) {
			return std::nullopt;
		}
		const auto status = statusIt->get<std::string>();
		if (status != "accepted" && status != "rejected") {
			return std::nullopt;
		}
		const auto revision = ReadUInt32(*revisionIt);
		if (!revision || (status == "accepted" && *revision == 0)) {
			return std::nullopt;
		}

		Json normalized = Json::object();
		normalized["type"] = "npcLootResult";
		normalized["operationId"] = *operationId;
		normalized["status"] = status;
		if (const auto reasonIt = a_packet.find("reason"); reasonIt != a_packet.end()) {
			if (!reasonIt->is_string() || reasonIt->get<std::string>().size() > kMaximumReasonLength) {
				return std::nullopt;
			}
			normalized["reason"] = reasonIt->get<std::string>();
		}
		normalized["formId"] = *formId;
		normalized["count"] = *count;
		normalized["revision"] = *revision;
		normalized["takerPlayerId"] = *takerPlayerId;
		return normalized;
	}
}
