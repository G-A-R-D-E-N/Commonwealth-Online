#include "F4TQuestWire.h"

#include "F4TQuestStoryOrder.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace F4T::QuestWire
{
	namespace
	{
		constexpr std::uint32_t kMaximumStage = 0xFFFF;
		constexpr std::size_t kMaximumModeBytes = 32;
		constexpr std::size_t kMaximumReasonBytes = 64;

		bool IsType(const Json& a_packet, std::string_view a_type)
		{
			if (!a_packet.is_object()) {
				return false;
			}
			const auto type = a_packet.find("type");
			return type != a_packet.end() && type->is_string() && type->get<std::string>() == a_type;
		}

		std::optional<std::string> CanonicalFormKey(std::string_view a_key)
		{
			const auto parsed = QuestKey::Parse(a_key);
			if (!parsed) {
				return std::nullopt;
			}
			return QuestKey::Format(*parsed);
		}

		std::optional<std::string> ReadFormKey(const Json& a_object, const char* a_field)
		{
			const auto field = a_object.find(a_field);
			if (field == a_object.end() || !field->is_string()) {
				return std::nullopt;
			}
			return CanonicalFormKey(field->get<std::string>());
		}

		std::optional<std::uint32_t> ReadUnsigned(const Json& a_object, const char* a_field, std::uint32_t a_maximum)
		{
			const auto field = a_object.find(a_field);
			if (field == a_object.end() || !field->is_number_integer()) {
				return std::nullopt;
			}
			const auto value = field->get<std::int64_t>();
			if (value < 0 || value > static_cast<std::int64_t>(a_maximum)) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(value);
		}

		std::optional<std::uint32_t> ReadStage(const Json& a_object, const char* a_field)
		{
			return ReadUnsigned(a_object, a_field, kMaximumStage);
		}

		std::optional<std::uint32_t> ReadUint32(const Json& a_object, const char* a_field)
		{
			return ReadUnsigned(a_object, a_field, std::numeric_limits<std::uint32_t>::max());
		}

		std::optional<std::string> ReadString(const Json& a_object, const char* a_field, std::size_t a_maximumBytes)
		{
			const auto field = a_object.find(a_field);
			if (field == a_object.end() || !field->is_string()) {
				return std::nullopt;
			}
			auto value = field->get<std::string>();
			if (value.empty() || value.size() > a_maximumBytes) {
				return std::nullopt;
			}
			return value;
		}

		std::optional<std::string> ReadOptionalString(const Json& a_object, const char* a_field, std::size_t a_maximumBytes)
		{
			if (a_object.find(a_field) == a_object.end()) {
				return std::string{};
			}
			return ReadString(a_object, a_field, a_maximumBytes);
		}

		std::optional<std::uint32_t> ReadOptionalUint32(const Json& a_object, const char* a_field)
		{
			if (a_object.find(a_field) == a_object.end()) {
				return std::uint32_t{ 0 };
			}
			return ReadUint32(a_object, a_field);
		}

		std::optional<std::vector<std::string>> ReadFormKeyList(const Json& a_object, const char* a_field, std::size_t a_maximum)
		{
			std::vector<std::string> keys;
			const auto field = a_object.find(a_field);
			if (field == a_object.end()) {
				return keys;
			}
			if (!field->is_array() || field->size() > a_maximum) {
				return std::nullopt;
			}
			for (const auto& entry : *field) {
				if (!entry.is_string()) {
					return std::nullopt;
				}
				const auto key = CanonicalFormKey(entry.get<std::string>());
				if (!key) {
					return std::nullopt;
				}
				keys.push_back(*key);
			}
			return keys;
		}

		const char* ScopeName(QuestLogic::Scope a_scope)
		{
			switch (a_scope) {
			case QuestLogic::Scope::Solo:
				return "solo";
			case QuestLogic::Scope::Party:
				return "party";
			case QuestLogic::Scope::Fireteam:
				return "fireteam";
			case QuestLogic::Scope::Shared:
			default:
				return "shared";
			}
		}
	}

	Json EncodeReport(const QuestReport& a_report)
	{
		return Json{
			{ "type", std::string(kReportType) },
			{ "quest", a_report.formKey },
			{ "stage", a_report.stage },
			{ "completed", a_report.completed },
		};
	}

	Json EncodeReady()
	{
		return Json{ { "type", std::string(kReadyType) } };
	}

	Json EncodeScope(const QuestScope& a_scope)
	{
		return Json{
			{ "type", std::string(kScopeType) },
			{ "quest", a_scope.formKey },
			{ "scope", ScopeName(a_scope.scope) },
		};
	}

	std::optional<QuestNotReadyIn> DecodeNotReady(const Json& a_packet)
	{
		if (!IsType(a_packet, kNotReadyType)) {
			return std::nullopt;
		}
		const auto reason = ReadString(a_packet, "reason", kMaximumReasonBytes);
		if (!reason) {
			return std::nullopt;
		}
		QuestNotReadyIn result{};
		result.reason = *reason;
		if (const auto quest = a_packet.find("quest"); quest != a_packet.end()) {
			if (!quest->is_string()) {
				return std::nullopt;
			}
			const auto key = CanonicalFormKey(quest->get<std::string>());
			if (!key) {
				return std::nullopt;
			}
			result.quest = *key;
		}
		return result;
	}

	std::optional<QuestNotReachableIn> DecodeNotReachable(const Json& a_packet)
	{
		if (!IsType(a_packet, kNotReachableType)) {
			return std::nullopt;
		}
		const auto reason = ReadString(a_packet, "reason", kMaximumReasonBytes);
		if (!reason) {
			return std::nullopt;
		}
		QuestNotReachableIn result{};
		result.reason = *reason;
		if (const auto quest = a_packet.find("quest"); quest != a_packet.end()) {
			if (!quest->is_string()) {
				return std::nullopt;
			}
			const auto key = CanonicalFormKey(quest->get<std::string>());
			if (!key) {
				return std::nullopt;
			}
			result.quest = *key;
		}
		if (const auto missing = a_packet.find("missing"); missing != a_packet.end()) {
			if (!missing->is_string()) {
				return std::nullopt;
			}
			const auto key = CanonicalFormKey(missing->get<std::string>());
			if (!key) {
				return std::nullopt;
			}
			result.missing = *key;
		}
		if (const auto label = a_packet.find("previousLabel"); label != a_packet.end()) {
			if (!label->is_string()) {
				return std::nullopt;
			}
			const auto text = label->get<std::string>();
			if (text.empty() || text.size() > kMaximumLabelBytes) {
				return std::nullopt;
			}
			result.previousLabel = text;
		}
		return result;
	}

	std::optional<QuestStageIn> DecodeStage(const Json& a_packet)
	{
		if (!IsType(a_packet, kStageType)) {
			return std::nullopt;
		}

		const auto quest = ReadFormKey(a_packet, "quest");
		const auto stage = ReadStage(a_packet, "stage");
		const auto fromPlayerId = ReadUint32(a_packet, "fromPlayerId");
		if (!quest || !stage || !fromPlayerId) {
			return std::nullopt;
		}

		bool completed = false;
		if (const auto flag = a_packet.find("completed"); flag != a_packet.end()) {
			if (!flag->is_boolean()) {
				return std::nullopt;
			}
			completed = flag->get<bool>();
		}
		return QuestStageIn{ *quest, *stage, *fromPlayerId, completed };
	}

	std::optional<QuestSyncIn> DecodeSync(const Json& a_packet)
	{
		if (!IsType(a_packet, kSyncType)) {
			return std::nullopt;
		}

		const auto stages = a_packet.find("stages");
		if (stages == a_packet.end() || !stages->is_array() || stages->size() > kMaximumStages) {
			return std::nullopt;
		}

		QuestSyncIn result{};
		for (const auto& entry : *stages) {
			if (!entry.is_object()) {
				return std::nullopt;
			}
			const auto quest = ReadFormKey(entry, "quest");
			const auto stage = ReadStage(entry, "stage");
			if (!quest || !stage) {
				return std::nullopt;
			}
			result.stages.push_back(QuestLogic::StageEntry{ *quest, *stage });
		}

		const auto completed = ReadFormKeyList(a_packet, "completed", kMaximumCompleted);
		if (!completed) {
			return std::nullopt;
		}
		result.completed = *completed;
		return result;
	}

	std::optional<QuestPolicyIn> DecodePolicy(const Json& a_packet)
	{
		if (!IsType(a_packet, kPolicyType)) {
			return std::nullopt;
		}

		const auto mode = ReadString(a_packet, "mode", kMaximumModeBytes);
		const auto version = ReadUint32(a_packet, "version");
		if (!mode || !version) {
			return std::nullopt;
		}

		const auto soloQuests = ReadFormKeyList(a_packet, "soloQuests", kMaximumSoloQuests);
		if (!soloQuests) {
			return std::nullopt;
		}

		const auto partyQuests = ReadFormKeyList(a_packet, "partyQuests", kMaximumPartyQuests);
		if (!partyQuests) {
			return std::nullopt;
		}

		std::vector<QuestLogic::Gate> gates;
		const auto gateList = a_packet.find("gates");
		if (gateList != a_packet.end()) {
			if (!gateList->is_array() || gateList->size() > kMaximumGates) {
				return std::nullopt;
			}
			for (const auto& entry : *gateList) {
				if (!entry.is_object()) {
					return std::nullopt;
				}
				const auto quest = ReadFormKey(entry, "quest");
				const auto stage = ReadStage(entry, "stage");
				if (!quest || !stage) {
					return std::nullopt;
				}
				gates.push_back(QuestLogic::Gate{ *quest, *stage });
			}
		}

		std::optional<F4T::QuestStory::Order> storyOrder;
		if (const auto order = a_packet.find("storyOrder"); order != a_packet.end()) {
			if (!order->is_object()) {
				return std::nullopt;
			}
			if (const auto rows = order->find("quests"); rows == order->end() || !rows->is_array() || rows->size() > kMaximumStoryRows) {
				return std::nullopt;
			}
			storyOrder = F4T::QuestStory::Order::Load(*order);
			if (!storyOrder) {
				return std::nullopt;
			}
		}

		QuestPolicyIn policy{};
		policy.mode = *mode;
		policy.version = *version;
		policy.soloQuests = *soloQuests;
		policy.gates = std::move(gates);
		policy.partyQuests = *partyQuests;
		const auto fireteamQuests = ReadFormKeyList(a_packet, "fireteamQuests", kMaximumFireteamQuests);
		if (!fireteamQuests) {
			return std::nullopt;
		}
		policy.fireteamQuests = *fireteamQuests;
		if (const auto flag = a_packet.find("fireteam"); flag != a_packet.end()) {
			if (!flag->is_boolean()) {
				return std::nullopt;
			}
			policy.fireteamSupported = flag->get<bool>();
		}
		policy.storyOrder = std::move(storyOrder);
		return policy;
	}

	Json EncodeAssist(const QuestAssistOut& a_assist)
	{
		Json packet{
			{ "type", std::string(kAssistType) },
			{ "sequence", a_assist.sequence },
			{ "quest", a_assist.formKey },
			{ "objectiveId", a_assist.objectiveId },
			{ "kind", a_assist.kind },
		};
		if (!a_assist.targetFormKey.empty()) packet["targetFormKey"] = a_assist.targetFormKey;
		if (a_assist.attackSequence != 0) packet["attackSequence"] = a_assist.attackSequence;
		return packet;
	}

	Json EncodeCreditAck(std::string_view a_creditId, std::uint32_t a_stage)
	{
		return Json{
			{ "type", std::string(kCreditAckType) },
			{ "creditId", std::string{ a_creditId } },
			{ "stage", a_stage },
		};
	}

	Json EncodeFireteamInvite(std::uint32_t a_targetPlayerId)
	{
		return Json{
			{ "type", std::string(kFireteamInviteType) },
			{ "targetPlayerId", a_targetPlayerId },
		};
	}

	Json EncodeFireteamRespond(std::string_view a_inviteId, bool a_accept)
	{
		return Json{
			{ "type", std::string(kFireteamRespondType) },
			{ "inviteId", std::string{ a_inviteId } },
			{ "accept", a_accept },
		};
	}

	Json EncodeFireteamLeave()
	{
		return Json{ { "type", std::string(kFireteamLeaveType) } };
	}

	Json EncodeFireteamKick(std::uint32_t a_targetPlayerId)
	{
		return Json{
			{ "type", std::string(kFireteamKickType) },
			{ "targetPlayerId", a_targetPlayerId },
		};
	}

	std::optional<QuestCreditIn> DecodeCredit(const Json& a_packet)
	{
		if (!IsType(a_packet, kCreditType)) {
			return std::nullopt;
		}
		const auto creditId = ReadString(a_packet, "creditId", kMaximumCreditIdBytes);
		const auto actor = ReadUint32(a_packet, "actorPlayerId");
		const auto formKey = ReadFormKey(a_packet, "quest");
		const auto objectiveId = ReadUint32(a_packet, "objectiveId");
		const auto kind = ReadString(a_packet, "kind", kMaximumReasonBytes);
		if (!creditId || creditId->empty() || !actor || !formKey || !objectiveId || !kind) {
			return std::nullopt;
		}
		QuestCreditIn result{};
		result.creditId = *creditId;
		result.actorPlayerId = *actor;
		result.formKey = *formKey;
		result.objectiveId = *objectiveId;
		result.kind = *kind;
		if (a_packet.contains("targetFormKey")) {
			const auto target = ReadFormKey(a_packet, "targetFormKey");
			if (!target) {
				return std::nullopt;
			}
			result.targetFormKey = *target;
		}
		return result;
	}

	std::optional<QuestPresenceIn> DecodePresence(const Json& a_packet)
	{
		if (!IsType(a_packet, kPresenceType)) {
			return std::nullopt;
		}
		const auto owner = ReadUint32(a_packet, "ownerPlayerId");
		const auto formKey = ReadFormKey(a_packet, "quest");
		const auto stage = ReadStage(a_packet, "stage");
		if (!owner || !formKey || !stage) {
			return std::nullopt;
		}
		QuestPresenceIn result{};
		result.ownerPlayerId = *owner;
		result.formKey = *formKey;
		result.stage = *stage;
		return result;
	}

	std::optional<FireteamInviteIn> DecodeFireteamInvite(const Json& a_packet)
	{
		if (!IsType(a_packet, kFireteamInviteType)) {
			return std::nullopt;
		}
		const auto inviteId = ReadString(a_packet, "inviteId", kMaximumInviteIdBytes);
		const auto from = ReadUint32(a_packet, "fromPlayerId");
		const auto fromName = ReadOptionalString(a_packet, "fromName", kMaximumMemberNameBytes);
		const auto target = ReadOptionalUint32(a_packet, "targetPlayerId");
		if (!inviteId || !from || !fromName || !target || (*from == 0) == (*target == 0)) {
			return std::nullopt;
		}
		FireteamInviteIn result{};
		result.inviteId = *inviteId;
		result.fromPlayerId = *from;
		result.fromName = *fromName;
		result.targetPlayerId = *target;
		return result;
	}

	std::optional<FireteamDeclineIn> DecodeFireteamDecline(const Json& a_packet)
	{
		if (!IsType(a_packet, kFireteamRespondType)) {
			return std::nullopt;
		}
		const auto accept = a_packet.find("accept");
		const auto inviteId = ReadString(a_packet, "inviteId", kMaximumInviteIdBytes);
		const auto from = ReadUint32(a_packet, "fromPlayerId");
		if (accept == a_packet.end() || !accept->is_boolean() || accept->get<bool>() || !inviteId || !from || *from == 0) {
			return std::nullopt;
		}
		FireteamDeclineIn result{};
		result.inviteId = *inviteId;
		result.fromPlayerId = *from;
		return result;
	}

	std::optional<FireteamEventIn> DecodeFireteamEvent(const Json& a_packet)
	{
		if (!IsType(a_packet, kFireteamEventType) && !IsType(a_packet, kFireteamRespondType)) {
			return std::nullopt;
		}
		const auto reason = ReadString(a_packet, "reason", kMaximumReasonBytes);
		if (!reason) {
			return std::nullopt;
		}
		return FireteamEventIn{ *reason };
	}

	std::optional<FireteamSnapshotIn> DecodeFireteamSnapshot(const Json& a_packet)
	{
		if (!IsType(a_packet, kFireteamSnapshotType)) {
			return std::nullopt;
		}
		const auto members = a_packet.find("members");
		const auto leader = ReadUint32(a_packet, "leaderPlayerId");
		if (members == a_packet.end() || !members->is_array() || members->size() > kMaximumFireteamMembers || !leader) {
			return std::nullopt;
		}
		FireteamSnapshotIn result{};
		if (const auto fireteamId = a_packet.find("fireteamId"); fireteamId != a_packet.end()) {
			if (!fireteamId->is_string()) {
				return std::nullopt;
			}
			const auto& value = fireteamId->get_ref<const std::string&>();
			if (value.size() > kMaximumFireteamIdBytes) {
				return std::nullopt;
			}
			result.fireteamId = value;
		}
		result.leaderPlayerId = *leader;
		for (const auto& entry : *members) {
			if (!entry.is_object()) {
				return std::nullopt;
			}
			const auto playerId = ReadUint32(entry, "playerId");
			if (!playerId || *playerId == 0) {
				return std::nullopt;
			}
			const bool duplicate = std::any_of(result.members.begin(), result.members.end(), [&playerId](const FireteamMemberIn& a_member) {
				return a_member.playerId == *playerId;
			});
			if (duplicate) {
				return std::nullopt;
			}
			FireteamMemberIn member{};
			member.playerId = *playerId;
			if (const auto name = ReadString(entry, "characterName", kMaximumMemberNameBytes)) {
				member.characterName = *name;
			}
			result.members.push_back(std::move(member));
		}
		if (result.fireteamId.empty()) {
			if (!result.members.empty() || result.leaderPlayerId != 0) {
				return std::nullopt;
			}
		} else {
			const bool leaderIsMember = std::any_of(result.members.begin(), result.members.end(), [&result](const FireteamMemberIn& a_member) {
				return a_member.playerId == result.leaderPlayerId;
			});
			if (result.members.empty() || !leaderIsMember) {
				return std::nullopt;
			}
		}
		return result;
	}
}
