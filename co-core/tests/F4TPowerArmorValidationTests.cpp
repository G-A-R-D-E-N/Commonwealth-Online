#include "F4TPowerArmorValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>
// TODO-verify: concept-level wire and plan checks, engine facts unverified; source:docs/code-notes/power-armor-sync.md.

int main()
{
	using Json = nlohmann::json;
	using F4T::PowerArmorValidation::NormalizePowerArmor;

	assert(!NormalizePowerArmor(Json::array()).has_value());
	assert(!NormalizePowerArmor(Json{ 1 }).has_value());
	assert(!NormalizePowerArmor(Json("text")).has_value());

	assert(!NormalizePowerArmor(Json::object()).has_value());
	assert(!NormalizePowerArmor(Json{ { "inFrame", true } }).has_value());
	assert(!NormalizePowerArmor(Json{ { "frameBaseFormId", "" } }).has_value());
	assert(!NormalizePowerArmor(Json{ { "pieces", Json::array() } }).has_value());
	assert(!NormalizePowerArmor(Json{ { "inFrame", false }, { "frameBaseFormId", "" } }).has_value());
	assert(!NormalizePowerArmor(Json{ { "inFrame", false }, { "pieces", Json::array() } }).has_value());
	assert(!NormalizePowerArmor(Json{ { "frameBaseFormId", "" }, { "pieces", Json::array() } }).has_value());

	const auto empty = NormalizePowerArmor(Json{ { "inFrame", false }, { "frameBaseFormId", "" }, { "pieces", Json::array() } });
	assert(empty.has_value());
	assert(empty->at("inFrame") == false);
	assert(empty->at("frameBaseFormId") == "");
	assert(empty->at("pieces").is_array());
	assert(empty->at("pieces").empty());
	assert(empty->size() == 3);

	const auto withKeys = [](Json a_overrides) {
		Json value{
			{ "inFrame", false },
			{ "frameBaseFormId", "" },
			{ "pieces", Json::array() }
		};
		for (auto& [key, node] : a_overrides.items()) {
			value[key] = std::move(node);
		}
		return value;
	};

	const Json full{
		{ "inFrame", true },
		{ "frameBaseFormId", "5678" },
		{ "pieces", Json::array({
			Json{ { "slot", "head" }, { "formId", "1234" }, { "ignored", 1 } },
			Json{ { "slot", "torso" }, { "formId", "0000ABCD" }, { "mods", Json::array({ Json{ { "mod", "99" }, { "index", 3 }, { "rank", 2 } } }) } }
		}) },
		{ "serverOnlyInjection", "strip-me" }
	};
	const auto normalized = NormalizePowerArmor(full);
	assert(normalized.has_value());
	assert(normalized->at("inFrame") == true);
	assert(normalized->at("frameBaseFormId") == "00005678");
	assert(normalized->at("pieces").size() == 2);
	assert(normalized->at("pieces")[0].at("slot") == "head");
	assert(normalized->at("pieces")[0].at("formId") == "00001234");
	assert(!normalized->at("pieces")[0].contains("ignored"));
	assert(normalized->at("pieces")[1].at("mods")[0].at("mod") == "00000099");
	assert(normalized->at("pieces")[1].at("mods")[0].at("index") == 3);
	assert(normalized->at("pieces")[1].at("mods")[0].at("rank") == 2);
	assert(!normalized->contains("serverOnlyInjection"));

	assert(!NormalizePowerArmor(withKeys(Json{ { "inFrame", "yes" } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "inFrame", 1 } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "frameBaseFormId", 1234 } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "frameBaseFormId", "zz" } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "frameBaseFormId", "123456789" } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::object() } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ 1 }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "slot", "head" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "formId", "1234" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "slot", "" }, { "formId", "1234" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "slot", std::string(65, 'x') }, { "formId", "1234" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "slot", "head" }, { "formId", "" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "slot", "head" }, { "formId", "0" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "slot", "head" }, { "formId", "00000000" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "slot", "head" }, { "formId", "nothex" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ Json{ { "slot", "head" }, { "formId", "123456789" } } }) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({
		Json{ { "slot", "head" }, { "formId", "1234" } },
		Json{ { "slot", "head" }, { "formId", "5678" } }
	}) } })).has_value());

	{
		Json pieces = Json::array();
		for (int index = 0; index < 8; ++index) {
			pieces.push_back(Json{ { "slot", "slot" + std::to_string(index) }, { "formId", "1234" } });
		}
		const auto atCap = NormalizePowerArmor(withKeys(Json{ { "pieces", pieces } }));
		assert(atCap.has_value());
		assert(atCap->at("pieces").size() == 8);
		pieces.push_back(Json{ { "slot", "slot8" }, { "formId", "1234" } });
		assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", pieces } })).has_value());
	}

	{
		Json mods = Json::array();
		for (int index = 0; index < 32; ++index) {
			mods.push_back(Json{ { "mod", "99" }, { "index", index }, { "rank", 1 } });
		}
		const Json piece{ { "slot", "head" }, { "formId", "1234" }, { "mods", mods } };
		assert(NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ piece }) } })).has_value());
		mods.push_back(Json{ { "mod", "99" }, { "index", 32 }, { "rank", 1 } });
		const Json overCap{ { "slot", "head" }, { "formId", "1234" }, { "mods", mods } };
		assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({ overCap }) } })).has_value());
	}

	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({
		Json{ { "slot", "head" }, { "formId", "1234" }, { "mods", Json::array({ Json{ { "mod", "0" }, { "index", 0 }, { "rank", 0 } } }) } }
	}) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({
		Json{ { "slot", "head" }, { "formId", "1234" }, { "mods", Json::array({ Json{ { "mod", "99" }, { "index", 256 }, { "rank", 0 } } }) } }
	}) } })).has_value());
	assert(!NormalizePowerArmor(withKeys(Json{ { "pieces", Json::array({
		Json{ { "slot", "head" }, { "formId", "1234" }, { "mods", Json::array({ Json{ { "mod", "99" }, { "index", 0 }, { "rank", -1 } } }) } }
	}) } })).has_value());

	return 0;
}
