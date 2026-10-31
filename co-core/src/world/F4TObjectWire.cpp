#include "F4TObjectWire.h"

#include "F4TFormIdentity.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>

namespace F4T::ObjectWire
{
	namespace
	{
		using Json = nlohmann::json;

		std::string Hex8(std::uint32_t a_value)
		{
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
			return buffer;
		}

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

		std::optional<bool> ReadBoolean(const Json& a_object, const char* a_field)
		{
			const auto found = a_object.find(a_field);
			if (found == a_object.end() || !found->is_boolean()) return std::nullopt;
			return found->get<bool>();
		}

		std::optional<double> ReadFiniteDouble(const Json& a_object, const char* a_field)
		{
			const auto found = a_object.find(a_field);
			if (found == a_object.end() || found->is_boolean() || !found->is_number()) return std::nullopt;
			const auto value = found->get<double>();
			return std::isfinite(value) ? std::optional<double>{ value } : std::nullopt;
		}

		std::optional<std::string> ReadBoundedString(const Json& a_object, const char* a_field, std::size_t a_maximum, bool a_allowEmpty)
		{
			const auto found = a_object.find(a_field);
			if (found == a_object.end() || !found->is_string()) return std::nullopt;
			const auto& text = found->get_ref<const std::string&>();
			if (text.size() > a_maximum) return std::nullopt;
			if (!a_allowEmpty && text.empty()) return std::nullopt;
			return text;
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
			const auto value = ReadBoundedString(a_packet, "operationId", kMaximumOperationIdChars, false);
			return value.has_value();
		}

		std::optional<Kind> ReadKind(const Json& a_packet)
		{
			const auto found = a_packet.find("kind");
			if (found == a_packet.end() || !found->is_string()) return std::nullopt;
			return ParseKind(found->get_ref<const std::string&>());
		}

		std::optional<Action> ReadAction(const Json& a_packet)
		{
			const auto found = a_packet.find("action");
			if (found == a_packet.end() || !found->is_string()) return std::nullopt;
			return ParseAction(found->get_ref<const std::string&>());
		}

		bool ValidLockFields(const Json& a_item, std::optional<bool> a_locked)
		{
			if (a_item.contains("lockLevel")) {
				if (!a_locked.has_value() || !*a_locked) return false;
				const auto level = ReadUnsigned(a_item, "lockLevel");
				if (!level || *level > kMaximumLockLevel) return false;
			}
			return true;
		}

		bool ValidStateFields(const Json& a_item, bool a_withType)
		{
			const bool onlyKnownKeys = a_withType ?
				HasOnlyKeys(a_item, { "type", "object", "kind", "open", "locked", "lockLevel", "revision" }) :
				HasOnlyKeys(a_item, { "object", "kind", "open", "locked", "lockLevel", "revision" });
			if (!onlyKnownKeys) return false;
			if (!HasKeys(a_item, { "object", "kind", "revision" })) return false;
			const auto kind = ReadKind(a_item);
			const auto revision = ReadUnsigned(a_item, "revision");
			if (!kind || !revision || *revision == 0) return false;
			if (!ParseObjectKey(a_item.at("object"))) return false;
			if (*kind == Kind::Door) {
				if (!ReadBoolean(a_item, "open")) return false;
			} else if (a_item.contains("open")) {
				return false;
			}
			const auto locked = a_item.contains("locked") ? ReadBoolean(a_item, "locked") : std::optional<bool>{};
			if (a_item.contains("locked") && !locked) return false;
			if (!ValidLockFields(a_item, locked)) return false;
			return true;
		}
	}

	std::string_view KindName(Kind a_kind) noexcept
	{
		return a_kind == Kind::Door ? std::string_view{ "door" } : std::string_view{ "activator" };
	}

	std::string_view ActionName(Action a_action) noexcept
	{
		switch (a_action) {
		case Action::Open:
			return "open";
		case Action::Close:
			return "close";
		case Action::Toggle:
			return "toggle";
		case Action::Activate:
			return "activate";
		}
		return "activate";
	}

	std::optional<Kind> ParseKind(std::string_view a_kind) noexcept
	{
		if (a_kind == "door") return Kind::Door;
		if (a_kind == "activator") return Kind::Activator;
		return std::nullopt;
	}

	std::optional<Action> ParseAction(std::string_view a_action) noexcept
	{
		if (a_action == "open") return Action::Open;
		if (a_action == "close") return Action::Close;
		if (a_action == "toggle") return Action::Toggle;
		if (a_action == "activate") return Action::Activate;
		return std::nullopt;
	}

	std::optional<ObjectKey> ParseObjectKey(const Json& a_object)
	{
		if (!IsExactKeySet(a_object, { "plugin", "localFormId" })) return std::nullopt;
		const auto plugin = ReadBoundedString(a_object, "plugin", kMaximumPluginChars, false);
		const auto recordId = ReadHex8(a_object, "localFormId", false);
		if (!plugin || !recordId) return std::nullopt;
		const auto split = F4T::FormIdentity::SplitFormId(*recordId);
		const F4T::FormIdentity::RecordIdentity identity{ *plugin, split.localId };
		if (!F4T::FormIdentity::IsValidRecordIdentity(identity, split.kind)) return std::nullopt;
		return ObjectKey{ *plugin, split.localId };
	}

