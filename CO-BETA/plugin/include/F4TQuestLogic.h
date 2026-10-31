#pragma once

#include "F4TFormIdentity.h"
#include "F4TQuestStoryOrder.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace F4T::QuestKey
{
	enum class FileKind : std::uint8_t
	{
		Regular,
		Light
	};

	struct Key
	{
		std::string plugin;
		std::uint32_t localId{};

		[[nodiscard]] bool operator==(const Key&) const noexcept = default;
	};

	struct LoadOrderFile
	{
		std::string plugin;
		FileKind kind{ FileKind::Regular };
		std::uint32_t index{};
	};

	inline constexpr std::size_t kLocalIdDigits = 6;
	// Wire caps; must stay equal to co-core/include/F4TQuestProtocol.h (kMaximumPluginChars 128, kMaximumQuestKeyChars 135).
	inline constexpr std::size_t kMaximumPluginNameBytes = 128;
	inline constexpr std::size_t kMaximumQuestKeyBytes = kLocalIdDigits + 1 + kMaximumPluginNameBytes;

	[[nodiscard]] std::string Format(const Key& a_key);
	[[nodiscard]] std::optional<Key> Parse(std::string_view a_key);
	[[nodiscard]] std::optional<Key> FromFormId(std::uint32_t a_formId, const std::vector<LoadOrderFile>& a_loadOrder);
	[[nodiscard]] std::optional<std::uint32_t> ToFormId(const Key& a_key, const std::vector<LoadOrderFile>& a_loadOrder);
}

namespace F4T::QuestLogic
{
	inline constexpr std::uint64_t kSettleMilliseconds = 5000;
	inline constexpr std::uint64_t kEchoWindowMilliseconds = 3000;
	inline constexpr std::uint64_t kRemoteCompletionWindowMilliseconds = 10000;
	// Quest type ids must match RE/Q/QUEST_TYPE.h (kNone = 0x0, kMiscellaneous = 0x6).
	inline constexpr std::int32_t kMiscellaneousQuestType = 6;
	inline constexpr std::size_t kMaximumFriendQuests = 32;
	inline constexpr std::size_t kMaximumFriendsPerQuest = 64;

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

	struct Gate
	{
		std::string quest;
		std::uint32_t stage{};
	};

	struct StageEntry
	{
		std::string quest;
		std::uint32_t stage{};
		bool completed{};

		[[nodiscard]] bool operator==(const StageEntry&) const noexcept = default;
	};

	enum class DropCause : std::uint8_t
	{
		NotReached,
		NotShared,
		Solo
	};

	struct DroppedStage
	{
		std::string quest;
		DropCause cause{ DropCause::NotReached };
	};

	struct Policy
	{
		Mode mode{ Mode::CoopStory };
		std::uint32_t version{};
		std::vector<std::string> soloQuests;
		std::vector<Gate> gates;
		std::vector<std::string> partyQuests;
	};

	struct Observation
	{
		std::string quest;
		std::uint32_t stage{};
		std::int32_t questType{};
		bool running{ false };
		bool completed{ false };
		bool started{ false };
	};

	struct Report
	{
		std::string quest;
		std::uint32_t stage{};
		bool completed{ false };
	};

	struct ApplyContext
	{
		std::uint32_t currentStage{};
		bool started{ false };
		bool running{ false };
		bool completed{ false };
		bool busy{ false };
	};

	struct FriendStage
	{
		std::uint32_t playerId{};
		std::uint32_t stage{};
		bool completed{ false };
	};

	struct View
	{
		std::string quest;
		std::uint32_t stage{};
		bool completed{ false };
		bool held{ false };
		std::vector<FriendStage> friends;
	};

	[[nodiscard]] Mode ParseMode(std::string_view a_mode) noexcept;
	[[nodiscard]] std::string_view ModeName(Mode a_mode) noexcept;
	[[nodiscard]] bool ModeSharesProgress(Mode a_mode) noexcept;
	[[nodiscard]] bool IsSharedQuestType(std::int32_t a_questType) noexcept;

	class Tracker
	{
	public:
		void Reset();
		void SetStoryOrder(const F4T::QuestStory::Order& a_order);
		[[nodiscard]] bool IsStoryReached(std::string_view a_quest) const;
		[[nodiscard]] std::optional<std::string> NextStoryStep() const;
		void BeginSettle(std::uint64_t a_nowMilliseconds) noexcept;
		void SetPolicy(Policy a_policy);
		[[nodiscard]] const Policy& GetPolicy() const noexcept;
		[[nodiscard]] bool IsSolo(std::string_view a_quest) const;
		[[nodiscard]] bool IsParty(std::string_view a_quest) const;
		[[nodiscard]] bool IsGated(std::string_view a_quest, std::uint32_t a_stage) const;
		[[nodiscard]] bool SharesProgress() const noexcept;

		[[nodiscard]] std::optional<Report> Observe(const Observation& a_observation, std::uint64_t a_nowMilliseconds);

		bool QueueRemoteStage(std::string_view a_quest, std::uint32_t a_stage, bool a_completed = false);
		void QueueRemoteSync(const std::vector<StageEntry>& a_stages, const std::vector<std::string>& a_completed);
		[[nodiscard]] std::vector<std::string> TakeRemoteCompletions();
		void RestoreRemoteCompletions(std::vector<std::string> a_quests);

		[[nodiscard]] std::optional<StageEntry> TakeReadyStage(
			std::string_view a_quest,
			const ApplyContext& a_context,
			std::uint64_t a_nowMilliseconds);
		[[nodiscard]] std::vector<std::string> PendingQuests() const;
		[[nodiscard]] std::vector<DroppedStage> TakeDroppedStages();
		[[nodiscard]] std::vector<View> Snapshot() const;

		void NoteRemoteApplied(std::string_view a_quest, std::uint32_t a_stage, std::uint64_t a_nowMilliseconds);

		void NoteFriendStage(std::string_view a_quest, std::uint32_t a_playerId, std::uint32_t a_stage, bool a_completed);
		void SetLocalPlayerId(std::optional<std::uint32_t> a_playerId);

	private:
		struct QuestState
		{
			std::uint32_t lastStage{};
			std::uint64_t remoteAppliedAtMilliseconds{};
			bool completed{ false };
			bool initialized{ false };
			bool hasRemoteApplied{ false };
		};

		[[nodiscard]] bool IsReportable(std::string_view a_quest, std::int32_t a_questType) const;
		void NoteDropped(std::string_view a_quest, DropCause a_cause);

		Policy policy_{};
		F4T::QuestStory::Order storyOrder_{};
		std::unordered_map<std::string, QuestState> quests_;
		std::unordered_map<std::string, std::vector<StageEntry>> pending_;
		std::vector<DroppedStage> dropped_;
		std::vector<std::string> remoteCompletions_;
		std::uint64_t settleUntilMilliseconds_{ 0 };
		std::unordered_map<std::string, std::vector<FriendStage>> friends_;
		std::optional<std::uint32_t> localPlayerId_;
	};
}
