#include "F4TQuestLogic.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <utility>

namespace F4T::QuestKey
{
	namespace
	{
		constexpr std::string_view kHexDigits = "0123456789ABCDEF";

		bool IsHexDigit(char a_character)
		{
			return kHexDigits.find(static_cast<char>(std::toupper(static_cast<unsigned char>(a_character)))) !=
				std::string_view::npos;
		}

		bool IsSafePluginName(std::string_view a_plugin)
		{
			if (a_plugin.empty() || a_plugin.size() > kMaximumPluginNameBytes) {
				return false;
			}
			return std::all_of(a_plugin.begin(), a_plugin.end(), [](char a_character) {
				const auto value = static_cast<unsigned char>(a_character);
				return value >= 0x20 && value != 0x7F && a_character != ':' && a_character != '"' && a_character != '\\';
			});
		}

		bool EqualsIgnoreCase(std::string_view a_lhs, std::string_view a_rhs)
		{
			if (a_lhs.size() != a_rhs.size()) {
				return false;
			}
			return std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), [](char a_left, char a_right) {
				return std::toupper(static_cast<unsigned char>(a_left)) == std::toupper(static_cast<unsigned char>(a_right));
			});
		}

		std::string ToLowerAscii(std::string_view a_plugin)
		{
			std::string lowered(a_plugin);
			std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](char a_character) {
				return a_character >= 'A' && a_character <= 'Z' ? static_cast<char>(a_character - 'A' + 'a') : a_character;
			});
			return lowered;
		}

		const LoadOrderFile* FindByIndex(const std::vector<LoadOrderFile>& a_loadOrder, FileKind a_kind, std::uint32_t a_index)
		{
			const auto entry = std::find_if(a_loadOrder.begin(), a_loadOrder.end(), [&](const LoadOrderFile& a_file) {
				return a_file.kind == a_kind && a_file.index == a_index;
			});
			return entry == a_loadOrder.end() ? nullptr : &*entry;
		}

		const LoadOrderFile* FindByName(const std::vector<LoadOrderFile>& a_loadOrder, std::string_view a_plugin)
		{
			const auto entry = std::find_if(a_loadOrder.begin(), a_loadOrder.end(), [&](const LoadOrderFile& a_file) {
				return EqualsIgnoreCase(a_file.plugin, a_plugin);
			});
			return entry == a_loadOrder.end() ? nullptr : &*entry;
		}
	}

	std::string Format(const Key& a_key)
	{
		return std::format("{:06X}:{}", a_key.localId, a_key.plugin);
	}

	std::optional<Key> Parse(std::string_view a_key)
	{
		if (a_key.empty() || a_key.size() > kMaximumQuestKeyBytes) {
			return std::nullopt;
		}

		const auto colon = a_key.find(':');
		if (colon == std::string_view::npos || a_key.find(':', colon + 1) != std::string_view::npos) {
			return std::nullopt;
		}

		const auto hex = a_key.substr(0, colon);
		const auto plugin = a_key.substr(colon + 1);
		if (!IsSafePluginName(plugin) || hex.size() != kLocalIdDigits ||
			!std::all_of(hex.begin(), hex.end(), IsHexDigit)) {
			return std::nullopt;
		}

		std::uint32_t localId{};
		const auto [end, error] = std::from_chars(hex.data(), hex.data() + hex.size(), localId, 16);
		if (error != std::errc{} || end != hex.data() + hex.size() || localId > F4T::FormIdentity::MaximumRegularLocalId) {
			return std::nullopt;
		}
		return Key{ ToLowerAscii(plugin), localId };
	}

	std::optional<Key> FromFormId(std::uint32_t a_formId, const std::vector<LoadOrderFile>& a_loadOrder)
	{
		const auto split = F4T::FormIdentity::SplitFormId(a_formId);
		if (split.kind == F4T::FormIdentity::Kind::Runtime) {
			return std::nullopt;
		}

		const auto kind = split.kind == F4T::FormIdentity::Kind::Light ? FileKind::Light : FileKind::Regular;
		const auto* file = FindByIndex(a_loadOrder, kind, split.fileIndex);
		if (!file || !F4T::FormIdentity::IsValidRecordIdentity({ file->plugin, split.localId }, split.kind)) {
			return std::nullopt;
		}
		return Key{ ToLowerAscii(file->plugin), split.localId };
	}

	std::optional<std::uint32_t> ToFormId(const Key& a_key, const std::vector<LoadOrderFile>& a_loadOrder)
	{
		const auto* file = FindByName(a_loadOrder, a_key.plugin);
		if (!file) {
			return std::nullopt;
		}

		const auto kind = file->kind == FileKind::Light ? F4T::FormIdentity::Kind::Light : F4T::FormIdentity::Kind::Regular;
		if (!F4T::FormIdentity::IsValidRecordIdentity({ a_key.plugin, a_key.localId }, kind)) {
			return std::nullopt;
		}
		if (file->kind == FileKind::Light) {
			if (file->index > F4T::FormIdentity::MaximumLightFileIndex) {
				return std::nullopt;
			}
			return (static_cast<std::uint32_t>(F4T::FormIdentity::LightFileMarker) << 24) | (file->index << 12) |
				a_key.localId;
		}
		if (file->index >= F4T::FormIdentity::RuntimeFileMarker) {
			return std::nullopt;
		}
		return (file->index << 24) | a_key.localId;
	}
}

