#include "F4TServerProfileStore.h"

#ifndef F4T_SERVER_PROFILE_STORE_TESTING
#include "pch.h"
#include "F4TDebugLogging.h"
#include "F4TF4SECapabilities.h"
#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"
#endif

#include "F4TResumeTokenProtection.h"
#include "F4TSaveProfileLogic.h"
#include "F4TServerKey.h"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <memory>
#include <mutex>
#include <random>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

namespace
{
	std::mutex g_storeMutex;
	nlohmann::json g_profiles = nlohmann::json::object();
	std::string g_connectedWorldId;
	bool g_loaded = false;
	std::atomic<bool> g_warnedNoProfilesPath{ false };
#ifdef F4T_SERVER_PROFILE_STORE_TESTING
	std::optional<std::filesystem::path> g_testProfilesPath;
	std::function<void()> g_testPostRotationFault;
#endif

#ifdef F4T_SERVER_PROFILE_STORE_TESTING
	void LogStoreWarning(std::string_view)
	{}

	void LogStoreDebug(std::string_view)
	{}
#else
	void LogStoreWarning(std::string_view a_message)
	{
		REX::WARN("[ServerProfileStore] {}", a_message);
	}

	void LogStoreDebug(std::string_view a_message)
	{
		F4T::DebugLogging::Write("ServerProfileStore", a_message);
	}
#endif

#ifndef F4T_SERVER_PROFILE_STORE_TESTING
	std::filesystem::path GetDocumentsPath()
	{
		wchar_t* knownBuffer{ nullptr };
		const auto knownResult =
			REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(knownBuffer));
		std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(knownBuffer, REX::W32::CoTaskMemFree);
		if (knownPath && knownResult == 0) {
			return std::filesystem::path(knownPath.get());
		}

		char* userProfileRaw = nullptr;
		std::size_t userProfileLen = 0;
		if (_dupenv_s(&userProfileRaw, &userProfileLen, "USERPROFILE") == 0 && userProfileRaw && userProfileRaw[0] != '\0') {
			std::unique_ptr<char[], decltype(&std::free)> userProfile(userProfileRaw, &std::free);
			return std::filesystem::path(userProfile.get()) / "Documents";
		}

		return {};
	}

	std::string SanitizeFolderComponent(std::string_view a_name, std::string_view a_fallback)
	{
		if (a_name.empty()) {
			return std::string{ a_fallback };
		}

		std::string sanitized{ a_name };

		for (auto& ch : sanitized) {
			if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
				ch = '_';
			}
		}

		std::size_t start = 0;
		while (start < sanitized.size() && std::isspace(static_cast<unsigned char>(sanitized[start])) != 0) {
			++start;
		}
		std::size_t end = sanitized.size();
		while (end > start && std::isspace(static_cast<unsigned char>(sanitized[end - 1])) != 0) {
			--end;
		}

		if (start >= end) {
			return std::string{ a_fallback };
		}

		return sanitized.substr(start, end - start);
	}
