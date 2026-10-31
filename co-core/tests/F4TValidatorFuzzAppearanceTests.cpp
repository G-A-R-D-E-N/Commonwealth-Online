#include "F4TValidatorFuzzHarness.h"

#include "F4TAppearanceValidation.h"
#include "F4TPlayerStateValidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
// TODO-verify: concept-level wire and plan checks, engine facts unverified; source:docs/code-notes/power-armor-sync.md.

// Bounds and key sets cited from: source:co-core/src/validation/F4TAppearanceValidation.cpp, source:co-core/src/validation/F4TPlayerStateValidation.cpp, CO-BETA/docs/protocol/packets.md (appearance v4 and transform equipment fields).
namespace
{
	using namespace ValidatorFuzz;
	using F4T::AppearanceValidation::NormalizeAppearance;
	using F4T::PlayerStateValidation::NormalizePlayerState;

	constexpr std::size_t kFormIdDigits = 8;
	constexpr std::size_t kMaximumHeadParts = 64;
	constexpr std::size_t kMaximumMorphs = 128;
	constexpr std::size_t kMaximumMorphRegions = 128;
	constexpr std::size_t kMaximumFacialBoneMorphs = 128;
	constexpr std::size_t kMaximumTints = 128;
	constexpr std::size_t kMaximumEquippedItems = 32;
	constexpr std::size_t kMaximumEquipmentSlotBytes = 64;
	constexpr std::size_t kMaximumActionEvents = 16;
	constexpr std::size_t kMaximumPowerArmorPieces = 8;
	constexpr std::size_t kMaximumCharacterNameBytes = 128;
	constexpr const char* kAppearanceFormKeys[] = { "raceFormId", "hairColorFormId", "facialHairColorFormId", "complexionFormId" };

	bool IsHexDigit(char a_ch)
	{
		return (a_ch >= '0' && a_ch <= '9') || (a_ch >= 'a' && a_ch <= 'f') || (a_ch >= 'A' && a_ch <= 'F');
	}

