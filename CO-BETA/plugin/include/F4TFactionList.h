#pragma once

#include "F4TFactionCapacity.h"
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
	inline constexpr std::size_t kMaximumEntries = F4T::FactionCapacity::ListPageEntries;
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

	inline std::optional<std::size_t> ReadPageNumber(const nlohmann::json& a_json, const char* a_field)
	{
		const auto field = a_json.find(a_field);
		if (field == a_json.end() || !field->is_number_integer() || field->get<std::int64_t>() < 0 || field->get<std::uint64_t>() > F4T::FactionCapacity::MaximumFactions) {
			return std::nullopt;
		}
		return static_cast<std::size_t>(field->get<std::uint64_t>());
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

	struct Page
	{
		std::vector<Entry> entries;
		std::size_t offset{ 0 };
		std::size_t total{ 0 };
	};

	inline std::optional<Page> ParseFactionPage(const nlohmann::json& a_packet)
	{
		auto entries = ParseFactionList(a_packet);
		const auto offset = a_packet.contains("offset") ? ReadPageNumber(a_packet, "offset") : std::optional<std::size_t>{ 0 };
		if (!entries || !offset) {
			return std::nullopt;
		}
		const auto total = a_packet.contains("total") ? ReadPageNumber(a_packet, "total") : std::optional<std::size_t>{ *offset + entries->size() };
		if (!total) {
			return std::nullopt;
		}
		return Page{ std::move(*entries), *offset, *total };
	}

	inline nlohmann::json ToJson(const std::vector<Entry>& a_entries)
	{
		auto array = nlohmann::json::array();
		for (const auto& entry : a_entries) {
			array.push_back(nlohmann::json{
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
		return array;
	}
}