#endif

	std::string GenerateProfileIdSuffix()
	{
		std::random_device device;
		constexpr char kHex[] = "0123456789abcdef";
		std::string result;
		result.reserve(32);
		for (std::size_t index = 0; index < 32; ++index) {
			result.push_back(kHex[device() % 16u]);
		}
		return result;
	}

	bool IsValidProfileEntry(const nlohmann::json& a_entry)
	{
		return a_entry.is_object();
	}

	std::filesystem::path GetProfilesPath()
	{
#ifdef F4T_SERVER_PROFILE_STORE_TESTING
		if (g_testProfilesPath) {
			return *g_testProfilesPath;
		}
		return {};
#else
		auto path = F4T::ServerProfileStore::GetSaveFolderDirectory();
		if (path.empty()) {
			if (!g_warnedNoProfilesPath.exchange(true)) {
				LogStoreWarning("Could not resolve Documents path for server profile storage.");
			}
			return {};
		}

		path /= "CommonwealthOnline";
		path /= "server-profiles.json";
		return path;
#endif
	}

	bool TryLoadProfilesFile(const std::filesystem::path& a_path, nlohmann::json& a_out)
	{
		std::ifstream input(a_path);
		if (!input.is_open()) {
			return false;
		}

		try {
			nlohmann::json loaded;
			input >> loaded;
			if (!loaded.is_object()) {
				return false;
			}
			a_out = std::move(loaded);
			return true;
		} catch (const std::exception&) {
			return false;
		}
	}

	std::filesystem::path GetBackupPath(const std::filesystem::path& a_path)
	{
		auto backup = a_path;
		backup += ".bak";
		return backup;
	}

	bool CommitFileToDisk(const std::filesystem::path& a_path)
	{
#ifdef _WIN32
		const auto fileDescriptor = _wopen(a_path.c_str(), _O_WRONLY | _O_BINARY);
		if (fileDescriptor < 0) {
			return false;
		}
		const auto committed = _commit(fileDescriptor) == 0;
		_close(fileDescriptor);
		return committed;
#else
		const auto fileDescriptor = open(a_path.c_str(), O_RDONLY);
		if (fileDescriptor < 0) {
			return false;
		}
		const auto committed = fsync(fileDescriptor) == 0;
		close(fileDescriptor);
		return committed;
#endif
	}

	void MigrateLegacyResumeTokens();

	void EnsureLoaded()
	{
		if (g_loaded) {
			return;
		}

		g_loaded = true;
		const auto path = GetProfilesPath();
		if (path.empty()) {
			return;
		}

		if (TryLoadProfilesFile(path, g_profiles)) {
			LogStoreDebug(std::format("loaded profiles path={} entries={}", path.string(), g_profiles.value("servers", nlohmann::json::object()).size()));
			MigrateLegacyResumeTokens();
			return;
		}

		const auto backupPath = GetBackupPath(path);
		if (TryLoadProfilesFile(backupPath, g_profiles)) {
			LogStoreWarning(std::format("Recovered server profiles from backup: {}", backupPath.string()));
			MigrateLegacyResumeTokens();
			return;
		}

		g_profiles = nlohmann::json::object();
		LogStoreDebug(std::format("no valid profile store found path={}", path.string()));
	}

	bool SaveProfiles()
	{
		const auto path = GetProfilesPath();
		if (path.empty()) {
			return false;
		}

		LogStoreDebug(std::format("saving profiles path={} entries={}", path.string(), g_profiles.value("servers", nlohmann::json::object()).size()));
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) {
			LogStoreWarning(std::format("Failed to create server profile directory: {}", ec.message()));
			return false;
		}

		auto tempPath = path;
		tempPath += ".tmp";
		const auto backupPath = GetBackupPath(path);
		{
			std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				LogStoreWarning(std::format("Failed to open temporary server profile store for write: {}", tempPath.string()));
				return false;
			}
			output << g_profiles.dump(2) << '\n';
			output.flush();
			if (!output.good()) {
				output.close();
				std::filesystem::remove(tempPath, ec);
				LogStoreWarning(std::format("Failed to flush temporary server profile store: {}", tempPath.string()));
				return false;
			}
			output.close();
			if (output.fail()) {
				std::filesystem::remove(tempPath, ec);
				LogStoreWarning(std::format("Failed to close temporary server profile store: {}", tempPath.string()));
				return false;
			}
		}

		if (!CommitFileToDisk(tempPath)) {
			std::filesystem::remove(tempPath, ec);
			LogStoreWarning(std::format("Failed to commit temporary server profile store to disk: {}", tempPath.string()));
			return false;
		}

		if (std::filesystem::exists(path)) {
			nlohmann::json currentProfiles;
			if (TryLoadProfilesFile(path, currentProfiles)) {
				std::filesystem::remove(backupPath, ec);
				ec.clear();
				std::filesystem::rename(path, backupPath, ec);
				if (ec) {
					const auto errorMessage = ec.message();
					std::error_code cleanupError;
					std::filesystem::remove(tempPath, cleanupError);
					LogStoreWarning(std::format("Failed to rotate server profile backup: {}", errorMessage));
					return false;
				}
			} else {
				std::filesystem::remove(path, ec);
				if (ec) {
					const auto errorMessage = ec.message();
					std::error_code cleanupError;
					std::filesystem::remove(tempPath, cleanupError);
					LogStoreWarning(std::format("Failed to remove corrupt server profile store: {}", errorMessage));
					return false;
				}
			}
		}

