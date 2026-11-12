#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#ifdef F4T_SERVER_PROFILE_STORE_TESTING
#include <functional>
#endif
#include <optional>
#include <string>
#include <utility>

namespace F4T::ServerProfileStore
{
	struct StoredResumeCredential
	{
		std::string token;
		double expiresAt{};
	};

	struct FirstJoinRecovery
	{
		std::string worldId, profileId, characterId, saveFile, sha256;
		std::uint64_t generation{};
	};

	std::filesystem::path GetStoreDirectory();
	std::filesystem::path GetSaveFolderDirectory();
	std::string MakeServerKey(const std::string& a_host, std::uint16_t a_port);
	std::optional<std::string> ResolveServerKey(const std::string& a_host, std::uint16_t a_port);
	std::pair<std::string, std::uint16_t> ProfileEndpoint(const std::string& a_host, std::uint16_t a_port, const std::string& a_joinId);
	std::string ProfileServerKey(const std::string& a_host, std::uint16_t a_port, const std::string& a_joinId);

	void SetConnectedWorldId(std::string a_worldId);
	std::string ConnectedWorldId();
	void SetConnectedCharacterSlot(std::optional<std::uint32_t> a_slot);
	bool BeginFreshCharacter(const std::string& a_serverKey, std::uint32_t a_slot, const std::string& a_worldId);
	bool ConfirmFreshCharacter(const std::string& a_serverKey, std::uint32_t a_slot, const std::string& a_worldId);
	bool HasUnconfirmedCharacterBinding(const std::string& a_serverKey);
	bool HasCharacterSlotBindings(const std::string& a_serverKey);
	std::optional<std::string> LookupSaveFile(const std::string& a_serverKey);
	std::optional<std::string> LookupCharacterId(const std::string& a_serverKey);
	bool HasFirstJoinHold(const std::string& a_serverKey);
	bool FirstJoinHoldIsSaveLess(const std::string& a_serverKey);
	bool ClearSaveLessFirstJoinHold(const std::string& a_serverKey);
	std::optional<std::uint32_t> ConnectedCharacterSlot();
	std::optional<std::uint32_t> FirstJoinHoldSlot(const std::string& a_serverKey);
	bool BindSaveFile(const std::string& a_serverKey, const std::string& a_saveFile);
	bool BindSaveFileClearingHold(const std::string& a_serverKey, const std::string& a_saveFile);
	bool RecordFirstJoinHold(const std::string& a_serverKey, std::uint64_t a_attemptId, const std::string& a_saveName, const std::string& a_reason, std::optional<std::uint32_t> a_slot = std::nullopt);
	bool RecordOnboardingResume(const std::string& a_serverKey, const std::string& a_saveFile);
	std::optional<std::string> LookupOnboardingResumeSave(const std::string& a_serverKey);
	void RepairFirstJoinBackup(const std::string& a_serverKey);
	bool StageFirstJoinRecovery(const std::string& a_serverKey, const std::string& a_worldId,
		const std::string& a_profileId, const std::string& a_characterId, const std::string& a_saveFile,
		const std::string& a_sha256, std::uint64_t a_generation);
	std::optional<FirstJoinRecovery> LookupFirstJoinRecovery(const std::string& a_serverKey);
	bool ApplyFirstJoinRecovery(const std::string& a_serverKey, const FirstJoinRecovery& a_recovery);
	bool FirstJoinRecoveryApplies(const FirstJoinRecovery& a_staged, const std::string& a_scopeWorldId,
		const nlohmann::json& a_approval, const std::optional<std::string>& a_localSha256);
	bool TouchProfile(const std::string& a_serverKey);

	std::string EnsureProfileId(const std::string& a_serverKey);
	std::optional<std::string> LookupProfileId(const std::string& a_serverKey);

	std::optional<StoredResumeCredential> LookupResumeCredential(const std::string& a_serverKey);
	std::optional<StoredResumeCredential> LookupPreviousResumeCredential(const std::string& a_serverKey);
	bool StoreResumeCredential(
		const std::string& a_serverKey,
		const std::string& a_resumeToken,
		double a_resumeExpiresAt);
	bool ClearResumeCredential(const std::string& a_serverKey, const std::optional<std::string>& a_expectedToken = std::nullopt);
	bool FinalizeResumeCredential(const std::string& a_serverKey);

#ifdef F4T_SERVER_PROFILE_STORE_TESTING
	void SetProfilesPathForTesting(std::filesystem::path a_path);
	void SetPostRotationFaultForTesting(std::function<void()> a_fault);
#endif
}
