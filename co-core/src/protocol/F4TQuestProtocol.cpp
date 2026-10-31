#include "F4TQuestProtocol.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::QuestProtocol
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::uint32_t> HexDigit(char a_character) noexcept
		{
			if (a_character >= '0' && a_character <= '9') return static_cast<std::uint32_t>(a_character - '0');
			if (a_character >= 'a' && a_character <= 'f') return static_cast<std::uint32_t>(a_character - 'a' + 10);
			if (a_character >= 'A' && a_character <= 'F') return static_cast<std::uint32_t>(a_character - 'A' + 10);
			return std::nullopt;
		}

		bool TypeIs(const Json& a_packet, std::string_view a_type)
		{
			const auto found = a_packet.find("type");
			return found != a_packet.end() && found->is_string() && found->get_ref<const std::string&>() == a_type;
		}

		std::optional<std::uint32_t> ReadUnsigned(const Json& a_value, std::uint64_t a_maximum)
		{
			if (!a_value.is_number_integer() || a_value.is_boolean()) return std::nullopt;
			try {
				if (a_value.is_number_unsigned()) {
					const auto value = a_value.get<std::uint64_t>();
					if (value > a_maximum) return std::nullopt;
					return static_cast<std::uint32_t>(value);
				}
				const auto value = a_value.get<std::int64_t>();
				if (value < 0 || static_cast<std::uint64_t>(value) > a_maximum) return std::nullopt;
				return static_cast<std::uint32_t>(value);
			} catch (...) {
				return std::nullopt;
			}
		}

		std::optional<FormKey> ReadQuest(const Json& a_value)
		{
			if (!a_value.is_string()) return std::nullopt;
			return ParseFormKey(a_value.get_ref<const std::string&>());
		}

		std::optional<StageRef> ParseStageRef(const Json& a_item)
		{
			if (!a_item.is_object()) return std::nullopt;
			const auto quest = a_item.find("quest");
			const auto stage = a_item.find("stage");
			if (quest == a_item.end() || stage == a_item.end()) return std::nullopt;
			const auto key = ReadQuest(*quest);
			if (!key) return std::nullopt;
			const auto parsed = ParseStage(*stage);
			if (!parsed) return std::nullopt;
			return StageRef{ *key, *parsed };
		}

		template <typename TValue, typename TParse>
		std::optional<std::vector<TValue>> ReadArray(
			const Json& a_packet,
			std::string_view a_name,
			std::size_t a_maximum,
			TParse a_parse)
		{
			const auto found = a_packet.find(a_name);
			if (found == a_packet.end()) return std::vector<TValue>{};
			if (!found->is_array() || found->size() > a_maximum) return std::nullopt;
			std::vector<TValue> values;
			values.reserve(found->size());
			for (const auto& item : *found) {
				const auto parsed = a_parse(item);
				if (!parsed) return std::nullopt;
				values.push_back(*parsed);
			}
			return values;
		}
	}

	std::optional<FormKey> ParseFormKey(std::string_view a_value)
	{
		if (a_value.empty() || a_value.size() > kMaximumQuestKeyChars) return std::nullopt;
		const auto separator = a_value.find(':');
		if (separator != kFormKeyDigits) return std::nullopt;
		std::uint32_t localId = 0;
		for (std::size_t index = 0; index < kFormKeyDigits; ++index) {
			const auto digit = HexDigit(a_value[index]);
			if (!digit) return std::nullopt;
			localId = (localId << 4) | *digit;
		}
		if (localId == 0) return std::nullopt;
		const auto plugin = a_value.substr(separator + 1);
		if (plugin.empty() || plugin.size() > kMaximumPluginChars) return std::nullopt;
		std::string normalized;
		normalized.reserve(plugin.size());
		for (const auto character : plugin) {
			const auto byte = static_cast<unsigned char>(character);
			if (byte < 0x20 || byte == 0x7F || character == ':' || character == '/' || character == '\\') return std::nullopt;
			normalized.push_back(character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character);
		}
		return FormKey{ std::move(normalized), localId };
	}

	std::string FormatFormKey(const FormKey& a_key)
	{
		static constexpr std::string_view digits{ "0123456789ABCDEF" };
		std::string formatted(kFormKeyDigits, '0');
		for (std::size_t index = 0; index < kFormKeyDigits; ++index) {
			formatted[kFormKeyDigits - 1 - index] = digits[(a_key.localId >> (4 * index)) & 0xF];
		}
		formatted.push_back(':');
		formatted += a_key.plugin;
		return formatted;
	}

	std::optional<std::uint16_t> ParseStage(const Json& a_value)
	{
		const auto value = ReadUnsigned(a_value, kMaximumStage);
		if (!value) return std::nullopt;
		return static_cast<std::uint16_t>(*value);
	}

	std::string_view ModeName(Mode a_mode) noexcept
	{
		switch (a_mode) {
		case Mode::CoopStory:
			return "coop-story";
		case Mode::FactionWar:
			return "faction-war";
		case Mode::InstancedStory:
			return "instanced-story";
		case Mode::FreeRoam:
			return "free-roam";
		}
		return "coop-story";
	}

	std::optional<Mode> ParseMode(std::string_view a_value) noexcept
	{
		if (a_value == "coop-story") return Mode::CoopStory;
		if (a_value == "faction-war") return Mode::FactionWar;
		if (a_value == "instanced-story") return Mode::InstancedStory;
		if (a_value == "free-roam") return Mode::FreeRoam;
		return std::nullopt;
	}

	std::string_view OverrideName(Override a_policy) noexcept
	{
		switch (a_policy) {
		case Override::Shared:
			return "shared";
		case Override::Solo:
			return "solo";
		case Override::Blocked:
			return "blocked";
		}
		return "shared";
	}

	std::optional<Override> ParseOverride(std::string_view a_value) noexcept
	{
		if (a_value == "shared") return Override::Shared;
		if (a_value == "solo") return Override::Solo;
		if (a_value == "blocked") return Override::Blocked;
		return std::nullopt;
	}

	std::optional<Report> NormalizeReport(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, "questReport")) return std::nullopt;
		const auto quest = a_packet.find("quest");
		const auto stage = a_packet.find("stage");
		if (quest == a_packet.end() || stage == a_packet.end()) return std::nullopt;
		const auto key = ReadQuest(*quest);
		if (!key) return std::nullopt;
		const auto parsed = ParseStage(*stage);
		if (!parsed) return std::nullopt;
		bool completed = false;
		if (const auto flag = a_packet.find("completed"); flag != a_packet.end()) {
			if (!flag->is_boolean()) return std::nullopt;
			completed = flag->get<bool>();
		}
		return Report{ *key, *parsed, completed };
	}

	std::optional<ScopeRequest> NormalizeScope(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, "questScope")) return std::nullopt;
		const auto quest = a_packet.find("quest");
		const auto scope = a_packet.find("scope");
		if (quest == a_packet.end() || scope == a_packet.end() || !scope->is_string()) return std::nullopt;
		const auto key = ReadQuest(*quest);
		if (!key) return std::nullopt;
		const auto& name = scope->get_ref<const std::string&>();
		if (name == "solo") return ScopeRequest{ *key, Scope::Solo };
		if (name == "shared") return ScopeRequest{ *key, Scope::Shared };
		if (name == "party") return ScopeRequest{ *key, Scope::Party };
		return std::nullopt;
	}

	std::optional<Sync> NormalizeSync(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, "questSync")) return std::nullopt;
		if (const auto stages = a_packet.find("stages"); stages == a_packet.end() || !stages->is_array()) return std::nullopt;
		const auto stages = ReadArray<StageRef>(a_packet, "stages", kMaximumSyncStages, ParseStageRef);
		if (!stages) return std::nullopt;
		const auto completed = ReadArray<FormKey>(a_packet, "completed", kMaximumCompletedQuests, ReadQuest);
		if (!completed) return std::nullopt;
		return Sync{ *stages, *completed };
	}

	std::optional<Policy> NormalizePolicy(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, "questPolicy")) return std::nullopt;
		const auto mode = a_packet.find("mode");
		if (mode == a_packet.end() || !mode->is_string()) return std::nullopt;
		const auto parsedMode = ParseMode(mode->get_ref<const std::string&>());
		if (!parsedMode) return std::nullopt;
		std::uint32_t version = 0;
		if (const auto stored = a_packet.find("version"); stored != a_packet.end()) {
			const auto parsedVersion = ReadUnsigned(*stored, 0xFFFFFFFFu);
			if (!parsedVersion) return std::nullopt;
			version = *parsedVersion;
		}
		const auto solo = ReadArray<FormKey>(a_packet, "soloQuests", kMaximumSoloQuests, ReadQuest);
		if (!solo) return std::nullopt;
		const auto gates = ReadArray<StageRef>(a_packet, "gates", kMaximumGates, ParseStageRef);
		if (!gates) return std::nullopt;
		return Policy{ *parsedMode, version, *solo, *gates };
	}

	Json MakeNotReady(std::string_view a_reason, std::string_view a_quest, double a_unixTimeSeconds)
	{
		Json packet{
			{ "type", std::string{ kNotReadyType } },
			{ "reason", std::string{ a_reason } },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (!a_quest.empty()) packet["quest"] = std::string{ a_quest };
		return packet;
	}

	Json MakeNotReachable(
		std::string_view a_reason,
		std::string_view a_quest,
		std::string_view a_missingQuest,
		std::string_view a_previousLabel,
		double a_unixTimeSeconds)
	{
		Json packet{
			{ "type", std::string{ kNotReachableType } },
			{ "reason", std::string{ a_reason } },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (!a_quest.empty()) packet["quest"] = std::string{ a_quest };
		if (!a_missingQuest.empty()) packet["missing"] = std::string{ a_missingQuest };
		if (!a_previousLabel.empty()) packet["previousLabel"] = std::string{ a_previousLabel };
		return packet;
	}

	Json MakeStage(const Stage& a_stage, double a_unixTimeSeconds)
	{
		Json packet{
			{ "type", "questStage" },
			{ "quest", FormatFormKey(a_stage.quest) },
			{ "stage", a_stage.stage },
			{ "fromPlayerId", a_stage.fromPlayerId },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (a_stage.completed) packet["completed"] = true;
		return packet;
	}

	Json MakeSync(const Sync& a_sync, double a_unixTimeSeconds)
	{
		Json stages = Json::array();
		for (const auto& entry : a_sync.stages) {
			stages.push_back(Json{ { "quest", FormatFormKey(entry.quest) }, { "stage", entry.stage } });
		}
		Json completed = Json::array();
		for (const auto& quest : a_sync.completed) {
			completed.push_back(FormatFormKey(quest));
		}
		return Json{
			{ "type", "questSync" },
			{ "stages", std::move(stages) },
			{ "completed", std::move(completed) },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	Json MakePolicy(const Policy& a_policy, double a_unixTimeSeconds)
	{
		Json solo = Json::array();
		for (const auto& quest : a_policy.soloQuests) {
			solo.push_back(FormatFormKey(quest));
		}
		Json gates = Json::array();
		for (const auto& gate : a_policy.gates) {
			gates.push_back(Json{ { "quest", FormatFormKey(gate.quest) }, { "stage", gate.stage } });
		}
		Json party = Json::array();
		for (const auto& quest : a_policy.partyQuests) {
			party.push_back(FormatFormKey(quest));
		}
		return Json{
			{ "type", "questPolicy" },
			{ "mode", std::string{ ModeName(a_policy.mode) } },
			{ "version", a_policy.version },
			{ "soloQuests", std::move(solo) },
			{ "partyQuests", std::move(party) },
			{ "gates", std::move(gates) },
			{ "serverTime", a_unixTimeSeconds }
		};
	}
}
