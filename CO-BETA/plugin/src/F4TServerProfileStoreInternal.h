#pragma once

#include "F4TServerProfileStore.h"

#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ServerProfileStore::Internal
{
	class ProfileFileLock
	{
	public:
		explicit ProfileFileLock(const std::filesystem::path& a_profilesPath);
		~ProfileFileLock();
		ProfileFileLock(const ProfileFileLock&) = delete;
		ProfileFileLock& operator=(const ProfileFileLock&) = delete;
		bool Held() const { return m_handle >= 0; }

	private:
		int m_handle{ -1 };
	};

	// Caller must hold g_storeMutex for the whole lifetime; false means the file lock timed out.
	class StoreTransaction
	{
	public:
		StoreTransaction();
		~StoreTransaction();
		StoreTransaction(const StoreTransaction&) = delete;
		StoreTransaction& operator=(const StoreTransaction&) = delete;
		explicit operator bool() const { return m_ok; }

	private:
		std::optional<ProfileFileLock> m_lock;
		bool m_ok{ false };
	};

	void LogStoreWarning(std::string_view a_message);
	bool QuarantineFile(const std::filesystem::path& a_path);
	bool AcquireSaveLock(std::optional<ProfileFileLock>& a_out, const std::filesystem::path& a_profilesPath);
	void AppendPreviousCharacterSave(nlohmann::json& a_entry, const std::string& a_worldId, std::uint32_t a_slot, const nlohmann::json& a_binding);
#ifdef F4T_SERVER_PROFILE_STORE_TESTING
	void SetLockTimeoutForTesting(int a_milliseconds);
#endif

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