namespace F4T::QuestLogic
{
	Mode ParseMode(std::string_view a_mode) noexcept
	{
		if (a_mode == "faction-war") {
			return Mode::FactionWar;
		}
		if (a_mode == "instanced-story") {
			return Mode::InstancedStory;
		}
		if (a_mode == "free-roam") {
			return Mode::FreeRoam;
		}
		return Mode::CoopStory;
	}

	std::string_view ModeName(Mode a_mode) noexcept
	{
		switch (a_mode) {
		case Mode::FactionWar:
			return "faction-war";
		case Mode::InstancedStory:
			return "instanced-story";
		case Mode::FreeRoam:
			return "free-roam";
		case Mode::CoopStory:
		default:
			return "coop-story";
		}
	}

	bool ModeSharesProgress(Mode a_mode) noexcept
	{
		return a_mode == Mode::CoopStory || a_mode == Mode::FactionWar;
	}

	bool IsSharedQuestType(std::int32_t a_questType) noexcept
	{
		return a_questType > 0 && a_questType != kMiscellaneousQuestType;
	}

	void Tracker::Reset()
	{
		policy_ = Policy{};
		quests_.clear();
		pending_.clear();
		dropped_.clear();
		remoteCompletions_.clear();
		friends_.clear();
		settleUntilMilliseconds_ = 0;
	}

	void Tracker::SetStoryOrder(const F4T::QuestStory::Order& a_order)
	{
		storyOrder_ = a_order;
	}

	bool Tracker::IsStoryReached(std::string_view a_quest) const
	{
		if (storyOrder_.Empty()) {
			return true;
		}
		const auto parsed = QuestKey::Parse(a_quest);
		if (!parsed) {
			return false;
		}
		for (const auto& [quest, state] : quests_) {
			const auto held = QuestKey::Parse(quest);
			if (held && held->plugin == parsed->plugin && held->localId == parsed->localId) {
				return true;
			}
		}
		const F4T::QuestProtocol::FormKey key{ parsed->plugin, parsed->localId };
		const auto* row = storyOrder_.Find(key);
		if (row == nullptr || !row->startable) {
			return false;
		}
		for (const auto& requirement : row->prerequisites) {
			bool satisfied = false;
			for (const auto& [quest, state] : quests_) {
				const auto have = QuestKey::Parse(quest);
				if (!have || have->plugin != requirement.quest.plugin || have->localId != requirement.quest.localId ||
						!state.initialized) {
					continue;
				}
				satisfied =
					requirement.completed ? state.completed : state.lastStage >= requirement.stage.value_or(0);
				break;
			}
			if (!satisfied) {
				return false;
			}
		}
		return true;
	}