#ifdef F4T_SERVER_PROFILE_STORE_TESTING
		if (g_testPostRotationFault) {
			const auto fault = std::exchange(g_testPostRotationFault, nullptr);
			fault();
		}
#endif

		ec.clear();
		std::filesystem::rename(tempPath, path, ec);
		if (ec) {
			std::error_code cleanupError;
			std::filesystem::remove(tempPath, cleanupError);
			LogStoreWarning(std::format("Failed to install new server profile store: {}", ec.message()));
			return false;
		}
		LogStoreDebug(std::format("profile store commit complete path={}", path.string()));
		return true;
	}

	void MigrateLegacyResumeTokens()
	{
		if (!g_profiles.contains("servers") || !g_profiles["servers"].is_object()) {
			return;
		}

		bool migrated = false;
		for (auto& entry : g_profiles["servers"]) {
			if (!entry.is_object() || !entry.contains("resumeToken")) {
				continue;
			}
			migrated = true;
			const auto legacyToken = entry["resumeToken"];
			entry.erase("resumeToken");
			entry.erase("resumeTokenProtected");
			if (!legacyToken.is_string()) {
				continue;
			}
			if (const auto protectedToken = F4T::ResumeTokenProtection::Protect(legacyToken.get_ref<const std::string&>())) {
				entry["resumeTokenProtected"] = *protectedToken;
			} else {
				LogStoreWarning("Failed to protect a legacy server resume token; it was dropped.");
			}
		}

		if (!migrated) {
			return;
		}
		if (!SaveProfiles()) {
			LogStoreWarning("Failed to rewrite server profile store after resume token migration.");
			return;
		}

		const auto path = GetProfilesPath();
		const auto backupPath = GetBackupPath(path);
		std::error_code ec;
		std::filesystem::copy_file(path, backupPath, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) {
			std::filesystem::remove(backupPath, ec);
		}
	}
}

namespace F4T::ServerProfileStore
{
	std::filesystem::path GetStoreDirectory()
	{
		return GetProfilesPath().parent_path();
	}

	std::filesystem::path GetSaveFolderDirectory()
	{
#ifdef F4T_SERVER_PROFILE_STORE_TESTING
		return {};
#else
		auto documentsPath = GetDocumentsPath();
		if (documentsPath.empty()) {
			return {};
		}
		return documentsPath / "My Games" / SanitizeFolderComponent(F4T::F4SECapabilities::GetSaveFolderName(), "Fallout4");
#endif
	}

	std::string MakeServerKey(const std::string& a_host, const std::uint16_t a_port)
	{
		return std::format("{}:{}", ServerProfileKey::NormalizeHost(a_host), a_port);
	}

	std::optional<std::string> ResolveServerKey(const std::string& a_host, std::uint16_t a_port)
	{
		const auto preferredKey = MakeServerKey(a_host, a_port);

		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();

		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) {
			return std::nullopt;
		}

		const auto preferredEntry = servers->find(preferredKey);
		if (preferredEntry != servers->end() && IsValidProfileEntry(*preferredEntry)) {
			return preferredKey;
		}

		for (auto entryIt = servers->begin(); entryIt != servers->end(); ++entryIt) {
			if (!IsValidProfileEntry(entryIt.value())) {
				continue;
			}
			if (ServerProfileKey::ServerKeyMatches(entryIt.key(), a_host, a_port)) {
				return entryIt.key();
			}
		}

