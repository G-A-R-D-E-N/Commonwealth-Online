#include "F4TPlayerStateValidation.h"

#include "F4TActionEventValidation.h"
#include "F4TAppearanceValidation.h"
#include "F4TNetworkValidation.h"
#include "F4TPowerArmorValidation.h"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace F4T::PlayerStateValidation
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr std::size_t kMaximumCharacterNameChars = 128;
		constexpr std::size_t kMaximumEquippedItems = 32;
		constexpr std::size_t kMaximumEquipmentSlotChars = 64;

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
		constexpr std::size_t kMaximumItemMods = 32;
		constexpr std::int64_t kMinimumPlayerLevel = 1;
		constexpr std::int64_t kMaximumPlayerLevel = 65535;

		std::optional<std::uint32_t> ReadByte(const Json& a_node, const char* a_field)
		{
			const auto found = a_node.find(a_field);
			if (found == a_node.end() || !found->is_number_integer() || found->get<std::int64_t>() < 0 || found->get<std::int64_t>() > 0xFF) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(found->get<std::int64_t>());
		}

		std::optional<Json> NormalizeVitals(const Json& a_vitals)
		{
			if (!a_vitals.is_object()) {
				return std::nullopt;
			}
			const auto healthPercent = ReadByte(a_vitals, "healthPercent");
			if (!healthPercent || *healthPercent > 100) {
				return std::nullopt;
			}
			Json vitals{ { "healthPercent", *healthPercent } };
			for (const char* flag : { "isDowned", "isDead" }) {
				const auto found = a_vitals.find(flag);
				if (found == a_vitals.end()) {
					continue;
				}
				if (!found->is_boolean()) {
					return std::nullopt;
				}
				vitals[flag] = found->get<bool>();
			}
			if (const auto level = a_vitals.find("level"); level != a_vitals.end()) {
				if (!level->is_number_integer()) {
					return std::nullopt;
				}
				const auto value = level->get<std::int64_t>();
				if (value < kMinimumPlayerLevel || value > kMaximumPlayerLevel) {
					return std::nullopt;
				}
				vitals["level"] = value;
			}
			return vitals;
		}

		std::optional<Json> NormalizeItemMods(const Json& a_mods)
		{
			if (!a_mods.is_array() || a_mods.size() > kMaximumItemMods) {
				return std::nullopt;
			}
			Json mods = Json::array();
			for (const auto& node : a_mods) {
				if (!node.is_object()) {
					return std::nullopt;
				}
				const auto mod = node.find("mod");
				const auto index = ReadByte(node, "index");
				const auto rank = ReadByte(node, "rank");
				if (mod == node.end() || !mod->is_string() || !index || !rank ||
					!F4T::NetworkValidation::IsValidHexFormId(mod->get<std::string>(), false, false)) {
					return std::nullopt;
				}
				mods.push_back(Json{ { "mod", NormalizeFormId(mod->get<std::string>()) }, { "index", *index }, { "rank", *rank } });
			}
			return mods;
		}
	}

	std::optional<nlohmann::json> NormalizePlayerState(const nlohmann::json& a_packet)
	{
		using Json = nlohmann::json;
		if (!a_packet.is_object()) {
			return std::nullopt;
		}

		Json clean{ { "type", "playerState" } };
		bool hasState = false;

		if (const auto found = a_packet.find("equippedItems"); found != a_packet.end()) {
			if (!found->is_array() || found->size() > kMaximumEquippedItems) {
				return std::nullopt;
			}
			Json items = Json::array();
			std::unordered_set<std::string> seenSlots;
			seenSlots.reserve(found->size());
			for (const auto& node : *found) {
				if (!node.is_object()) {
					return std::nullopt;
				}
				const auto slotIt = node.find("slot");
				const auto formIt = node.find("formId");
				if (slotIt == node.end() || !slotIt->is_string()) {
					return std::nullopt;
				}
				const auto slot = slotIt->get<std::string>();
				const auto form = formIt != node.end() && formIt->is_string() ? formIt->get<std::string>() : std::string{};
				if (slot.empty() || slot.size() > kMaximumEquipmentSlotChars ||
					!seenSlots.insert(slot).second ||
					!F4T::NetworkValidation::IsValidHexFormId(form, true, true)) {
					return std::nullopt;
				}
				Json item{
					{ "slot", slot },
					{ "formId", form.empty() ? std::string{} : NormalizeFormId(form) }
				};
				if (const auto mods = node.find("mods"); mods != node.end()) {
					auto normalizedMods = NormalizeItemMods(*mods);
					if (!normalizedMods || form.empty()) {
						return std::nullopt;
					}
					item["mods"] = std::move(*normalizedMods);
				}
				items.push_back(std::move(item));
			}
			clean["equippedItems"] = std::move(items);
			hasState = true;
		}

		if (const auto found = a_packet.find("appearance"); found != a_packet.end()) {
			if (const auto appearance = F4T::AppearanceValidation::NormalizeAppearance(*found)) {
				clean["appearance"] = *appearance;
				hasState = true;
			}
		}

		if (const auto found = a_packet.find("powerArmor"); found != a_packet.end()) {
			const auto powerArmor = F4T::PowerArmorValidation::NormalizePowerArmor(*found);
			if (!powerArmor) {
				return std::nullopt;
			}
			clean["powerArmor"] = *powerArmor;
			hasState = true;
		}

		if (const auto found = a_packet.find("actionEvents"); found != a_packet.end()) {
			if (!found->is_array()) {
				return std::nullopt;
			}
			const auto actions = F4T::ActionEventValidation::NormalizeActionEvents(*found, true);
			if (actions.size() != found->size()) {
				return std::nullopt;
			}
			clean["actionEvents"] = actions;
			hasState = true;
		}

		if (const auto found = a_packet.find("characterName"); found != a_packet.end()) {
			if (!found->is_string()) {
				return std::nullopt;
			}
			const auto name = found->get<std::string>();
			if (name.size() > kMaximumCharacterNameChars) {
				return std::nullopt;
			}
			clean["characterName"] = name;
			hasState = true;
		}

		if (const auto found = a_packet.find("vitals"); found != a_packet.end()) {
			const auto vitals = NormalizeVitals(*found);
			if (!vitals) {
				return std::nullopt;
			}
			clean["vitals"] = *vitals;
			hasState = true;
		}

		return hasState ? std::optional{ std::move(clean) } : std::nullopt;
	}
}