	std::optional<ObjectPosition> ParsePosition(const Json& a_position)
	{
		if (!IsExactKeySet(a_position, { "cellId", "worldspaceId", "x", "y", "z" })) return std::nullopt;
		const auto cell = ReadHex8(a_position, "cellId", true);
		const auto worldspace = ReadBoundedString(a_position, "worldspaceId", 8, true);
		const auto x = ReadFiniteDouble(a_position, "x");
		const auto y = ReadFiniteDouble(a_position, "y");
		const auto z = ReadFiniteDouble(a_position, "z");
		if (!cell || !worldspace || !x || !y || !z) return std::nullopt;
		std::optional<std::uint32_t> worldspaceValue;
		if (!worldspace->empty()) {
			worldspaceValue = ReadHex8(a_position, "worldspaceId", true);
			if (!worldspaceValue) return std::nullopt;
		}
		return ObjectPosition{ Hex8(*cell), worldspaceValue ? Hex8(*worldspaceValue) : std::string{}, *x, *y, *z };
	}

	std::optional<Json> NormalizeActivateRequest(const Json& a_packet)
	{
		if (!IsExactKeySet(a_packet, { "type", "operationId", "object", "kind", "action", "position" })) return std::nullopt;
		if (!TypeIs(a_packet, "objectActivateRequest") || !ValidOperationId(a_packet)) return std::nullopt;
		if (!ReadKind(a_packet) || !ReadAction(a_packet)) return std::nullopt;
		if (!ParseObjectKey(a_packet.at("object")) || !ParsePosition(a_packet.at("position"))) return std::nullopt;
		return a_packet;
	}

	std::optional<Json> NormalizeLockRequest(const Json& a_packet)
	{
		if (!HasOnlyKeys(a_packet, { "type", "operationId", "object", "locked", "lockLevel", "position" })) return std::nullopt;
		if (!HasKeys(a_packet, { "type", "operationId", "object", "locked", "position" })) return std::nullopt;
		if (!TypeIs(a_packet, "objectLockRequest") || !ValidOperationId(a_packet)) return std::nullopt;
		const auto locked = ReadBoolean(a_packet, "locked");
		if (!locked || !ValidLockFields(a_packet, locked)) return std::nullopt;
		if (!ParseObjectKey(a_packet.at("object")) || !ParsePosition(a_packet.at("position"))) return std::nullopt;
		return a_packet;
	}

	std::optional<Json> NormalizeObjectState(const Json& a_packet)
	{
		if (!TypeIs(a_packet, "objectState") || !ValidStateFields(a_packet, true)) return std::nullopt;
		return a_packet;
	}

	std::optional<Json> NormalizeObjectStateSnapshot(const Json& a_packet)
	{
		if (!IsExactKeySet(a_packet, { "type", "objects" }) || !TypeIs(a_packet, "objectStateSnapshot")) return std::nullopt;
		const auto& objects = a_packet.at("objects");
		if (!objects.is_array() || objects.size() > kMaximumSnapshotObjects) return std::nullopt;
		std::set<ObjectKey> keys;
		for (const auto& item : objects) {
			if (!ValidStateFields(item, false)) return std::nullopt;
			const auto key = ParseObjectKey(item.at("object"));
			if (!key || !keys.insert(*key).second) return std::nullopt;
		}
		return a_packet;
	}

	std::optional<Json> NormalizeObjectStateRejected(const Json& a_packet)
	{
		if (!IsExactKeySet(a_packet, { "type", "operationId", "reason" }) || !TypeIs(a_packet, "objectStateRejected")) return std::nullopt;
		if (!ValidOperationId(a_packet)) return std::nullopt;
		return ReadBoundedString(a_packet, "reason", kMaximumReasonChars, false) ? std::optional<Json>{ a_packet } : std::nullopt;
	}

	Json BuildObjectState(const ObjectState& a_state)
	{
		Json packet{
			{ "type", "objectState" },
			{ "object", Json{ { "plugin", a_state.object.plugin }, { "localFormId", Hex8(a_state.object.localId) } } },
			{ "kind", std::string{ KindName(a_state.kind) } },
			{ "revision", a_state.revision }
		};
		if (a_state.kind == Kind::Door) {
			packet["open"] = a_state.open;
		}
		if (a_state.hasLock) {
			packet["locked"] = a_state.locked;
			if (a_state.locked) {
				packet["lockLevel"] = a_state.lockLevel;
			}
		}
		return packet;
	}

	Json BuildStateSnapshotPacket(const std::vector<ObjectState>& a_states)
	{
		Json objects = Json::array();
		for (const auto& state : a_states) {
			if (objects.size() >= kMaximumSnapshotObjects) break;
			auto item = BuildObjectState(state);
			item.erase("type");
			objects.push_back(std::move(item));
		}
		return Json{ { "type", "objectStateSnapshot" }, { "objects", std::move(objects) } };
	}
}
