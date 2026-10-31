#include "F4TInventoryWire.h"

#include "F4TInventoryContract.h"

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>

namespace F4T::InventoryWire
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::size_t kMaximumReasonChars = 64;

		enum class Status : std::uint8_t
		{
			Committed,
			Rejected
		};

		std::optional<std::uint32_t> ReadUnsigned(const Json& a_object, const char* a_field)
		{
			const auto found = a_object.find(a_field);
			if (found == a_object.end() || !found->is_number_unsigned()) return std::nullopt;
			const auto value = found->get<std::uint64_t>();
			if (value > (std::numeric_limits<std::uint32_t>::max)()) return std::nullopt;
			return static_cast<std::uint32_t>(value);
		}

		std::optional<std::uint32_t> ReadHex8(const Json& a_object, const char* a_field, bool a_allowZero)
		{
			const auto found = a_object.find(a_field);
			if (found == a_object.end() || !found->is_string()) return std::nullopt;
			const auto& text = found->get_ref<const std::string&>();
			if (text.size() != 8) return std::nullopt;
			std::uint32_t value = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
			if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
			if (!a_allowZero && value == 0) return std::nullopt;
			return value;
		}

		bool TypeIs(const Json& a_packet, const char* a_type)
		{
			const auto found = a_packet.find("type");
			return found != a_packet.end() && found->is_string() && found->get_ref<const std::string&>() == a_type;
		}

		bool IsExactKeySet(const Json& a_object, std::initializer_list<const char*> a_keys)
		{
			if (!a_object.is_object() || a_object.size() != a_keys.size()) return false;
			return std::all_of(a_keys.begin(), a_keys.end(), [&a_object](const char* a_key) { return a_object.contains(a_key); });
		}

		bool HasOnlyKeys(const Json& a_object, std::initializer_list<const char*> a_keys)
		{
			if (!a_object.is_object()) return false;
			for (const auto& [name, value] : a_object.items()) {
				(void)value;
				if (std::none_of(a_keys.begin(), a_keys.end(), [&name](const char* a_key) { return name == a_key; })) return false;
			}
			return true;
		}

		bool HasKeys(const Json& a_object, std::initializer_list<const char*> a_keys)
		{
			if (!a_object.is_object()) return false;
			return std::all_of(a_keys.begin(), a_keys.end(), [&a_object](const char* a_key) { return a_object.contains(a_key); });
		}

		bool ValidOperationId(const Json& a_packet)
		{
			const auto found = a_packet.find("operationId");
			if (found == a_packet.end() || !found->is_string()) return false;
			const auto& text = found->get_ref<const std::string&>();
			return !text.empty() && text.size() <= F4T::Drops::kMaximumOperationIdChars;
		}

		bool ValidItemKey(const Json& a_packet)
		{
			const auto found = a_packet.find("itemKey");
			if (found == a_packet.end() || !found->is_string()) return false;
			const auto& text = found->get_ref<const std::string&>();
			return text.size() <= F4T::InventoryContract::kMaximumItemKeyChars && F4T::InventoryContract::ParseItemKey(text).has_value();
		}

		bool ValidCount(const Json& a_object)
		{
			const auto count = ReadUnsigned(a_object, "count");
			return count && *count != 0 && *count <= F4T::Drops::kMaximumDropCount;
		}

		bool ValidDirection(const Json& a_packet)
		{
			const auto found = a_packet.find("direction");
			if (found == a_packet.end() || !found->is_string()) return false;
			const auto& text = found->get_ref<const std::string&>();
			return text == "take" || text == "put";
		}

		bool ValidReference(const Json& a_reference)
		{
			if (!IsExactKeySet(a_reference, { "cellId", "refId" })) return false;
			const auto cell = ReadHex8(a_reference, "cellId", true);
			const auto ref = ReadHex8(a_reference, "refId", false);
			return cell && ref && F4T::FormIdentity::SplitFormId(*ref).kind != F4T::FormIdentity::Kind::Runtime;
		}

		std::optional<Status> ReadStatus(const Json& a_packet)
		{
			const auto found = a_packet.find("status");
			if (found == a_packet.end() || !found->is_string()) return std::nullopt;
			const auto& text = found->get_ref<const std::string&>();
			if (text == "committed") return Status::Committed;
			if (text == "rejected") return Status::Rejected;
			return std::nullopt;
		}

		bool ReasonOmitsRevision(const std::string& a_reason)
		{
			return a_reason == "rateLimited" || a_reason == "inventoryAuthorityUnavailable" ||
				a_reason == "inventoryPersistenceUnavailable";
		}

		bool ValidRejectedShape(const Json& a_packet)
		{
			const auto found = a_packet.find("reason");
			if (found == a_packet.end() || !found->is_string()) return false;
			const auto& reason = found->get_ref<const std::string&>();
			if (reason.empty() || reason.size() > kMaximumReasonChars) return false;
			const bool hasRevision = a_packet.contains("revision");
			if (ReasonOmitsRevision(reason)) return !hasRevision;
			return hasRevision && ReadUnsigned(a_packet, "revision").has_value();
		}

		bool ValidTradeLines(const Json& a_lines)
		{
			if (!a_lines.is_array() || a_lines.size() > kMaximumTradeLines) return false;
			std::set<std::string> keys;
			for (const auto& line : a_lines) {
				if (!IsExactKeySet(line, { "itemKey", "count" }) || !ValidItemKey(line) || !ValidCount(line)) return false;
				if (!keys.insert(line.at("itemKey").get<std::string>()).second) return false;
			}
			return true;
		}
	}

	std::optional<Json> NormalizeContainerRequest(const Json& a_packet)
	{
		if (!IsExactKeySet(a_packet, { "type", "operationId", "expectedRevision", "direction", "container", "itemKey", "count" })) return std::nullopt;
		if (!TypeIs(a_packet, "inventoryContainerRequest") || !ValidOperationId(a_packet) ||
			!ReadUnsigned(a_packet, "expectedRevision") || !ValidDirection(a_packet) || !ValidItemKey(a_packet) ||
			!ValidCount(a_packet)) return std::nullopt;
		if (!ValidReference(a_packet.at("container"))) return std::nullopt;
		return a_packet;
	}

	std::optional<Json> NormalizeContainerResult(const Json& a_packet)
	{
		if (!TypeIs(a_packet, "inventoryContainerResult") ||
			!HasOnlyKeys(a_packet, { "type", "operationId", "status", "reason", "revision", "containerRevision", "direction", "itemKey", "count" }) ||
			!HasKeys(a_packet, { "type", "operationId", "status", "direction", "itemKey", "count" }) ||
			!ValidOperationId(a_packet) || !ValidDirection(a_packet) || !ValidItemKey(a_packet) || !ValidCount(a_packet)) return std::nullopt;
		if (a_packet.contains("containerRevision") && !ReadUnsigned(a_packet, "containerRevision")) return std::nullopt;
		const auto status = ReadStatus(a_packet);
		if (!status) return std::nullopt;
		if (*status == Status::Committed) {
			if (a_packet.contains("reason") || !ReadUnsigned(a_packet, "revision")) return std::nullopt;
			return a_packet;
		}
		return ValidRejectedShape(a_packet) ? std::optional<Json>{ a_packet } : std::nullopt;
	}

	std::optional<Json> NormalizeTradeRequest(const Json& a_packet)
	{
		if (!IsExactKeySet(a_packet, { "type", "operationId", "expectedRevision", "vendor", "sell", "buy" })) return std::nullopt;
		if (!TypeIs(a_packet, "inventoryTradeRequest") || !ValidOperationId(a_packet) || !ReadUnsigned(a_packet, "expectedRevision") ||
			!ValidReference(a_packet.at("vendor"))) return std::nullopt;
		return ValidTradeLines(a_packet.at("sell")) && ValidTradeLines(a_packet.at("buy")) ? std::optional<Json>{ a_packet } : std::nullopt;
	}

	std::optional<Json> NormalizeTradeResult(const Json& a_packet)
	{
		if (!TypeIs(a_packet, "inventoryTradeResult") ||
			!HasOnlyKeys(a_packet, { "type", "operationId", "status", "reason", "revision" }) ||
			!HasKeys(a_packet, { "type", "operationId", "status" }) || !ValidOperationId(a_packet)) return std::nullopt;
		const auto status = ReadStatus(a_packet);
		if (!status) return std::nullopt;
		if (*status == Status::Committed) {
			if (a_packet.contains("reason") || !ReadUnsigned(a_packet, "revision")) return std::nullopt;
			return a_packet;
		}
		return ValidRejectedShape(a_packet) ? std::optional<Json>{ a_packet } : std::nullopt;
	}
}
