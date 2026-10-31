#pragma once

#include "F4TQuestLogic.h"

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
	inline constexpr std::string_view kNotReadyType = "questNotReady";
	inline constexpr std::string_view kNotReachableType = "questNotReachable";
	// Reason codes; must stay equal to co-core/include/F4TQuestProtocol.h (kNotReadyReason, kUpdateRequiredReason).
	inline constexpr std::string_view kNotReadyReason = "notReady";
	inline constexpr std::string_view kUpdateRequiredReason = "updateRequired";

	inline constexpr std::size_t kMaximumLabelBytes = 256;
	inline constexpr std::size_t kMaximumStages = 512;
	inline constexpr std::size_t kMaximumCompleted = 512;
	inline constexpr std::size_t kMaximumSoloQuests = 256;
	inline constexpr std::size_t kMaximumPartyQuests = 256;
	inline constexpr std::size_t kMaximumGates = 256;

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
	};

	[[nodiscard]] Json EncodeReport(const QuestReport& a_report);
	[[nodiscard]] Json EncodeScope(const QuestScope& a_scope);
	[[nodiscard]] Json EncodeReady();
	[[nodiscard]] std::optional<QuestNotReadyIn> DecodeNotReady(const Json& a_packet);
	[[nodiscard]] std::optional<QuestNotReachableIn> DecodeNotReachable(const Json& a_packet);
	[[nodiscard]] std::optional<QuestStageIn> DecodeStage(const Json& a_packet);
	[[nodiscard]] std::optional<QuestSyncIn> DecodeSync(const Json& a_packet);
	[[nodiscard]] std::optional<QuestPolicyIn> DecodePolicy(const Json& a_packet);
}
