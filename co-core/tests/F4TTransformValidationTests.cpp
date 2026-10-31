#include "F4TTransformValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>

int main()
{
	using Json = nlohmann::json;
	using F4T::TransformValidation::NormalizeTransform;

	const Json base{
		{ "type", "transform" },
		{ "x", 1.0 },
		{ "y", 2.0 },
		{ "z", 3.0 },
		{ "angleZ", 90.0 },
		{ "cellId", "ABC" },
		{ "worldspaceId", "0" },
		{ "movementType", "normal" },
		{ "unknownField", "preserve-me" }
	};
	const auto normalized = NormalizeTransform(base);
	assert(normalized.has_value());
	assert(normalized->at("cellId") == "00000ABC");
	assert(normalized->at("worldspaceId") == "00000000");
	assert(!normalized->contains("unknownField"));

	Json optionals = base;
	optionals["movementSpeed"] = 100000.0;
	optionals["animationGraphSpeed"] = -1.0;
	optionals["animationDirection"] = 360.0;
	optionals["aimPitch"] = -180.0;
	optionals["turnDelta"] = 10000.0;
	optionals["isMoving"] = true;
	optionals["isSprinting"] = "invalid";
	optionals["actorStateFlags1"] = 7.0;
	optionals["actorStateFlags2"] = -1;
	optionals["actionEvents"] = Json::array({
		Json{ { "sequence", 1.0 }, { "type", 3.0 }, { "eventName", "fireSingle" } },
		Json{ { "sequence", 0 }, { "type", 3 }, { "eventName", "fireSingle" } }
	});
	const auto cleaned = NormalizeTransform(optionals);
	assert(cleaned.has_value());
	assert(cleaned->contains("movementSpeed"));
	assert(cleaned->contains("animationGraphSpeed"));
	assert(cleaned->contains("animationDirection"));
	assert(cleaned->contains("aimPitch"));
	assert(cleaned->contains("turnDelta"));
	assert(cleaned->at("animationDirection") == 360.0);
	assert(cleaned->at("aimPitch") == -180.0);
	assert(cleaned->at("turnDelta") == 10000.0);
	assert(cleaned->contains("isMoving"));
	assert(!cleaned->contains("isSprinting"));
	assert(cleaned->at("actorStateFlags1") == 7);
	assert(!cleaned->contains("actorStateFlags2"));
	assert(cleaned->at("actionEvents").size() == 1);
	assert(cleaned->at("actionEvents")[0].at("sequence") == 1);

	Json fractionalFlags = base;
	fractionalFlags["actorStateFlags1"] = 7.5;
	const auto strippedFlags = NormalizeTransform(fractionalFlags);
	assert(strippedFlags.has_value());
	assert(!strippedFlags->contains("actorStateFlags1"));

	Json invalidOptional = base;
	invalidOptional["movementSpeed"] = -1.0;
	invalidOptional["animationGraphSpeed"] = 100001.0;
	invalidOptional["animationDirection"] = 361.0;
	invalidOptional["aimPitch"] = 181.0;
	invalidOptional["turnDelta"] = 10001.0;
	const auto stripped = NormalizeTransform(invalidOptional);
	assert(stripped.has_value());
	assert(!stripped->contains("movementSpeed"));
	assert(!stripped->contains("animationGraphSpeed"));
	assert(!stripped->contains("animationDirection"));
	assert(!stripped->contains("aimPitch"));
	assert(!stripped->contains("turnDelta"));

	Json nonArrayActions = base;
	nonArrayActions["actionEvents"] = Json::object();
	const auto noActions = NormalizeTransform(nonArrayActions);
	assert(noActions.has_value());
	assert(noActions->at("actionEvents").is_array());
	assert(noActions->at("actionEvents").empty());

	Json transition = base;
	transition["movementType"] = "fast_travel";
	assert(NormalizeTransform(transition).has_value());

	Json invalid = base;
	invalid["cellId"] = "0";
	assert(!NormalizeTransform(invalid).has_value());
	invalid = base;
	invalid["x"] = 10000001.0;
	assert(!NormalizeTransform(invalid).has_value());
	invalid = base;
	invalid["movementType"] = "warp";
	assert(!NormalizeTransform(invalid).has_value());
	invalid = base;
	invalid["worldspaceId"] = 12;
	assert(!NormalizeTransform(invalid).has_value());

	return 0;
}
