#include "F4TQuestWire.h"

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

		QuestPolicyIn policy{};
		policy.mode = *mode;
		policy.version = *version;
		policy.soloQuests = *soloQuests;
		policy.gates = std::move(gates);
		policy.partyQuests = *partyQuests;
		return policy;
	}
}
