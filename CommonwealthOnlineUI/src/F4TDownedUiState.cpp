#ifndef F4T_DOWNED_TESTING
#include "pch.h"
#endif

#include "F4TDownedUiState.h"
#include "F4TViewJson.h"

#include <nlohmann/json.hpp>

#include <format>
#include <utility>

namespace F4T::DownedUi
{
	namespace
	{
		constexpr std::string_view kSanctuaryPoint = "sanctuary";
		constexpr std::string_view kLastBedPoint = "lastbed";
		constexpr std::string_view kCheckpointPoint = "checkpoint";
		constexpr std::string_view kFriendPoint = "friend";
		constexpr std::string_view kStimpakChoice = "stimpak";

		bool IsPrintableAscii(char a_ch) noexcept
		{
			const auto value = static_cast<unsigned char>(a_ch);
			return value >= 0x20 && value <= 0x7E;
		}

		std::size_t DecodeUtf8(std::string_view a_text, std::size_t a_index, std::uint32_t& a_codePoint) noexcept
		{
			const auto lead = static_cast<unsigned char>(a_text[a_index]);
			std::size_t length = 0;
			std::uint32_t minimum = 0;
			if (lead >= 0xC2 && lead <= 0xDF) {
				length = 2;
				minimum = 0x80U;
				a_codePoint = lead & 0x1FU;
			} else if (lead >= 0xE0 && lead <= 0xEF) {
				length = 3;
				minimum = 0x800U;
				a_codePoint = lead & 0x0FU;
			} else if (lead >= 0xF0 && lead <= 0xF4) {
				length = 4;
				minimum = 0x10000U;
				a_codePoint = lead & 0x07U;
			} else {
				return 0;
			}
			if (a_index + length > a_text.size()) {
				return 0;
			}
			for (std::size_t offset = 1; offset < length; ++offset) {
				const auto continuation = static_cast<unsigned char>(a_text[a_index + offset]);
				if ((continuation & 0xC0U) != 0x80U) {
					return 0;
				}
				a_codePoint = (a_codePoint << 6U) | (continuation & 0x3FU);
			}
			if (a_codePoint < minimum || a_codePoint > 0x10FFFFU || (a_codePoint >= 0xD800U && a_codePoint <= 0xDFFFU)) {
				return 0;
			}
			return length;
		}

		bool IsBlockedCodePoint(std::uint32_t a_codePoint) noexcept
		{
			return (a_codePoint >= 0x80U && a_codePoint <= 0x9FU) || a_codePoint == 0x061CU ||
				(a_codePoint >= 0x200BU && a_codePoint <= 0x200FU) || (a_codePoint >= 0x2028U && a_codePoint <= 0x202EU) ||
				(a_codePoint >= 0x2066U && a_codePoint <= 0x2069U) || a_codePoint == 0xFEFFU;
		}

		std::string_view ReadString(const nlohmann::json& a_json, const char* a_key) noexcept
		{
			if (!a_json.is_object()) {
				return {};
			}
			const auto entry = a_json.find(a_key);
			if (entry == a_json.end() || !entry->is_string()) {
				return {};
			}
			return entry->get_ref<const std::string&>();
		}

		std::string ReadText(const nlohmann::json& a_json, const char* a_key, std::size_t a_maxLength)
		{
			return SanitizeText(ReadString(a_json, a_key), a_maxLength);
		}

		bool ReadFlag(const nlohmann::json& a_json, const char* a_key, bool a_default) noexcept
		{
			if (!a_json.is_object()) {
				return a_default;
			}
			const auto entry = a_json.find(a_key);
			if (entry == a_json.end() || !entry->is_boolean()) {
				return a_default;
			}
			return entry->get<bool>();
		}

		double ReadUnitInterval(const nlohmann::json& a_json, const char* a_key) noexcept
		{
			if (!a_json.is_object()) {
				return 0.0;
			}
			const auto entry = a_json.find(a_key);
			if (entry == a_json.end() || !entry->is_number()) {
				return 0.0;
			}
			const auto value = entry->get<double>();
			if (!(value > 0.0)) {
				return 0.0;
			}
			return value > 1.0 ? 1.0 : value;
		}

		std::int32_t ReadRemainingMs(const nlohmann::json& a_json) noexcept
		{
			if (!a_json.is_object()) {
				return 0;
			}
			const auto entry = a_json.find("remainingMs");
			if (entry == a_json.end() || !entry->is_number()) {
				return 0;
			}
			const auto value = entry->get<double>();
			if (!(value > 0.0)) {
				return 0;
			}
			constexpr auto kCeiling = static_cast<double>(kMaxRemainingMs);
			return value >= kCeiling ? kMaxRemainingMs : static_cast<std::int32_t>(value);
		}

