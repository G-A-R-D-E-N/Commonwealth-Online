#include "F4TAppearanceValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

int main()
{
	using Json = nlohmann::json;
	using F4T::AppearanceValidation::NormalizeAppearance;

	assert(!NormalizeAppearance(Json::array()).has_value());

	const auto defaults = NormalizeAppearance(Json::object());
	assert(defaults.has_value());
	assert(defaults->size() == 1);
	assert(defaults->at("version") == 4);

	const Json full{
		{ "version", 7 },
		{ "raceFormId", "14" },
		{ "hairColorFormId", "" },
		{ "facialHairColorFormId", "0" },
		{ "complexionFormId", "ABC" },
		{ "height", 1.05 },
		{ "isFemale", true },
		{ "morphWeight", Json{ { "thin", -10.0 }, { "muscular", 20.0 }, { "large", 30.0 } } },
		{ "bodyTintColor", Json{ { "r", 1 }, { "g", 2 }, { "b", 3 }, { "a", 255 } } },
		{ "headParts", Json::array({ "0", "ABC" }) },
		{ "morphs", Json::array({ Json{ { "id", "0" }, { "value", -25.0 } } }) },
		{ "morphRegions", Json::array({ -1000.0, 0.0, 1000.0 }) },
		{ "facialBoneMorphs", Json::array({ Json{
			{ "id", "1234" },
			{ "position", Json::array({ -10000.0, 0.0, 10000.0 }) },
			{ "rotation", Json::array({ 1.0, 2.0, 3.0 }) },
			{ "scale", Json::array({ -100.0, 0.0, 100.0 }) }
		} }) },
		{ "tints", Json::array({
			Json{ { "id", 0 }, { "type", 0u }, { "value", 0 }, { "color", "0" } },
			Json{ { "id", 65535 }, { "type", 4294967295u }, { "value", 255 } }
		}) },
		{ "serverOnlyInjection", "strip-me" }
	};
	const auto normalized = NormalizeAppearance(full);
	assert(normalized.has_value());
	assert(normalized->at("version") == 7);
	assert(normalized->at("raceFormId") == "00000014");
	assert(normalized->at("hairColorFormId") == "");
	assert(normalized->at("facialHairColorFormId") == "00000000");
	assert(normalized->at("complexionFormId") == "00000ABC");
	assert(normalized->at("headParts")[0] == "00000000");
	assert(normalized->at("headParts")[1] == "00000ABC");
	assert(normalized->at("morphs")[0].at("id") == "00000000");
	assert(normalized->at("facialBoneMorphs")[0].at("id") == "00001234");
	assert(normalized->at("tints")[0].at("color") == "00000000");
	assert(normalized->at("tints")[0].at("swatch") == 0);
	assert(!normalized->at("tints")[1].contains("swatch"));
	assert(!normalized->contains("serverOnlyInjection"));

	Json invalid = full;
	invalid["version"] = 0;
	assert(!NormalizeAppearance(invalid).has_value());

	invalid = full;
	invalid["raceFormId"] = "not-hex";
	assert(!NormalizeAppearance(invalid).has_value());

	invalid = full;
	invalid["height"] = 4.01;
	assert(!NormalizeAppearance(invalid).has_value());

	invalid = full;
	invalid["morphWeight"].erase("large");
	assert(!NormalizeAppearance(invalid).has_value());

	invalid = full;
	invalid["bodyTintColor"]["a"] = 256;
	assert(!NormalizeAppearance(invalid).has_value());

	invalid = full;
	invalid["headParts"] = Json::array();
	for (int index = 0; index < 65; ++index) {
		invalid["headParts"].push_back("0");
	}
	assert(!NormalizeAppearance(invalid).has_value());

	invalid = full;
	invalid["facialBoneMorphs"][0]["position"] = Json::array({ 0.0, 1.0 });
	assert(!NormalizeAppearance(invalid).has_value());

	invalid = full;
	invalid["tints"][0]["color"] = "";
	assert(!NormalizeAppearance(invalid).has_value());

	invalid = full;
	invalid["tints"][0]["swatch"] = 65536;
	assert(!NormalizeAppearance(invalid).has_value());

	return 0;
}
