#include "F4TActionEventValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>

int main()
{
	using Json = nlohmann::json;
	using F4T::ActionEventValidation::NormalizeActionEvents;

	assert(NormalizeActionEvents(Json::object(), false).empty());
	assert(NormalizeActionEvents(Json::object(), true).empty());

	const Json valid = Json::array({
		Json{
			{ "sequence", 1 },
			{ "type", 1 },
			{ "eventName", "meleeattackStart" },
			{ "actorStateFlags1", 7 },
			{ "ignored", true }
		},
		Json{
			{ "sequence", 2 },
			{ "type", 3 },
			{ "eventName", "fireSingle" },
			{ "actorStateFlags2", "invalid-optional" }
		}
	});
	const auto normalized = NormalizeActionEvents(valid, true);
	assert(normalized.size() == 2);
	assert(normalized[0].at("actorStateFlags1") == 7);
	assert(!normalized[0].contains("ignored"));
	assert(!normalized[1].contains("actorStateFlags2"));

	const Json floating = Json::array({ Json{
		{ "sequence", 3.0 },
		{ "type", 3.0 },
		{ "eventName", "fireSingle" },
		{ "actorStateFlags1", 9.0 }
	} });
	const auto floatingNormalized = NormalizeActionEvents(floating, true);
	assert(floatingNormalized.size() == 1);
	assert(floatingNormalized[0].at("sequence") == 3);
	assert(floatingNormalized[0].at("type") == 3);
	assert(floatingNormalized[0].at("actorStateFlags1") == 9);

	Json fractional = floating;
	fractional[0]["sequence"] = 3.5;
	assert(NormalizeActionEvents(fractional, true).empty());
	fractional = floating;
	fractional[0]["type"] = 2.5;
	assert(NormalizeActionEvents(fractional, true).empty());
	fractional = floating;
	fractional[0]["actorStateFlags1"] = 9.5;
	const auto optionalFractional = NormalizeActionEvents(fractional, true);
	assert(optionalFractional.size() == 1);
	assert(!optionalFractional[0].contains("actorStateFlags1"));

	Json withInvalid = valid;
	withInvalid.push_back(Json{ { "sequence", 0 }, { "type", 3 }, { "eventName", "fireSingle" } });
	assert(NormalizeActionEvents(withInvalid, true).empty());
	const auto lenient = NormalizeActionEvents(withInvalid, false);
	assert(lenient.size() == 2);

	Json tooMany = Json::array();
	for (int index = 1; index <= 20; ++index) {
		tooMany.push_back(Json{ { "sequence", index }, { "type", 3 }, { "eventName", "fireSingle" } });
	}
	assert(NormalizeActionEvents(tooMany, false).size() == 16);
	assert(NormalizeActionEvents(tooMany, true).size() == 16);

	return 0;
}
