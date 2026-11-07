#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace F4T::ProgressionValidation
{
	inline constexpr const char* kSpecialFields[] = {
		"strength",
		"perception",
		"endurance",
		"charisma",
		"intelligence",
		"agility",
		"luck"
	};
	// TODO-verify: SPECIAL 1..10 bounds are unconfirmed against vanilla; confirm in game.
	inline constexpr std::int64_t kMinimumSpecialValue = 1;
	inline constexpr std::int64_t kMaximumSpecialValue = 10;
	inline constexpr std::int64_t kMinimumPlayerLevel = 1;
	inline constexpr std::int64_t kMaximumPlayerLevel = 65535;
	inline constexpr std::int64_t kMaximumExperience = 4294967295;
	inline constexpr std::int64_t kMinimumPerkRank = 1;
	inline constexpr std::int64_t kMaximumPerkRank = 10;
	inline constexpr std::size_t kMaximumOwnedPerks = 512;

	[[nodiscard]] std::optional<nlohmann::json> NormalizeProgression(const nlohmann::json& a_progression);
	[[nodiscard]] bool IsProgressionAccepted(const nlohmann::json& a_previousPlayerState, const nlohmann::json& a_progression);
}
