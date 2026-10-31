#pragma once

#include "F4TQuestProtocol.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::QuestState
{
	inline constexpr std::size_t kMaximumTrackedQuests = 512;
	inline constexpr std::size_t kMaximumCompletedQuests = 512;
	inline constexpr std::size_t kMaximumQuestsPerProfile = kMaximumTrackedQuests / 4;
	inline constexpr std::size_t kMaximumCompletionsPerProfile = kMaximumCompletedQuests / 4;
	inline constexpr std::uint16_t kMaximumStageAdvance = 1024;
	inline constexpr std::uint16_t kMaximumStageCorrectionGap = kMaximumStageAdvance / 2;
	inline constexpr std::size_t kMaximumHeldStages = 256;
	inline constexpr std::size_t kMaximumGates = 256;
	inline constexpr std::size_t kMaximumSoloPlayers = 256;
	inline constexpr std::size_t kMaximumSoloQuestsPerPlayer = 256;
	inline constexpr std::size_t kMaximumPartyPlayers = 256;
	inline constexpr std::size_t kMaximumPartyQuestsPerPlayer = 256;
	inline constexpr std::size_t kMaximumProfileIdChars = 64;
	inline constexpr std::size_t kMaximumProfileRecords = 256;
	inline constexpr std::size_t kMaximumProfileQuestsPerRecord = 64;

	using ScopeMap = std::map<std::string, std::vector<QuestProtocol::FormKey>>;

	enum class Sharing : std::uint8_t
	{
		Shared,
		Solo,
		Party,
		Blocked
	};

	enum class Outcome : std::uint8_t
	{
		Applied,
		CompletedOnly,
		Corrected,
		IgnoredDuplicate,
		IgnoredStale,
		Held,
		DroppedSolo,
		DroppedBlocked,
		RejectedCapacity,
		RejectedImplausible
	};

	struct Entry
	{
		QuestProtocol::FormKey quest;
		std::uint16_t stage{};
		std::uint32_t fromPlayerId{};
		bool confirmed{};
		std::string party;
	};

	struct HeldEntry
	{
		QuestProtocol::FormKey quest;
		std::uint16_t stage{};
		bool completed{};
		std::uint32_t fromPlayerId{};
		std::string party;
	};

	struct ReportRequest
	{
		QuestProtocol::FormKey quest;
		std::uint16_t stage{};
		bool completed{};
		Sharing sharing{ Sharing::Shared };
		std::uint32_t fromPlayerId{};
		std::string party;
		std::string profileId;
	};

	struct ReportResult
	{
		Outcome outcome{ Outcome::RejectedCapacity };
		std::optional<Entry> relayed;
		bool completed{};
		bool profileRecorded{};
	};

	struct TrackedQuest
	{
		QuestProtocol::FormKey quest;
		std::string party;
	};

	struct ProfileQuest
	{
		QuestProtocol::FormKey quest;
		std::uint16_t stage{};
		bool completed{};
	};

	class Table
	{
	public:
		explicit Table(std::size_t a_maximumQuests = kMaximumTrackedQuests);

		[[nodiscard]] ReportResult Report(const ReportRequest& a_request);
		[[nodiscard]] Sharing ResolveSharing(
			std::string_view a_profileId,
			const QuestProtocol::FormKey& a_quest,
			QuestProtocol::Mode a_mode,
			const std::vector<QuestProtocol::OverrideEntry>& a_overrides) const;
		[[nodiscard]] static bool Receives(
			Sharing a_receiverSharing,
			std::string_view a_receiverParty,
			std::string_view a_track,
			std::string_view a_sourceParty) noexcept;

		[[nodiscard]] bool IsGated(const QuestProtocol::FormKey& a_quest, std::uint16_t a_stage) const;
		[[nodiscard]] bool AddGate(const QuestProtocol::FormKey& a_quest, std::uint16_t a_stage);
		[[nodiscard]] std::vector<HeldEntry> ReleaseGate(const QuestProtocol::FormKey& a_quest, std::uint16_t a_stage);
		[[nodiscard]] std::vector<QuestProtocol::StageRef> Gates() const;
		[[nodiscard]] std::vector<HeldEntry> Held() const;

		[[nodiscard]] bool SetSolo(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest, bool a_solo);
		[[nodiscard]] bool IsSolo(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest) const;
		[[nodiscard]] std::vector<QuestProtocol::FormKey> SoloQuests(std::string_view a_profileId) const;
		[[nodiscard]] bool SetParty(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest, bool a_party);
		[[nodiscard]] bool IsParty(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest) const;
		[[nodiscard]] std::vector<QuestProtocol::FormKey> PartyQuests(std::string_view a_profileId) const;
		[[nodiscard]] bool SetScope(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest, QuestProtocol::Scope a_scope);

		[[nodiscard]] std::vector<Entry> Stages() const;
		[[nodiscard]] std::optional<Entry> FindStage(const QuestProtocol::FormKey& a_quest, std::string_view a_party) const;
		[[nodiscard]] std::optional<std::uint16_t> ProfileStage(
			std::string_view a_profileId,
			const QuestProtocol::FormKey& a_quest) const;
		[[nodiscard]] bool ProfileCompleted(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest) const;
		[[nodiscard]] std::vector<ProfileQuest> ProfileQuests(std::string_view a_profileId) const;
		[[nodiscard]] bool IsCompletedFor(const QuestProtocol::FormKey& a_quest, std::string_view a_party) const;
		[[nodiscard]] std::vector<QuestProtocol::FormKey> Completed() const;
		[[nodiscard]] std::vector<TrackedQuest> CompletedTracks() const;
		[[nodiscard]] bool IsCompleted(const QuestProtocol::FormKey& a_quest) const;
		[[nodiscard]] std::size_t TrackedCount() const noexcept;
		[[nodiscard]] bool IsEmpty() const noexcept;

		[[nodiscard]] std::uint32_t Version() const noexcept;
		void BumpVersion() noexcept;

		[[nodiscard]] nlohmann::json ToJson() const;
		[[nodiscard]] static std::optional<Table> FromJson(const nlohmann::json& a_stored);

	private:
		struct Completion
		{
			QuestProtocol::FormKey quest;
			std::uint32_t fromPlayerId{};
			std::string party;
		};

		[[nodiscard]] bool HasCompletion(const QuestProtocol::FormKey& a_quest, std::string_view a_party) const;
		[[nodiscard]] bool RecordCompleted(const QuestProtocol::FormKey& a_quest, std::uint32_t a_fromPlayerId, std::string_view a_party);
		[[nodiscard]] ReportResult Hold(const ReportRequest& a_request);
		[[nodiscard]] std::optional<Entry> Find(const QuestProtocol::FormKey& a_quest, std::string_view a_party) const;
		[[nodiscard]] Entry* FindMutable(const QuestProtocol::FormKey& a_quest, std::string_view a_party);
		[[nodiscard]] std::size_t TrackedByProfile(std::uint32_t a_fromPlayerId) const;
		[[nodiscard]] std::size_t CompletedByProfile(std::uint32_t a_fromPlayerId) const;
		[[nodiscard]] bool RecordProfileProgress(const ReportRequest& a_request);

		std::size_t _maximumQuests;
		std::uint32_t _version{ 1 };
		std::vector<Entry> _stages;
		std::vector<Completion> _completed;
		std::vector<HeldEntry> _held;
		std::vector<QuestProtocol::StageRef> _gates;
		ScopeMap _solo;
		ScopeMap _party;
		std::map<std::string, std::vector<ProfileQuest>> _profiles;
		std::map<std::string, std::uint64_t> _profileActivity;
		std::uint64_t _profileClock{};
	};
}
