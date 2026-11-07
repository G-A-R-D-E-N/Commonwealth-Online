#pragma once

#include "F4TFactionPayload.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::Factions::List
{
	inline constexpr std::size_t kMaximumEntries = 64;
	inline constexpr std::size_t kMaximumJsonBytes = 12 * 1024;
	inline constexpr std::size_t kMaximumIdBytes = 64;
	inline constexpr std::size_t kMaximumNameBytes = 64;
	inline constexpr std::size_t kMaximumLeaderBytes = 128;
	inline constexpr std::size_t kMaximumTagBytes = 8;

	struct Entry
	{
		std::string id;
		std::string name;
		std::string tag;
		std::string color;
		std::string description;
		std::string leader;
		std::uint32_t memberCount{ 0 };
		std::uint32_t onlineCount{ 0 };
		bool yours{ false };
	};

	inline std::optional<std::uint32_t> ReadCount(const nlohmann::json& a_entry, const char* a_field)
	{
		const auto field = a_entry.find(a_field);
		if (field == a_entry.end() || !field->is_number_unsigned() || field->get<std::uint64_t>() > 0xFFFFFFFFULL) {
			return std::nullopt;
		}
		return static_cast<std::uint32_t>(field->get<std::uint64_t>());
	}

	inline std::optional<Entry> ParseEntry(const nlohmann::json& a_entry)
	{
		const auto id = Payload::ReadTextField(a_entry, "id");
		const auto name = Payload::SanitizeText(Payload::ReadTextField(a_entry, "name").value_or(""), kMaximumNameBytes);
		const auto memberCount = ReadCount(a_entry, "memberCount");
		const auto onlineCount = ReadCount(a_entry, "onlineCount");
		const auto yours = a_entry.find("yours");
		if (!id || !Payload::IsSafeToken(*id, kMaximumIdBytes) || name.empty() || !memberCount || !onlineCount ||
			yours == a_entry.end() || !yours->is_boolean()) {
			return std::nullopt;
		}
		return Entry{
			*id,
			name,
			Payload::SanitizeText(Payload::ReadTextField(a_entry, "tag").value_or(""), kMaximumTagBytes),
			Payload::SanitizeText(Payload::ReadTextField(a_entry, "color").value_or(""), Payload::kMaximumColorBytes),
			Payload::SanitizeText(Payload::ReadTextField(a_entry, "description").value_or(""), Payload::kMaximumDescriptionBytes),
			Payload::SanitizeText(Payload::ReadTextField(a_entry, "leader").value_or(""), kMaximumLeaderBytes),
			*memberCount,
			*onlineCount,
			yours->get<bool>()
		};
	}

	inline std::optional<std::vector<Entry>> ParseFactionList(const nlohmann::json& a_packet)
	{
		const auto factions = a_packet.find("factions");
		if (factions == a_packet.end() || !factions->is_array() || factions->size() > kMaximumEntries) {
			return std::nullopt;
		}
		std::vector<Entry> entries;
		for (const auto& factionJson : *factions) {
			auto entry = ParseEntry(factionJson);
			if (!entry) {
				return std::nullopt;
			}
			entries.push_back(std::move(*entry));
		}
		return entries;
	}

	inline nlohmann::json ToJson(const std::vector<Entry>& a_entries)
	{
		std::vector<nlohmann::json> jsons;
		for (const auto& entry : a_entries) {
			jsons.push_back(nlohmann::json{
				{ "id", entry.id },
				{ "name", entry.name },
				{ "tag", entry.tag },
				{ "color", entry.color },
				{ "description", entry.description },
				{ "leader", entry.leader },
				{ "memberCount", entry.memberCount },
				{ "onlineCount", entry.onlineCount },
				{ "yours", entry.yours }
			});
		}
		std::vector<bool> keep(a_entries.size(), false);
		std::size_t bytes = 2;
		std::size_t kept = 0;
		for (const bool wantYours : { true, false }) {
			for (std::size_t index = 0; index < a_entries.size(); ++index) {
				if (a_entries[index].yours != wantYours) {
					continue;
				}
				const std::size_t cost = jsons[index].dump().size() + (kept == 0 ? 0U : 1U);
				if (bytes + cost > kMaximumJsonBytes) {
					break;
				}
				bytes += cost;
				++kept;
				keep[index] = true;
			}
		}
		auto array = nlohmann::json::array();
		for (std::size_t index = 0; index < jsons.size(); ++index) {
			if (keep[index]) {
				array.push_back(jsons[index]);
			}
		}
		return array;
	}
}
