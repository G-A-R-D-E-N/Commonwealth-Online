#include "F4TPlayerStateValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

int main()
{
	using Json = nlohmann::json;
	using F4T::PlayerStateValidation::NormalizePlayerState;

	assert(!NormalizePlayerState(Json::object()).has_value());
	assert(!NormalizePlayerState(Json::array()).has_value());

	const Json valid{
		{ "type", "playerState" },
		{ "characterName", "Nomad" },
		{ "equippedItems", Json::array({
			Json{ { "slot", "rightHand" }, { "formId", "14" }, { "ignored", 1 } },
			Json{ { "slot", "leftHand" }, { "formId", 123 } }
		}) },
		{ "appearance", Json{ { "raceFormId", "14" }, { "height", 1.0 }, { "ignored", true } } },
		{ "actionEvents", Json::array({ Json{
			{ "sequence", 1 },
			{ "type", 3 },
			{ "eventName", "fireSingle" },
			{ "actorStateFlags1", "drop-me" }
		} }) },
		{ "serverOnlyInjection", "strip-me" }
	};
	const auto normalized = NormalizePlayerState(valid);
	assert(normalized.has_value());
	assert(normalized->at("type") == "playerState");
	assert(normalized->at("characterName") == "Nomad");
	assert(normalized->at("equippedItems")[0].at("formId") == "00000014");
	assert(normalized->at("equippedItems")[1].at("formId") == "");
	assert(!normalized->at("equippedItems")[0].contains("ignored"));
	assert(normalized->at("appearance").at("version") == 4);
	assert(normalized->at("appearance").at("raceFormId") == "00000014");
	assert(!normalized->at("appearance").contains("ignored"));
	assert(!normalized->at("actionEvents")[0].contains("actorStateFlags1"));
	assert(!normalized->contains("serverOnlyInjection"));

	Json invalid = valid;
	invalid["appearance"]["height"] = 4.1;
	const auto withoutAppearance = NormalizePlayerState(invalid);
	assert(withoutAppearance.has_value());
	assert(!withoutAppearance->contains("appearance"));
	assert(withoutAppearance->at("equippedItems")[0].at("slot") == "rightHand");

	invalid = valid;
	invalid["equippedItems"][0]["slot"] = "";
	assert(!NormalizePlayerState(invalid).has_value());

	invalid = valid;
	invalid["equippedItems"].push_back(Json{ { "slot", "rightHand" }, { "formId", "9999" } });
	assert(!NormalizePlayerState(invalid).has_value());

	invalid = valid;
	invalid["actionEvents"][0]["sequence"] = 0;
	assert(!NormalizePlayerState(invalid).has_value());

	invalid = valid;
	invalid["actionEvents"] = Json::array();
	for (int index = 1; index <= 17; ++index) {
		invalid["actionEvents"].push_back(Json{ { "sequence", index }, { "type", 3 }, { "eventName", "fireSingle" } });
	}
	assert(!NormalizePlayerState(invalid).has_value());

	Json nameOnly{ { "characterName", std::string(128, 'N') } };
	assert(NormalizePlayerState(nameOnly).has_value());
	nameOnly["characterName"] = std::string(129, 'N');
	assert(!NormalizePlayerState(nameOnly).has_value());

		const auto ItemWithMods = [](Json a_mods) {
		return Json{ { "type", "playerState" }, { "equippedItems", Json::array({
			Json{ { "slot", "rightHand" }, { "form" "Id", "1F66A" }, { "mods", std::move(a_mods) } } }) } };
	};
	const auto modded = NormalizePlayerState(ItemWithMods(Json::array({
		Json{ { "mod", "4F21D" }, { "index", 0 }, { "rank", 1 }, { "extra", true } },
		Json{ { "mod", "184C4B" }, { "index", 2 }, { "rank", 0 } } })));
	assert(modded.has_value());
	const auto& recipe = modded->at("equippedItems")[0].at("mods");
	assert(recipe.size() == 2);
	assert(recipe[0] == (Json{ { "mod", "0004F21D" }, { "index", 0 }, { "rank", 1 } }));
	assert(recipe[1].at("mod") == "00184C4B");
	assert(!NormalizePlayerState(valid)->at("equippedItems")[0].contains("mods"));
	assert(!NormalizePlayerState(ItemWithMods(Json::array({ Json{ { "mod", "zz" }, { "index", 0 }, { "rank", 0 } } }))));
	assert(!NormalizePlayerState(ItemWithMods(Json::array({ Json{ { "mod", "0" }, { "index", 0 }, { "rank", 0 } } }))));
	assert(!NormalizePlayerState(ItemWithMods(Json::array({ Json{ { "mod", "4F21D" }, { "index", 256 }, { "rank", 0 } } }))));
	assert(!NormalizePlayerState(ItemWithMods(Json::array({ Json{ { "mod", "4F21D" }, { "rank", 0 } } }))));
	assert(!NormalizePlayerState(ItemWithMods(Json::object())));
	Json tooMany = Json::array();
	for (int index = 0; index < 33; ++index) {
		tooMany.push_back(Json{ { "mod", "4F21D" }, { "index", index }, { "rank", 0 } });
	}
	assert(!NormalizePlayerState(ItemWithMods(tooMany)));

	const Json vitals{
		{ "type", "playerState" },
		{ "vitals", Json{ { "healthPercent", 87 }, { "isDowned", false }, { "isDead", false }, { "level", 42 } } }
	};
	const auto normalizedVitals = NormalizePlayerState(vitals);
	assert(normalizedVitals.has_value());
	assert(normalizedVitals->at("vitals").size() == 4);
	assert(normalizedVitals->at("vitals").at("healthPercent") == 87);
	assert(normalizedVitals->at("vitals").at("isDowned") == false);
	assert(normalizedVitals->at("vitals").at("level") == 42);
	assert(!normalizedVitals->contains("equippedItems"));

	const auto vitalsOnly = NormalizePlayerState(Json{
		{ "vitals", Json{ { "healthPercent", 0 }, { "isDead", true } } } });
	assert(vitalsOnly.has_value());
	assert(vitalsOnly->at("vitals").size() == 2);
	assert(vitalsOnly->at("vitals").at("isDead") == true);

	assert(!NormalizePlayerState(Json{ { "vitals", Json::array() } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "isDowned", true } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", 101 } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", -1 } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", 50.5 } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", "50" } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", 50 }, { "isDead", 1 } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", 50 }, { "isDowned", "true" } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", 50 }, { "level", 0 } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", 50 }, { "level", 65536 } } } }));
	assert(NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", 50 }, { "level", 65535 } } } }));
	assert(!NormalizePlayerState(Json{ { "vitals", Json{ { "healthPercent", 50 }, { "level", 1.5 } } } }));

	const auto stripped = NormalizePlayerState(Json{
		{ "vitals", Json{ { "healthPercent", 50 }, { "serverOnlyInjection", "strip-me" } } } });
	assert(stripped.has_value());
	assert(stripped->at("vitals").size() == 1);

	Json vitalsAndDurable = valid;
	vitalsAndDurable["vitals"] = Json{ { "healthPercent", 12 }, { "isDowned", true } };
	const auto combined = NormalizePlayerState(vitalsAndDurable);
	assert(combined.has_value());
	assert(combined->at("characterName") == "Nomad");
	assert(combined->at("vitals").at("isDowned") == true);

	return 0;
}
