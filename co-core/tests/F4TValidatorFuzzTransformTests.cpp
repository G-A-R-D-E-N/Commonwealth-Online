#include "F4TValidatorFuzzHarness.h"

#include "F4TActionEventValidation.h"
#include "F4TTransformValidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace
{
	using namespace ValidatorFuzz;

	constexpr std::size_t kMaximumActionEvents = 16;

	bool IsUint32Integer(const Json& a_value)
	{
		return a_value.is_number_integer() && InRange(a_value, 0.0, kUint32Max);
	}

	bool IsSequence(const Json& a_value)
	{
		return IsUint32Integer(a_value) && InRange(a_value, 1.0, kUint32Max);
	}

	bool IsEventType(const Json& a_value)
	{
		return a_value.is_number_integer() && InRange(a_value, 1.0, 3.0);
	}

	bool IsString(const Json& a_value)
	{
		return a_value.is_string();
	}

	bool IsAllowedEvent(const Json& a_type, const Json& a_name)
	{
		const auto type = a_type.get<std::uint32_t>();
		const auto name = std::string_view{ a_name.get_ref<const std::string&>() };
		return ((type == 1U || type == 2U) && name == "meleeattackStart") || (type == 3U && name == "fireSingle");
	}

	bool ActionEventEntry(const Json& a_value)
	{
		if (!a_value.is_object() ||
		    !OnlyKeys(a_value, { "sequence", "type", "eventName", "actorStateFlags1", "actorStateFlags2" })) {
			return false;
		}
		return Field(a_value, "sequence", IsSequence, true) &&
		       Field(a_value, "type", IsEventType, true) &&
		       Field(a_value, "eventName", IsString, true) &&
		       IsAllowedEvent(a_value.at("type"), a_value.at("eventName")) &&
		       AllFields(a_value, { "actorStateFlags1", "actorStateFlags2" }, IsUint32Integer, false);
	}

	bool MovementType(const Json& a_value)
	{
		constexpr std::string_view kTypes[] = {
			"normal", "teleport", "cell_change", "worldspace_change", "load", "spawn", "fast_travel"
		};
		return a_value.is_string() &&
		       std::find(std::begin(kTypes), std::end(kTypes), std::string_view{ a_value.get_ref<const std::string&>() }) !=
		           std::end(kTypes);
	}

	bool GraphSpeed(const Json& a_value) { return InRange(a_value, -1.0, 100000.0); }
	bool Direction(const Json& a_value) { return InRange(a_value, -360.0, 360.0); }
	bool AimPitch(const Json& a_value) { return InRange(a_value, -180.0, 180.0); }
	bool TurnDelta(const Json& a_value) { return InRange(a_value, -10000.0, 10000.0); }

	bool TransformInvariant(const Json&, const Json& a_output)
	{
		return a_output.is_object() &&
		       OnlyKeys(a_output, { "type", "x", "y", "z", "angleZ", "cellId", "worldspaceId", "movementType", "clientTime",
		                            "movementSpeed", "animationGraphSpeed", "animationDirection", "aimPitch", "turnDelta",
		                            "isMoving", "isSprinting", "isSneaking", "isJumping", "isCrouching", "weaponDrawn",
		                            "actorStateFlags1", "actorStateFlags2", "actionEvents" }) &&
		       TypeIs(a_output, "transform") &&
		       AllFields(a_output, { "x", "y", "z", "angleZ" }, Coordinate, true) &&
		       Field(a_output, "cellId", HexNonZero, true) &&
		       Field(a_output, "worldspaceId", HexOrEmpty, true) &&
		       Field(a_output, "movementType", MovementType, true) &&
		       Field(a_output, "clientTime", ClientTime) &&
		       Field(a_output, "movementSpeed", Speed) &&
		       Field(a_output, "animationGraphSpeed", GraphSpeed) &&
		       Field(a_output, "animationDirection", Direction) &&
		       Field(a_output, "aimPitch", AimPitch) &&
		       Field(a_output, "turnDelta", TurnDelta) &&
		       AllFields(a_output, { "isMoving", "isSprinting", "isSneaking", "isJumping", "isCrouching", "weaponDrawn" }, Flag, false) &&
		       AllFields(a_output, { "actorStateFlags1", "actorStateFlags2" }, IsUint32Integer, false) &&
		       Field(a_output, "actionEvents", [](const Json& a_value) { return Elements(a_value, kMaximumActionEvents, ActionEventEntry); });
	}

	Json ActionEventsSample()
	{
		return Json::array({
			Json{ { "sequence", 1 }, { "type", 1 }, { "eventName", "meleeattackStart" }, { "actorStateFlags1", 7 } },
			Json{ { "sequence", 2 }, { "type", 3 }, { "eventName", "fireSingle" } }
		});
	}

	Json TransformSample()
	{
		return Json{
			{ "type", "transform" },
			{ "x", 1024.5 },
			{ "y", -2048.25 },
			{ "z", 128.0 },
			{ "angleZ", 90.0 },
			{ "cellId", HexWord(51966) },
			{ "worldspaceId", HexWord(61453) },
			{ "movementType", "normal" },
			{ "clientTime", 1000.5 },
			{ "movementSpeed", 120.0 },
			{ "animationGraphSpeed", 0.5 },
			{ "animationDirection", 45.0 },
			{ "aimPitch", -10.0 },
			{ "turnDelta", 3.5 },
			{ "isMoving", true },
			{ "isSprinting", false },
			{ "isSneaking", false },
			{ "isJumping", false },
			{ "isCrouching", false },
			{ "weaponDrawn", true },
			{ "actorStateFlags1", 7 },
			{ "actorStateFlags2", 0 },
			{ "actionEvents", ActionEventsSample() }
		};
	}

	const JsonTarget kTransformTarget{
		"NormalizeTransform",
		TransformSample,
		F4T::TransformValidation::NormalizeTransform,
		TransformInvariant
	};

	void RejectKnownBadTransforms(std::uint64_t a_seed)
	{
		const Trial trial{ "NormalizeTransform", a_seed, 0 };
		const auto base = TransformSample();
		auto zeroCell = base;
		zeroCell["cellId"] = "00000000";
		auto longCell = base;
		longCell["cellId"] = "123456789";
		auto prefixedCell = base;
		prefixedCell["cellId"] = "0x0000CAFE";
		auto farX = base;
		farX["x"] = 10000001.0;
		auto badMovement = base;
		badMovement["movementType"] = "warp";
		auto numericWorld = base;
		numericWorld["worldspaceId"] = 12;
		auto noCell = base;
		noCell.erase("cellId");
		const Json knownBad[] = { zeroCell, longCell, prefixedCell, farX, badMovement, numericWorld, noCell };
		for (const auto& input : knownBad) {
			if (Invoke(trial, [&] { return F4T::TransformValidation::NormalizeTransform(input); })) {
				Fail(trial, "known-bad transform accepted; input=" + Preview(input));
			}
		}
	}

	void FuzzActionEvents(std::uint64_t a_seed)
	{
		using F4T::ActionEventValidation::NormalizeActionEvents;
		const Trial corpusTrial{ "NormalizeActionEvents", a_seed, 0 };
		const auto corpus = ActionEventsSample();
		if (Invoke(corpusTrial, [&] { return NormalizeActionEvents(corpus, true); }) != corpus) {
			Fail(corpusTrial, "seed corpus events were rejected or altered: " + Preview(corpus));
		}

		const Json knownBad[] = {
			Json{ { "sequence", 0 }, { "type", 3 }, { "eventName", "fireSingle" } },
			Json{ { "sequence", 4294967296.0 }, { "type", 3 }, { "eventName", "fireSingle" } },
			Json{ { "sequence", 3.5 }, { "type", 3 }, { "eventName", "fireSingle" } },
			Json{ { "sequence", 1 }, { "type", 0 }, { "eventName", "fireSingle" } },
			Json{ { "sequence", 1 }, { "type", 4 }, { "eventName", "fireSingle" } },
			Json{ { "sequence", 1 }, { "type", 1 }, { "eventName", "fireSingle" } },
			Json{ { "sequence", 1 }, { "type", 3 }, { "eventName", "meleeattackStart" } },
			Json{ { "sequence", 1 }, { "type", 3 } },
			Json{ { "sequence", 1 }, { "type", 3 }, { "eventName", 7 } },
			nullptr
		};
		for (const auto& bad : knownBad) {
			const auto input = Json::array({ bad });
			if (!Invoke(corpusTrial, [&] { return NormalizeActionEvents(input, true); }).empty()) {
				Fail(corpusTrial, "known-bad event accepted; input=" + Preview(input));
			}
		}

		Rng rng{ std::mt19937_64{ a_seed } };
		std::size_t accepted = 0;
		std::size_t rejected = 0;
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ "NormalizeActionEvents", a_seed, iteration };
			auto input = ActionEventsSample();
			for (std::size_t mutations = 1 + rng.Below(4); mutations > 0; --mutations) {
				MutateOnce(input, rng);
			}
			const auto lenient = Invoke(trial, [&] { return NormalizeActionEvents(input, false); });
			const auto strict = Invoke(trial, [&] { return NormalizeActionEvents(input, true); });
			const auto bound = input.is_array() ? (std::min)(input.size(), kMaximumActionEvents) : std::size_t{ 0 };
			if (!lenient.is_array() || lenient.size() > bound || !Elements(lenient, kMaximumActionEvents, ActionEventEntry)) {
				Fail(trial, "accepted events violate the entry allow-list or cap; input=" + Preview(input) + " output=" + Preview(lenient));
			}
			if (!strict.empty() && strict != lenient) {
				Fail(trial, "strict output differs from lenient output; input=" + Preview(input));
			}
			if (Invoke(trial, [&] { return NormalizeActionEvents(lenient, false); }) != lenient ||
			    Invoke(trial, [&] { return NormalizeActionEvents(lenient, true); }) != lenient) {
				Fail(trial, "normalizer is not idempotent; input=" + Preview(input) + " output=" + Preview(lenient));
			}
			if (lenient.empty()) {
				++rejected;
			} else {
				++accepted;
			}
		}
		if (accepted == 0) {
			Fail(corpusTrial, "no accepted event arrays were produced");
		}
		if (rejected == 0) {
			Fail(corpusTrial, "no rejected event arrays were produced");
		}
	}
}

int main()
{
	std::uint64_t seed = kBaseSeed;
	FuzzJsonTarget(kTransformTarget, seed++);
	RejectKnownBadTransforms(seed++);
	FuzzActionEvents(seed++);
	std::cout << "validator fuzz transform passed: targets=2 iterations_per_target=" << kIterations << " base_seed=" << kBaseSeed << '\n';
	return 0;
}