	std::optional<std::string> CanonicalFormId(const Json& a_value)
	{
		if (!a_value.is_string()) {
			return std::nullopt;
		}
		const auto& text = a_value.get_ref<const std::string&>();
		if (text.empty()) {
			return std::string{};
		}
		if (text.size() > kFormIdDigits || !std::all_of(text.begin(), text.end(), IsHexDigit)) {
			return std::nullopt;
		}
		std::uint32_t value = 0;
		for (const char ch : text) {
			value = value * 16U + static_cast<std::uint32_t>(ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
		}
		return HexWord(value);
	}

	bool Version(const Json& a_value) { return InRange(a_value, 1.0, 1000.0); }
	bool Height(const Json& a_value) { return InRange(a_value, 0.25, 4.0); }
	bool MorphAmount(const Json& a_value) { return InRange(a_value, -100.0, 100.0); }
	bool MorphValue(const Json& a_value) { return InRange(a_value, -1000.0, 1000.0); }
	bool ColorChannel(const Json& a_value) { return InRange(a_value, 0.0, 255.0); }
	bool Uint16(const Json& a_value) { return InRange(a_value, 0.0, 65535.0); }
	bool Uint32(const Json& a_value) { return InRange(a_value, 0.0, kUint32Max); }
	bool Vec3Within(const Json& a_value, double a_limit)
	{
		return a_value.is_array() && a_value.size() == 3 &&
		       std::all_of(a_value.begin(), a_value.end(), [a_limit](const Json& a_child) { return InRange(a_child, -a_limit, a_limit); });
	}
	bool Vec3Position(const Json& a_value) { return Vec3Within(a_value, 10000.0); }
	bool Vec3Scale(const Json& a_value) { return Vec3Within(a_value, 100.0); }
	bool ActionType(const Json& a_value) { return InRange(a_value, 1.0, 3.0); }
	bool IsText(const Json& a_value) { return a_value.is_string(); }
	bool SlotName(const Json& a_value)
	{
		return a_value.is_string() && !a_value.get_ref<const std::string&>().empty() &&
		       a_value.get_ref<const std::string&>().size() <= kMaximumEquipmentSlotBytes;
	}
	bool CharacterName(const Json& a_value)
	{
		return a_value.is_string() && a_value.get_ref<const std::string&>().size() <= kMaximumCharacterNameBytes;
	}

	bool MorphWeights(const Json& a_value)
	{
		return a_value.is_object() && OnlyKeys(a_value, { "thin", "muscular", "large" }) &&
		       AllFields(a_value, { "thin", "muscular", "large" }, MorphAmount, true);
	}

	bool BodyTint(const Json& a_value)
	{
		return a_value.is_object() && OnlyKeys(a_value, { "r", "g", "b", "a" }) &&
		       AllFields(a_value, { "r", "g", "b", "a" }, ColorChannel, true);
	}

	bool MorphEntry(const Json& a_value)
	{
		return a_value.is_object() && OnlyKeys(a_value, { "id", "value" }) &&
		       Field(a_value, "id", HexAny, true) && Field(a_value, "value", MorphValue, true);
	}

	bool FacialBoneEntry(const Json& a_value)
	{
		return a_value.is_object() && OnlyKeys(a_value, { "id", "position", "rotation", "scale" }) &&
		       Field(a_value, "id", HexAny, true) && Field(a_value, "position", Vec3Position, true) &&
		       Field(a_value, "rotation", Vec3Position, true) && Field(a_value, "scale", Vec3Scale, true);
	}

	bool TintEntry(const Json& a_value)
	{
		return a_value.is_object() && OnlyKeys(a_value, { "id", "type", "value", "color", "swatch" }) &&
		       Field(a_value, "id", Uint16, true) && Field(a_value, "type", Uint32, true) &&
		       Field(a_value, "value", ColorChannel, true) && Field(a_value, "color", HexAny) &&
		       Field(a_value, "swatch", Uint16) && (!a_value.contains("swatch") || a_value.contains("color"));
	}

	bool HeadParts(const Json& a_value) { return Elements(a_value, kMaximumHeadParts, HexAny); }
	bool Morphs(const Json& a_value) { return Elements(a_value, kMaximumMorphs, MorphEntry); }
	bool MorphRegions(const Json& a_value) { return Elements(a_value, kMaximumMorphRegions, MorphValue); }
	bool FacialBoneMorphs(const Json& a_value) { return Elements(a_value, kMaximumFacialBoneMorphs, FacialBoneEntry); }
	bool Tints(const Json& a_value) { return Elements(a_value, kMaximumTints, TintEntry); }

	bool AppearanceShape(const Json& a_value)
	{
		return a_value.is_object() &&
		       OnlyKeys(a_value, { "version", "raceFormId", "hairColorFormId", "facialHairColorFormId", "complexionFormId",
		                           "height", "isFemale", "morphWeight", "bodyTintColor", "headParts", "morphs",
		                           "morphRegions", "facialBoneMorphs", "tints" }) &&
		       Field(a_value, "version", Version, true) &&
		       AllFields(a_value, { "raceFormId", "hairColorFormId", "facialHairColorFormId", "complexionFormId" }, HexOrEmpty, false) &&
		       Field(a_value, "height", Height) && Field(a_value, "isFemale", Flag) &&
		       Field(a_value, "morphWeight", MorphWeights) && Field(a_value, "bodyTintColor", BodyTint) &&
		       Field(a_value, "headParts", HeadParts) && Field(a_value, "morphs", Morphs) &&
		       Field(a_value, "morphRegions", MorphRegions) && Field(a_value, "facialBoneMorphs", FacialBoneMorphs) &&
		       Field(a_value, "tints", Tints);
	}

	bool SameJson(const Json& a_input, const Json& a_output) { return a_input == a_output; }

	bool FormValuePreserved(const Json& a_input, const Json& a_output)
	{
		const auto expected = CanonicalFormId(a_input);
		return expected && a_output.is_string() && a_output.get_ref<const std::string&>() == *expected;
	}

	template <typename Check>
	bool KeyPreserved(const Json& a_input, const Json& a_output, const char* a_key, Check a_check)
	{
		const auto in = a_input.find(a_key);
		const auto out = a_output.find(a_key);
		if (in == a_input.end() || out == a_output.end()) {
			return in == a_input.end() && out == a_output.end();
		}
		return a_check(*in, *out);
	}

	bool SameValue(const Json& a_input, const Json& a_output, const char* a_key)
	{
		return KeyPreserved(a_input, a_output, a_key, SameJson);
	}

	template <typename Check>
	bool ElementsPreserved(const Json& a_input, const Json& a_output, const char* a_key, Check a_check)
	{
		return KeyPreserved(a_input, a_output, a_key, [&](const Json& a_in, const Json& a_out) {
			if (!a_in.is_array() || !a_out.is_array() || a_in.size() != a_out.size()) {
				return false;
			}
			for (std::size_t index = 0; index < a_in.size(); ++index) {
				if (!a_check(a_in[index], a_out[index])) {
					return false;
				}
			}
			return true;
		});
	}

	bool MorphPreserved(const Json& a_input, const Json& a_output)
	{
		return KeyPreserved(a_input, a_output, "id", FormValuePreserved) && SameValue(a_input, a_output, "value");
	}

	bool FacialBonePreserved(const Json& a_input, const Json& a_output)
	{
		return KeyPreserved(a_input, a_output, "id", FormValuePreserved) && SameValue(a_input, a_output, "position") &&
		       SameValue(a_input, a_output, "rotation") && SameValue(a_input, a_output, "scale");
	}

	bool TintPreserved(const Json& a_input, const Json& a_output)
	{
		return SameValue(a_input, a_output, "id") && SameValue(a_input, a_output, "type") &&
		       SameValue(a_input, a_output, "value") && KeyPreserved(a_input, a_output, "color", FormValuePreserved);
	}

	bool AppearancePreserved(const Json& a_input, const Json& a_output)
	{
		return AppearanceShape(a_output) && SameValue(a_input, a_output, "height") &&
		       SameValue(a_input, a_output, "isFemale") &&
		       std::all_of(std::begin(kAppearanceFormKeys), std::end(kAppearanceFormKeys), [&](const char* a_key) {
			       return KeyPreserved(a_input, a_output, a_key, FormValuePreserved);
		       }) &&
		       ElementsPreserved(a_input, a_output, "headParts", FormValuePreserved) &&
		       ElementsPreserved(a_input, a_output, "morphs", MorphPreserved) &&
		       ElementsPreserved(a_input, a_output, "morphRegions", SameJson) &&
		       ElementsPreserved(a_input, a_output, "facialBoneMorphs", FacialBonePreserved) &&
		       ElementsPreserved(a_input, a_output, "tints", TintPreserved);
	}

	bool EquipmentEntry(const Json& a_value)
	{
		return a_value.is_object() && OnlyKeys(a_value, { "slot", "formId" }) &&
		       Field(a_value, "slot", SlotName, true) && Field(a_value, "formId", HexOrEmpty, true);
	}

	bool UniqueSlots(const Json& a_value)
	{
		std::set<std::string> slots;
		for (const auto& item : a_value) {
			slots.insert(item.at("slot").get<std::string>());
		}
		return slots.size() == a_value.size();
	}

	bool EquipmentList(const Json& a_value)
	{
		return Elements(a_value, kMaximumEquippedItems, EquipmentEntry) && UniqueSlots(a_value);
	}

	bool EquipmentPreserved(const Json& a_input, const Json& a_output)
	{
		const auto form = a_input.find("formId");
		const auto expected = form != a_input.end() && form->is_string() ? CanonicalFormId(*form) : std::optional<std::string>{ std::string{} };
		return SameValue(a_input, a_output, "slot") && expected && a_output.contains("formId") &&
		       a_output.at("formId") == Json(*expected);
	}

	bool ActionEntry(const Json& a_value)
	{
		return a_value.is_object() &&
		       OnlyKeys(a_value, { "sequence", "type", "eventName", "actorStateFlags1", "actorStateFlags2" }) &&
		       Field(a_value, "sequence", Positive32, true) && Field(a_value, "type", ActionType, true) &&
		       Field(a_value, "eventName", IsText, true) && Field(a_value, "actorStateFlags1", Uint32) &&
		       Field(a_value, "actorStateFlags2", Uint32);
	}

	bool ActionEvents(const Json& a_value) { return Elements(a_value, kMaximumActionEvents, ActionEntry); }

	bool ActionPreserved(const Json& a_input, const Json& a_output)
	{
		return SameValue(a_input, a_output, "sequence") && SameValue(a_input, a_output, "type") &&
		       SameValue(a_input, a_output, "eventName");
	}

	bool PowerArmorPiece(const Json& a_value)
	{
		return a_value.is_object() && OnlyKeys(a_value, { "slot", "formId", "mods" }) &&
		       Field(a_value, "slot", SlotName, true) && Field(a_value, "formId", HexAny, true) &&
		       (!a_value.contains("mods") || a_value.at("mods").is_array());
	}

	bool PowerArmorPieces(const Json& a_value) { return Elements(a_value, kMaximumPowerArmorPieces, PowerArmorPiece); }

	bool PowerArmorShape(const Json& a_value)
	{
		return a_value.is_object() &&
		       OnlyKeys(a_value, { "inFrame", "frameBaseFormId", "pieces" }) &&
		       Field(a_value, "inFrame", Flag, true) && Field(a_value, "frameBaseFormId", HexOrEmpty, true) &&
		       Field(a_value, "pieces", PowerArmorPieces, true);
	}

	bool PlayerStateShape(const Json& a_value)
	{
		return a_value.is_object() &&
		       OnlyKeys(a_value, { "type", "equippedItems", "appearance", "actionEvents", "characterName", "powerArmor" }) &&
		       TypeIs(a_value, "playerState") &&
		       (a_value.contains("equippedItems") || a_value.contains("appearance") ||
		           a_value.contains("actionEvents") || a_value.contains("characterName") ||
		           a_value.contains("powerArmor")) &&
		       Field(a_value, "equippedItems", EquipmentList) && Field(a_value, "appearance", AppearanceShape) &&
		       Field(a_value, "actionEvents", ActionEvents) && Field(a_value, "characterName", CharacterName) &&
		       Field(a_value, "powerArmor", PowerArmorShape);
	}

	bool AppearanceInvariant(const Json& a_input, const Json& a_output)
	{
		return AppearancePreserved(a_input, a_output);
	}

	bool AppearanceKeptOrDropped(const Json& a_input, const Json& a_output)
	{
		const auto in = a_input.find("appearance");
		if (in != a_input.end() && !a_output.contains("appearance")) {
			return !NormalizeAppearance(*in);
		}
		return KeyPreserved(a_input, a_output, "appearance", AppearancePreserved);
	}

	bool PlayerStateInvariant(const Json& a_input, const Json& a_output)
	{
		return PlayerStateShape(a_output) && ElementsPreserved(a_input, a_output, "equippedItems", EquipmentPreserved) &&
		       ElementsPreserved(a_input, a_output, "actionEvents", ActionPreserved) &&
		       AppearanceKeptOrDropped(a_input, a_output) &&
		       SameValue(a_input, a_output, "characterName");
	}

	Json AppearanceSample()
	{
		return Json{
			{ "version", 4 },
			{ "raceFormId", HexWord(20) },
			{ "hairColorFormId", "" },
			{ "facialHairColorFormId", HexWord(0) },
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
				Json{ { "id", 0 }, { "type", 0 }, { "value", 0 }, { "color", "0" } },
				Json{ { "id", 65535 }, { "type", 4294967295U }, { "value", 255 } }
			}) }
		};
	}

	Json PlayerStateSample()
	{
		return Json{
			{ "characterName", "Nomad" },
			{ "equippedItems", Json::array({
				Json{ { "slot", "rightHand" }, { "formId", HexWord(20) } },
				Json{ { "slot", "leftHand" }, { "formId", "" } }
			}) },
			{ "appearance", AppearanceSample() },
			{ "actionEvents", Json::array({ Json{ { "sequence", 1 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
		};
	}

	const JsonTarget kTargets[] = {
		{ "NormalizeAppearance", AppearanceSample, NormalizeAppearance, AppearanceInvariant },
		{ "NormalizePlayerState", PlayerStateSample, NormalizePlayerState, PlayerStateInvariant }
	};
}

int main()
{
	std::uint64_t seed = kBaseSeed;
	for (const auto& target : kTargets) {
		FuzzJsonTarget(target, seed++);
	}
	std::cout << "validator appearance fuzz passed: json_targets=" << std::size(kTargets)
	          << " iterations_per_target=" << kIterations << " base_seed=" << kBaseSeed << '\n';
	return 0;
}