		std::int32_t ReadStimpaks(const nlohmann::json& a_json) noexcept
		{
			if (!a_json.is_object()) {
				return 0;
			}
			const auto entry = a_json.find("stimpaks");
			if (entry == a_json.end() || !entry->is_number()) {
				return 0;
			}
			const auto value = entry->get<double>();
			if (!(value > 0.0)) {
				return 0;
			}
			return value >= static_cast<double>(kMaxStimpaks) ? kMaxStimpaks : static_cast<std::int32_t>(value);
		}

		std::int32_t ReadStimpakCost(const nlohmann::json& a_json) noexcept
		{
			if (!a_json.is_object()) {
				return 1;
			}
			const auto entry = a_json.find("stimpakCost");
			if (entry == a_json.end() || !entry->is_number()) {
				return 1;
			}
			const auto value = entry->get<double>();
			if (!(value >= 0.0)) {
				return 1;
			}
			return value >= static_cast<double>(kMaxStimpaks) ? kMaxStimpaks : static_cast<std::int32_t>(value);
		}

		Rescuer ReadRescuer(const nlohmann::json& a_json, const char* a_key)
		{
			Rescuer rescuer{};
			if (!a_json.is_object()) {
				return rescuer;
			}
			const auto entry = a_json.find(a_key);
			if (entry == a_json.end() || !entry->is_object()) {
				return rescuer;
			}
			rescuer.active = true;
			rescuer.name = ReadText(*entry, "name", kMaxNameLength);
			rescuer.progress = ReadUnitInterval(*entry, "progress");
			return rescuer;
		}

		nlohmann::json FormatRescuer(const Rescuer& a_rescuer)
		{
			if (!a_rescuer.active) {
				return nlohmann::json(nullptr);
			}
			return nlohmann::json{
				{ "name", a_rescuer.name },
				{ "progress", a_rescuer.progress }
			};
		}
	}

	std::string SanitizeText(std::string_view a_text, std::size_t a_maxLength)
	{
		std::string sanitized;
		sanitized.reserve(a_text.size() < a_maxLength ? a_text.size() : a_maxLength);
		std::size_t index = 0;
		while (index < a_text.size() && sanitized.size() < a_maxLength) {
			const auto lead = static_cast<unsigned char>(a_text[index]);
			if (lead < 0x80) {
				if (IsPrintableAscii(a_text[index])) {
					sanitized.push_back(a_text[index]);
				}
				++index;
				continue;
			}
			std::uint32_t codePoint = 0;
			const auto length = DecodeUtf8(a_text, index, codePoint);
			if (length == 0) {
				++index;
				continue;
			}
			if (!IsBlockedCodePoint(codePoint) && sanitized.size() + length <= a_maxLength) {
				sanitized.append(a_text.substr(index, length));
			}
			index += length;
		}
		return sanitized;
	}

	bool IsEchoableId(std::string_view a_id) noexcept
	{
		if (a_id.empty() || a_id.size() > kMaxIdLength) {
			return false;
		}
		for (const auto ch : a_id) {
			if (!IsPrintableAscii(ch)) {
				return false;
			}
		}
		return true;
	}

	std::optional<Phase> ParsePhase(std::string_view a_phase) noexcept
	{
		if (a_phase == "downed") {
			return Phase::Downed;
		}
		if (a_phase == "dead") {
			return Phase::Dead;
		}
		if (a_phase == "hidden") {
			return Phase::Hidden;
		}
		return std::nullopt;
	}

	const char* PhaseName(Phase a_phase) noexcept
	{
		switch (a_phase) {
		case Phase::Downed:
			return "downed";
		case Phase::Dead:
			return "dead";
		default:
			return "hidden";
		}
	}

	bool IsKnownPoint(std::string_view a_point) noexcept
	{
		return a_point == kSanctuaryPoint || a_point == kLastBedPoint || a_point == kCheckpointPoint;
	}

	bool IsOverlayVisible(const State& a_state) noexcept
	{
		return a_state.phase == Phase::Downed || a_state.reviving.active || a_state.prompt.active;
	}

	bool IsRespawnMenuVisible(const State& a_state) noexcept
	{
		return a_state.phase == Phase::Downed || a_state.phase == Phase::Dead;
	}