	std::optional<std::string> Tracker::NextStoryStep() const
	{
		if (storyOrder_.Empty()) {
			return std::nullopt;
		}
		for (const auto& row : storyOrder_.Rows()) {
			const std::string key = QuestKey::Format(F4T::QuestKey::Key{ row.quest.plugin, row.quest.localId });
			if (!IsStoryReached(key)) {
				continue;
			}
			bool finished = false;
			for (const auto& [quest, state] : quests_) {
				const auto have = QuestKey::Parse(quest);
				if (have && have->plugin == row.quest.plugin && have->localId == row.quest.localId &&
					state.initialized) {
					finished = state.completed;
					break;
				}
			}
			if (!finished) {
				return key;
			}
		}
		return std::nullopt;
	}

	void Tracker::BeginSettle(std::uint64_t a_nowMilliseconds) noexcept
	{
		settleUntilMilliseconds_ = a_nowMilliseconds + kSettleMilliseconds;
	}

	void Tracker::SetPolicy(Policy a_policy)
	{
		policy_ = std::move(a_policy);
		if (!SharesProgress()) {
			pending_.clear();
			friends_.clear();
		}
	}

	const Policy& Tracker::GetPolicy() const noexcept
	{
		return policy_;
	}

	bool Tracker::SharesProgress() const noexcept
	{
		return ModeSharesProgress(policy_.mode);
	}

	bool Tracker::IsSolo(std::string_view a_quest) const
	{
		return std::find(policy_.soloQuests.begin(), policy_.soloQuests.end(), a_quest) != policy_.soloQuests.end();
	}

	bool Tracker::IsParty(std::string_view a_quest) const
	{
		return std::find(policy_.partyQuests.begin(), policy_.partyQuests.end(), a_quest) != policy_.partyQuests.end();
	}

	bool Tracker::IsGated(std::string_view a_quest, std::uint32_t a_stage) const
	{
		return std::any_of(policy_.gates.begin(), policy_.gates.end(), [&](const Gate& a_gate) {
			return a_gate.quest == a_quest && a_stage == a_gate.stage;
		});
	}

	bool Tracker::IsReportable(std::string_view a_quest, std::int32_t a_questType) const
	{
		return SharesProgress() && !IsSolo(a_quest) && IsSharedQuestType(a_questType);
	}

	std::optional<Report> Tracker::Observe(const Observation& a_observation, std::uint64_t a_nowMilliseconds)
	{
		if (a_observation.quest.empty()) {
			return std::nullopt;
		}

		auto& state = quests_[a_observation.quest];
		const auto previousStage = state.lastStage;
		const bool first = !state.initialized;
		const bool moved = !first && a_observation.stage != previousStage;
		const bool forward = a_observation.stage > previousStage;
		const bool completedNow = a_observation.completed && !state.completed;

		state.lastStage = a_observation.stage;
		state.completed = a_observation.completed;
		state.initialized = true;

		if (first || a_nowMilliseconds < settleUntilMilliseconds_ || !IsReportable(a_observation.quest, a_observation.questType)) {
			return std::nullopt;
		}

		const bool remoteRecently =
			state.hasRemoteApplied && a_nowMilliseconds - state.remoteAppliedAtMilliseconds < kRemoteCompletionWindowMilliseconds;

		if (completedNow) {
			if (remoteRecently) {
				return std::nullopt;
			}
			return Report{ a_observation.quest, a_observation.stage, true };
		}

		if (!moved || !forward || !a_observation.started || !a_observation.running) {
			return std::nullopt;
		}
		if (state.hasRemoteApplied &&
			a_nowMilliseconds - state.remoteAppliedAtMilliseconds < kEchoWindowMilliseconds) {
			return std::nullopt;
		}
		return Report{ a_observation.quest, a_observation.stage, false };
	}

