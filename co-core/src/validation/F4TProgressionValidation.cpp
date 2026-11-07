#include "F4TProgressionValidation.h"

#include "F4TNetworkValidation.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <utility>

namespace F4T::ProgressionValidation
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::int64_t> ReadInteger(const Json& a_node, const char* a_field, std::int64_t a_minimum, std::int64_t a_maximum)
		{
			const auto found = a_node.find(a_field);
			if (found == a_node.end() || !found->is_number_integer()) {
				return std::nullopt;
			}
			const auto value = found->get<std::int64_t>();
			if (value < a_minimum || value > a_maximum) {
				return std::nullopt;
			}
			return value;
		}

		std::optional<std::string> NormalizePerkFormId(const Json& a_perk)
		{
			const auto found = a_perk.find("formId");
			if (found == a_perk.end() || !found->is_string()) {
				return std::nullopt;
			}
			const auto& text = found->get_ref<const std::string&>();
			if (!F4T::NetworkValidation::IsValidHexFormId(text, false, false)) {
				return std::nullopt;
			}
			std::uint32_t parsed = 0;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
			if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
				return std::nullopt;
			}
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), "%08X", parsed);
			return std::string{ buffer };
		}

		std::optional<Json> NormalizePerks(const Json& a_perks)
		{
			if (!a_perks.is_array() || a_perks.size() > kMaximumOwnedPerks) {
				return std::nullopt;
			}
			Json perks = Json::array();
			std::unordered_set<std::string> seen;
			seen.reserve(a_perks.size());
			for (const auto& node : a_perks) {
				if (!node.is_object()) {
					return std::nullopt;
				}
				const auto formId = NormalizePerkFormId(node);
				const auto rank = ReadInteger(node, "rank", kMinimumPerkRank, kMaximumPerkRank);
				if (!formId || !rank || !seen.insert(*formId).second) {
					return std::nullopt;
				}
				perks.push_back(Json{ { "formId", *formId }, { "rank", *rank } });
			}
			return perks;
		}

		// TODO-verify: 50 XP per level is a lenient floor, not the game curve; confirm in game.
		constexpr std::int64_t kExperiencePerLevelFloor = 50;
		constexpr std::int64_t kLevelStepAllowance = 10;

		std::int64_t MaximumLevelForExperience(std::int64_t a_experience)
		{
			return kMinimumPlayerLevel + a_experience / kExperiencePerLevelFloor;
		}
	}

	std::optional<Json> NormalizeProgression(const Json& a_progression)
	{
		if (!a_progression.is_object()) {
			return std::nullopt;
		}
		const auto special = a_progression.find("special");
		if (special == a_progression.end() || !special->is_object()) {
			return std::nullopt;
		}
		Json values = Json::object();
		for (const char* field : kSpecialFields) {
			const auto value = ReadInteger(*special, field, kMinimumSpecialValue, kMaximumSpecialValue);
			if (!value) {
				return std::nullopt;
			}
			values[field] = *value;
		}
		const auto level = ReadInteger(a_progression, "level", kMinimumPlayerLevel, kMaximumPlayerLevel);
		const auto experience = ReadInteger(a_progression, "experience", 0, kMaximumExperience);
		const auto perksFound = a_progression.find("perks");
		if (!level || !experience || perksFound == a_progression.end()) {
			return std::nullopt;
		}
		auto perks = NormalizePerks(*perksFound);
		if (!perks) {
			return std::nullopt;
		}
		return Json{
			{ "special", std::move(values) },
			{ "level", *level },
			{ "experience", *experience },
			{ "perks", std::move(*perks) }
		};
	}

	bool IsProgressionAccepted(const Json& a_previousPlayerState, const Json& a_progression)
	{
		const auto level = ReadInteger(a_progression, "level", kMinimumPlayerLevel, kMaximumPlayerLevel);
		const auto experience = ReadInteger(a_progression, "experience", 0, kMaximumExperience);
		if (!level || !experience) {
			return false;
		}
		if (*experience == 0) {
			return true;
		}
		std::int64_t previousLevel = kMinimumPlayerLevel;
		if (const auto previous = a_previousPlayerState.find("progression"); previous != a_previousPlayerState.end()) {
			previousLevel = ReadInteger(*previous, "level", kMinimumPlayerLevel, kMaximumPlayerLevel).value_or(kMinimumPlayerLevel);
		}
		return *level <= std::max(previousLevel, MaximumLevelForExperience(*experience)) + kLevelStepAllowance;
	}
}
