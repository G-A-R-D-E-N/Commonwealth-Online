#pragma once

#include "F4TQuestLogic.h"
#include "F4TQuestProtocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace F4T::QuestWire
{
	using Json = nlohmann::json;

	inline constexpr std::string_view kReportType = "questReport";
	inline constexpr std::string_view kStageType = "questStage";
	inline constexpr std::string_view kSyncType = "questSync";
	inline constexpr std::string_view kPolicyType = "questPolicy";
	inline constexpr std::string_view kScopeType = "questScope";
	inline constexpr std::string_view kReadyType = "questReady";
	inline constexpr std::string_view kNotReadyType = F4T::QuestProtocol::kNotReadyType;
	inline constexpr std::string_view kNotReachableType = F4T::QuestProtocol::kNotReachableType;
	inline constexpr std::string_view kCreditType = F4T::QuestProtocol::kQuestCreditType;
	inline constexpr std::string_view kPresenceType = F4T::QuestProtocol::kQuestPresenceType;
	inline constexpr std::string_view kAssistType = F4T::QuestProtocol::kQuestAssistType;
	inline constexpr std::string_view kCreditAckType = F4T::QuestProtocol::kQuestCreditAckType;
	inline constexpr std::string_view kFireteamInviteType = F4T::QuestProtocol::kFireteamInviteType;
	inline constexpr std::string_view kFireteamRespondType = F4T::QuestProtocol::kFireteamRespondType;
	inline constexpr std::string_view kFireteamLeaveType = F4T::QuestProtocol::kFireteamLeaveType;
	inline constexpr std::string_view kFireteamKickType = F4T::QuestProtocol::kFireteamKickType;
	inline constexpr std::string_view kFireteamSnapshotType = F4T::QuestProtocol::kFireteamSnapshotType;
	inline constexpr std::string_view kFireteamEventType = F4T::QuestProtocol::kFireteamEventType;
	inline constexpr std::string_view kNotReadyReason = F4T::QuestProtocol::kNotReadyReason;
	inline constexpr std::string_view kUpdateRequiredReason = F4T::QuestProtocol::kUpdateRequiredReason;
	inline constexpr std::string_view kFireteamReasonFull = F4T::QuestProtocol::kFireteamReasonFull;
	inline constexpr std::string_view kFireteamReasonAlreadyMember = F4T::QuestProtocol::kFireteamReasonAlreadyMember;
	inline constexpr std::string_view kFireteamReasonNoInvite = F4T::QuestProtocol::kFireteamReasonNoInvite;
	inline constexpr std::string_view kFireteamReasonNotLeader = F4T::QuestProtocol::kFireteamReasonNotLeader;
	inline constexpr std::string_view kFireteamReasonInvalidTarget = F4T::QuestProtocol::kFireteamReasonInvalidTarget;
	inline constexpr std::string_view kFireteamReasonSelfTarget = F4T::QuestProtocol::kFireteamReasonSelfTarget;
	inline constexpr std::string_view kFireteamReasonUnknownMember = F4T::QuestProtocol::kFireteamReasonUnknownMember;
	inline constexpr std::string_view kFireteamReasonNotMember = F4T::QuestProtocol::kFireteamReasonNotMember;
	inline constexpr std::string_view kStorageFullReason = F4T::QuestProtocol::kStorageFullReason;
	inline constexpr std::string_view kNotInFireteamReason = F4T::QuestProtocol::kNotInFireteamReason;

	inline constexpr std::size_t kMaximumLabelBytes = 256;
	inline constexpr std::size_t kMaximumStages = 512;
	inline constexpr std::size_t kMaximumCompleted = 512;
	inline constexpr std::size_t kMaximumSoloQuests = 256;
	inline constexpr std::size_t kMaximumPartyQuests = 256;
	inline constexpr std::size_t kMaximumGates = 256;
	inline constexpr std::size_t kMaximumFireteamQuests = 256;
	inline constexpr std::size_t kMaximumCreditIdBytes = 32;
	inline constexpr std::size_t kMaximumFireteamIdBytes = 32;
	inline constexpr std::size_t kMaximumFireteamMembers = 4;
	inline constexpr std::size_t kMaximumMemberNameBytes = 64;
	inline constexpr std::size_t kMaximumInviteIdBytes = 64;
	inline constexpr std::size_t kMaximumObjectiveId = 0xFFFFFFFFu;
	inline constexpr std::size_t kMaximumStoryRows = 2048;

	struct QuestReport
	{
		std::string formKey;
		std::uint32_t stage{};
		bool completed{ false };
	};

	struct QuestScope
	{
		std::string formKey;
		QuestLogic::Scope scope{ QuestLogic::Scope::Shared };
	};

	struct QuestStageIn
	{
		std::string formKey;
		std::uint32_t stage{};
		std::uint32_t fromPlayerId{};
		bool completed{};
	};

	struct QuestSyncIn
	{
		std::vector<QuestLogic::StageEntry> stages;
		std::vector<std::string> completed;
	};

	struct QuestNotReadyIn
	{
		std::string reason;
		std::string quest;
	};

	struct QuestNotReachableIn
	{
		std::string reason;
		std::string quest;
		std::string missing;
		std::string previousLabel;
	};

	struct QuestPolicyIn
	{
		std::string mode;
		std::uint32_t version{};
		std::vector<std::string> soloQuests;
		std::vector<QuestLogic::Gate> gates;
		std::vector<std::string> partyQuests;
		std::vector<std::string> fireteamQuests;
		bool fireteamSupported{};
		std::optional<F4T::QuestStory::Order> storyOrder;
	};

	struct QuestAssistOut
	{
		std::uint32_t sequence{};
		std::string formKey;
		std::uint32_t objectiveId{};
		std::string kind;
		std::string targetFormKey;
		std::uint32_t attackSequence{};
	};

	struct QuestCreditIn
	{
		std::string creditId;
		std::uint32_t actorPlayerId{};
		std::string formKey;
		std::uint32_t objectiveId{};
		std::string kind;
		std::string targetFormKey;
	};

	struct QuestPresenceIn
	{
		std::uint32_t ownerPlayerId{};
		std::string formKey;
		std::uint32_t stage{};
	};

	struct FireteamMemberIn
	{
		std::uint32_t playerId{};
		std::string characterName;
	};

	struct FireteamSnapshotIn
	{
		std::string fireteamId;
		std::uint32_t leaderPlayerId{};
		std::vector<FireteamMemberIn> members;
	};

	struct FireteamInviteIn
	{
		std::string inviteId;
		std::uint32_t fromPlayerId{};
		std::string fromName;
		std::uint32_t targetPlayerId{};
	};

	struct FireteamDeclineIn
	{
		std::string inviteId;
		std::uint32_t fromPlayerId{};
	};

	struct FireteamEventIn
	{
		std::string reason;
	};

	[[nodiscard]] Json EncodeReport(const QuestReport& a_report);
	[[nodiscard]] Json EncodeScope(const QuestScope& a_scope);
	[[nodiscard]] Json EncodeReady();
	[[nodiscard]] Json EncodeAssist(const QuestAssistOut& a_assist);
	[[nodiscard]] Json EncodeCreditAck(std::string_view a_creditId, std::uint32_t a_stage);
	[[nodiscard]] Json EncodeFireteamInvite(std::uint32_t a_targetPlayerId);
	[[nodiscard]] Json EncodeFireteamRespond(std::string_view a_inviteId, bool a_accept);
	[[nodiscard]] Json EncodeFireteamLeave();
	[[nodiscard]] Json EncodeFireteamKick(std::uint32_t a_targetPlayerId);
	[[nodiscard]] std::optional<QuestCreditIn> DecodeCredit(const Json& a_packet);
	[[nodiscard]] std::optional<QuestPresenceIn> DecodePresence(const Json& a_packet);
	[[nodiscard]] std::optional<FireteamSnapshotIn> DecodeFireteamSnapshot(const Json& a_packet);
	[[nodiscard]] std::optional<FireteamInviteIn> DecodeFireteamInvite(const Json& a_packet);
	[[nodiscard]] std::optional<FireteamDeclineIn> DecodeFireteamDecline(const Json& a_packet);
	[[nodiscard]] std::optional<FireteamEventIn> DecodeFireteamEvent(const Json& a_packet);
	[[nodiscard]] std::optional<QuestNotReadyIn> DecodeNotReady(const Json& a_packet);
	[[nodiscard]] std::optional<QuestNotReachableIn> DecodeNotReachable(const Json& a_packet);
	[[nodiscard]] std::optional<QuestStageIn> DecodeStage(const Json& a_packet);
	[[nodiscard]] std::optional<QuestSyncIn> DecodeSync(const Json& a_packet);
	[[nodiscard]] std::optional<QuestPolicyIn> DecodePolicy(const Json& a_packet);
}