	bool Tracker::QueueRemoteStage(std::string_view a_quest, std::uint32_t a_stage, bool a_completed)
	{
		if (a_quest.empty()) {
			return false;
		}
		if (!SharesProgress() || IsSolo(a_quest)) {
			NoteDropped(a_quest, SharesProgress() ? DropCause::Solo : DropCause::NotShared);
			return false;
		}
		if (!IsStoryReached(a_quest)) {
			return false;
		}

		auto& held = pending_[std::string(a_quest)];
		const auto position = std::lower_bound(
			held.begin(),
			held.end(),
			a_stage,
			[](const StageEntry& a_entry, std::uint32_t a_value) { return a_entry.stage < a_value; });
		if (position != held.end() && position->stage == a_stage) {
			position->completed = position->completed || a_completed;
			return true;
		}
		held.insert(position, StageEntry{ std::string(a_quest), a_stage, a_completed });
		return true;
	}

	void Tracker::QueueRemoteSync(const std::vector<StageEntry>& a_stages, const std::vector<std::string>& a_completed)
	{
		for (const auto& stage : a_stages) {
			QueueRemoteStage(stage.quest, stage.stage, stage.completed);
		}

		if (!SharesProgress()) {
			return;
		}
		for (const auto& quest : a_completed) {
			if (quest.empty() || IsSolo(quest) || !IsStoryReached(quest)) {
				continue;
			}
			if (std::find(remoteCompletions_.begin(), remoteCompletions_.end(), quest) == remoteCompletions_.end()) {
				remoteCompletions_.push_back(quest);
			}
		}
	}

	std::vector<std::string> Tracker::TakeRemoteCompletions()
	{
		return std::exchange(remoteCompletions_, {});
	}

	void Tracker::RestoreRemoteCompletions(std::vector<std::string> a_quests)
	{
		if (a_quests.empty()) {
			return;
		}
		for (const auto& quest : remoteCompletions_) {
			if (std::find(a_quests.begin(), a_quests.end(), quest) == a_quests.end()) {
				a_quests.push_back(quest);
			}
		}
		remoteCompletions_ = std::move(a_quests);
	}

	void Tracker::NoteRemoteApplied(std::string_view a_quest, std::uint32_t a_stage, std::uint64_t a_nowMilliseconds)
	{
		auto& state = quests_[std::string(a_quest)];
		state.initialized = true;
		state.hasRemoteApplied = true;
		state.remoteAppliedAtMilliseconds = a_nowMilliseconds;
		state.lastStage = std::max(state.lastStage, a_stage);
	}

	void Tracker::NoteFriendStage(std::string_view a_quest, std::uint32_t a_playerId, std::uint32_t a_stage, bool a_completed)
	{
		if (a_quest.empty() || a_playerId == 0 || !SharesProgress() || IsSolo(a_quest)) {
			return;
		}
		if (friends_.find(std::string(a_quest)) == friends_.end() && friends_.size() >= kMaximumFriendQuests) {
			return;
		}

		auto& players = friends_[std::string(a_quest)];
		const auto position = std::lower_bound(
			players.begin(),
			players.end(),
			a_playerId,
			[](const FriendStage& a_entry, std::uint32_t a_value) { return a_entry.playerId < a_value; });
		if (position != players.end() && position->playerId == a_playerId) {
			position->stage = a_stage;
			position->completed = a_completed;
			return;
		}
		if (players.size() >= kMaximumFriendsPerQuest) {
			return;
		}
		players.insert(position, FriendStage{ a_playerId, a_stage, a_completed });
	}

	void Tracker::SetLocalPlayerId(std::optional<std::uint32_t> a_playerId)
	{
		localPlayerId_ = a_playerId;
	}

