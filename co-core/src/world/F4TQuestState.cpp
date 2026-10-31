#include "F4TQuestState.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace F4T::QuestState
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<QuestProtocol::FormKey> ReadQuest(const Json& a_value)
		{
			if (!a_value.is_string()) return std::nullopt;
			return QuestProtocol::ParseFormKey(a_value.get_ref<const std::string&>());
		}

		std::optional<QuestProtocol::StageRef> ReadStageRef(const Json& a_item)
		{
			if (!a_item.is_object()) return std::nullopt;
			const auto quest = a_item.find("quest");
			const auto stage = a_item.find("stage");
			if (quest == a_item.end() || stage == a_item.end()) return std::nullopt;
			const auto key = ReadQuest(*quest);
			if (!key) return std::nullopt;
			const auto parsed = QuestProtocol::ParseStage(*stage);
			if (!parsed) return std::nullopt;
			return QuestProtocol::StageRef{ *key, *parsed };
		}

		bool Contains(const std::vector<QuestProtocol::FormKey>& a_quests, const QuestProtocol::FormKey& a_quest)
		{
			return std::find(a_quests.begin(), a_quests.end(), a_quest) != a_quests.end();
		}

		std::uint32_t ReadUnsigned(const Json& a_item, const char* a_name, std::uint32_t a_maximum)
		{
			const auto found = a_item.find(a_name);
			if (found == a_item.end() || !found->is_number_unsigned()) return 0;
			const auto stored = found->get<std::uint64_t>();
			return stored > a_maximum ? 0 : static_cast<std::uint32_t>(stored);
		}

		std::optional<std::string> ReadParty(const Json& a_item)
		{
			const auto found = a_item.find("party");
			if (found == a_item.end()) return std::string{};
			if (!found->is_string()) return std::nullopt;
			const auto& name = found->get_ref<const std::string&>();
			if (name.empty() || name.size() > kMaximumProfileIdChars) return std::nullopt;
			return name;
		}

		std::optional<ProfileQuest> ReadProfileQuest(const Json& a_item)
		{
			if (!a_item.is_object()) return std::nullopt;
			const auto quest = a_item.find("quest");
			const auto stage = a_item.find("stage");
			if (quest == a_item.end()) return std::nullopt;
			const auto key = ReadQuest(*quest);
			if (!key) return std::nullopt;
			std::uint16_t parsed = 0;
			if (stage != a_item.end()) {
				const auto value = QuestProtocol::ParseStage(*stage);
				if (!value) return std::nullopt;
				parsed = *value;
			}
			bool completed = false;
			if (const auto flag = a_item.find("completed"); flag != a_item.end()) {
				if (!flag->is_boolean()) return std::nullopt;
				completed = flag->get<bool>();
			}
			return ProfileQuest{ *key, parsed, completed };
		}

		std::optional<TrackedQuest> ReadCompletion(const Json& a_item)
		{
			if (a_item.is_string()) {
				const auto quest = ReadQuest(a_item);
				if (!quest) return std::nullopt;
				return TrackedQuest{ *quest, {} };
			}
			if (!a_item.is_object()) return std::nullopt;
			const auto quest = a_item.find("quest");
			const auto party = ReadParty(a_item);
			if (quest == a_item.end() || !party || party->empty()) return std::nullopt;
			const auto key = ReadQuest(*quest);
			if (!key) return std::nullopt;
			return TrackedQuest{ *key, *party };
		}

		bool SetListed(
			ScopeMap& a_map,
			std::string_view a_profileId,
			const QuestProtocol::FormKey& a_quest,
			bool a_listed,
			std::size_t a_maximumPlayers,
			std::size_t a_maximumQuests)
		{
			if (a_profileId.empty() || a_profileId.size() > kMaximumProfileIdChars) return false;
			const auto profileId = std::string{ a_profileId };
			const auto player = a_map.find(profileId);
			if (a_listed) {
				if (player != a_map.end()) {
					if (Contains(player->second, a_quest)) return false;
					if (player->second.size() >= a_maximumQuests) return false;
					player->second.push_back(a_quest);
					return true;
				}
				if (a_map.size() >= a_maximumPlayers) return false;
				a_map.emplace(profileId, std::vector<QuestProtocol::FormKey>{ a_quest });
				return true;
			}
			if (player == a_map.end()) return false;
			const auto found = std::find(player->second.begin(), player->second.end(), a_quest);
			if (found == player->second.end()) return false;
			player->second.erase(found);
			if (player->second.empty()) a_map.erase(player);
			return true;
		}

		bool IsListed(const ScopeMap& a_map, std::string_view a_profileId, const QuestProtocol::FormKey& a_quest)
		{
			const auto player = a_map.find(std::string{ a_profileId });
			return player != a_map.end() && Contains(player->second, a_quest);
		}

		std::vector<QuestProtocol::FormKey> ListedQuests(const ScopeMap& a_map, std::string_view a_profileId)
		{
			const auto player = a_map.find(std::string{ a_profileId });
			return player == a_map.end() ? std::vector<QuestProtocol::FormKey>{} : player->second;
		}

		Json ScopeJson(const ScopeMap& a_map)
		{
			Json object = Json::object();
			for (const auto& [profileId, quests] : a_map) {
				Json list = Json::array();
				for (const auto& quest : quests) {
					list.push_back(QuestProtocol::FormatFormKey(quest));
				}
				object[profileId] = std::move(list);
			}
			return object;
		}

		void ReadScopeMap(const Json& a_stored, const char* a_name, ScopeMap& a_map, std::size_t a_maximumPlayers, std::size_t a_maximumQuests)
		{
			const auto found = a_stored.find(a_name);
			if (found == a_stored.end()) return;
			for (const auto& [profileId, quests] : found->items()) {
				if (a_map.size() >= a_maximumPlayers) break;
				if (profileId.empty() || profileId.size() > kMaximumProfileIdChars || !quests.is_array()) continue;
				std::vector<QuestProtocol::FormKey> accepted;
				for (const auto& item : quests) {
					if (accepted.size() >= a_maximumQuests) break;
					const auto quest = ReadQuest(item);
					if (!quest || Contains(accepted, *quest)) continue;
					accepted.push_back(*quest);
				}
				if (!accepted.empty()) a_map.emplace(profileId, std::move(accepted));
			}
		}
	}

	Table::Table(std::size_t a_maximumQuests) :
		_maximumQuests(a_maximumQuests)
	{}

	std::optional<Entry> Table::Find(const QuestProtocol::FormKey& a_quest, std::string_view a_party) const
	{
		const auto found = std::find_if(_stages.begin(), _stages.end(), [&](const auto& a_entry) {
			return a_entry.quest == a_quest && a_entry.party == a_party;
		});
		return found == _stages.end() ? std::nullopt : std::optional<Entry>{ *found };
	}

	Entry* Table::FindMutable(const QuestProtocol::FormKey& a_quest, std::string_view a_party)
	{
		const auto found = std::find_if(_stages.begin(), _stages.end(), [&](const auto& a_entry) {
			return a_entry.quest == a_quest && a_entry.party == a_party;
		});
		return found == _stages.end() ? nullptr : std::addressof(*found);
	}

	bool Table::RecordProfileProgress(const ReportRequest& a_request)
	{
		if (a_request.profileId.empty() || a_request.profileId.size() > kMaximumProfileIdChars) return false;
		_profileActivity[a_request.profileId] = ++_profileClock;
		auto profile = _profiles.find(a_request.profileId);
		if (profile == _profiles.end()) {
			if (_profiles.size() >= kMaximumProfileRecords) {
				const auto idlest = std::min_element(_profiles.begin(), _profiles.end(), [this](const auto& a_left, const auto& a_right) {
					return _profileActivity.at(a_left.first) < _profileActivity.at(a_right.first);
				});
				if (idlest == _profiles.end()) return false;
				_profileActivity.erase(idlest->first);
				_profiles.erase(idlest);
			}
			_profiles.emplace(
				a_request.profileId,
				std::vector<ProfileQuest>{ ProfileQuest{ a_request.quest, a_request.stage, a_request.completed } });
			return true;
		}
		for (auto& entry : profile->second) {
			if (!(entry.quest == a_request.quest)) continue;
			bool changed = false;
			if (a_request.stage > entry.stage) {
				entry.stage = a_request.stage;
				changed = true;
			}
			if (a_request.completed && !entry.completed) {
				entry.completed = true;
				changed = true;
			}
			return changed;
		}
		if (profile->second.size() >= kMaximumProfileQuestsPerRecord) return false;
		profile->second.push_back(ProfileQuest{ a_request.quest, a_request.stage, a_request.completed });
		return true;
	}

	std::optional<std::uint16_t> Table::ProfileStage(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest) const
	{
		const auto profile = _profiles.find(std::string{ a_profileId });
		if (profile == _profiles.end()) return std::nullopt;
		for (const auto& entry : profile->second) {
			if (entry.quest == a_quest) return entry.stage;
		}
		return std::nullopt;
	}

	bool Table::ProfileCompleted(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest) const
	{
		const auto profile = _profiles.find(std::string{ a_profileId });
		if (profile == _profiles.end()) return false;
		for (const auto& entry : profile->second) {
			if (entry.quest == a_quest) return entry.completed;
		}
		return false;
	}

	std::vector<ProfileQuest> Table::ProfileQuests(std::string_view a_profileId) const
	{
		const auto profile = _profiles.find(std::string{ a_profileId });
		return profile == _profiles.end() ? std::vector<ProfileQuest>{} : profile->second;
	}

	bool Table::HasCompletion(const QuestProtocol::FormKey& a_quest, std::string_view a_party) const
	{
		return std::any_of(_completed.begin(), _completed.end(), [&](const auto& a_entry) {
			return a_entry.quest == a_quest && a_entry.party == a_party;
		});
	}

	bool Table::RecordCompleted(const QuestProtocol::FormKey& a_quest, std::uint32_t a_fromPlayerId, std::string_view a_party)
	{
		if (HasCompletion(a_quest, a_party)) return false;
		if (_completed.size() >= kMaximumCompletedQuests) return false;
		if (CompletedByProfile(a_fromPlayerId) >= kMaximumCompletionsPerProfile) return false;
		_completed.push_back(Completion{ a_quest, a_fromPlayerId, std::string{ a_party } });
		return true;
	}

	std::size_t Table::TrackedByProfile(std::uint32_t a_fromPlayerId) const
	{
		return static_cast<std::size_t>(std::count_if(_stages.begin(), _stages.end(), [a_fromPlayerId](const auto& a_entry) {
			return a_entry.fromPlayerId == a_fromPlayerId;
		}));
	}

	std::size_t Table::CompletedByProfile(std::uint32_t a_fromPlayerId) const
	{
		return static_cast<std::size_t>(std::count_if(_completed.begin(), _completed.end(), [a_fromPlayerId](const auto& a_entry) {
			return a_entry.fromPlayerId == a_fromPlayerId;
		}));
	}

	bool Table::IsGated(const QuestProtocol::FormKey& a_quest, std::uint16_t a_stage) const
	{
		return std::any_of(_gates.begin(), _gates.end(), [&](const auto& a_gate) {
			return a_gate.quest == a_quest && a_gate.stage == a_stage;
		});
	}

	ReportResult Table::Hold(const ReportRequest& a_request)
	{
		const auto existing = std::find_if(_held.begin(), _held.end(), [&](const auto& a_entry) {
			return a_entry.quest == a_request.quest && a_entry.stage == a_request.stage && a_entry.party == a_request.party;
		});
		if (existing != _held.end()) {
			existing->completed = existing->completed || a_request.completed;
			return ReportResult{ Outcome::Held };
		}
		if (_held.size() >= kMaximumHeldStages) return ReportResult{ Outcome::RejectedCapacity };
		_held.push_back(HeldEntry{ a_request.quest, a_request.stage, a_request.completed, a_request.fromPlayerId, a_request.party });
		return ReportResult{ Outcome::Held };
	}

	ReportResult Table::Report(const ReportRequest& a_request)
	{
		if (a_request.sharing == Sharing::Blocked) return ReportResult{ Outcome::DroppedBlocked };
		if (a_request.sharing == Sharing::Solo) {
			return ReportResult{ Outcome::DroppedSolo, std::nullopt, false, RecordProfileProgress(a_request) };
		}
		if (a_request.sharing == Sharing::Party && a_request.party.empty()) {
			return ReportResult{ Outcome::DroppedSolo, std::nullopt, false, RecordProfileProgress(a_request) };
		}
		ReportRequest request{ a_request };
		if (request.sharing != Sharing::Party) request.party.clear();
		if (IsGated(request.quest, request.stage)) {
			const auto recorded = RecordProfileProgress(request);
			auto held = Hold(request);
			held.profileRecorded = recorded;
			return held;
		}

		auto* tracked = FindMutable(request.quest, request.party);
		if (tracked == nullptr) {
			if (_stages.size() >= _maximumQuests) return ReportResult{ Outcome::RejectedCapacity };
			if (TrackedByProfile(request.fromPlayerId) >= kMaximumQuestsPerProfile) {
				return ReportResult{ Outcome::RejectedCapacity };
			}
			if (request.stage > kMaximumStageAdvance) return ReportResult{ Outcome::RejectedImplausible };
			const auto recorded = RecordProfileProgress(request);
			_stages.push_back(Entry{ request.quest, request.stage, request.fromPlayerId, false, request.party });
			const bool completed = request.completed && RecordCompleted(request.quest, request.fromPlayerId, request.party);
			return ReportResult{
				Outcome::Applied,
				Entry{ request.quest, request.stage, request.fromPlayerId, false, request.party },
				completed,
				recorded
			};
		}
		if (request.stage > tracked->stage) {
			const auto advance = static_cast<std::uint32_t>(request.stage) - tracked->stage;
			if (advance > kMaximumStageAdvance) return ReportResult{ Outcome::RejectedImplausible };
			const auto recorded = RecordProfileProgress(request);
			tracked->stage = request.stage;
			if (request.fromPlayerId != tracked->fromPlayerId) tracked->confirmed = true;
			tracked->fromPlayerId = request.fromPlayerId;

			const bool completed = request.completed && RecordCompleted(request.quest, request.fromPlayerId, request.party);
			return ReportResult{
				Outcome::Applied,
				Entry{ request.quest, request.stage, request.fromPlayerId, true, request.party },
				completed,
				recorded
			};
		}
		if (request.stage == tracked->stage) {
			const auto recorded = RecordProfileProgress(request);
			const bool confirmedByAnotherPlayer = request.fromPlayerId != tracked->fromPlayerId;
			if (confirmedByAnotherPlayer) tracked->confirmed = true;
			const bool completed = request.completed && RecordCompleted(request.quest, request.fromPlayerId, request.party);
			const bool acknowledged = completed || (request.completed && confirmedByAnotherPlayer);
			return ReportResult{
				acknowledged ? Outcome::CompletedOnly : Outcome::IgnoredDuplicate,
				acknowledged ? std::optional<Entry>{ *tracked } : std::nullopt,
				completed,
				recorded
			};
		}
		const auto gap = static_cast<std::uint32_t>(tracked->stage) - request.stage;
		if (!tracked->confirmed && request.fromPlayerId != tracked->fromPlayerId && gap > kMaximumStageCorrectionGap) {
			const auto recorded = RecordProfileProgress(request);
			tracked->stage = request.stage;
			tracked->fromPlayerId = request.fromPlayerId;
			return ReportResult{ Outcome::Corrected, std::optional<Entry>{ *tracked }, false, recorded };
		}
		const auto recorded = RecordProfileProgress(request);
		const bool completed = request.completed && RecordCompleted(request.quest, request.fromPlayerId, request.party);
		return ReportResult{
			completed ? Outcome::CompletedOnly : Outcome::IgnoredStale,
			completed ? std::optional<Entry>{ *tracked } : std::nullopt,
			completed,
			recorded
		};
	}

	Sharing Table::ResolveSharing(
		std::string_view a_profileId,
		const QuestProtocol::FormKey& a_quest,
		QuestProtocol::Mode a_mode,
		const std::vector<QuestProtocol::OverrideEntry>& a_overrides) const
	{
		const auto override_ = std::find_if(a_overrides.begin(), a_overrides.end(), [&a_quest](const auto& a_entry) {
			return a_entry.quest == a_quest;
		});
		if (override_ != a_overrides.end()) {
			switch (override_->policy) {
			case QuestProtocol::Override::Shared:
				return Sharing::Shared;
			case QuestProtocol::Override::Solo:
				return Sharing::Solo;
			case QuestProtocol::Override::Blocked:
				return Sharing::Blocked;
			}
		}
		if (IsSolo(a_profileId, a_quest)) return Sharing::Solo;
		if (IsParty(a_profileId, a_quest)) return Sharing::Party;
		switch (a_mode) {
		case QuestProtocol::Mode::CoopStory:
		case QuestProtocol::Mode::FactionWar:
			return Sharing::Shared;
		case QuestProtocol::Mode::InstancedStory:
		case QuestProtocol::Mode::FreeRoam:
			return Sharing::Solo;
		}
		return Sharing::Shared;
	}

	bool Table::Receives(
		Sharing a_receiverSharing,
		std::string_view a_receiverParty,
		std::string_view a_track,
		std::string_view a_sourceParty) noexcept
	{
		if (!a_track.empty()) return a_receiverSharing == Sharing::Party && a_receiverParty == a_track;
		if (a_receiverSharing != Sharing::Party) return true;
		return !a_receiverParty.empty() && a_receiverParty == a_sourceParty;
	}

	bool Table::AddGate(const QuestProtocol::FormKey& a_quest, std::uint16_t a_stage)
	{
		if (IsGated(a_quest, a_stage)) return false;
		if (_gates.size() >= kMaximumGates) return false;
		_gates.push_back(QuestProtocol::StageRef{ a_quest, a_stage });
		return true;
	}

	std::vector<HeldEntry> Table::ReleaseGate(const QuestProtocol::FormKey& a_quest, std::uint16_t a_stage)
	{
		std::erase_if(_gates, [&](const auto& a_gate) { return a_gate.quest == a_quest && a_gate.stage == a_stage; });
		std::vector<HeldEntry> promoted;
		std::vector<HeldEntry> retained;
		for (const auto& entry : _held) {
			if (entry.quest != a_quest || entry.stage != a_stage) {
				retained.push_back(entry);
				continue;
			}
			if (entry.completed) (void)RecordCompleted(entry.quest, entry.fromPlayerId, entry.party);
			auto* tracked = FindMutable(entry.quest, entry.party);
			if (tracked == nullptr) {
				if (_stages.size() >= _maximumQuests) {
					retained.push_back(entry);
					continue;
				}
				_stages.push_back(Entry{ entry.quest, entry.stage, entry.fromPlayerId, true, entry.party });
				promoted.push_back(entry);
				continue;
			}
			if (entry.stage <= tracked->stage) continue;
			tracked->stage = entry.stage;
			tracked->fromPlayerId = entry.fromPlayerId;
			tracked->confirmed = true;
			promoted.push_back(entry);
		}
		_held = std::move(retained);
		return promoted;
	}

	std::vector<QuestProtocol::StageRef> Table::Gates() const
	{
		return _gates;
	}

	std::vector<HeldEntry> Table::Held() const
	{
		return _held;
	}

	bool Table::SetSolo(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest, bool a_solo)
	{
		return SetListed(_solo, a_profileId, a_quest, a_solo, kMaximumSoloPlayers, kMaximumSoloQuestsPerPlayer);
	}

	bool Table::IsSolo(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest) const
	{
		return IsListed(_solo, a_profileId, a_quest);
	}

	std::vector<QuestProtocol::FormKey> Table::SoloQuests(std::string_view a_profileId) const
	{
		return ListedQuests(_solo, a_profileId);
	}

	bool Table::SetParty(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest, bool a_party)
	{
		return SetListed(_party, a_profileId, a_quest, a_party, kMaximumPartyPlayers, kMaximumPartyQuestsPerPlayer);
	}

	bool Table::IsParty(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest) const
	{
		return IsListed(_party, a_profileId, a_quest);
	}

	std::vector<QuestProtocol::FormKey> Table::PartyQuests(std::string_view a_profileId) const
	{
		return ListedQuests(_party, a_profileId);
	}

	bool Table::SetScope(std::string_view a_profileId, const QuestProtocol::FormKey& a_quest, QuestProtocol::Scope a_scope)
	{
		const auto wantSolo = a_scope == QuestProtocol::Scope::Solo;
		const auto wantParty = a_scope == QuestProtocol::Scope::Party;
		const auto before = *this;
		bool changed = false;
		if (IsSolo(a_profileId, a_quest) != wantSolo) {
			if (!SetSolo(a_profileId, a_quest, wantSolo)) {
				*this = before;
				return false;
			}
			changed = true;
		}
		if (IsParty(a_profileId, a_quest) != wantParty) {
			if (!SetParty(a_profileId, a_quest, wantParty)) {
				*this = before;
				return false;
			}
			changed = true;
		}
		return changed;
	}

	std::vector<Entry> Table::Stages() const
	{
		return _stages;
	}

	std::optional<Entry> Table::FindStage(const QuestProtocol::FormKey& a_quest, std::string_view a_party) const
	{
		return Find(a_quest, a_party);
	}

	bool Table::IsCompletedFor(const QuestProtocol::FormKey& a_quest, std::string_view a_party) const
	{
		return HasCompletion(a_quest, a_party);
	}

	std::vector<QuestProtocol::FormKey> Table::Completed() const
	{
		std::vector<QuestProtocol::FormKey> completed;
		for (const auto& entry : _completed) {
			if (entry.party.empty()) completed.push_back(entry.quest);
		}
		return completed;
	}

	std::vector<TrackedQuest> Table::CompletedTracks() const
	{
		std::vector<TrackedQuest> tracked;
		tracked.reserve(_completed.size());
		for (const auto& entry : _completed) {
			tracked.push_back(TrackedQuest{ entry.quest, entry.party });
		}
		return tracked;
	}

	bool Table::IsCompleted(const QuestProtocol::FormKey& a_quest) const
	{
		return HasCompletion(a_quest, std::string_view{});
	}

	std::size_t Table::TrackedCount() const noexcept
	{
		return _stages.size();
	}

	bool Table::IsEmpty() const noexcept
	{
		return _stages.empty() && _completed.empty() && _held.empty() && _gates.empty() && _solo.empty() && _party.empty() &&
			_profiles.empty();
	}

	std::uint32_t Table::Version() const noexcept
	{
		return _version;
	}

	void Table::BumpVersion() noexcept
	{
		_version = _version == 0xFFFFFFFFu ? 1u : _version + 1u;
	}

	Json Table::ToJson() const
	{
		Json stages = Json::array();
		for (const auto& entry : _stages) {
			Json item{
				{ "quest", QuestProtocol::FormatFormKey(entry.quest) },
				{ "stage", entry.stage },
				{ "fromPlayerId", entry.fromPlayerId },
				{ "confirmed", entry.confirmed }
			};
			if (!entry.party.empty()) item["party"] = entry.party;
			stages.push_back(std::move(item));
		}
		Json completed = Json::array();
		for (const auto& entry : _completed) {
			if (entry.party.empty()) {
				completed.push_back(QuestProtocol::FormatFormKey(entry.quest));
			} else {
				completed.push_back(Json{ { "quest", QuestProtocol::FormatFormKey(entry.quest) }, { "party", entry.party } });
			}
		}
		Json held = Json::array();
		for (const auto& entry : _held) {
			Json item{
				{ "quest", QuestProtocol::FormatFormKey(entry.quest) },
				{ "stage", entry.stage },
				{ "completed", entry.completed },
				{ "fromPlayerId", entry.fromPlayerId }
			};
			if (!entry.party.empty()) item["party"] = entry.party;
			held.push_back(std::move(item));
		}
		Json gates = Json::array();
		for (const auto& gate : _gates) {
			gates.push_back(Json{ { "quest", QuestProtocol::FormatFormKey(gate.quest) }, { "stage", gate.stage } });
		}
		Json profiles = Json::object();
		for (const auto& [profileId, quests] : _profiles) {
			Json list = Json::array();
			for (const auto& entry : quests) {
				list.push_back(Json{
					{ "quest", QuestProtocol::FormatFormKey(entry.quest) },
					{ "stage", entry.stage },
					{ "completed", entry.completed }
				});
			}
			profiles[profileId] = std::move(list);
		}
		Json profileActivity = Json::object();
		for (const auto& [profileId, clock] : _profileActivity) profileActivity[profileId] = clock;
		return Json{
			{ "version", _version },
			{ "stages", std::move(stages) },
			{ "completed", std::move(completed) },
			{ "held", std::move(held) },
			{ "gates", std::move(gates) },
			{ "solo", ScopeJson(_solo) },
			{ "party", ScopeJson(_party) },
			{ "profiles", std::move(profiles) },
			{ "profileActivity", std::move(profileActivity) }
		};
	}

	std::optional<Table> Table::FromJson(const Json& a_stored)
	{
		if (!a_stored.is_object()) return std::nullopt;
		for (const auto name : { "stages", "completed", "held", "gates" }) {
			const auto found = a_stored.find(name);
			if (found != a_stored.end() && !found->is_array()) return std::nullopt;
		}
		for (const auto name : { "solo", "party", "profiles", "profileActivity" }) {
			const auto found = a_stored.find(name);
			if (found != a_stored.end() && !found->is_object()) return std::nullopt;
		}

		Table table;
		if (const auto version = a_stored.find("version"); version != a_stored.end()) {
			if (version->is_number_integer() && !version->is_boolean()) {
				const auto stored = version->get<std::int64_t>();
				if (stored > 0 && stored <= static_cast<std::int64_t>(0xFFFFFFFFu)) table._version = static_cast<std::uint32_t>(stored);
			}
		}
		if (const auto stages = a_stored.find("stages"); stages != a_stored.end()) {
			for (const auto& item : *stages) {
				if (table._stages.size() >= table._maximumQuests) break;
				const auto entry = ReadStageRef(item);
				const auto party = ReadParty(item);
				if (!entry || !party || table.Find(entry->quest, *party)) continue;
				const auto fromPlayerId = ReadUnsigned(item, "fromPlayerId", 0xFFFFFFFFu);
				bool confirmed = false;
				if (const auto flag = item.find("confirmed"); flag != item.end() && flag->is_boolean()) confirmed = flag->get<bool>();
				table._stages.push_back(Entry{ entry->quest, entry->stage, fromPlayerId, confirmed, *party });
			}
		}
		if (const auto completed = a_stored.find("completed"); completed != a_stored.end()) {
			for (const auto& item : *completed) {
				if (table._completed.size() >= kMaximumCompletedQuests) break;
				const auto tracked = ReadCompletion(item);
				if (!tracked || table.HasCompletion(tracked->quest, tracked->party)) continue;
				table._completed.push_back(Completion{ tracked->quest, 0, tracked->party });
			}
		}
		if (const auto held = a_stored.find("held"); held != a_stored.end()) {
			for (const auto& item : *held) {
				if (table._held.size() >= kMaximumHeldStages) break;
				const auto entry = ReadStageRef(item);
				const auto party = ReadParty(item);
				if (!entry || !party) continue;
				const auto duplicate = std::find_if(table._held.begin(), table._held.end(), [&entry, &party](const auto& a_held) {
					return a_held.quest == entry->quest && a_held.stage == entry->stage && a_held.party == *party;
				});
				if (duplicate != table._held.end()) continue;
				bool completed = false;
				if (const auto flag = item.find("completed"); flag != item.end() && flag->is_boolean()) completed = flag->get<bool>();
				const auto fromPlayerId = ReadUnsigned(item, "fromPlayerId", 0xFFFFFFFFu);
				table._held.push_back(HeldEntry{ entry->quest, entry->stage, completed, fromPlayerId, *party });
			}
		}
		if (const auto gates = a_stored.find("gates"); gates != a_stored.end()) {
			for (const auto& item : *gates) {
				if (table._gates.size() >= kMaximumGates) break;
				const auto gate = ReadStageRef(item);
				if (!gate || table.IsGated(gate->quest, gate->stage)) continue;
				table._gates.push_back(*gate);
			}
		}
		ReadScopeMap(a_stored, "solo", table._solo, kMaximumSoloPlayers, kMaximumSoloQuestsPerPlayer);
		ReadScopeMap(a_stored, "party", table._party, kMaximumPartyPlayers, kMaximumPartyQuestsPerPlayer);
		if (const auto profiles = a_stored.find("profiles"); profiles != a_stored.end()) {
			for (const auto& [profileId, quests] : profiles->items()) {
				if (table._profiles.size() >= kMaximumProfileRecords) break;
				if (profileId.empty() || profileId.size() > kMaximumProfileIdChars || !quests.is_array()) continue;
				std::vector<ProfileQuest> accepted;
				for (const auto& item : quests) {
					if (accepted.size() >= kMaximumProfileQuestsPerRecord) break;
					const auto entry = ReadProfileQuest(item);
					if (!entry) continue;
					const auto duplicate = std::find_if(accepted.begin(), accepted.end(), [&entry](const auto& a_quest) {
						return a_quest.quest == entry->quest;
					});
					if (duplicate != accepted.end()) continue;
					accepted.push_back(*entry);
				}
				if (!accepted.empty()) table._profiles.emplace(profileId, std::move(accepted));
			}
		}
		if (const auto clocks = a_stored.find("profileActivity"); clocks != a_stored.end()) {
			for (const auto& [profileId, clock] : clocks->items()) {
				if (!clock.is_number_unsigned() || !table._profiles.contains(profileId)) continue;
				const auto stored = clock.get<std::uint64_t>();
				table._profileActivity[profileId] = stored;
				table._profileClock = (std::max)(table._profileClock, stored);
			}
		}
		for (const auto& entry : table._profiles) (void)table._profileActivity.try_emplace(entry.first, ++table._profileClock);
		return table;
	}
}
