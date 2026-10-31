#pragma once

#include "F4TValidatorFuzzInputs.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace ValidatorFuzz
{
	inline constexpr std::uint64_t kBaseSeed = 20261010U;
	inline constexpr std::size_t kIterations = 1500;
	inline constexpr std::size_t kPreviewLength = 400;

	struct Trial
	{
		std::string_view function;
		std::uint64_t seed{};
		std::size_t iteration{};
	};

	[[noreturn]] inline void Fail(const Trial& a_trial, const std::string& a_detail)
	{
		std::cerr << "validator fuzz failure: function=" << a_trial.function << " seed=" << a_trial.seed
		          << " iteration=" << a_trial.iteration << ": " << a_detail << '\n';
		std::exit(EXIT_FAILURE);
	}

	template <typename Call>
	auto Invoke(const Trial& a_trial, Call&& a_call) -> decltype(a_call())
	{
		try {
			return a_call();
		} catch (const std::exception& error) {
			Fail(a_trial, std::string{ "escaped std::exception: " } + error.what());
		} catch (...) {
			Fail(a_trial, "escaped non-std exception");
		}
	}

	inline std::string Preview(const Json& a_value)
	{
		auto text = a_value.dump();
		if (text.size() > kPreviewLength) {
			text.resize(kPreviewLength);
			text += "...";
		}
		return text;
	}

	inline bool IsFiniteTree(const Json& a_value)
	{
		if (a_value.is_number_float()) {
			return std::isfinite(a_value.get<double>());
		}
		if (a_value.is_array() || a_value.is_object()) {
			return std::all_of(a_value.begin(), a_value.end(), [](const Json& a_child) { return IsFiniteTree(a_child); });
		}
		return true;
	}

	inline bool InRange(const Json& a_value, double a_minimum, double a_maximum)
	{
		return a_value.is_number() && a_value.get<double>() >= a_minimum && a_value.get<double>() <= a_maximum;
	}

	inline bool IsCanonicalHex(const Json& a_value, bool a_allowEmpty, bool a_allowZero)
	{
		if (!a_value.is_string()) {
			return false;
		}
		const auto& text = a_value.get_ref<const std::string&>();
		if (text.empty()) {
			return a_allowEmpty;
		}
		if (text.size() != 8 || !std::all_of(text.begin(), text.end(), [](char a_ch) {
			return (a_ch >= '0' && a_ch <= '9') || (a_ch >= 'A' && a_ch <= 'F');
		})) {
			return false;
		}
		return a_allowZero || text != HexWord(0);
	}

	inline bool Field(const Json& a_object, std::string_view a_key, bool (*a_check)(const Json&), bool a_required = false)
	{
		const auto found = a_object.find(a_key);
		if (found == a_object.end()) {
			return !a_required;
		}
		return a_check(*found);
	}

	inline bool AllFields(
		const Json& a_object,
		std::initializer_list<std::string_view> a_keys,
		bool (*a_check)(const Json&),
		bool a_required)
	{
		return std::all_of(a_keys.begin(), a_keys.end(), [&](std::string_view a_key) {
			return Field(a_object, a_key, a_check, a_required);
		});
	}

	inline bool OnlyKeys(const Json& a_object, std::initializer_list<std::string_view> a_allowed)
	{
		if (!a_object.is_object()) {
			return false;
		}
		for (auto it = a_object.begin(); it != a_object.end(); ++it) {
			if (std::find(a_allowed.begin(), a_allowed.end(), std::string_view{ it.key() }) == a_allowed.end()) {
				return false;
			}
		}
		return true;
	}

	inline bool TypeIs(const Json& a_object, std::string_view a_type)
	{
		const auto found = a_object.find("type");
		return found != a_object.end() && found->is_string() &&
		       std::string_view{ found->get_ref<const std::string&>() } == a_type;
	}

	inline bool Elements(const Json& a_value, std::size_t a_maximum, bool (*a_check)(const Json&))
	{
		return a_value.is_array() && a_value.size() <= a_maximum &&
		       std::all_of(a_value.begin(), a_value.end(), a_check);
	}

	inline bool Positive32(const Json& a_value) { return InRange(a_value, 1.0, kUint32Max); }
	inline bool Flag(const Json& a_value) { return a_value.is_boolean(); }
	inline bool Coordinate(const Json& a_value) { return InRange(a_value, -10000000.0, 10000000.0); }
	inline bool Speed(const Json& a_value) { return InRange(a_value, 0.0, 100000.0); }
	inline bool ClientTime(const Json& a_value) { return InRange(a_value, 0.0, 1e12); }
	inline bool HexNonZero(const Json& a_value) { return IsCanonicalHex(a_value, false, false); }
	inline bool HexOrEmpty(const Json& a_value) { return IsCanonicalHex(a_value, true, true); }
	inline bool HexAny(const Json& a_value) { return IsCanonicalHex(a_value, false, true); }

	struct JsonTarget
	{
		std::string_view function;
		Json (*sample)();
		std::optional<Json> (*normalize)(const Json& a_input);
		bool (*invariant)(const Json& a_input, const Json& a_output);
	};

	inline void FuzzJsonTarget(const JsonTarget& a_target, std::uint64_t a_seed)
	{
		const Trial corpusTrial{ a_target.function, a_seed, 0 };
		const auto corpus = a_target.sample();
		const auto corpusOutput = Invoke(corpusTrial, [&] { return a_target.normalize(corpus); });
		if (!corpusOutput) {
			Fail(corpusTrial, "seed corpus packet was rejected: " + Preview(corpus));
		}

		Rng rng{ std::mt19937_64{ a_seed } };
		std::size_t accepted = 0;
		std::size_t rejected = 0;
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ a_target.function, a_seed, iteration };
			auto input = a_target.sample();
			for (std::size_t mutations = 1 + rng.Below(4); mutations > 0; --mutations) {
				MutateOnce(input, rng);
			}
			const auto output = Invoke(trial, [&] { return a_target.normalize(input); });
			if (!output) {
				++rejected;
				continue;
			}
			++accepted;
			if (!IsFiniteTree(*output)) {
				Fail(trial, "non-finite number in accepted output; input=" + Preview(input));
			}
			if (!a_target.invariant(input, *output)) {
				Fail(trial, "accepted output violates documented bounds; input=" + Preview(input) + " output=" + Preview(*output));
			}
			const auto again = Invoke(trial, [&] { return a_target.normalize(*output); });
			if (!again || *again != *output) {
				Fail(trial, "normalizer is not idempotent; input=" + Preview(input) + " output=" + Preview(*output));
			}
		}
		if (accepted == 0) {
			Fail(corpusTrial, "no accepted outputs were produced");
		}
		if (rejected == 0) {
			Fail(corpusTrial, "no rejected outputs were produced");
		}
	}
}
