#include "DurableStateFileStore.h"
#include "DurableStateSections.h"
#include "F4TCharacterSlots.h"
#include "F4TDurableState.h"

#include <algorithm>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr std::string_view kKnownPeopleSections[] = { "knownPeople" };
		constexpr std::string_view kEraseProfileSections[] = { "profiles", "knownPeople" };
		constexpr std::string_view kPendingQuestEraseKey = "pendingQuestErase";
		constexpr std::string_view kPendingQuestEraseSections[] = { kPendingQuestEraseKey };
		constexpr std::size_t kMaxPendingQuestErase = 4096;
		constexpr std::size_t kPendingQuestEraseWarnAt = kMaxPendingQuestErase / 4 * 3;
	}

	nlohmann::json DurableStateFileStore::GetKnownPeople() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("knownPeople", nlohmann::json::object());
	}

	bool DurableStateFileStore::SaveKnownPeople(const nlohmann::json& a_knownPeople)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kKnownPeopleSections, [&](nlohmann::json& a_root) {
			a_root["knownPeople"] = a_knownPeople;
			return true;
		}, true);
	}

	bool DurableStateFileStore::EraseProfileByKey(std::string_view a_profileKey, bool a_dropParent)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kEraseProfileSections, [&](nlohmann::json& a_root) {
			const bool erased = F4T::CharacterSlots::EraseProfileNode(a_root, a_profileKey);
			return ScrubKnownPeopleReferences(a_root, a_profileKey, a_dropParent) || erased;
		}, true);
	}

	std::vector<std::string> DurableStateFileStore::PendingQuestErase() const
	{
		const std::scoped_lock lock(_gate);
		std::vector<std::string> ids;
		const auto pending = _root.find(std::string{ kPendingQuestEraseKey });
		if (pending == _root.end() || !pending->is_array()) return ids;
		for (const auto& entry : *pending) {
			if (ids.size() >= kMaxPendingQuestErase) break;
			if (entry.is_string()) ids.push_back(entry.get<std::string>());
		}
		return ids;
	}

	bool DurableStateFileStore::ClearPendingQuestErase(const std::vector<std::string>& a_questProfiles)
	{
		if (a_questProfiles.empty()) return false;
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked(kPendingQuestEraseSections, [&](nlohmann::json& a_root) {
			const auto pending = a_root.find(std::string{ kPendingQuestEraseKey });
			if (pending == a_root.end() || !pending->is_array()) return false;
			const auto before = pending->size();
			for (auto entry = pending->begin(); entry != pending->end();) {
				if (entry->is_string() && std::find(a_questProfiles.begin(), a_questProfiles.end(), entry->get_ref<const std::string&>()) != a_questProfiles.end()) {
					entry = pending->erase(entry);
				} else {
					++entry;
				}
			}
			return pending->size() != before;
		}, true);
	}

	bool DurableStateFileStore::EraseQuestProfilesFromStore(const std::vector<std::string>& a_questProfiles)
	{
		if (a_questProfiles.empty()) return true;
		std::uint64_t ticket = 0;
		try {
			const std::scoped_lock lock(_gate);
			(void)MutateAndPersistLocked(DurableStateSections::kQuestSections, [&](nlohmann::json& a_root) {
				const auto quests = a_root.find("quests");
				if (quests == a_root.end() || !quests->is_object()) return false;
				bool erased = false;
				for (const char* table : { "profiles", "profileActivity", "solo", "party", "fireteam", "tutorialExit" }) {
					const auto found = quests->find(table);
					if (found == quests->end() || !found->is_object()) continue;
					for (const auto& id : a_questProfiles) erased |= found->erase(id) > 0;
				}
				return erased;
			}, true, true);
			ticket = _pipeline->LastPostedTicket();
		} catch (const std::exception& a_error) {
			std::cerr << "Stored quest cleanup failed; markers kept: " << a_error.what() << '\n';
			return false;
		}
		try {
			WaitForCommits();
			return TicketDurable(ticket);
		} catch (const std::exception& a_error) {
			std::cerr << "Stored quest cleanup not durable; markers kept: " << a_error.what() << '\n';
			return false;
		}
	}

	bool QueuePendingQuestErase(nlohmann::json& a_root, const nlohmann::json& a_erasedRecord, const nlohmann::json& a_keptRecord)
	{
		const auto erased = a_erasedRecord.find("questProfile");
		if (erased == a_erasedRecord.end() || !erased->is_string() || erased->get_ref<const std::string&>().empty()) return true;
		const auto kept = a_keptRecord.find("questProfile");
		if (kept != a_keptRecord.end() && *kept == *erased) return true;
		auto& pending = a_root[std::string{ kPendingQuestEraseKey }];
		if (!pending.is_array()) pending = nlohmann::json::array();
		if (std::find(pending.begin(), pending.end(), *erased) != pending.end()) return true;
		if (pending.size() >= kMaxPendingQuestErase) {
			std::cerr << "Quest cleanup queue is full (" << kMaxPendingQuestErase << " pending); character deletion is refused until it drains.\n";
			return false;
		}
		pending.push_back(*erased);
		if (pending.size() == kPendingQuestEraseWarnAt) {
			std::cerr << "Quest cleanup queue holds " << pending.size() << " of " << kMaxPendingQuestErase << " pending deletions.\n";
		}
		return true;
	}

	bool ScrubKnownPeopleReferences(nlohmann::json& a_root, std::string_view a_profileKey, bool a_dropParent)
	{
		std::string parent{ a_profileKey };
		if (const auto slotKey = F4T::CharacterSlots::ParseSlotKey(a_profileKey)) parent = slotKey->first;
		const bool hasParent = a_dropParent && parent != a_profileKey;
		bool removed = false;
		if (const auto outer = a_root.find("knownPeople"); outer != a_root.end() && outer->is_object()) {
			if (const auto people = outer->find("knownPeople"); people != outer->end() && people->is_object()) {
				removed |= people->erase(std::string{ a_profileKey }) > 0;
				if (hasParent) removed |= people->erase(parent) > 0;
				for (auto entry = people->begin(); entry != people->end();) {
					if (entry->is_array()) {
						const std::size_t before = entry->size();
						for (auto item = entry->begin(); item != entry->end();) {
							if (item->is_string() && (item->get_ref<const std::string&>() == a_profileKey || (hasParent && item->get_ref<const std::string&>() == parent))) item = entry->erase(item);
							else ++item;
						}
						removed |= entry->size() != before;
					}
					if (entry->is_array() && entry->empty()) entry = people->erase(entry);
					else ++entry;
				}
			}
		}
		if (const auto outer = a_root.find("knownPeople"); outer != a_root.end() && outer->is_object()) {
			if (const auto stored = outer->find("friends"); stored != outer->end() && stored->is_object()) {
				removed |= stored->erase(std::string{ a_profileKey }) > 0;
				if (hasParent) removed |= stored->erase(parent) > 0;
				for (auto entry = stored->begin(); entry != stored->end();) {
					if (entry->is_array()) {
						const std::size_t before = entry->size();
						for (auto item = entry->begin(); item != entry->end();) {
							const auto id = item->is_object() ? item->find("profileId") : item->end();
							if (id != item->end() && id->is_string() &&
								(id->get_ref<const std::string&>() == a_profileKey || (hasParent && id->get_ref<const std::string&>() == parent))) {
								item = entry->erase(item);
							} else {
								++item;
							}
						}
						removed |= entry->size() != before;
					}
					if (entry->is_array() && entry->empty()) entry = stored->erase(entry);
					else ++entry;
				}
			}
		}
		return removed;
	}

	inline constexpr const char* kKnownPeopleIndexTag = "Ms9Hd1Zu7Kf2Wq5Xc0Tb8Ne";
	static_assert(kKnownPeopleIndexTag != nullptr);
}