		return std::nullopt;
	}

	std::pair<std::string, std::uint16_t> ProfileEndpoint(const std::string& a_host, std::uint16_t a_port, const std::string& a_joinId)
	{
		constexpr std::uint16_t kSteamGuestProfilePort = 7777;
		if (ServerProfileKey::NormalizeHost(a_host) == "127.0.0.1" && ServerProfileKey::IsSteamGuestJoinId(a_joinId)) {
			return { ServerProfileKey::SteamGuestProfileHost(a_joinId), kSteamGuestProfilePort };
		}
		return { a_host, a_port };
	}

	std::string ProfileServerKey(const std::string& a_host, std::uint16_t a_port, const std::string& a_joinId)
	{
		const auto [host, port] = ProfileEndpoint(a_host, a_port, a_joinId);
		return ResolveServerKey(host, port).value_or(MakeServerKey(host, port));
	}

	void SetConnectedWorldId(std::string a_worldId)
	{
		const bool valid = a_worldId.size() == 32 &&
			std::all_of(a_worldId.begin(), a_worldId.end(), [](unsigned char a_char) { return std::isxdigit(a_char) != 0; });
		const std::scoped_lock lock(g_storeMutex);
		g_connectedWorldId = valid ? std::move(a_worldId) : std::string{};
	}

	std::optional<std::string> LookupBoundString(const std::string& a_serverKey, const char* a_field)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();

		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) {
			return std::nullopt;
		}

		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object()) {
			return std::nullopt;
		}

		const nlohmann::json* binding = &*entry;
		const auto worlds = entry->find("worldSaves");
		if (!g_connectedWorldId.empty() && worlds != entry->end() && worlds->is_object()) {
			const auto world = worlds->find(g_connectedWorldId);
			if (world == worlds->end() || !world->is_object()) {
				return std::nullopt;
			}
			binding = &*world;
		}

		const auto value = binding->find(a_field);
		if (value == binding->end() || !value->is_string() || value->get<std::string>().empty()) {
			return std::nullopt;
		}

		return value->get<std::string>();
	}

	std::optional<std::string> LookupSaveFile(const std::string& a_serverKey)
	{
		return LookupBoundString(a_serverKey, "saveFile");
	}

	std::optional<std::string> LookupCharacterId(const std::string& a_serverKey)
	{
		return LookupBoundString(a_serverKey, "characterId");
	}

	nlohmann::json& ProfileEntry(const std::string& a_serverKey)
	{
		if (!g_profiles.contains("servers") || !g_profiles["servers"].is_object()) {
			g_profiles["servers"] = nlohmann::json::object();
		}
		auto& entry = g_profiles["servers"][a_serverKey];
		if (!entry.is_object()) {
			entry = nlohmann::json::object();
		}
		return entry;
	}

	std::optional<std::string> LookupStringField(const std::string& a_serverKey, const char* a_field)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();

		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) {
			return std::nullopt;
		}
		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object()) {
			return std::nullopt;
		}
		const auto value = entry->find(a_field);
		if (value == entry->end() || !value->is_string() || value->get<std::string>().empty()) {
			return std::nullopt;
		}
		return value->get<std::string>();
	}

	bool BindSaveFile(const std::string& a_serverKey, const std::string& a_saveFile)
	{
		if (a_serverKey.empty() || a_saveFile.empty()) {
			return false;
		}

		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto previousProfiles = g_profiles;

		if (!g_profiles.contains("servers") || !g_profiles["servers"].is_object()) {
			g_profiles["servers"] = nlohmann::json::object();
		}

		const auto now = std::chrono::system_clock::now();
		const auto epochSeconds =
			std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

		auto& entry = g_profiles["servers"][a_serverKey];
		if (!entry.is_object()) {
			entry = nlohmann::json::object();
		}

		entry["saveFile"] = a_saveFile;
		entry["lastJoined"] = epochSeconds;
		const auto characterId = SaveProfileLogic::ExtractCharacterId(a_saveFile);
		if (characterId) {
			entry["characterId"] = *characterId;
		}
		if (!g_connectedWorldId.empty()) {
			if (!entry.contains("worldSaves") || !entry["worldSaves"].is_object()) {
				entry["worldSaves"] = nlohmann::json::object();
			}
			auto& world = entry["worldSaves"][g_connectedWorldId];
			world = nlohmann::json{ { "saveFile", a_saveFile } };
			if (characterId) {
				world["characterId"] = *characterId;
			}
		}

		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	bool TouchProfile(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();

		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) {
			return false;
		}

		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object()) {
			return false;
		}

		const auto previousProfiles = g_profiles;
		const auto now = std::chrono::system_clock::now();
		const auto epochSeconds =
			std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
		(*entry)["lastJoined"] = epochSeconds;
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	std::string EnsureProfileId(const std::string& a_serverKey)
	{
		if (a_serverKey.empty()) {
			return {};
		}

		const std::scoped_lock lock(g_storeMutex);
		if (GetProfilesPath().empty()) {
			return {};
		}
		EnsureLoaded();
		const auto servers = g_profiles.find("servers");
		if (servers != g_profiles.end() && servers->is_object()) {
			const auto entry = servers->find(a_serverKey);
			if (entry != servers->end() && entry->is_object()) {
				const auto value = entry->find("profileId");
				if (value != entry->end() && value->is_string() && !value->get_ref<const std::string&>().empty()) {
					return value->get<std::string>();
				}
			}
		}

		const auto previousProfiles = g_profiles;
		const auto profileId = std::format("co-{}", GenerateProfileIdSuffix());
		ProfileEntry(a_serverKey)["profileId"] = profileId;
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return {};
		}
		return profileId;
	}

	std::optional<std::string> LookupProfileId(const std::string& a_serverKey)
	{
		return LookupStringField(a_serverKey, "profileId");
	}

	std::optional<StoredResumeCredential> LookupResumeCredential(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();

		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) {
			return std::nullopt;
		}
		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object()) {
			return std::nullopt;
		}
		const auto token = entry->find("resumeTokenProtected");
		const auto expiry = entry->find("resumeExpiresAt");
		if (token == entry->end() || !token->is_string() || token->get_ref<const std::string&>().empty() ||
			expiry == entry->end() || expiry->is_boolean() || !expiry->is_number()) {
			return std::nullopt;
		}
		try {
			const auto expiresAt = expiry->get<double>();
			if (!std::isfinite(expiresAt) || expiresAt <= 0.0) {
				return std::nullopt;
			}
			const auto plaintext = ResumeTokenProtection::Unprotect(token->get_ref<const std::string&>());
			if (!plaintext || plaintext->empty()) {
				LogStoreWarning("Stored server resume token could not be unprotected; continuing without a resume credential.");
				return std::nullopt;
			}
			return StoredResumeCredential{ *plaintext, expiresAt };
		} catch (...) {
			return std::nullopt;
		}
	}

	bool StoreResumeCredential(
		const std::string& a_serverKey,
		const std::string& a_resumeToken,
		double a_resumeExpiresAt)
	{
		if (a_serverKey.empty() || a_resumeToken.empty() || !std::isfinite(a_resumeExpiresAt) || a_resumeExpiresAt <= 0.0) {
			return false;
		}

		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto protectedToken = ResumeTokenProtection::Protect(a_resumeToken);
		if (!protectedToken) {
			LogStoreWarning("Failed to protect server resume token; credential not stored.");
			return false;
		}
		const auto previousProfiles = g_profiles;
		auto& entry = ProfileEntry(a_serverKey);
		entry["resumeTokenProtected"] = *protectedToken;
		entry["resumeExpiresAt"] = a_resumeExpiresAt;
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	bool ClearResumeCredential(const std::string& a_serverKey)
	{
		if (a_serverKey.empty()) {
			return false;
		}

		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) {
			return false;
		}
		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object()) {
			return false;
		}
		const bool hasToken = entry->contains("resumeTokenProtected");
		const bool hasExpiry = entry->contains("resumeExpiresAt");
		if (!hasToken && !hasExpiry) {
			return false;
		}

		const auto previousProfiles = g_profiles;
		entry->erase("resumeTokenProtected");
		entry->erase("resumeExpiresAt");
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

#ifdef F4T_SERVER_PROFILE_STORE_TESTING
	void SetProfilesPathForTesting(std::filesystem::path a_path)
	{
		const std::scoped_lock lock(g_storeMutex);
		g_testProfilesPath = std::move(a_path);
		g_profiles = nlohmann::json::object();
		g_loaded = false;
		g_warnedNoProfilesPath = false;
	}

	void SetPostRotationFaultForTesting(std::function<void()> a_fault)
	{
		const std::scoped_lock lock(g_storeMutex);
		g_testPostRotationFault = std::move(a_fault);
	}
#endif
}
