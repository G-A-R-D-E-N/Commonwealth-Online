#include "DurableStateFileStore.h"
#include "F4TCharacterSlots.h"

#include <mutex>
#include <string>

namespace CommonwealthOnline::Server
{
	nlohmann::json DurableStateFileStore::GetKnownPeople() const
	{
		const std::scoped_lock lock(_gate);
		return _root.value("knownPeople", nlohmann::json::object());
	}

	void DurableStateFileStore::SaveKnownPeople(const nlohmann::json& a_knownPeople)
	{
		const std::scoped_lock lock(_gate);
		(void)MutateAndPersistLocked([&](nlohmann::json& a_root) {
			a_root["knownPeople"] = a_knownPeople;
			return true;
		}, true);
	}

	bool DurableStateFileStore::EraseProfileByKey(std::string_view a_profileKey, bool a_dropParent)
	{
		const std::scoped_lock lock(_gate);
		return MutateAndPersistLocked([&](nlohmann::json& a_root) {
			const bool erased = F4T::CharacterSlots::EraseProfileNode(a_root, a_profileKey);
			return ScrubKnownPeopleReferences(a_root, a_profileKey, a_dropParent) || erased;
		}, true);
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
}
