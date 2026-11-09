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

		constexpr std::int64_t kLevelsPerElapsedHour = 10;

		std::optional<double> ReadTimestamp(const Json& a_playerState, const char* a_field)
		{
			const auto found = a_playerState.find(a_field);
			if (found == a_playerState.end() || !found->is_number()) {
				return std::nullopt;
			}
			return found->get<double>();
		}

		std::optional<std::int64_t> StoredLevel(const Json& a_playerState)
		{
			const auto previous = a_playerState.find("progression");
			if (previous == a_playerState.end() || !previous->is_object()) {
				return std::nullopt;
			}
			return ReadInteger(*previous, "level", kMinimumPlayerLevel, kMaximumPlayerLevel);
		}

		std::int64_t ElapsedLevelAllowance(const Json& a_previousPlayerState, double a_nowSeconds)
		{
			auto updated = ReadTimestamp(a_previousPlayerState, "progressionUpdatedAt");
			if (!updated) {
				updated = ReadTimestamp(a_previousPlayerState, "updatedAt");
			}
			if (!updated) {
				return 0;
			}
			const auto hours = std::min((a_nowSeconds - *updated) / 3600.0, static_cast<double>(kMaximumPlayerLevel));
			return hours > 0.0 ? static_cast<std::int64_t>(hours) * kLevelsPerElapsedHour : 0;
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

	bool IsProgressionAccepted(const Json& a_previousPlayerState, const Json& a_progression, double a_nowSeconds)
	{
		const auto level = ReadInteger(a_progression, "level", kMinimumPlayerLevel, kMaximumPlayerLevel);
		const auto experience = ReadInteger(a_progression, "experience", 0, kMaximumExperience);
		if (!level || !experience) {
			return false;
		}
		const auto previousLevel = StoredLevel(a_previousPlayerState);
		if (!previousLevel) {
			return *level <= std::max(kMinimumPlayerLevel, MaximumLevelForExperience(*experience)) + kLevelStepAllowance;
		}
		if (*level < *previousLevel) {
			return false;
		}
		return *level <= *previousLevel + kLevelStepAllowance + ElapsedLevelAllowance(a_previousPlayerState, a_nowSeconds);
	}

	bool RestartsLevelAllowance(const Json& a_previousPlayerState, const Json& a_progression)
	{
		const auto level = ReadInteger(a_progression, "level", kMinimumPlayerLevel, kMaximumPlayerLevel);
		const auto previousLevel = StoredLevel(a_previousPlayerState);
		return !ReadTimestamp(a_previousPlayerState, "progressionUpdatedAt") || !level || !previousLevel || *level != *previousLevel;
	}
}
