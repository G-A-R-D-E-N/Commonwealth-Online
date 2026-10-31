#include "F4TValidatorFuzzHarness.h"

#include "F4TContentManifest.h"
#include "F4TDebugSwitch.h"
#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TIdentityProtocol.h"
#include "F4TServerWorldState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace ValidatorFuzz;

	using Entry = F4T::ContentManifest::Entry;
	using TextCheck = bool (*)(const Trial& a_trial, const std::string& a_text);

	constexpr std::string_view kWhitespace = " \t\n\v\f\r";
	constexpr std::size_t kMaximumPathLength = 512;
	constexpr std::string_view kTransientKeys[] = { "actionEvents", "playerId", "serverTime", "sequence", "authorityEpoch" };
	constexpr std::string_view kSwitchWords[] = { "1", "true", "yes", "on", "debug", "enabled", "0", "false", "no", "off", "release",
		"disabled", "maybe", "onn", "tru e", "", "2" };
	constexpr std::string_view kPathPrefixes[] = { "", "", "./", "/", ".\\", "\\", "../" };
	constexpr std::string_view kPathSegments[] = { "a", "Data", ".", "..", "", "C:", "x y", "b.esp", "\x01", "\x7f" };
	constexpr std::string_view kPathSeparators[] = { "/", "\\", "//" };

	const std::string kUpperFingerprint = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
	const std::string kLowerFingerprint = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	const std::string kEntryHash(64, 'a');
	const std::string kServerInstanceId = "0123456789abcdef0123456789abcdef";
	const std::string kTokenHash(64, 'b');

	const F4T::Identity::Requirements kPinned{ .clientVersion = std::string{ "  0.2.0 " }, .falloutRuntimeVersion = std::string{ " \t " },
		.contentManifestFingerprint = kUpperFingerprint };

	const std::string kRejectedPaths[] = { "", "/", "\\", "./", ".", "..", "../x", "a/../b", "..\\x", "a\\..\\b", "a//b", "a\\\\b", "a/",
		"C:/Data/x.esp", "C:x.esp", "\\\\?\\C:\\x", std::string("a\0b", 3), "a\x01" "b", "a\x7f" "b", "a:b", std::string(kMaximumPathLength + 1, 'a') };
	const std::string kAcceptedPaths[] = { "CommonwealthOnline.esp", ".\\CommonwealthOnline.esp", "Scripts\\COCharacterSetupQuestScript.pex",
		std::string(kMaximumPathLength, 'a') };

	const std::string* TextOf(const Json& a_value)
	{
		return a_value.is_string() ? &a_value.get_ref<const std::string&>() : nullptr;
	}

	void Require(const Trial& a_trial, bool a_holds, std::string_view a_detail, const std::string& a_input)
	{
		if (!a_holds) {
			Fail(a_trial, std::string{ a_detail } + "; input=\"" + a_input + "\"");
		}
	}

	bool IsTwo(const Json& a_value) { return InRange(a_value, 2.0, 2.0); }
	bool IsCurrentSchema(const Json& a_value) { return InRange(a_value, F4T::DurableState::CurrentSchemaVersion, F4T::DurableState::CurrentSchemaVersion); }
	bool IsObject(const Json& a_value) { return a_value.is_object(); }
	bool NonNegative(const Json& a_value) { return InRange(a_value, 0.0, std::numeric_limits<double>::max()); }

	bool BoundedText(const Json& a_value, std::size_t a_maximum)
	{
		const auto* text = TextOf(a_value);
		return text && !text->empty() && text->size() <= a_maximum &&
		       std::none_of(text->begin(), text->end(), [](char a_ch) { return static_cast<unsigned char>(a_ch) < 0x20 || a_ch == 0x7f; }) &&
		       std::isspace(static_cast<unsigned char>(text->front())) == 0 && std::isspace(static_cast<unsigned char>(text->back())) == 0;
	}

	bool Text64(const Json& a_value) { return BoundedText(a_value, 64); }
	bool Text256(const Json& a_value) { return BoundedText(a_value, 256); }

	bool ProfileText(const Json& a_value)
	{
		const auto* text = TextOf(a_value);
		return text && text->size() >= 8 && text->size() <= 128 && std::all_of(text->begin(), text->end(), [](char a_ch) {
			return std::isalnum(static_cast<unsigned char>(a_ch)) != 0 || std::string_view{ "-_.:@" }.find(a_ch) != std::string_view::npos;
		});
	}

	bool Capabilities(const Json& a_value)
	{
		std::set<std::string> unique;
		return a_value.is_array() && a_value.size() <= 64 && std::all_of(a_value.begin(), a_value.end(), [&](const Json& a_item) {
			return Text64(a_item) && unique.insert(*TextOf(a_item)).second;
		});
	}

	template <std::size_t Length, bool LowerOnly>
	bool Hex(const Json& a_value)
	{
		const auto* text = TextOf(a_value);
		return text && text->size() == Length && std::all_of(text->begin(), text->end(), [](char a_ch) {
			return (a_ch >= '0' && a_ch <= '9') || (a_ch >= 'a' && a_ch <= 'f') || (!LowerOnly && a_ch >= 'A' && a_ch <= 'F');
		});
	}

	Json HelloSample()
	{
		return Json{ { "protocolVersion", F4T::Identity::ProtocolVersion }, { "clientVersion", "0.2.0" }, { "falloutRuntimeVersion", "1.10.163" },
		             { "profileId", "co-12345678" }, { "contentManifestFingerprint", kUpperFingerprint },
		             { "capabilities", Json::array({ "interest-v1", "player-state-v1" }) }, { "resumeToken", "resume-token-0001" } };
	}

	std::optional<Json> NormalizeHelloJson(const Json& a_hello)
	{
		const auto result = F4T::Identity::NormalizeHello(a_hello, kPinned);
		if (!result.accepted || !result.identity) {
			return std::nullopt;
		}
		const auto& id = *result.identity;
		Json hello{ { "protocolVersion", F4T::Identity::ProtocolVersion }, { "clientVersion", id.clientVersion }, { "falloutRuntimeVersion", id.falloutRuntimeVersion },
		            { "profileId", id.profileId }, { "contentManifestFingerprint", id.contentManifestFingerprint }, { "capabilities", id.capabilities } };
		if (id.resumeToken) {
			hello["resumeToken"] = *id.resumeToken;
		}
		return hello;
	}

	bool HelloInvariant(const Json&, const Json& a_output)
	{
		return a_output.is_object() &&
		       OnlyKeys(a_output, { "protocolVersion", "clientVersion", "falloutRuntimeVersion", "profileId", "contentManifestFingerprint", "capabilities", "resumeToken" }) &&
		       Field(a_output, "protocolVersion", IsTwo, true) && Field(a_output, "clientVersion", Text64, true) &&
		       Field(a_output, "falloutRuntimeVersion", Text64, true) && Field(a_output, "profileId", ProfileText, true) &&
		       Field(a_output, "contentManifestFingerprint", Hex<64, true>, true) && Field(a_output, "capabilities", Capabilities, true) &&
		       Field(a_output, "resumeToken", Text256) && a_output.at("contentManifestFingerprint") == kLowerFingerprint;
	}

	const JsonTarget kHelloTarget{ "Identity::NormalizeHello", HelloSample, NormalizeHelloJson, HelloInvariant };

	Json RootSample()
	{
		return Json{ { "schemaVersion", F4T::DurableState::CurrentSchemaVersion }, { "serverInstanceId", kServerInstanceId }, { "savedAt", 1000.5 },
		             { "profiles", Json{ { "co-12345678", Json{ { "resume", Json{ { "tokenHash", kTokenHash }, { "expiresAt", 2000.0 } } }, { "playerState", Json{ { "health", 100.0 } } } } } } },
		             { "worldState", Json::object() }, { "inventoryAuthorityBlocked", false }, { "drops", Json{ { "nextEntityId", F4T::Drops::kFirstDroppedEntityId }, { "live", Json::object() } } } };
	}

	bool ProfileOk(const Json& a_profile)
	{
		const auto resume = a_profile.find("resume");
		const auto state = a_profile.find("playerState");
		const bool resumeOk = resume == a_profile.end() || (resume->is_object() && Field(*resume, "tokenHash", Hex<64, false>, true) && Field(*resume, "expiresAt", NonNegative, true));
		const bool stateOk = state == a_profile.end() || (state->is_object() && std::none_of(std::begin(kTransientKeys), std::end(kTransientKeys), [&](std::string_view a_key) { return state->contains(a_key); }));
		return a_profile.is_object() && resumeOk && stateOk;
	}

	bool RootInvariant(const Json&, const Json& a_output)
	{
		if (!a_output.is_object() || !Field(a_output, "schemaVersion", IsCurrentSchema, true) || !a_output.contains("inventoryAuthorityBlocked") || !a_output.at("inventoryAuthorityBlocked").is_boolean() ||
		    !Field(a_output, "serverInstanceId", Hex<32, false>, true) ||
		    !Field(a_output, "profiles", IsObject, true) || !Field(a_output, "worldState", IsObject, true)) {
			return false;
		}
		const auto& profiles = a_output.at("profiles");
		return std::all_of(profiles.begin(), profiles.end(), ProfileOk);
	}

	std::string_view TrimView(std::string_view a_text)
	{
		const auto begin = a_text.find_first_not_of(kWhitespace);
		return begin == std::string_view::npos ? std::string_view{} : a_text.substr(begin, a_text.find_last_not_of(kWhitespace) - begin + 1);
	}

	std::string Padded(std::string_view a_text, Rng& a_rng)
	{
		std::string text{ a_text };
		if (a_rng.Below(3) == 0) { text.insert(0, 1 + a_rng.Below(3), kWhitespace[a_rng.Below(kWhitespace.size())]); }
		if (a_rng.Below(3) == 0) { text.append(1 + a_rng.Below(3), kWhitespace[a_rng.Below(kWhitespace.size())]); }
		return text;
	}

	void FuzzText(std::string_view a_function, std::uint64_t a_seed, std::string (*a_generate)(Rng&), TextCheck a_check)
	{
		Rng rng{ std::mt19937_64{ a_seed } };
		std::size_t accepted = 0;
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			accepted += a_check(Trial{ a_function, a_seed, iteration }, a_generate(rng)) ? 1 : 0;
		}
		if (accepted == 0 || accepted == kIterations) {
			Fail(Trial{ a_function, a_seed, 0 }, "generator produced no accepted or no rejected outputs");
		}
	}

	std::string RootText(Rng& a_rng)
	{
		auto root = RootSample();
		for (std::size_t mutations = 1 + a_rng.Below(4); mutations > 0; --mutations) { MutateOnce(root, a_rng); }
		return root.dump();
	}

	bool CheckRoot(const Trial& a_trial, const std::string& a_text)
	{
		const auto root = Invoke(a_trial, [&] { return Json::parse(a_text); });
		const bool accepted = Invoke(a_trial, [&] {
			try {
				F4T::DurableState::ValidateRoot(root);
				return true;
			} catch (const std::invalid_argument&) {
				return false;
			}
		});
		Require(a_trial, !accepted || RootInvariant(root, root), "accepted durable root violates documented shape", a_text);
		return accepted;
	}

	std::string HhmmText(Rng& a_rng) { return Padded(ClockText(a_rng), a_rng); }

	std::optional<std::string> OracleHhmm(std::string_view a_text)
	{
		const auto text = TrimView(a_text);
		if (text.empty() || text.size() > 4 || !std::all_of(text.begin(), text.end(), [](char a_ch) { return a_ch >= '0' && a_ch <= '9'; })) {
			return std::nullopt;
		}
		const auto padded = std::string(4 - text.size(), '0') + std::string{ text };
		const bool valid = (padded[0] - '0') * 10 + (padded[1] - '0') <= 23 && (padded[2] - '0') * 10 + (padded[3] - '0') <= 59;
		return valid ? std::optional<std::string>{ padded } : std::nullopt;
	}

	bool CheckHhmm(const Trial& a_trial, const std::string& a_text)
	{
		const auto actual = Invoke(a_trial, [&] { return F4T::ServerWorldState::NormalizeHhmm(a_text); });
		Require(a_trial, actual == OracleHhmm(a_text), "NormalizeHhmm disagrees with oracle", a_text);
		Require(a_trial, !actual || Invoke(a_trial, [&] { return F4T::ServerWorldState::NormalizeHhmm(*actual); }) == actual, "NormalizeHhmm is not idempotent", a_text);
		return actual.has_value();
	}

	std::string WeatherText(Rng& a_rng)
	{
		std::string text = a_rng.Below(4) == 0 ? HexWord(static_cast<std::uint32_t>(a_rng.engine())) : a_rng.Below(3) == 0 ? "0x" : "";
		for (std::size_t count = a_rng.Below(11); count > 0; --count) {
			text += kHexAlphabet[a_rng.Below(kHexAlphabet.size())];
		}
		return Padded(text, a_rng);
	}

	std::optional<std::uint32_t> OracleWeather(std::string_view a_text)
	{
		auto text = TrimView(a_text);
		if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
			text.remove_prefix(2);
		}
		if (text.empty() || text.size() > 8) {
			return std::nullopt;
		}
		std::uint32_t value = 0;
		for (const char ch : text) {
			if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
				return std::nullopt;
			}
			value = value * 16U + static_cast<std::uint32_t>(ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
		}
		return value;
	}

	bool CheckWeather(const Trial& a_trial, const std::string& a_text)
	{
		const auto value = OracleWeather(a_text);
		const auto upper = value ? std::optional<std::string>{ HexWord(*value) } : std::nullopt;
		auto lower = upper;
		if (lower) {
			std::transform(lower->begin(), lower->end(), lower->begin(), [](char a_ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(a_ch))); });
		}
		const auto actual = Invoke(a_trial, [&] { return F4T::ServerWorldState::NormalizeWeatherConsoleArg(a_text); });
		const auto relay = Invoke(a_trial, [&] { return F4T::ServerWorldState::RelayWeatherFormId(a_text); });
		Require(a_trial, actual == lower && relay == upper, "weather normalizers disagree with oracle", a_text);
		Require(a_trial, !actual || Invoke(a_trial, [&] { return F4T::ServerWorldState::NormalizeWeatherConsoleArg(*actual); }) == actual, "weather output not idempotent", a_text);
		return actual.has_value();
	}

	std::string SwitchText(Rng& a_rng)
	{
		std::string text{ kSwitchWords[a_rng.Below(std::size(kSwitchWords))] };
		for (auto& ch : text) {
			if (a_rng.Below(3) == 0) {
				ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
			}
		}
		return Padded(text, a_rng);
	}

	std::optional<bool> OracleSwitch(std::string_view a_text)
	{
		std::string value{ TrimView(a_text) };
		std::transform(value.begin(), value.end(), value.begin(), [](char a_ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(a_ch))); });
		if (value == "1" || value == "true" || value == "yes" || value == "on" || value == "debug" || value == "enabled") { return true; }
		if (value == "0" || value == "false" || value == "no" || value == "off" || value == "release" || value == "disabled") { return false; }
		return std::nullopt;
	}

	bool CheckSwitch(const Trial& a_trial, const std::string& a_text)
	{
		const auto actual = Invoke(a_trial, [&] { return F4T::DebugSwitch::ParseValue(a_text); });
		Require(a_trial, actual == OracleSwitch(a_text), "ParseValue disagrees with accept lists", a_text);
		return actual.has_value();
	}

	std::string PathText(Rng& a_rng)
	{
		std::string text{ kPathPrefixes[a_rng.Below(std::size(kPathPrefixes))] };
		for (std::size_t count = 1 + a_rng.Below(5); count > 0; --count) {
			text += kPathSegments[a_rng.Below(std::size(kPathSegments))];
			text += kPathSeparators[a_rng.Below(std::size(kPathSeparators))];
		}
		text += kPathSegments[a_rng.Below(std::size(kPathSegments))];
		if (a_rng.Below(4) == 0) {
			MutateText(text, a_rng);
		}
		return text;
	}

	std::string RewritePath(std::string_view a_raw)
	{
		std::string text{ a_raw };
		std::replace(text.begin(), text.end(), '\\', '/');
		std::string_view view{ text };
		while (view.starts_with("./")) {
			view.remove_prefix(2);
		}
		while (view.starts_with('/')) {
			view.remove_prefix(1);
		}
		return std::string{ view };
	}

	bool HasSafeSegments(std::string_view a_path)
	{
		for (std::size_t cursor = 0;;) {
			const auto next = a_path.find('/', cursor);
			const auto segment = a_path.substr(cursor, next == std::string_view::npos ? std::string_view::npos : next - cursor);
			if (segment.empty() || segment == "." || segment == ".." ||
			    std::any_of(segment.begin(), segment.end(), [](char a_ch) { return static_cast<unsigned char>(a_ch) < 0x20 || a_ch == 0x7f || a_ch == ':'; })) {
				return false;
			}
			if (next == std::string_view::npos) {
				return true;
			}
			cursor = next + 1;
		}
	}

	std::optional<std::string> FingerprintOf(const std::vector<Entry>& a_entries)
	{
		return F4T::ContentManifest::ComputeFingerprint(a_entries);
	}

	bool CheckPath(const Trial& a_trial, const std::string& a_raw)
	{
		const Entry entry{ a_raw, kEntryHash };
		const bool valid = Invoke(a_trial, [&] { return F4T::ContentManifest::IsValidEntry(entry); });
		const auto fingerprint = Invoke(a_trial, [&] { return FingerprintOf({ entry }); });
		Require(a_trial, fingerprint.has_value() == valid, "IsValidEntry disagrees with ComputeFingerprint", a_raw);
		if (!valid) {
			return false;
		}
		const auto canonical = RewritePath(a_raw);
		Require(a_trial, canonical.size() <= kMaximumPathLength && HasSafeSegments(canonical), "accepted relativePath has an unsafe segment", a_raw);
		Require(a_trial, Invoke(a_trial, [&] { return FingerprintOf({ Entry{ canonical, kEntryHash } }); }) == fingerprint, "path is not stable under re-normalization", a_raw);
		Require(a_trial, !Invoke(a_trial, [&] { return FingerprintOf({ entry, Entry{ canonical, kEntryHash } }); }), "path and normalized form both accepted", a_raw);
		return true;
	}

	void CheckKnownPaths()
	{
		const Trial trial{ "ContentManifest::IsValidEntry", kBaseSeed, 0 };
		for (const auto& path : kRejectedPaths) { Require(trial, !CheckPath(trial, path), "unsafe relativePath was accepted", path); }
		for (const auto& path : kAcceptedPaths) { Require(trial, CheckPath(trial, path), "documented relativePath was rejected", path); }
	}
}

int main()
{
	std::uint64_t seed = kBaseSeed;
	FuzzJsonTarget(kHelloTarget, seed++);
	const auto rootCorpus = RootSample().dump();
	const Trial corpus{ "DurableState::ValidateRoot", kBaseSeed, 0 };
	Require(corpus, CheckRoot(corpus, rootCorpus), "seed root was rejected", rootCorpus);
	FuzzText("DurableState::ValidateRoot", seed++, RootText, CheckRoot);
	FuzzText("ServerWorldState::NormalizeHhmm", seed++, HhmmText, CheckHhmm);
	FuzzText("ServerWorldState::NormalizeWeatherConsoleArg", seed++, WeatherText, CheckWeather);
	FuzzText("DebugSwitch::ParseValue", seed++, SwitchText, CheckSwitch);
	FuzzText("ContentManifest::IsValidEntry", seed++, PathText, CheckPath);
	CheckKnownPaths();
	std::cout << "validator fuzz misc passed: iterations_per_target=" << kIterations << " base_seed=" << kBaseSeed << '\n';
	return 0;
}