	std::optional<StageEntry> Tracker::TakeReadyStage(
		std::string_view a_quest,
		const ApplyContext& a_context,
		std::uint64_t a_nowMilliseconds)
	{
		if (a_quest.empty()) {
			return std::nullopt;
		}
		if (!SharesProgress() || IsSolo(a_quest)) {
			NoteDropped(a_quest, SharesProgress() ? DropCause::Solo : DropCause::NotShared);
			pending_.erase(std::string(a_quest));
			return std::nullopt;
		}

		if (!IsStoryReached(a_quest)) {
			NoteDropped(a_quest, DropCause::NotReached);
			pending_.erase(std::string(a_quest));
			return std::nullopt;
		}

		const auto pending = pending_.find(std::string(a_quest));
		if (pending == pending_.end()) {
			return std::nullopt;
		}

		auto& held = pending->second;
		for (const auto& entry : held) {
			if (entry.stage <= a_context.currentStage && entry.completed &&
				std::find(remoteCompletions_.begin(), remoteCompletions_.end(), entry.quest) == remoteCompletions_.end()) {
				remoteCompletions_.push_back(entry.quest);
			}
		}
		std::erase_if(held, [&](const StageEntry& a_entry) { return a_entry.stage <= a_context.currentStage; });
		if (held.empty()) {
			pending_.erase(pending);
			return std::nullopt;
		}
		if (!a_context.started || !a_context.running || a_context.completed || a_context.busy) {
			return std::nullopt;
		}

		const auto ready = std::find_if(held.begin(), held.end(), [&](const StageEntry& a_entry) {
			return !IsGated(a_quest, a_entry.stage);
		});
		if (ready == held.end()) {
			return std::nullopt;
		}

		const StageEntry entry = *ready;
		held.erase(ready);
		if (held.empty()) {
			pending_.erase(pending);
		}

		NoteRemoteApplied(a_quest, entry.stage, a_nowMilliseconds);
		return entry;
	}

	void Tracker::NoteDropped(std::string_view a_quest, DropCause a_cause)
	{
		const auto found = std::find_if(dropped_.begin(), dropped_.end(), [a_quest](const auto& a_entry) {
			return a_entry.quest == a_quest;
		});
		if (found != dropped_.end()) {
			found->cause = a_cause;
			return;
		}
		dropped_.push_back(DroppedStage{ std::string(a_quest), a_cause });
	}

	std::vector<DroppedStage> Tracker::TakeDroppedStages()
	{
		return std::exchange(dropped_, {});
	}

	std::vector<std::string> Tracker::PendingQuests() const
	{
		std::vector<std::string> quests;
		quests.reserve(pending_.size());
		for (const auto& [quest, stages] : pending_) {
			if (!stages.empty()) {
				quests.push_back(quest);
			}
		}
		return quests;
	}

	std::vector<View> Tracker::Snapshot() const
	{
		std::vector<View> views;
		views.reserve(quests_.size() + pending_.size());
		for (const auto& [quest, state] : quests_) {
			views.push_back(View{ quest, state.lastStage, state.completed, false });
		}
		for (const auto& [quest, stages] : pending_) {
			if (stages.empty()) {
				continue;
			}
			const auto existing = std::find_if(views.begin(), views.end(), [&quest](const View& a_view) {
				return a_view.quest == quest;
			});
			if (existing != views.end()) {
				existing->held = true;
			} else {
				views.push_back(View{ quest, 0, false, true });
			}
		}
		std::erase_if(views, [this](const View& a_view) {
			return a_view.stage == 0 && !a_view.completed && !a_view.held && !IsSolo(a_view.quest) && !IsParty(a_view.quest);
		});
		for (auto& view : views) {
			const auto found = friends_.find(view.quest);
			if (found == friends_.end() || IsSolo(view.quest)) {
				continue;
			}
			for (const auto& entry : found->second) {
				if (!localPlayerId_ || entry.playerId != *localPlayerId_) {
					view.friends.push_back(entry);
				}
			}
		}
		std::sort(views.begin(), views.end(), [](const View& a_left, const View& a_right) {
			return a_left.quest < a_right.quest;
		});
		return views;
	}
}
