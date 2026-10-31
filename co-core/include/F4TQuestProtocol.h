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
	inline constexpr std::string_view kSyncCapability = "quest-sync-v1";
	inline constexpr std::string_view kReadyCapability = "quest-ready-v1";
	inline constexpr std::string_view kNotReadyType = "questNotReady";
	inline constexpr std::string_view kNotReadyReason = "notReady";
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
		Party
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
}