	std::optional<State> ParseState(std::string_view a_json)
	{
		if (a_json.empty() || a_json.size() > kMaxJsonBytes) {
			return std::nullopt;
		}

		const auto parsed = nlohmann::json::parse(a_json.begin(), a_json.end(), nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object()) {
			return std::nullopt;
		}

		const auto phase = ParsePhase(ReadString(parsed, "phase"));
		if (!phase) {
			return std::nullopt;
		}

		State state{};
		state.phase = *phase;
		state.remainingMs = ReadRemainingMs(parsed);
		state.stimpaks = ReadStimpaks(parsed);
		state.stimpakCost = ReadStimpakCost(parsed);
		state.rescuer = ReadRescuer(parsed, "rescuer");
		state.reviving = ReadRescuer(parsed, "reviving");
		state.prompt = ReadRescuer(parsed, "prompt");

		if (const auto entry = parsed.find("points"); entry != parsed.end() && entry->is_array()) {
			for (const auto& candidate : *entry) {
				if (state.points.size() >= kMaxPoints) {
					break;
				}
				if (!candidate.is_object()) {
					continue;
				}
				Point point{};
				point.id = ReadText(candidate, "id", kMaxIdLength);
				if (!IsKnownPoint(point.id)) {
					continue;
				}
				point.label = ReadText(candidate, "label", kMaxLabelLength);
				point.available = ReadFlag(candidate, "available", false);
				state.points.push_back(std::move(point));
			}
		}

		if (const auto entry = parsed.find("friends"); entry != parsed.end() && entry->is_array()) {
			for (const auto& candidate : *entry) {
				if (state.friends.size() >= kMaxFriends) {
					break;
				}
				if (!candidate.is_object()) {
					continue;
				}
				const auto id = ReadString(candidate, "id");
				if (!IsEchoableId(id)) {
					continue;
				}
				Friend friendEntry{};
				friendEntry.id = std::string(id);
				friendEntry.name = ReadText(candidate, "name", kMaxNameLength);
				state.friends.push_back(std::move(friendEntry));
			}
		}

		return state;
	}

	std::string FormatSetStateScript(const State& a_state, std::string_view a_entryPoint)
	{
		nlohmann::json payload{
			{ "phase", PhaseName(a_state.phase) },
			{ "remainingMs", a_state.remainingMs },
			{ "stimpaks", a_state.stimpaks },
			{ "stimpakCost", a_state.stimpakCost },
			{ "rescuer", FormatRescuer(a_state.rescuer) },
			{ "reviving", FormatRescuer(a_state.reviving) },
			{ "prompt", FormatRescuer(a_state.prompt) },
			{ "points", nlohmann::json::array() },
			{ "friends", nlohmann::json::array() }
		};

		for (const auto& point : a_state.points) {
			payload["points"].push_back({
				{ "id", point.id },
				{ "label", point.label },
				{ "available", point.available }
			});
		}

		for (const auto& friendEntry : a_state.friends) {
			payload["friends"].push_back({
				{ "id", friendEntry.id },
				{ "name", friendEntry.name }
			});
		}

		return std::format(
			"if(window.{0}){{window.{0}({1});}}",
			a_entryPoint,
			F4T::ViewJson::Dump(payload));
	}

	std::optional<Choice> ParseChoice(std::string_view a_json)
	{
		if (a_json.empty() || a_json.size() > kMaxChoiceBytes) {
			return std::nullopt;
		}

		const auto parsed = nlohmann::json::parse(a_json.begin(), a_json.end(), nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object()) {
			return std::nullopt;
		}
		const auto action = ReadString(parsed, "action");
		if (action == "stimpak") {
			return Choice{ std::string(kStimpakChoice), {} };
		}
		if (action != "respawn") {
			return std::nullopt;
		}

		const auto point = ReadString(parsed, "point");
		if (!IsKnownPoint(point) && point != kFriendPoint) {
			return std::nullopt;
		}

		Choice choice{};
		choice.point = std::string(point);
		if (point == kFriendPoint) {
			const auto friendId = ReadString(parsed, "friendId");
			if (!IsEchoableId(friendId)) {
				return std::nullopt;
			}
			choice.friendId = std::string(friendId);
		}

		return choice;
	}

	bool ShouldPushStateScript(
		std::string_view a_lastScript,
		PrismaView a_lastScriptHandle,
		PrismaView a_currentHandle,
		std::string_view a_nextScript) noexcept
	{
		if (a_currentHandle != a_lastScriptHandle) {
			return true;
		}
		return a_lastScript != a_nextScript;
	}
}
