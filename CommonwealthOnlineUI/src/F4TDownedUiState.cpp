#ifndef F4T_DOWNED_TESTING
#include "pch.h"
#endif

#include "F4TDownedUiState.h"

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

		bool IsPrintableAscii(char a_ch) noexcept
		{
			const auto value = static_cast<unsigned char>(a_ch);
			return value >= 0x20 && value <= 0x7E;
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
		for (const auto ch : a_text) {
			if (sanitized.size() >= a_maxLength) {
				break;
			}
			if (IsPrintableAscii(ch)) {
				sanitized.push_back(ch);
			}
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
		return a_state.phase == Phase::Downed || a_state.reviving.active;
	}

	bool IsRespawnMenuVisible(const State& a_state) noexcept
	{
		return a_state.phase == Phase::Dead;
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
		state.rescuer = ReadRescuer(parsed, "rescuer");
		state.reviving = ReadRescuer(parsed, "reviving");

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
			{ "rescuer", FormatRescuer(a_state.rescuer) },
			{ "reviving", FormatRescuer(a_state.reviving) },
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
			payload.dump());
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
		if (ReadString(parsed, "action") != "respawn") {
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
