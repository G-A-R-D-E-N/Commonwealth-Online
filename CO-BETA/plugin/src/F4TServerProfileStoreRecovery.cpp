#include "F4TServerProfileStore.h"
#include "F4TServerProfileStoreInternal.h"

#include "F4TCharacterSlots.h"
#include "F4TSaveProfileLogic.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>

namespace
{
	std::optional<F4T::ServerProfileStore::FirstJoinRecovery> ParseFirstJoinRecovery(const nlohmann::json& a_json)
	{
		const auto text = [&a_json](const char* a_field) -> std::optional<std::string> {
			const auto value = a_json.find(a_field);
			if (value == a_json.end() || !value->is_string()) return std::nullopt;
			return value->get<std::string>();
		};
		const auto worldId = text("worldId");
		const auto profileId = text("profileId");
		const auto characterId = text("characterId");
		const auto saveFile = text("saveFile");
		const auto sha256 = text("sha256");
		const auto generation = a_json.find("generation");
		if (!a_json.is_object() || !worldId || !profileId || !characterId || !saveFile || !sha256 ||
			generation == a_json.end() || !generation->is_number_unsigned() || generation->get<std::uint64_t>() == 0) {
			return std::nullopt;
		}
		return F4T::ServerProfileStore::FirstJoinRecovery{ *worldId, *profileId, *characterId, *saveFile, *sha256, generation->get<std::uint64_t>() };
	}
}

namespace F4T::ServerProfileStore::Internal
{
	nlohmann::json FirstJoinRecoveryToJson(const FirstJoinRecovery& a_recovery)
	{
		return nlohmann::json{ { "worldId", a_recovery.worldId }, { "profileId", a_recovery.profileId },
			{ "characterId", a_recovery.characterId }, { "saveFile", a_recovery.saveFile }, { "sha256", a_recovery.sha256 },
			{ "generation", a_recovery.generation } };
	}

	void AppendPreviousCharacterSave(nlohmann::json& a_entry, const std::string& a_worldId, std::uint32_t a_slot, const nlohmann::json& a_binding)
	{
		constexpr std::size_t kPreviousCharacterSaveLimit = 5;
		auto& archive = a_entry["previousCharacterSaves"];
		archive.push_back(nlohmann::json{ { "worldId", a_worldId }, { "slot", a_slot }, { "binding", a_binding } });
		const auto tagged = [](const nlohmann::json& a_item) {
			return a_item.is_object() && a_item.contains("worldId") && a_item["worldId"].is_string() &&
				a_item.contains("slot") && a_item["slot"].is_number_unsigned();
		};
		const auto sameContext = [&](const nlohmann::json& a_item) {
			return tagged(a_item) && a_item["worldId"] == a_worldId && a_item["slot"] == a_slot;
		};
		const auto untagged = [&](const nlohmann::json& a_item) { return !tagged(a_item); };
		const auto trim = [&](const auto& a_matches) {
			std::size_t count = 0;
			for (const auto& item : archive) count += a_matches(item) ? 1 : 0;
			for (auto it = archive.begin(); count > kPreviousCharacterSaveLimit && it != archive.end();) {
				if (a_matches(*it)) {
					it = archive.erase(it);
					--count;
				} else {
					++it;
				}
			}
		};
		trim(sameContext);
		trim(untagged);
	}

	bool ProfileIdMatchesAccount(const std::string& a_account, const std::string& a_profileId)
	{
		if (!F4T::CharacterSlots::AccountKey(a_account)) return false;
		if (a_account == a_profileId) return true;
		const auto slot = F4T::CharacterSlots::ParseSlotKey(a_profileId);
		return slot && slot->first == a_account && slot->second < F4T::CharacterSlots::MaximumCharacterSlots;
	}
}

namespace F4T::ServerProfileStore
{
	using namespace Internal;

	std::optional<FirstJoinRecovery> LookupFirstJoinRecovery(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) return std::nullopt;
		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object()) return std::nullopt;
		const auto recovery = entry->find("firstJoinRecovery");
		const auto account = entry->find("profileId");
		if (recovery == entry->end() || account == entry->end() || !account->is_string()) return std::nullopt;
		const auto staged = ParseFirstJoinRecovery(*recovery);
		if (!staged || staged->worldId != g_connectedWorldId ||
			!ProfileIdMatchesAccount(account->get_ref<const std::string&>(), staged->profileId)) {
			return std::nullopt;
		}
		return staged;
	}

	bool ApplyFirstJoinRecovery(const std::string& a_serverKey, const FirstJoinRecovery& a_recovery)
	{
		if (a_serverKey.empty() || a_recovery.saveFile.empty()) return false;
		const std::scoped_lock lock(g_storeMutex);
		StoreTransaction txn;
		if (!txn) return false;
		if (a_recovery.worldId.empty() || a_recovery.worldId != g_connectedWorldId) return false;
		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) return false;
		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object() || !entry->contains("firstJoinHold") ||
			!entry->contains("firstJoinRecovery") || entry->at("firstJoinRecovery") != FirstJoinRecoveryToJson(a_recovery)) {
			return false;
		}
		const auto previousProfiles = g_profiles;
		if (!WriteSlotAwareBinding(*entry, a_recovery.saveFile)) {
			g_profiles = previousProfiles;
			return false;
		}
		entry->erase("firstJoinHold");
		entry->erase("firstJoinRecovery");
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		RefreshBackupFromPrimary();
		return true;
	}

	void RepairFirstJoinBackup(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		const auto path = GetProfilesPath();
		if (path.empty()) return;
		std::optional<ProfileFileLock> repairLock;
		if (!AcquireSaveLock(repairLock, path)) return;
		const auto backupPath = GetBackupPath(path);
		std::error_code ec;
		if (!std::filesystem::exists(backupPath, ec) || ec) return;
		const auto holds = [&](const nlohmann::json& a_profiles) {
			const auto servers = a_profiles.find("servers");
			if (servers == a_profiles.end() || !servers->is_object()) return true;
			const auto entry = servers->find(a_serverKey);
			return entry != servers->end() && (!entry->is_object() || entry->contains("firstJoinHold"));
		};
		nlohmann::json primary;
		if (!TryLoadProfilesFile(path, primary) || holds(primary)) return;
		nlohmann::json backup;
		if (TryLoadProfilesFile(backupPath, backup) && !holds(backup)) return;
		RefreshBackupFromPrimary();
	}

	bool FirstJoinRecoveryApplies(const FirstJoinRecovery& a_staged, const std::string& a_scopeWorldId,
		const nlohmann::json& a_approval, const std::optional<std::string>& a_localSha256)
	{
		const nlohmann::json expected{ { "profileId", a_staged.profileId }, { "characterId", a_staged.characterId },
			{ "saveFile", a_staged.saveFile }, { "sha256", a_staged.sha256 }, { "generation", a_staged.generation } };
		return !a_staged.worldId.empty() && a_staged.worldId == a_scopeWorldId && a_approval == expected &&
			a_localSha256 && SaveProfileLogic::EqualsIgnoreCase(*a_localSha256, a_staged.sha256);
	}
}
