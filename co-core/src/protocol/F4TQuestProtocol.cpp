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

		constexpr std::size_t kMaximumPrefixedIdDigits = 10;
		constexpr std::size_t kMaximumInviteIdChars = 64;

		bool ValidPrefixedId(std::string_view a_value, std::string_view a_prefix, std::size_t a_maximumChars) noexcept
		{
			if (a_value.size() <= a_prefix.size() || a_value.size() > a_maximumChars) return false;
			if (a_value.substr(0, a_prefix.size()) != a_prefix) return false;
			const auto digits = a_value.substr(a_prefix.size());
			if (digits.size() > kMaximumPrefixedIdDigits) return false;
			for (const auto character : digits) {
				if (character < '0' || character > '9') return false;
			}
			return true;
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
		const auto parsedScope = ParseScope(scope->get_ref<const std::string&>());
		if (!parsedScope) return std::nullopt;
		return ScopeRequest{ *key, *parsedScope };
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
		const auto party = ReadArray<FormKey>(a_packet, "partyQuests", kMaximumPartyQuests, ReadQuest).value_or(std::vector<FormKey>{});
		const auto fireteamQuests = ReadArray<FormKey>(a_packet, "fireteamQuests", kMaximumFireteamQuests, ReadQuest);
		if (!fireteamQuests) return std::nullopt;
		const auto gates = ReadArray<StageRef>(a_packet, "gates", kMaximumGates, ParseStageRef);
		if (!gates) return std::nullopt;
		bool fireteamSupported = false;
		if (const auto supported = a_packet.find("fireteam"); supported != a_packet.end()) {
			if (!supported->is_boolean()) return std::nullopt;
			fireteamSupported = supported->get<bool>();
		}
		return Policy{ *parsedMode, version, *solo, *gates, party, *fireteamQuests, fireteamSupported };
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
		Json fireteam = Json::array();
		for (const auto& quest : a_policy.fireteamQuests) {
			fireteam.push_back(FormatFormKey(quest));
		}
		return Json{
			{ "type", "questPolicy" },
			{ "mode", std::string{ ModeName(a_policy.mode) } },
			{ "version", a_policy.version },
			{ "soloQuests", std::move(solo) },
			{ "partyQuests", std::move(party) },
			{ "fireteamQuests", std::move(fireteam) },
			{ "fireteam", a_policy.fireteamSupported },
			{ "gates", std::move(gates) },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	std::string_view ScopeName(Scope a_scope) noexcept
	{
		switch (a_scope) {
		case Scope::Shared:
			return "shared";
		case Scope::Solo:
			return "solo";
		case Scope::Party:
			return "party";
		case Scope::Fireteam:
			return "fireteam";
		}
		return "shared";
	}

	std::optional<Scope> ParseScope(std::string_view a_value) noexcept
	{
		if (a_value == "shared") return Scope::Shared;
		if (a_value == "solo") return Scope::Solo;
		if (a_value == "party") return Scope::Party;
		if (a_value == "fireteam") return Scope::Fireteam;
		return std::nullopt;
	}

	std::string_view AssistKindName(AssistKind a_kind) noexcept
	{
		switch (a_kind) {
		case AssistKind::KillActor:
			return "killActor";
		case AssistKind::ReachLocation:
			return "reachLocation";
		case AssistKind::Activate:
			return "activate";
		case AssistKind::Dialogue:
			return "dialogue";
		case AssistKind::Escort:
			return "escort";
		}
		return "killActor";
	}

	std::optional<AssistKind> ParseAssistKind(std::string_view a_value) noexcept
	{
		if (a_value == "killActor") return AssistKind::KillActor;
		if (a_value == "reachLocation") return AssistKind::ReachLocation;
		if (a_value == "activate") return AssistKind::Activate;
		if (a_value == "dialogue") return AssistKind::Dialogue;
		if (a_value == "escort") return AssistKind::Escort;
		return std::nullopt;
	}

	bool ValidFireteamId(std::string_view a_value) noexcept
	{
		return ValidPrefixedId(a_value, kFireteamTrackPrefix, kMaximumFireteamIdChars);
	}

	bool ValidCreditId(std::string_view a_value) noexcept
	{
		return ValidPrefixedId(a_value, "c-", kMaximumCreditIdChars);
	}

	std::optional<Assist> NormalizeAssist(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, kQuestAssistType)) return std::nullopt;
		for (const auto* forbidden : { "stage", "profileId", "targetProfileId", "playerId", "targetPlayerId", "creditId" }) {
			if (a_packet.find(forbidden) != a_packet.end()) return std::nullopt;
		}
		const auto sequence = a_packet.find("sequence");
		const auto quest = a_packet.find("quest");
		const auto objectiveId = a_packet.find("objectiveId");
		const auto kind = a_packet.find("kind");
		if (sequence == a_packet.end() || quest == a_packet.end() || objectiveId == a_packet.end() || kind == a_packet.end()) {
			return std::nullopt;
		}
		if (!kind->is_string()) return std::nullopt;
		const auto parsedSequence = ReadUnsigned(*sequence, 0xFFFFFFFFu);
		if (!parsedSequence) return std::nullopt;
		const auto parsedQuest = ReadQuest(*quest);
		if (!parsedQuest) return std::nullopt;
		const auto parsedObjective = ReadUnsigned(*objectiveId, kMaximumObjectiveId);
		if (!parsedObjective) return std::nullopt;
		const auto parsedKind = ParseAssistKind(kind->get_ref<const std::string&>());
		if (!parsedKind) return std::nullopt;
		FormKey target;
		if (const auto found = a_packet.find("targetFormKey"); found != a_packet.end()) {
			const auto parsedTarget = ReadQuest(*found);
			if (!parsedTarget) return std::nullopt;
			target = *parsedTarget;
		}
		std::uint32_t attackSequence = 0;
		if (const auto attack = a_packet.find("attackSequence"); attack != a_packet.end()) {
			if (*parsedKind != AssistKind::KillActor) return std::nullopt;
			const auto parsedAttack = ReadUnsigned(*attack, 0xFFFFFFFFu);
			if (!parsedAttack) return std::nullopt;
			attackSequence = *parsedAttack;
		} else if (*parsedKind == AssistKind::KillActor) {
			return std::nullopt;
		}
		return Assist{ *parsedSequence, *parsedQuest, *parsedObjective, *parsedKind, target, attackSequence };
	}

	std::optional<CreditAck> NormalizeCreditAck(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, kQuestCreditAckType)) return std::nullopt;
		const auto creditId = a_packet.find("creditId");
		const auto stage = a_packet.find("stage");
		if (creditId == a_packet.end() || stage == a_packet.end() || !creditId->is_string()) return std::nullopt;
		const auto& id = creditId->get_ref<const std::string&>();
		if (!ValidCreditId(id)) return std::nullopt;
		const auto parsedStage = ParseStage(*stage);
		if (!parsedStage) return std::nullopt;
		return CreditAck{ id, *parsedStage };
	}

	std::optional<FireteamInvite> NormalizeFireteamInvite(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, kFireteamInviteType)) return std::nullopt;
		const auto target = a_packet.find("targetPlayerId");
		if (target == a_packet.end()) return std::nullopt;
		const auto parsedTarget = ReadUnsigned(*target, 0xFFFFFFFFu);
		if (!parsedTarget) return std::nullopt;
		FireteamInvite invite;
		invite.targetPlayerId = *parsedTarget;
		return invite;
	}

	std::optional<FireteamRespond> NormalizeFireteamRespond(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, kFireteamRespondType)) return std::nullopt;
		const auto inviteId = a_packet.find("inviteId");
		const auto accept = a_packet.find("accept");
		if (inviteId == a_packet.end() || accept == a_packet.end()) return std::nullopt;
		if (!inviteId->is_string() || !accept->is_boolean()) return std::nullopt;
		const auto& id = inviteId->get_ref<const std::string&>();
		if (id.empty() || id.size() > kMaximumInviteIdChars) return std::nullopt;
		return FireteamRespond{ id, accept->get<bool>() };
	}

	std::optional<FireteamKick> NormalizeFireteamKick(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, kFireteamKickType)) return std::nullopt;
		const auto target = a_packet.find("targetPlayerId");
		if (target == a_packet.end()) return std::nullopt;
		const auto parsedTarget = ReadUnsigned(*target, 0xFFFFFFFFu);
		if (!parsedTarget) return std::nullopt;
		FireteamKick kick;
		kick.targetPlayerId = *parsedTarget;
		return kick;
	}

	std::optional<FireteamSnapshot> NormalizeFireteamSnapshot(const Json& a_packet)
	{
		if (!a_packet.is_object() || !TypeIs(a_packet, kFireteamSnapshotType)) return std::nullopt;
		const auto fireteamId = a_packet.find("fireteamId");
		const auto leaderPlayerId = a_packet.find("leaderPlayerId");
		const auto members = a_packet.find("members");
		if (fireteamId == a_packet.end() || leaderPlayerId == a_packet.end() || members == a_packet.end()) return std::nullopt;
		if (!fireteamId->is_string() || !members->is_array() || members->size() > kMaximumFireteamMembers) return std::nullopt;
		const auto& id = fireteamId->get_ref<const std::string&>();
		if (!id.empty() && !ValidFireteamId(id)) return std::nullopt;
		const auto parsedLeader = ReadUnsigned(*leaderPlayerId, 0xFFFFFFFFu);
		if (!parsedLeader) return std::nullopt;
		std::vector<FireteamMember> parsedMembers;
		parsedMembers.reserve(members->size());
		for (const auto& item : *members) {
			if (!item.is_object()) return std::nullopt;
			const auto playerId = item.find("playerId");
			const auto characterName = item.find("characterName");
			if (playerId == item.end() || characterName == item.end() || !characterName->is_string()) return std::nullopt;
			const auto parsedPlayer = ReadUnsigned(*playerId, 0xFFFFFFFFu);
			if (!parsedPlayer || *parsedPlayer == 0) return std::nullopt;
			const auto& name = characterName->get_ref<const std::string&>();
			if (name.size() > kMaximumCharacterNameChars) return std::nullopt;
			for (const auto& existing : parsedMembers) {
				if (existing.playerId == *parsedPlayer) return std::nullopt;
			}
			parsedMembers.push_back(FireteamMember{ *parsedPlayer, name });
		}
		if (parsedMembers.empty()) {
			if (!id.empty() || *parsedLeader != 0) return std::nullopt;
		} else {
			if (id.empty()) return std::nullopt;
			const auto leaderListed = std::any_of(parsedMembers.begin(), parsedMembers.end(), [&parsedLeader](const FireteamMember& a_member) {
				return a_member.playerId == *parsedLeader;
			});
			if (!leaderListed) return std::nullopt;
		}
		return FireteamSnapshot{ id, *parsedLeader, std::move(parsedMembers) };
	}

	Json MakeFireteamSnapshot(const FireteamSnapshot& a_snapshot, double a_unixTimeSeconds)
	{
		Json members = Json::array();
		for (const auto& member : a_snapshot.members) {
			members.push_back(Json{ { "playerId", member.playerId }, { "characterName", member.characterName } });
		}
		return Json{
			{ "type", std::string{ kFireteamSnapshotType } },
			{ "fireteamId", a_snapshot.fireteamId },
			{ "leaderPlayerId", a_snapshot.leaderPlayerId },
			{ "members", std::move(members) },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	Json MakeFireteamInvite(const FireteamInvite& a_invite, double a_unixTimeSeconds)
	{
		Json packet{
			{ "type", std::string{ kFireteamInviteType } },
			{ "inviteId", a_invite.inviteId },
			{ "fromPlayerId", a_invite.fromPlayerId },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (a_invite.targetPlayerId != 0) packet["targetPlayerId"] = a_invite.targetPlayerId;
		if (!a_invite.fromName.empty()) packet["fromName"] = a_invite.fromName;
		return packet;
	}

	Json MakeFireteamRespond(const FireteamRespond& a_respond, std::uint32_t a_fromPlayerId, double a_unixTimeSeconds)
	{
		Json packet{
			{ "type", std::string{ kFireteamRespondType } },
			{ "inviteId", a_respond.inviteId },
			{ "accept", a_respond.accept },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (a_fromPlayerId != 0) packet["fromPlayerId"] = a_fromPlayerId;
		return packet;
	}

	Json MakeFireteamEvent(std::string_view a_type, std::string_view a_reason, std::uint32_t a_playerId, double a_unixTimeSeconds)
	{
		Json packet{
			{ "type", std::string{ a_type } },
			{ "reason", std::string{ a_reason } },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (a_playerId != 0) packet["playerId"] = a_playerId;
		return packet;
	}

	Json MakeCredit(const Credit& a_credit, double a_unixTimeSeconds)
	{
		Json packet{
			{ "type", std::string{ kQuestCreditType } },
			{ "creditId", a_credit.creditId },
			{ "actorPlayerId", a_credit.actorPlayerId },
			{ "quest", FormatFormKey(a_credit.quest) },
			{ "objectiveId", a_credit.objectiveId },
			{ "kind", std::string{ AssistKindName(a_credit.kind) } },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (!a_credit.target.plugin.empty()) packet["targetFormKey"] = FormatFormKey(a_credit.target);
		return packet;
	}

	Json MakePresence(const Presence& a_presence, double a_unixTimeSeconds)
	{
		Json refs = Json::array();
		const auto count = std::min(a_presence.refs.size(), kMaximumPresenceRefs);
		for (std::size_t index = 0; index < count; ++index) {
			const auto& ref = a_presence.refs[index];
			Json entry{
				{ "formKey", FormatFormKey(ref.form) },
				{ "enabled", ref.enabled },
				{ "locked", ref.locked }
			};
			if (!ref.cell.empty()) entry["cell"] = ref.cell;
			if (!ref.worldspace.empty()) entry["worldspace"] = ref.worldspace;
			if (ref.hasPosition) entry["position"] = Json::array({ ref.x, ref.y, ref.z });
			refs.push_back(std::move(entry));
		}
		return Json{
			{ "type", std::string{ kQuestPresenceType } },
			{ "ownerPlayerId", a_presence.ownerPlayerId },
			{ "quest", FormatFormKey(a_presence.quest) },
			{ "stage", a_presence.stage },
			{ "refs", std::move(refs) },
			{ "serverTime", a_unixTimeSeconds }
		};
	}
}
