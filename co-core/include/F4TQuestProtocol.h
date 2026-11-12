#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::QuestProtocol
{
	inline constexpr std::size_t kFormKeyDigits = 6;
	inline constexpr std::size_t kMaximumPluginChars = 128;
	inline constexpr std::size_t kMaximumQuestKeyChars = kFormKeyDigits + 1 + kMaximumPluginChars;
	inline constexpr std::uint16_t kMaximumStage = 0xFFFF;
	inline constexpr std::size_t kMaximumSyncStages = 512;
	inline constexpr std::size_t kMaximumCompletedQuests = 512;
	inline constexpr std::size_t kMaximumSoloQuests = 256;
	inline constexpr std::size_t kMaximumPartyQuests = 256;
	inline constexpr std::size_t kMaximumGates = 256;
	inline constexpr std::size_t kMaximumOverrideEntries = 256;
	inline constexpr std::size_t kMaximumSyncFrameBytes = 48U * 1024U;
	inline constexpr std::size_t kMaximumFireteamQuests = 256;
	inline constexpr std::size_t kMaximumPresenceRefs = 32;
	inline constexpr std::size_t kMaximumCharacterNameChars = 64;
	inline constexpr std::size_t kMaximumFireteamIdChars = 32;
	inline constexpr std::size_t kMaximumCreditIdChars = 32;
	inline constexpr std::size_t kMaximumFireteamMembers = 4;
	inline constexpr std::uint32_t kMaximumObjectiveId = 0xFFFFFFFFu;
	inline constexpr std::string_view kSyncCapability = "quest-sync-v1";
	inline constexpr std::string_view kReadyCapability = "quest-ready-v1";
	inline constexpr std::string_view kFireteamCapability = "quest-fireteam-v1";
	inline constexpr std::string_view kFireteamInviteType = "fireteamInvite";
	inline constexpr std::string_view kFireteamRespondType = "fireteamRespond";
	inline constexpr std::string_view kFireteamLeaveType = "fireteamLeave";
	inline constexpr std::string_view kFireteamKickType = "fireteamKick";
	inline constexpr std::string_view kFireteamSnapshotType = "fireteamSnapshot";
	inline constexpr std::string_view kFireteamEventType = "fireteamEvent";
	inline constexpr std::string_view kQuestAssistType = "questAssist";
	inline constexpr std::string_view kQuestCreditType = "questCredit";
	inline constexpr std::string_view kQuestCreditAckType = "questCreditAck";
	inline constexpr std::string_view kQuestPresenceType = "questPresence";
	inline constexpr std::string_view kFireteamReasonFull = "full";
	inline constexpr std::string_view kFireteamReasonAlreadyMember = "already_member";
	inline constexpr std::string_view kFireteamReasonNotMember = "not_member";
	inline constexpr std::string_view kFireteamReasonNoInvite = "no_invite";
	inline constexpr std::string_view kFireteamReasonNotLeader = "not_leader";
	inline constexpr std::string_view kFireteamReasonInvalidTarget = "invalid_target";
	inline constexpr std::string_view kFireteamReasonSelfTarget = "self_target";
	inline constexpr std::string_view kFireteamReasonUnknownMember = "unknown_member";
	inline constexpr std::string_view kFireteamLeaveLeft = "left";
	inline constexpr std::string_view kFireteamLeaveKicked = "kicked";
	inline constexpr std::string_view kFireteamLeaveDisbanded = "disbanded";
	inline constexpr std::string_view kFactionTrackPrefix = "f-";
	inline constexpr std::string_view kFireteamTrackPrefix = "p-";
	inline constexpr std::string_view kNotReadyType = "questNotReady";
	inline constexpr std::string_view kNotReadyReason = "notReady";
	inline constexpr std::string_view kStorageFullReason = "storageFull";
	inline constexpr std::string_view kNotInFireteamReason = "notInFireteam";
	inline constexpr std::string_view kUpdateRequiredReason = "updateRequired";
	inline constexpr std::string_view kNotReachableType = "questNotReachable";
	inline constexpr std::string_view kNotReachableUnknown = "unknown_quest";
	inline constexpr std::string_view kNotReachablePrerequisite = "prerequisite_missing";
	inline constexpr std::string_view kNotReachableNotStartable = "not_startable";
	inline constexpr std::string_view kNotReachableRegression = "stage_regression";
	inline constexpr std::uint32_t kTutorialQuestLocalId = 0x0001CC2A;
	inline constexpr std::string_view kTutorialQuestPlugin = "fallout4.esm";
	inline constexpr std::uint16_t kTutorialExitStage = 2;

	struct FormKey
	{
		std::string plugin;
		std::uint32_t localId{};

		[[nodiscard]] bool operator==(const FormKey&) const noexcept = default;

		[[nodiscard]] friend bool operator<(const FormKey& a_lhs, const FormKey& a_rhs) noexcept
		{
			if (a_lhs.plugin != a_rhs.plugin) {
				return a_lhs.plugin < a_rhs.plugin;
			}
			return a_lhs.localId < a_rhs.localId;
		}
	};

	enum class Mode : std::uint8_t
	{
		CoopStory,
		FactionWar,
		InstancedStory,
		FreeRoam
	};

	enum class Scope : std::uint8_t
	{
		Shared,
		Solo,
		Party,
		Fireteam
	};

	enum class AssistKind : std::uint8_t
	{
		KillActor,
		ReachLocation,
		Activate,
		Dialogue,
		Escort
	};

	enum class Override : std::uint8_t
	{
		Shared,
		Solo,
		Blocked
	};

	struct OverrideEntry
	{
		FormKey quest;
		Override policy{ Override::Shared };
	};

	struct Report
	{
		FormKey quest;
		std::uint16_t stage{};
		bool completed{};
	};

	struct ScopeRequest
	{
		FormKey quest;
		Scope scope{ Scope::Shared };
	};

	struct StageRef
	{
		FormKey quest;
		std::uint16_t stage{};
	};

	struct Stage
	{
		FormKey quest;
		std::uint16_t stage{};
		std::uint32_t fromPlayerId{};
		bool completed{};
	};

	struct Sync
	{
		std::vector<StageRef> stages;
		std::vector<FormKey> completed;
	};

	struct Policy
	{
		Mode mode{ Mode::CoopStory };
		std::uint32_t version{};
		std::vector<FormKey> soloQuests;
		std::vector<StageRef> gates;
		std::vector<FormKey> partyQuests;
		std::vector<FormKey> fireteamQuests;
		bool fireteamSupported{};
	};

	struct FireteamMember
	{
		std::uint32_t playerId{};
		std::string characterName;
	};

	struct FireteamSnapshot
	{
		std::string fireteamId;
		std::uint32_t leaderPlayerId{};
		std::vector<FireteamMember> members;
	};

	struct FireteamInvite
	{
		std::string inviteId;
		std::uint32_t fromPlayerId{};
		std::string fromName;
		std::uint32_t targetPlayerId{};
	};

	struct FireteamRespond
	{
		std::string inviteId;
		bool accept{};
	};

	struct FireteamKick
	{
		std::uint32_t targetPlayerId{};
	};

	struct Assist
	{
		std::uint32_t sequence{};
		FormKey quest;
		std::uint32_t objectiveId{};
		AssistKind kind{ AssistKind::KillActor };
		FormKey target;
		std::uint32_t attackSequence{};
	};

	struct Credit
	{
		std::string creditId;
		std::uint32_t actorPlayerId{};
		FormKey quest;
		std::uint32_t objectiveId{};
		AssistKind kind{ AssistKind::KillActor };
		FormKey target;
	};

	struct CreditAck
	{
		std::string creditId;
		std::uint16_t stage{};
	};

	struct PresenceRef
	{
		FormKey form;
		std::string cell;
		std::string worldspace;
		bool enabled{ true };
		bool locked{};
		bool hasPosition{};
		float x{};
		float y{};
		float z{};
	};

	struct Presence
	{
		std::uint32_t ownerPlayerId{};
		FormKey quest;
		std::uint16_t stage{};
		std::vector<PresenceRef> refs;
	};

	[[nodiscard]] std::optional<FormKey> ParseFormKey(std::string_view a_value);
	[[nodiscard]] std::string FormatFormKey(const FormKey& a_key);
	[[nodiscard]] std::optional<std::uint16_t> ParseStage(const nlohmann::json& a_value);
	[[nodiscard]] std::string_view ModeName(Mode a_mode) noexcept;
	[[nodiscard]] std::optional<Mode> ParseMode(std::string_view a_value) noexcept;
	[[nodiscard]] std::string_view OverrideName(Override a_policy) noexcept;
	[[nodiscard]] std::optional<Override> ParseOverride(std::string_view a_value) noexcept;

	[[nodiscard]] std::optional<Report> NormalizeReport(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<ScopeRequest> NormalizeScope(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Sync> NormalizeSync(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<Policy> NormalizePolicy(const nlohmann::json& a_packet);

	[[nodiscard]] nlohmann::json MakeNotReady(std::string_view a_reason, std::string_view a_quest, double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakeNotReachable(
		std::string_view a_reason,
		std::string_view a_quest,
		std::string_view a_missingQuest,
		std::string_view a_previousLabel,
		double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakeStage(const Stage& a_stage, double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakeSync(const Sync& a_sync, double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakePolicy(const Policy& a_policy, double a_unixTimeSeconds);

	[[nodiscard]] std::string_view ScopeName(Scope a_scope) noexcept;
	[[nodiscard]] std::optional<Scope> ParseScope(std::string_view a_value) noexcept;
	[[nodiscard]] std::string_view AssistKindName(AssistKind a_kind) noexcept;
	[[nodiscard]] std::optional<AssistKind> ParseAssistKind(std::string_view a_value) noexcept;

	[[nodiscard]] bool ValidFireteamId(std::string_view a_value) noexcept;
	[[nodiscard]] bool ValidCreditId(std::string_view a_value) noexcept;

	[[nodiscard]] std::optional<Assist> NormalizeAssist(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<CreditAck> NormalizeCreditAck(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<FireteamInvite> NormalizeFireteamInvite(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<FireteamRespond> NormalizeFireteamRespond(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<FireteamKick> NormalizeFireteamKick(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<FireteamSnapshot> NormalizeFireteamSnapshot(const nlohmann::json& a_packet);

	[[nodiscard]] nlohmann::json MakeFireteamSnapshot(const FireteamSnapshot& a_snapshot, double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakeFireteamInvite(const FireteamInvite& a_invite, double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakeFireteamRespond(const FireteamRespond& a_respond, std::uint32_t a_fromPlayerId, double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakeFireteamEvent(
		std::string_view a_type,
		std::string_view a_reason,
		std::uint32_t a_playerId,
		double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakeCredit(const Credit& a_credit, double a_unixTimeSeconds);
	[[nodiscard]] nlohmann::json MakePresence(const Presence& a_presence, double a_unixTimeSeconds);
}
