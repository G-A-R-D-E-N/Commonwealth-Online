#include "F4TServerProfileStore.h"
#include "F4TServerProfileStoreInternal.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace F4T::ServerProfileStore
{
	using namespace Internal;

	namespace
	{
		nlohmann::json* SaveLessHoldEntry(nlohmann::json& a_profiles, const std::string& a_serverKey)
		{
			const auto servers = a_profiles.find("servers");
			if (servers == a_profiles.end() || !servers->is_object()) return nullptr;
			const auto entry = servers->find(a_serverKey);
			if (entry == servers->end() || !entry->is_object()) return nullptr;
			const auto hold = entry->find("firstJoinHold");
			if (hold == entry->end() || !hold->is_object()) return nullptr;
			const auto saveName = hold->find("saveName");
			if (saveName == hold->end() || !saveName->is_string() || !saveName->get_ref<const std::string&>().empty()) return nullptr;
			return &*entry;
		}
	}

	bool FirstJoinHoldIsSaveLess(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		return SaveLessHoldEntry(g_profiles, a_serverKey) != nullptr;
	}

	std::optional<std::uint32_t> ConnectedCharacterSlot()
	{
		const std::scoped_lock lock(g_storeMutex);
		return g_connectedCharacterSlot;
	}

	std::optional<std::uint32_t> FirstJoinHoldSlot(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto entry = SaveLessHoldEntry(g_profiles, a_serverKey);
		if (!entry) return std::nullopt;
		const auto& hold = (*entry)["firstJoinHold"];
		const auto slot = hold.find("slot");
		if (slot == hold.end() || !slot->is_number_unsigned() || slot->get<std::uint64_t>() > 255) return std::nullopt;
		return static_cast<std::uint32_t>(slot->get<std::uint64_t>());
	}

	bool ClearSaveLessFirstJoinHold(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		StoreTransaction txn;
		if (!txn) return false;
		const auto entry = SaveLessHoldEntry(g_profiles, a_serverKey);
		if (!entry) return false;
		const auto previousProfiles = g_profiles;
		entry->erase("firstJoinHold");
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		RefreshBackupFromPrimary();
		return true;
	}
}
