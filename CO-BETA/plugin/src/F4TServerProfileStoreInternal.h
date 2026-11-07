#pragma once

#include "F4TServerProfileStore.h"

#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace F4T::ServerProfileStore::Internal
{
	extern std::mutex g_storeMutex;
	extern nlohmann::json g_profiles;
	extern std::string g_connectedWorldId;
	extern std::optional<std::uint32_t> g_connectedCharacterSlot;

	void EnsureLoaded();
	bool SaveProfiles();
	bool RefreshBackupFromPrimary();
	std::filesystem::path GetProfilesPath();
	std::filesystem::path GetBackupPath(const std::filesystem::path& a_path);
	bool TryLoadProfilesFile(const std::filesystem::path& a_path, nlohmann::json& a_out);
	void WriteSaveBinding(nlohmann::json& a_entry, const std::string& a_saveFile);
	bool WriteSlotAwareBinding(nlohmann::json& a_entry, const std::string& a_saveFile);
	nlohmann::json FirstJoinRecoveryToJson(const FirstJoinRecovery& a_recovery);
	bool ProfileIdMatchesAccount(const std::string& a_account, const std::string& a_profileId);
	std::string KeptCharacterId(const nlohmann::json& a_binding, const std::string& a_extracted);
}
