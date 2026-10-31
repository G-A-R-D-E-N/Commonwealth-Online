#include "F4TAuthoritativePacketValidation.h"

#include "F4TNetworkValidation.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::AuthoritativePacketValidation
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kMaximumNpcsPerPacket = 64;
		constexpr double kMaximumNpcDamage = 1000.0;

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

		std::optional<double> ReadDouble(const Json& a_value, double a_min, double a_max)
		{
			if (a_value.is_boolean() || !a_value.is_number()) {
				return std::nullopt;
			}
			try {
				const auto value = a_value.get<double>();
				if (std::isfinite(value) && value >= a_min && value <= a_max) {
					return value;
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
	}

	namespace
	{
		std::optional<std::string> CleanFormIdField(
			const Json& a_object,
			std::string_view a_name,
			bool a_allowEmpty,
			bool a_allowZero)
		{
			const auto found = a_object.find(a_name);
			if (found == a_object.end() || !found->is_string()) {
				return std::nullopt;
			}
			const auto value = found->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(value, a_allowEmpty, a_allowZero)) {
				return std::nullopt;
			}
			return value.empty() ? std::string{} : NormalizeFormId(value);
		}
	}

	std::optional<nlohmann::json> NormalizeWorldState(const nlohmann::json& a_packet)
	{
		using Json = nlohmann::json;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		// Allow-list, not a sanitized copy: an elected world host must not be
		// able to smuggle extra members through the server to every client.
		Json normalized = Json::object();
		normalized["type"] = "worldState";
		if (const auto found = a_packet.find("gameHour"); found != a_packet.end()) {
			const auto value = ReadDouble(*found, 0.0, 24.0);
			if (!value) {
				return std::nullopt;
			}
			normalized["gameHour"] = *value;
		}
		if (const auto found = a_packet.find("gameDaysPassed"); found != a_packet.end()) {
			const auto value = ReadDouble(*found, 0.0, 10'000'000.0);
			if (!value) {
				return std::nullopt;
			}
			normalized["gameDaysPassed"] = *value;
		}
		if (const auto found = a_packet.find("weatherFormId"); found != a_packet.end()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			const auto weather = found->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(weather, true, true)) {
				return std::nullopt;
			}
			normalized["weatherFormId"] = weather.empty() ? std::string{} : NormalizeFormId(weather);
		}
		if (const auto found = a_packet.find("timeSync"); found != a_packet.end()) {
			if (!found->is_boolean()) {
				return std::nullopt;
			}
			normalized["timeSync"] = found->get<bool>();
		}
		if (const auto found = a_packet.find("clientTime"); found != a_packet.end()) {
			if (const auto value = ReadDouble(*found, 0.0, 1e12)) {
				normalized["clientTime"] = *value;
			}
		}
		return normalized;
	}

	std::optional<nlohmann::json> NormalizeNpcState(const nlohmann::json& a_packet)
	{
		using Json = nlohmann::json;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto npcsIt = a_packet.find("npcs");
		if (npcsIt == a_packet.end() || !npcsIt->is_array() || npcsIt->size() > kMaximumNpcsPerPacket) {
			return std::nullopt;
		}

		Json cleanNpcs = Json::array();
		for (const auto& node : *npcsIt) {
			if (!node.is_object()) {
				return std::nullopt;
			}
			const auto sourceIt = node.find("sourceFormId");
			const auto cellIt = node.find("cellId");
			if (sourceIt == node.end() || !sourceIt->is_string() || cellIt == node.end() || !cellIt->is_string()) {
				return std::nullopt;
			}
			const auto source = sourceIt->get<std::string>();
			const auto cell = cellIt->get<std::string>();
			std::string world;
			if (const auto worldIt = node.find("worldspaceId"); worldIt != node.end() && worldIt->is_string()) {
				world = worldIt->get<std::string>();
			}
			if (!F4T::NetworkValidation::IsValidHexFormId(source, false, false) ||
				!F4T::NetworkValidation::IsValidHexFormId(cell, false, false) ||
				!F4T::NetworkValidation::IsValidHexFormId(world, true, true)) {
				return std::nullopt;
			}

			const auto xIt = node.find("x");
			const auto yIt = node.find("y");
			const auto zIt = node.find("z");
			const auto angleIt = node.find("angleZ");
			if (xIt == node.end() || yIt == node.end() || zIt == node.end() || angleIt == node.end()) {
				return std::nullopt;
			}
			const auto x = ReadDouble(*xIt, -F4T::NetworkValidation::MaximumAbsoluteCoordinate, F4T::NetworkValidation::MaximumAbsoluteCoordinate);
			const auto y = ReadDouble(*yIt, -F4T::NetworkValidation::MaximumAbsoluteCoordinate, F4T::NetworkValidation::MaximumAbsoluteCoordinate);
			const auto z = ReadDouble(*zIt, -F4T::NetworkValidation::MaximumAbsoluteCoordinate, F4T::NetworkValidation::MaximumAbsoluteCoordinate);
			const auto angleZ = ReadDouble(*angleIt, -F4T::NetworkValidation::MaximumAbsoluteCoordinate, F4T::NetworkValidation::MaximumAbsoluteCoordinate);
			if (!x || !y || !z || !angleZ) {
				return std::nullopt;
			}

			Json clean = Json::object();
			if (node.contains("npcId")) {
				const auto npcId = ReadUInt32Field(node, "npcId");
				if (!npcId || *npcId == 0) {
					return std::nullopt;
				}
				clean["npcId"] = *npcId;
			}
			if (node.contains("baseFormId")) {
				const auto baseFormId = CleanFormIdField(node, "baseFormId", false, false);
				if (!baseFormId) {
					return std::nullopt;
				}
				clean["baseFormId"] = *baseFormId;
			}
			for (const char* name : { "isDead", "isMoving", "isSprinting", "isSneaking" }) {
				if (const auto flag = node.find(name); flag != node.end() && flag->is_boolean()) {
					clean[name] = flag->get<bool>();
				}
			}
			if (const auto speed = node.find("movementSpeed"); speed != node.end()) {
				const auto value = ReadDouble(*speed, 0.0, F4T::NetworkValidation::MaximumMovementSpeed);
				if (!value) {
					return std::nullopt;
				}
				clean["movementSpeed"] = *value;
			}
			const auto healthIt = node.find("health");
			const auto maxHealthIt = node.find("maxHealth");
			if ((healthIt == node.end()) != (maxHealthIt == node.end())) {
				return std::nullopt;
			}
			if (healthIt != node.end()) {
				const auto health = ReadDouble(*healthIt, 0.0, F4T::NetworkValidation::MaximumActorHealth);
				const auto maxHealth = ReadDouble(*maxHealthIt, 0.0, F4T::NetworkValidation::MaximumActorHealth);
				if (!health || !maxHealth || *health > *maxHealth) {
					return std::nullopt;
				}
				clean["health"] = *health;
				clean["maxHealth"] = *maxHealth;
			}
			clean["sourceFormId"] = NormalizeFormId(source);
			clean["cellId"] = NormalizeFormId(cell);
			clean["worldspaceId"] = world.empty() ? std::string{} : NormalizeFormId(world);
			clean["x"] = *x;
			clean["y"] = *y;
			clean["z"] = *z;
			clean["angleZ"] = *angleZ;
			cleanNpcs.push_back(std::move(clean));
		}

		Json normalized = Json::object();
		normalized["type"] = "npcState";
		normalized["npcs"] = std::move(cleanNpcs);
		if (const auto found = a_packet.find("fullReplace"); found != a_packet.end()) {
			if (!found->is_boolean()) {
				return std::nullopt;
			}
			normalized["fullReplace"] = found->get<bool>();
		}
		if (const auto found = a_packet.find("clientTime"); found != a_packet.end()) {
			if (const auto value = ReadDouble(*found, 0.0, 1e12)) {
				normalized["clientTime"] = *value;
			}
		}
		if (const auto found = a_packet.find("authorityEpoch"); found != a_packet.end()) {
			const auto epoch = ReadUInt32(*found);
			if (!epoch || *epoch == 0) {
				return std::nullopt;
			}
			normalized["authorityEpoch"] = *epoch;
		}
		if (const auto found = a_packet.find("authorityCellId"); found != a_packet.end()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			const auto cell = found->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(cell, false, false)) {
				return std::nullopt;
			}
			normalized["authorityCellId"] = NormalizeFormId(cell);
		}
		if (const auto found = a_packet.find("authorityWorldspaceId"); found != a_packet.end()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			const auto world = found->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(world, true, true)) {
				return std::nullopt;
			}
			normalized["authorityWorldspaceId"] = world.empty() ? std::string{} : NormalizeFormId(world);
		}
		return normalized;
	}

	std::optional<nlohmann::json> NormalizeCombatHit(const nlohmann::json& a_packet)
	{
		using Json = nlohmann::json;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto targetIt = a_packet.find("targetPlayerId");
		const auto sequenceIt = a_packet.find("sequence");
		const auto damageIt = a_packet.find("damage");
		// The attack this hit came out of. Naming it is what stops a hit that was
		// delayed in transit from being authorized by whichever attack happens to
		// be current when it lands.
		const auto attackIt = a_packet.find("attackSequence");
		if (targetIt == a_packet.end() || sequenceIt == a_packet.end() || damageIt == a_packet.end() ||
			attackIt == a_packet.end()) {
			return std::nullopt;
		}
		const auto target = ReadUInt32(*targetIt);
		const auto sequence = ReadUInt32(*sequenceIt);
		const auto attackSequence = ReadUInt32(*attackIt);
		const auto damage = ReadDouble(*damageIt, 0.000001, 10'000.0);
		if (!target || *target == 0 || !sequence || *sequence == 0 || !damage ||
			!attackSequence || *attackSequence == 0) {
			return std::nullopt;
		}

		Json normalized = Json::object();
		normalized["type"] = "combatHit";
		normalized["targetPlayerId"] = *target;
		normalized["sequence"] = *sequence;
		normalized["attackSequence"] = *attackSequence;
		normalized["damage"] = *damage;
		if (const auto found = a_packet.find("weaponFormId"); found != a_packet.end()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			const auto weapon = found->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(weapon, false, true)) {
				return std::nullopt;
			}
			normalized["weaponFormId"] = NormalizeFormId(weapon);
		}
		return normalized;
	}

	std::optional<nlohmann::json> NormalizeNpcHit(const nlohmann::json& a_packet)
	{
		using Json = nlohmann::json;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto npcIt = a_packet.find("npcId");
		const auto cellIt = a_packet.find("cellId");
		const auto sourceIt = a_packet.find("sourceFormId");
		const auto sequenceIt = a_packet.find("sequence");
		const auto damageIt = a_packet.find("damage");
		const auto attackIt = a_packet.find("attackSequence");
		const auto epochIt = a_packet.find("authorityEpoch");
		if (npcIt == a_packet.end() || cellIt == a_packet.end() || sourceIt == a_packet.end() ||
			sequenceIt == a_packet.end() || damageIt == a_packet.end() || attackIt == a_packet.end() ||
			epochIt == a_packet.end()) {
			return std::nullopt;
		}
		const auto npcId = ReadUInt32(*npcIt);
		const auto sequence = ReadUInt32(*sequenceIt);
		const auto attackSequence = ReadUInt32(*attackIt);
		const auto authorityEpoch = ReadUInt32(*epochIt);
		const auto damage = ReadDouble(*damageIt, 0.000001, 10'000.0);
		if (!npcId || *npcId == 0 || !sequence || *sequence == 0 || !damage ||
			!attackSequence || *attackSequence == 0 || !authorityEpoch || *authorityEpoch == 0) {
			return std::nullopt;
		}
		if (!cellIt->is_string() || !sourceIt->is_string()) {
			return std::nullopt;
		}
		const auto cell = cellIt->get<std::string>();
		const auto source = sourceIt->get<std::string>();
		if (!F4T::NetworkValidation::IsValidHexFormId(cell, false, false) ||
			!F4T::NetworkValidation::IsValidHexFormId(source, false, false)) {
			return std::nullopt;
		}
		std::string world;
		if (const auto worldIt = a_packet.find("worldspaceId"); worldIt != a_packet.end() && worldIt->is_string()) {
			world = worldIt->get<std::string>();
		}
		if (!F4T::NetworkValidation::IsValidHexFormId(world, true, true)) {
			return std::nullopt;
		}

		Json normalized = Json::object();
		normalized["type"] = "npcHit";
		normalized["npcId"] = *npcId;
		normalized["cellId"] = NormalizeFormId(cell);
		normalized["worldspaceId"] = world.empty() ? std::string{} : NormalizeFormId(world);
		normalized["sourceFormId"] = NormalizeFormId(source);
		normalized["sequence"] = *sequence;
		normalized["attackSequence"] = *attackSequence;
		normalized["authorityEpoch"] = *authorityEpoch;
		normalized["damage"] = *damage;
		if (const auto found = a_packet.find("weaponFormId"); found != a_packet.end()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			const auto weapon = found->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(weapon, false, true)) {
				return std::nullopt;
			}
			normalized["weaponFormId"] = NormalizeFormId(weapon);
		}
		return normalized;
	}

	std::optional<nlohmann::json> NormalizeNpcDamage(const nlohmann::json& a_packet)
	{
		using Json = nlohmann::json;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}
		const auto targetIt = a_packet.find("targetPlayerId");
		const auto npcIt = a_packet.find("npcId");
		const auto cellIt = a_packet.find("cellId");
		const auto sourceIt = a_packet.find("sourceFormId");
		const auto sequenceIt = a_packet.find("sequence");
		const auto damageIt = a_packet.find("damage");
		const auto epochIt = a_packet.find("authorityEpoch");
		if (targetIt == a_packet.end() || npcIt == a_packet.end() || cellIt == a_packet.end() ||
			sourceIt == a_packet.end() || sequenceIt == a_packet.end() || damageIt == a_packet.end() ||
			epochIt == a_packet.end()) {
			return std::nullopt;
		}
		const auto target = ReadUInt32(*targetIt);
		const auto npcId = ReadUInt32(*npcIt);
		const auto sequence = ReadUInt32(*sequenceIt);
		const auto authorityEpoch = ReadUInt32(*epochIt);
		const auto damage = ReadDouble(*damageIt, 0.000001, kMaximumNpcDamage);
		if (!target || *target == 0 || !npcId || *npcId == 0 || !sequence || *sequence == 0 || !damage ||
			!authorityEpoch || *authorityEpoch == 0) {
			return std::nullopt;
		}
		if (!cellIt->is_string() || !sourceIt->is_string()) {
			return std::nullopt;
		}
		const auto cell = cellIt->get<std::string>();
		const auto source = sourceIt->get<std::string>();
		if (!F4T::NetworkValidation::IsValidHexFormId(cell, false, false) ||
			!F4T::NetworkValidation::IsValidHexFormId(source, false, false)) {
			return std::nullopt;
		}
		std::string world;
		if (const auto worldIt = a_packet.find("worldspaceId"); worldIt != a_packet.end() && worldIt->is_string()) {
			world = worldIt->get<std::string>();
		}
		if (!F4T::NetworkValidation::IsValidHexFormId(world, true, true)) {
			return std::nullopt;
		}

		Json normalized = Json::object();
		normalized["type"] = "npcDamage";
		normalized["targetPlayerId"] = *target;
		normalized["npcId"] = *npcId;
		normalized["cellId"] = NormalizeFormId(cell);
		normalized["worldspaceId"] = world.empty() ? std::string{} : NormalizeFormId(world);
		normalized["sourceFormId"] = NormalizeFormId(source);
		normalized["sequence"] = *sequence;
		normalized["authorityEpoch"] = *authorityEpoch;
		normalized["damage"] = *damage;
		if (const auto found = a_packet.find("weaponFormId"); found != a_packet.end()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			const auto weapon = found->get<std::string>();
			if (!F4T::NetworkValidation::IsValidHexFormId(weapon, false, true)) {
				return std::nullopt;
			}
			normalized["weaponFormId"] = NormalizeFormId(weapon);
		}
		return normalized;
	}
}
