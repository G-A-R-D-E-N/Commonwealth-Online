#include "DurableStateFileStore.h"
#include "DurableStateSections.h"
#include "F4TCharacterSlots.h"
#include "F4TDurableState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using namespace DurableStateSections;

		bool DropFirstJoinMark(nlohmann::json& a_root, std::string_view a_account, std::uint32_t a_slot)
		{
			const auto table = a_root.find("characterSlots");
			if (table == a_root.end() || !table->is_object()) return false;
			const auto account = table->find(std::string{ a_account });
			if (account == table->end() || !account->is_object()) return false;
			const auto marks = account->find("_firstJoinAbandoned");
			if (marks == account->end() || !marks->is_array()) return false;
			const auto before = marks->size();
			marks->erase(std::remove_if(marks->begin(), marks->end(), [&](const nlohmann::json& a_mark) {
				const auto slot = a_mark.find("slot");
				return slot != a_mark.end() && slot->is_number_unsigned() && slot->get<std::uint32_t>() == a_slot;
			}), marks->end());
			return marks->size() != before;
		}

		bool ProfileHasState(const nlohmann::json& a_root, std::string_view a_profileId)
		{
			return F4T::DurableState::GetPlayerState(a_root, a_profileId).has_value() ||
				F4T::DurableState::GetInventory(a_root, a_profileId).has_value() ||
				F4T::DurableState::GetStartingGrant(a_root, a_profileId).has_value();
		}
	}

	std::map<std::uint32_t, nlohmann::json> DurableStateFileStore::GetCharacterSlotRecords(std::string_view a_account) const
	{
		const std::scoped_lock lock(_gate);
		return F4T::CharacterSlots::GetSlotRecords(_root, a_account);
	}

	bool DurableStateFileStore::HasProfileState(std::string_view a_profileId) const
	{
		const std::scoped_lock lock(_gate);
		return ProfileHasState(_root, a_profileId);
	}

	bool DurableStateFileStore::SetCharacterSlotRecord(
		std::string_view a_account,
		std::uint32_t a_slot,
		const nlohmann::json& a_record)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kCharacterSlotSections, [&](nlohmann::json& a_root) {
			if (!F4T::CharacterSlots::SetSlotRecord(a_root, a_account, a_slot, a_record)) return false;
			DropFirstJoinMark(a_root, a_account, a_slot);
			return PreserveLegacyQuestIdentity(a_root, a_account, a_slot, a_record);
		}, true);
	}

	bool DurableStateFileStore::CreateCharacterSlot(
		std::string_view a_account,
		std::uint32_t a_slot,
		const nlohmann::json& a_record,
		const nlohmann::json& a_playerState)
	{
		const auto profileKey = F4T::CharacterSlots::SlotProfileKey(a_account, a_slot);
		if (profileKey.empty()) return false;
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ profileKey }, kCharacterSlotSections, [&](nlohmann::json& a_root) {
			if (F4T::CharacterSlots::GetSlotRecords(a_root, a_account).contains(a_slot) || ProfileHasState(a_root, profileKey)) return false;
			if (!F4T::CharacterSlots::SetSlotRecord(a_root, a_account, a_slot, a_record)) return false;
			DropFirstJoinMark(a_root, a_account, a_slot);
			return PreserveLegacyQuestIdentity(a_root, a_account, a_slot, a_record) &&
				F4T::DurableState::UpdatePlayerState(a_root, profileKey, a_playerState, _clock()) == F4T::DurableState::PlayerStateMutationOutcome::Changed;
		}, true);
	}

	bool DurableStateFileStore::PreserveLegacyQuestIdentity(nlohmann::json& a_root, std::string_view a_account, std::uint32_t a_slot, const nlohmann::json& a_record)
	{
		if (!a_record.contains("questProfile")) return true;
		PreserveLegacyAccountQuests(a_root, a_account);
		if (a_slot != 0) return true;
		auto& account = a_root["characterSlots"][std::string{ a_account }];
		const auto pending = account.find("_legacyQuest0");
		if (pending == account.end()) return true;
		if (!pending->is_string() || !a_record["questProfile"].is_string()) return false;
		if (*pending != a_record["questProfile"]) {
			auto& archive = account["_legacyQuestArchive"];
			if (archive.is_null()) archive = nlohmann::json::array();
			if (!archive.is_array()) return false;
			if (std::find(archive.begin(), archive.end(), *pending) == archive.end()) archive.push_back(*pending);
		}
		account.erase(pending);
		return true;
	}

	void DurableStateFileStore::PreserveLegacyAccountQuests(nlohmann::json& a_root, std::string_view a_account)
	{
		auto& account = a_root["characterSlots"][std::string{ a_account }];
		if (account.contains("_legacyAccountQuestBackup")) return;
		const auto quests = a_root.find("quests");
		if (quests == a_root.end() || !quests->is_object()) return;
		nlohmann::json backup = nlohmann::json::object();
		for (const char* field : { "profiles", "profileActivity", "tutorialExit" }) {
			const auto table = quests->find(field);
			if (table == quests->end() || !table->is_object()) continue;
			const auto old = table->find(std::string{ a_account });
			if (old != table->end()) backup[field] = *old;
		}
		if (!backup.empty()) account["_legacyAccountQuestBackup"] = std::move(backup);
	}

	bool DurableStateFileStore::EraseCharacterSlotRecord(std::string_view a_account, std::uint32_t a_slot, bool a_keepConfirmation)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kEraseCharacterSlotSections, [&](nlohmann::json& a_root) {
			const auto records = F4T::CharacterSlots::GetSlotRecords(a_root, a_account);
			if (const auto found = records.find(a_slot); found != records.end() && !QueuePendingQuestErase(a_root, found->second, nlohmann::json::object())) return false;
			const bool erased = F4T::CharacterSlots::EraseSlotRecord(a_root, a_account, a_slot);
			const bool dropped = !a_keepConfirmation && DropFirstJoinMark(a_root, a_account, a_slot);
			return erased || dropped;
		}, true);
	}

	bool DurableStateFileStore::EraseCharacterSlotState(std::string_view a_account, std::uint32_t a_slot, bool a_keepConfirmation, bool a_dropParent)
	{
		const auto profileKey = F4T::CharacterSlots::SlotProfileKey(a_account, a_slot);
		if (profileKey.empty()) return false;
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kEraseCharacterSlotStateSections, [&](nlohmann::json& a_root) {
			const auto records = F4T::CharacterSlots::GetSlotRecords(a_root, a_account);
			if (const auto found = records.find(a_slot); found != records.end() && !QueuePendingQuestErase(a_root, found->second, nlohmann::json::object())) return false;
			const bool erased = F4T::CharacterSlots::EraseSlotRecord(a_root, a_account, a_slot);
			const bool dropped = !a_keepConfirmation && DropFirstJoinMark(a_root, a_account, a_slot);
			const bool profileErased = F4T::CharacterSlots::EraseProfileNode(a_root, profileKey);
			const bool scrubbed = ScrubKnownPeopleReferences(a_root, profileKey, a_dropParent);
			return erased || dropped || profileErased || scrubbed;
		}, true);
	}

	std::vector<FirstJoinAbandonMark> DurableStateFileStore::GetFirstJoinAbandonedSlots(std::string_view a_account) const
	{
		const std::scoped_lock lock(_gate);
		std::vector<FirstJoinAbandonMark> marks;
		const auto table = _root.find("characterSlots");
		if (table == _root.end() || !table->is_object()) return marks;
		const auto account = table->find(std::string{ a_account });
		if (account == table->end() || !account->is_object()) return marks;
		const auto found = account->find("_firstJoinAbandoned");
		if (found == account->end() || !found->is_array()) return marks;
		for (const auto& mark : *found) {
			const auto slot = mark.find("slot");
			const auto at = mark.find("at");
			if (slot == mark.end() || at == mark.end() || !slot->is_number_unsigned() || !at->is_number()) continue;
			marks.push_back(FirstJoinAbandonMark{ slot->get<std::uint32_t>(), at->get<double>() });
		}
		return marks;
	}

	bool DurableStateFileStore::SetFirstJoinAbandonedSlot(std::string_view a_account, std::uint32_t a_slot, double a_unixTimeSeconds)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kCharacterSlotSections, [&](nlohmann::json& a_root) {
			DropFirstJoinMark(a_root, a_account, a_slot);
			auto& marks = a_root["characterSlots"][std::string{ a_account }]["_firstJoinAbandoned"];
			if (!marks.is_array()) marks = nlohmann::json::array();
			marks.push_back(nlohmann::json{ { "slot", a_slot }, { "at", a_unixTimeSeconds } });
			return true;
		}, true);
	}

	bool DurableStateFileStore::DropFirstJoinAbandonedSlot(std::string_view a_account, std::uint32_t a_slot)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kCharacterSlotSections, [&](nlohmann::json& a_root) {
			return DropFirstJoinMark(a_root, a_account, a_slot);
		}, true);
	}

	bool DurableStateFileStore::ClearFirstJoinAbandonedSlots(std::string_view a_account)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kCharacterSlotSections, [&](nlohmann::json& a_root) {
			const auto table = a_root.find("characterSlots");
			if (table == a_root.end() || !table->is_object()) return false;
			const auto account = table->find(std::string{ a_account });
			if (account == table->end() || !account->is_object()) return false;
			return account->erase("_firstJoinAbandoned") > 0;
		}, true);
	}

	bool DurableStateFileStore::QuestProfileOwnedByAnotherAccount(std::string_view a_account, std::string_view a_profile) const
	{
		const std::scoped_lock lock(_gate);
		const auto table = _root.find("characterSlots");
		if (table == _root.end() || !table->is_object()) return false;
		const nlohmann::json profile = std::string{ a_profile };
		for (const auto& account : table->items()) {
			if (account.key() == a_account || !account.value().is_object()) continue;
			for (const auto& [slot, record] : F4T::CharacterSlots::GetSlotRecords(_root, account.key())) {
				(void)slot;
				if (record.value("questProfile", nlohmann::json{}) == profile) return true;
			}
			if (account.value().value("_legacyQuest0", nlohmann::json{}) == profile) return true;
			const auto archive = account.value().find("_legacyQuestArchive");
			if (archive != account.value().end() && archive->is_array() && std::find(archive->begin(), archive->end(), profile) != archive->end()) return true;
		}
		return false;
	}

	std::optional<std::string> DurableStateFileStore::EnsureLegacyQuestProfile(std::string_view a_account, std::uint32_t a_slot, std::string_view a_questProfile, bool a_allowEmpty)
	{
		const std::scoped_lock lock(_gate);
		const auto records = F4T::CharacterSlots::GetSlotRecords(_root, a_account);
		const auto found = records.find(a_slot);
		if (found != records.end() && found->second.contains("questProfile")) return std::nullopt;
		const bool hasState = F4T::DurableState::GetPlayerState(_root, F4T::CharacterSlots::SlotProfileKey(a_account, a_slot)).has_value();
		if (found == records.end() && !hasState && !a_allowEmpty) return std::nullopt;
		std::string chosen{ a_questProfile };
		bool hasPending = false;
		if (a_slot == 0) {
			const auto table = _root.find("characterSlots");
			if (table != _root.end() && table->is_object()) {
				const auto account = table->find(std::string{ a_account });
				if (account != table->end() && account->is_object()) {
					const auto pending = account->find("_legacyQuest0");
					if (pending != account->end()) {
						if (!pending->is_string()) return std::nullopt;
						chosen = pending->get<std::string>();
						hasPending = true;
					}
				}
			}
		}
		if (found == records.end() && !hasState) {
			if (hasPending) return chosen;
			const bool committed = MutateAndPersistLocked(kCharacterSlotSections, [&](nlohmann::json& a_root) {
				a_root["characterSlots"][std::string{ a_account }]["_legacyQuest0"] = chosen;
				PreserveLegacyAccountQuests(a_root, a_account);
				return true;
			}, true, true);
			return committed ? std::optional{ chosen } : std::nullopt;
		}
		auto record = found == records.end() ? nlohmann::json::object() : found->second;
		record["questProfile"] = chosen;
		const bool committed = MutateAndPersistLocked(kCharacterSlotSections, [&](nlohmann::json& a_root) {
			if (!F4T::CharacterSlots::SetSlotRecord(a_root, a_account, a_slot, record)) return false;
			DropFirstJoinMark(a_root, a_account, a_slot);
			return PreserveLegacyQuestIdentity(a_root, a_account, a_slot, record);
		}, true, true);
		return committed ? std::optional{ chosen } : std::nullopt;
	}

	bool DurableStateFileStore::MigrateCharacterSlotsToSlotZero(std::string& a_conflictKey)
	{
		a_conflictKey.clear();
		const std::scoped_lock lock(_gate);
		try {
			// Constraint: migration runs inside the mutation so a failed persist rolls back.
			bool conflict = false;
			bool dirty = false;
			const auto committed = MutateAndPersistLocked(kCharacterSlotMigrationSections, [&](nlohmann::json& a_root) {
				const auto migrated = F4T::CharacterSlots::MigrateLegacyProfiles(a_root, _clock());
				if (migrated.conflict) {
					a_conflictKey = migrated.conflictKey;
					conflict = true;
					return false;
				}
				dirty = migrated.moved || migrated.alreadySlotted;
				return dirty;
			}, true);
			if (conflict) {
				return false;
			}
			return !dirty || committed;
		} catch (const std::exception&) {
			return false;
		}
	}

	bool DurableStateFileStore::ScopeLegacySaveAttestations(
		std::string_view a_account,
		const std::vector<std::pair<std::uint32_t, std::string>>& a_slotCharacters)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_account } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::ScopeLegacySaveAttestations(a_root, a_account, a_slotCharacters, _clock());
		}, true, true);
	}

	bool DurableStateFileStore::EraseSlotSaveAttestations(std::string_view a_account, std::uint32_t a_slot)
	{
		const std::scoped_lock lock(_gate);
		return MutateProfilesAndPersistLocked({ std::string{ a_account } }, {}, [&](nlohmann::json& a_root) {
			return F4T::DurableState::EraseSlotSaveAttestations(a_root, a_account, a_slot, _clock());
		}, true, true);
	}
}
