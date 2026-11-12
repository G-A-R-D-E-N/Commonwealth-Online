#include "F4TPlayerStateValidation.h"
#include "F4TProgressionValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <string>

namespace
{
	using Json = nlohmann::json;

	Json Progression(std::int64_t a_level, std::int64_t a_experience)
	{
		return Json{
			{ "special", Json{ { "strength", 5 }, { "perception", 5 }, { "endurance", 5 }, { "charisma", 5 }, { "intelligence", 5 }, { "agility", 5 }, { "luck", 5 } } },
			{ "level", a_level },
			{ "experience", a_experience },
			{ "perks", Json::array() }
		};
	}
}

// TODO-verify: SPECIAL bounds, the 50 XP-per-level floor and max experience are unconfirmed in game.
int main()
{
	using F4T::ProgressionValidation::IsProgressionAccepted;
	using F4T::ProgressionValidation::NormalizeProgression;
	using F4T::ProgressionValidation::RestartsLevelAllowance;
	using F4T::ProgressionValidation::kMaximumOwnedPerks;
	using F4T::PlayerStateValidation::NormalizePlayerState;

	const auto normalized = NormalizeProgression(Progression(2, 200));
	assert(normalized.has_value());
	assert(normalized->at("level") == 2);
	assert(normalized->at("experience") == 200);
	assert(normalized->at("special").at("luck") == 5);
	assert(normalized->at("perks").empty());

	Json withPerk = Progression(2, 200);
	withPerk["perks"] = Json::array({ Json{ { "formId", "14" }, { "rank", 2 } } });
	const auto perked = NormalizeProgression(withPerk);
	assert(perked.has_value());
	assert(perked->at("perks")[0].at("formId") == "00000014");
	assert(perked->at("perks")[0].at("rank") == 2);

	Json lowSpecial = Progression(2, 200);
	lowSpecial["special"]["strength"] = 0;
	assert(!NormalizeProgression(lowSpecial).has_value());

	Json highSpecial = Progression(2, 200);
	highSpecial["special"]["luck"] = 11;
	assert(!NormalizeProgression(highSpecial).has_value());

	Json missingSpecial = Progression(2, 200);
	missingSpecial["special"].erase("luck");
	assert(!NormalizeProgression(missingSpecial).has_value());

	assert(!NormalizeProgression(Progression(0, 0)).has_value());
	assert(!NormalizeProgression(Progression(2, -1)).has_value());
	assert(NormalizeProgression(Progression(1, 0)).has_value());
	assert(NormalizeProgression(Progression(1, 4294967295)).has_value());
	assert(!NormalizeProgression(Progression(1, 4294967296)).has_value());
	assert(NormalizeProgression(Progression(65535, 0)).has_value());
	assert(!NormalizeProgression(Progression(65536, 0)).has_value());

	Json zeroRank = withPerk;
	zeroRank["perks"][0]["rank"] = 0;
	assert(!NormalizeProgression(zeroRank).has_value());

	Json highRank = withPerk;
	highRank["perks"][0]["rank"] = 11;
	assert(!NormalizeProgression(highRank).has_value());

	Json zeroForm = withPerk;
	zeroForm["perks"][0]["formId"] = "0";
	assert(!NormalizeProgression(zeroForm).has_value());

	Json badForm = withPerk;
	badForm["perks"][0]["formId"] = "XYZ";
	assert(!NormalizeProgression(badForm).has_value());

	Json duplicate = withPerk;
	duplicate["perks"].push_back(withPerk["perks"][0]);
	assert(!NormalizeProgression(duplicate).has_value());

	// TODO-verify: the 512 owned-perk cap is a CO limit, not confirmed against the engine.
	Json fullPerks = Progression(2, 200);
	for (std::size_t index = 1; index <= kMaximumOwnedPerks; ++index) {
		fullPerks["perks"].push_back(Json{ { "formId", std::to_string(index) }, { "rank", 1 } });
	}
	assert(NormalizeProgression(fullPerks).has_value());
	fullPerks["perks"].push_back(Json{ { "formId", std::to_string(kMaximumOwnedPerks + 1) }, { "rank", 1 } });
	assert(!NormalizeProgression(fullPerks).has_value());

	assert(IsProgressionAccepted(Json::object(), Progression(2, 200), 0.0));
	assert(IsProgressionAccepted(Json::object(), Progression(1, 0), 0.0));
	assert(!IsProgressionAccepted(Json::object(), Progression(30, 0), 0.0));
	assert(!IsProgressionAccepted(Json::object(), Progression(20, 1), 0.0));
	assert(!IsProgressionAccepted(Json::object(), Progression(65535, 1), 0.0));

	Json stringLevel = Progression(2, 200);
	stringLevel["level"] = "2";
	assert(!IsProgressionAccepted(Json::object(), stringLevel, 0.0));
	Json fractionalExperience = Progression(2, 200);
	fractionalExperience["experience"] = 200.5;
	assert(!IsProgressionAccepted(Json::object(), fractionalExperience, 0.0));

	const Json previous{ { "progression", Progression(2, 300) } };
	assert(!IsProgressionAccepted(previous, Progression(1, 300), 0.0));
	assert(IsProgressionAccepted(previous, Progression(2, 1), 0.0));
	assert(IsProgressionAccepted(previous, Progression(12, 1), 0.0));
	assert(!IsProgressionAccepted(previous, Progression(13, 1), 0.0));
	assert(!IsProgressionAccepted(previous, Progression(30, 300), 0.0));
	assert(!IsProgressionAccepted(previous, Progression(30, 4294967295), 0.0));

	const Json timed{ { "progression", Progression(2, 300) }, { "updatedAt", 1000.0 } };
	assert(IsProgressionAccepted(timed, Progression(12, 300), 1000.0));
	assert(!IsProgressionAccepted(timed, Progression(13, 300), 1000.0));
	assert(IsProgressionAccepted(timed, Progression(32, 300), 1000.0 + 7200.0));
	assert(!IsProgressionAccepted(timed, Progression(33, 300), 1000.0 + 7200.0));

	const Json stamped{ { "progression", Progression(2, 300) }, { "updatedAt", 8200.0 }, { "progressionUpdatedAt", 1000.0 } };
	assert(IsProgressionAccepted(stamped, Progression(32, 300), 8200.0));
	assert(!IsProgressionAccepted(stamped, Progression(33, 300), 8200.0));
	assert(RestartsLevelAllowance(Json::object(), Progression(2, 300)));
	assert(RestartsLevelAllowance(timed, Progression(2, 300)));
	assert(RestartsLevelAllowance(stamped, Progression(3, 300)));
	assert(!RestartsLevelAllowance(stamped, Progression(2, 400)));

	const Json droppedToOne{ { "progression", Progression(1, 150) } };
	assert(!IsProgressionAccepted(droppedToOne, Progression(30, 150), 0.0));
	assert(IsProgressionAccepted(droppedToOne, Progression(2, 200), 0.0));
	assert(IsProgressionAccepted(Json{ { "characterName", "Nomad" } }, Progression(2, 300), 0.0));

	const auto cleaned = NormalizePlayerState(Json{ { "characterName", "Nomad" }, { "progression", Progression(0, 0) } });
	assert(cleaned.has_value());
	assert(cleaned->at("characterName") == "Nomad");
	assert(!cleaned->contains("progression"));

	const auto kept = NormalizePlayerState(Json{ { "progression", Progression(2, 200) } });
	assert(kept.has_value());
	assert(kept->at("progression").at("level") == 2);

	return 0;
}
