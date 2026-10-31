#pragma once

#include <cstdint>
#include <filesystem>
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

	std::filesystem::path GetStoreDirectory();
	std::filesystem::path GetSaveFolderDirectory();
	std::string MakeServerKey(const std::string& a_host, std::uint16_t a_port);
	std::optional<std::string> ResolveServerKey(const std::string& a_host, std::uint16_t a_port);
	std::pair<std::string, std::uint16_t> ProfileEndpoint(const std::string& a_host, std::uint16_t a_port, const std::string& a_joinId);
	std::string ProfileServerKey(const std::string& a_host, std::uint16_t a_port, const std::string& a_joinId);

	void SetConnectedWorldId(std::string a_worldId);
	std::optional<std::string> LookupSaveFile(const std::string& a_serverKey);
	std::optional<std::string> LookupCharacterId(const std::string& a_serverKey);
	bool BindSaveFile(const std::string& a_serverKey, const std::string& a_saveFile);
	bool TouchProfile(const std::string& a_serverKey);

	std::string EnsureProfileId(const std::string& a_serverKey);
	std::optional<std::string> LookupProfileId(const std::string& a_serverKey);

	std::optional<StoredResumeCredential> LookupResumeCredential(const std::string& a_serverKey);
	bool StoreResumeCredential(
		const std::string& a_serverKey,
		const std::string& a_resumeToken,
		double a_resumeExpiresAt);
	bool ClearResumeCredential(const std::string& a_serverKey);

#ifdef F4T_SERVER_PROFILE_STORE_TESTING
	void SetProfilesPathForTesting(std::filesystem::path a_path);
	void SetPostRotationFaultForTesting(std::function<void()> a_fault);
#endif
}
