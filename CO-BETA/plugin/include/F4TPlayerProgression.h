#pragma once

#include "F4TProgressionValidation.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace F4T::PlayerProgression
{
	struct PerkRank
	{
		std::string formId;
		std::int32_t rank{};

		bool operator==(const PerkRank&) const = default;
	};

	// TODO-verify: SPECIAL, experience and perk values are unconfirmed in game.
	struct Progression
	{
		std::array<std::int32_t, std::size(ProgressionValidation::kSpecialFields)> special{};
		std::int32_t level{};
		std::int64_t experience{};
		std::vector<PerkRank> perks;

		bool operator==(const Progression&) const = default;
	};

	void PublishTick();
	void HandleRejectedPacket(const nlohmann::json& a_packet);
}
