#include "F4TServerProfileStore.h"
#include "F4TServerProfileStoreInternal.h"

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

namespace F4T::ServerProfileStore::Internal
{
	std::mutex g_storeMutex;
	nlohmann::json g_profiles = nlohmann::json::object();
	std::string g_connectedWorldId;
	std::optional<std::uint32_t> g_connectedCharacterSlot;
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

	bool RefreshBackupFromPrimary()
	{
		const auto path = GetProfilesPath();
		std::error_code ec;
		for (int attempt = 0; attempt < 3; ++attempt) {
			std::filesystem::copy_file(path, GetBackupPath(path), std::filesystem::copy_options::overwrite_existing, ec);
			if (!ec) {
				return true;
			}
		}
		LogStoreWarning(std::format("Failed to refresh server profile backup after clearing hold: {}", ec.message()));
		return false;
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

		if (!RefreshBackupFromPrimary()) {
			std::error_code ec;
			std::filesystem::remove(GetBackupPath(GetProfilesPath()), ec);
		}
	}
	std::string KeptCharacterId(const nlohmann::json& a_binding, const std::string& a_extracted)
	{
		const auto stored = a_binding.find("characterId");
		if (stored != a_binding.end() && stored->is_string() &&
			SaveProfileLogic::CharacterIdsEqual(stored->get_ref<const std::string&>(), a_extracted)) {
			return stored->get<std::string>();
		}
		return a_extracted;
	}

	void WriteSaveBinding(nlohmann::json& a_entry, const std::string& a_saveFile)
	{
		const auto now = std::chrono::system_clock::now();
		const auto epochSeconds =
			std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
		a_entry["saveFile"] = a_saveFile;
		a_entry["lastJoined"] = epochSeconds;
		const auto characterId = F4T::SaveProfileLogic::ExtractCharacterId(a_saveFile);
		if (characterId) {
			a_entry["characterId"] = KeptCharacterId(a_entry, *characterId);
		}
		if (!g_connectedWorldId.empty()) {
			if (!a_entry.contains("worldSaves") || !a_entry["worldSaves"].is_object()) {
				a_entry["worldSaves"] = nlohmann::json::object();
			}
			auto& world = a_entry["worldSaves"][g_connectedWorldId];
			const auto keptWorldCharacterId = characterId ? KeptCharacterId(world, *characterId) : std::string{};
			world = nlohmann::json{ { "saveFile", a_saveFile } };
			if (characterId) {
				world["characterId"] = keptWorldCharacterId;
			}
		}
	}

	std::optional<F4T::ServerProfileStore::StoredResumeCredential> ReadStoredResumeCredential(
		const nlohmann::json& a_entry,
		const char* a_tokenField,
		const char* a_expiryField)
	{
		const auto token = a_entry.find(a_tokenField);
		const auto expiry = a_entry.find(a_expiryField);
		if (token == a_entry.end() || !token->is_string() || token->get_ref<const std::string&>().empty() ||
			expiry == a_entry.end() || expiry->is_boolean() || !expiry->is_number()) {
			return std::nullopt;
		}
		try {
			const auto expiresAt = expiry->get<double>();
			if (!std::isfinite(expiresAt) || expiresAt <= 0.0) {
				return std::nullopt;
			}
			const auto plaintext = F4T::ResumeTokenProtection::Unprotect(token->get_ref<const std::string&>());
			if (!plaintext || plaintext->empty()) {
				return std::nullopt;
			}
			return F4T::ServerProfileStore::StoredResumeCredential{ *plaintext, expiresAt };
		} catch (...) {
			return std::nullopt;
		}
	}
}

namespace F4T::ServerProfileStore
{
	using namespace Internal;

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

	std::string ConnectedWorldId()
	{
		const std::scoped_lock lock(g_storeMutex);
		return g_connectedWorldId;
	}

	void SetConnectedCharacterSlot(std::optional<std::uint32_t> a_slot)
	{
		const std::scoped_lock lock(g_storeMutex);
		g_connectedCharacterSlot = a_slot;
	}

	bool BeginFreshCharacter(const std::string& a_serverKey, std::uint32_t a_slot, const std::string& a_worldId)
	{
		if (a_serverKey.empty() || a_slot > 255) return false;
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto previousProfiles = g_profiles;
		try {
			auto& entry = g_profiles["servers"][a_serverKey];
			auto& binding = entry["characterSaves"][a_worldId][std::to_string(a_slot)];
			if (binding.is_object() && !binding.empty()) {
				entry["previousCharacterSaves"].push_back(binding);
			}
			binding = nlohmann::json{ { "awaitingCreate", true } };
		} catch (const nlohmann::json::exception&) {
			g_profiles = previousProfiles;
			return false;
		}
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	bool ConfirmFreshCharacter(const std::string& a_serverKey, std::uint32_t a_slot, const std::string& a_worldId)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto previousProfiles = g_profiles;
		try {
			auto& binding = g_profiles.at("servers").at(a_serverKey).at("characterSaves").at(a_worldId).at(std::to_string(a_slot));
			if (!binding.is_object() || !binding.contains("awaitingCreate") || binding["awaitingCreate"] != true) return false;
			binding.erase("awaitingCreate");
		} catch (const nlohmann::json::exception&) {
			g_profiles = previousProfiles;
			return false;
		}
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	bool HasCharacterSlotBindings(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) return false;
		const auto entry = servers->find(a_serverKey);
		return entry != servers->end() && entry->contains("characterSaves");
	}

	const nlohmann::json* CharacterSlotBinding(const nlohmann::json& a_entry)
	{
		static const nlohmann::json unconfirmed;
		const auto slots = a_entry.find("characterSaves");
		if (slots == a_entry.end()) return nullptr;
		if (!g_connectedCharacterSlot) return &unconfirmed;
		if (!slots->is_object()) return &*slots;
		const auto world = slots->find(g_connectedWorldId);
		if (world == slots->end()) return &unconfirmed;
		if (!world->is_object()) return &*world;
		const auto slot = world->find(std::to_string(*g_connectedCharacterSlot));
		return slot == world->end() ? &unconfirmed : &*slot;
	}

	bool HasUnconfirmedCharacterBinding(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) return false;
		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end()) return false;
		const auto* binding = CharacterSlotBinding(*entry);
		return binding && (!binding->is_object() || binding->contains("awaitingCreate"));
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
			if ((world == worlds->end() || !world->is_object()) && !CharacterSlotBinding(*entry)) {
				return std::nullopt;
			}
			if (world != worlds->end()) binding = &*world;
		}

		if (const auto* slot = CharacterSlotBinding(*entry)) binding = slot;
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

	bool HasFirstJoinHold(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto hasHold = [&](const nlohmann::json& a_profiles, bool a_persisted) {
			const auto servers = a_profiles.find("servers");
			if (servers == a_profiles.end()) return a_persisted;
			if (!servers->is_object()) return true;
			const auto entry = servers->find(a_serverKey);
			return entry != servers->end() && (!entry->is_object() || entry->contains("firstJoinHold"));
		};
		if (a_serverKey.empty() || hasHold(g_profiles, false)) return true;
		const auto path = GetProfilesPath();
		if (path.empty()) return true;
		auto pending = path;
		pending += ".tmp";
		std::error_code pendingError;
		if (std::filesystem::exists(pending, pendingError) || pendingError) return true;
		for (const auto& candidate : { path, GetBackupPath(path) }) {
			std::error_code ec;
			const bool exists = std::filesystem::exists(candidate, ec);
			if (ec) return true;
			if (!exists) continue;
			nlohmann::json persisted;
			if (!TryLoadProfilesFile(candidate, persisted) || hasHold(persisted, true)) return true;
		}
		return false;
	}

	static std::string OnboardingResumeSlotKey()
	{
		return g_connectedCharacterSlot ? std::to_string(*g_connectedCharacterSlot) : std::string{ "none" };
	}

	static void EraseOnboardingResume(nlohmann::json& a_entry)
	{
		const auto resume = a_entry.find("onboardingResume");
		if (resume == a_entry.end() || !resume->is_object()) return;
		const auto world = resume->find(g_connectedWorldId);
		if (world == resume->end() || !world->is_object()) return;
		world->erase(OnboardingResumeSlotKey());
		if (world->empty()) resume->erase(g_connectedWorldId);
		if (resume->empty()) a_entry.erase("onboardingResume");
	}

	bool Internal::WriteSlotAwareBinding(nlohmann::json& a_entry, const std::string& a_saveFile)
	{
		const auto* slot = CharacterSlotBinding(a_entry);
		if (!slot) {
			WriteSaveBinding(a_entry, a_saveFile);
			return true;
		}
		if (!slot->is_object() || slot->contains("awaitingCreate")) return false;
		const auto epochSeconds =
			std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		auto& binding = a_entry["characterSaves"][g_connectedWorldId][std::to_string(*g_connectedCharacterSlot)];
		const auto characterId = SaveProfileLogic::ExtractCharacterId(a_saveFile);
		const auto keptCharacterId = characterId ? KeptCharacterId(binding, *characterId) : std::string{};
		binding = nlohmann::json{ { "saveFile", a_saveFile } };
		if (characterId) binding["characterId"] = keptCharacterId;
		a_entry["lastJoined"] = epochSeconds;
		return true;
	}

	bool BindSaveFileWithHold(const std::string& a_serverKey, const std::string& a_saveFile, bool a_clearHold)
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

		auto& entry = g_profiles["servers"][a_serverKey];
		if (!entry.is_object()) {
			entry = nlohmann::json::object();
		}

		if (!WriteSlotAwareBinding(entry, a_saveFile)) return false;
		if (a_clearHold) {
			entry.erase("firstJoinHold");
			EraseOnboardingResume(entry);
		}

		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		if (a_clearHold) {
			RefreshBackupFromPrimary();
		}
		return true;
	}

	bool BindSaveFile(const std::string& a_serverKey, const std::string& a_saveFile)
	{
		return BindSaveFileWithHold(a_serverKey, a_saveFile, false);
	}

	bool BindSaveFileClearingHold(const std::string& a_serverKey, const std::string& a_saveFile)
	{
		return BindSaveFileWithHold(a_serverKey, a_saveFile, true);
	}

	bool RecordFirstJoinHold(const std::string& a_serverKey, std::uint64_t a_attemptId, const std::string& a_saveName, const std::string& a_reason, std::optional<std::uint32_t> a_slot)
	{
		if (a_serverKey.empty()) {
			return false;
		}

		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto previousProfiles = g_profiles;

		const auto epochSeconds =
			std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		auto& holdEntry = ProfileEntry(a_serverKey);
		if (a_attemptId != 0 || !a_saveName.empty()) {
			EraseOnboardingResume(holdEntry);
		}
		holdEntry["firstJoinHold"] = nlohmann::json{
			{ "attemptId", a_attemptId },
			{ "saveName", a_saveName },
			{ "reason", a_reason },
			{ "heldAt", epochSeconds }
		};
		if (a_saveName.empty() && a_slot) {
			holdEntry["firstJoinHold"]["slot"] = *a_slot;
		}

		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	bool RecordOnboardingResume(const std::string& a_serverKey, const std::string& a_saveFile)
	{
		if (a_serverKey.empty() || a_saveFile.empty()) {
			return false;
		}

		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		const auto previousProfiles = g_profiles;

		try {
			auto& entry = ProfileEntry(a_serverKey);
			if (!entry["onboardingResume"].is_object()) entry["onboardingResume"] = nlohmann::json::object();
			auto& world = entry["onboardingResume"][g_connectedWorldId];
			if (!world.is_object()) world = nlohmann::json::object();
			world[OnboardingResumeSlotKey()] = nlohmann::json{ { "saveFile", a_saveFile } };
		} catch (const nlohmann::json::exception&) {
			g_profiles = previousProfiles;
			return false;
		}

		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	std::optional<std::string> LookupOnboardingResumeSave(const std::string& a_serverKey)
	{
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();

		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) return std::nullopt;
		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object()) return std::nullopt;
		const auto resume = entry->find("onboardingResume");
		if (resume == entry->end() || !resume->is_object()) return std::nullopt;
		const auto world = resume->find(g_connectedWorldId);
		if (world == resume->end() || !world->is_object()) return std::nullopt;
		const auto slot = world->find(OnboardingResumeSlotKey());
		if (slot == world->end() || !slot->is_object()) return std::nullopt;
		const auto saveFile = slot->find("saveFile");
		if (saveFile == slot->end() || !saveFile->is_string() || saveFile->get<std::string>().empty()) return std::nullopt;
		return saveFile->get<std::string>();
	}

	bool StageFirstJoinRecovery(const std::string& a_serverKey, const std::string& a_worldId,
		const std::string& a_profileId, const std::string& a_characterId, const std::string& a_saveFile,
		const std::string& a_sha256, std::uint64_t a_generation)
	{
		if (a_serverKey.empty() || a_profileId.empty() || a_generation == 0 || a_saveFile.empty() ||
			a_saveFile.size() > 256 || a_saveFile.find_first_of("/\\:") != std::string::npos ||
			std::any_of(a_saveFile.begin(), a_saveFile.end(), [](unsigned char c) { return c < 32 || c == 127; }) ||
			a_sha256.size() != 64 || !std::all_of(a_sha256.begin(), a_sha256.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }) ||
			!SaveProfileLogic::CanRebindProfileSave(a_characterId, a_saveFile)) return false;
		const std::scoped_lock lock(g_storeMutex);
		EnsureLoaded();
		if (a_worldId.empty() || a_worldId != g_connectedWorldId) return false;
		const auto servers = g_profiles.find("servers");
		if (servers == g_profiles.end() || !servers->is_object()) return false;
		auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_object() || !entry->contains("firstJoinHold") ||
			!entry->contains("profileId") || !entry->at("profileId").is_string()) return false;
		const auto& account = entry->at("profileId").get_ref<const std::string&>();
		if (!ProfileIdMatchesAccount(account, a_profileId)) return false;
		const auto recovery = FirstJoinRecoveryToJson(FirstJoinRecovery{ a_worldId, a_profileId, a_characterId, a_saveFile, a_sha256, a_generation });
		const auto compatible = [&](const nlohmann::json& a_profiles) {
			const auto storedServers = a_profiles.find("servers");
			if (storedServers == a_profiles.end() || !storedServers->is_object()) return false;
			const auto storedEntry = storedServers->find(a_serverKey);
			if (storedEntry == storedServers->end()) return true;
			if (!storedEntry->is_object()) return false;
			if (!storedEntry->contains("firstJoinRecovery")) return true;
			const auto& previous = storedEntry->at("firstJoinRecovery");
			if (previous == recovery) return true;
			if (!previous.is_object() || !previous.contains("worldId") || !previous.at("worldId").is_string() ||
				!previous.contains("profileId") || !previous.at("profileId").is_string() ||
				!previous.contains("generation") || !previous.at("generation").is_number_unsigned() ||
				previous.at("worldId") != a_worldId || previous.at("profileId") != a_profileId ||
				previous.at("generation").get<std::uint64_t>() >= a_generation) return false;
			return true;
		};
		if (!compatible(g_profiles)) return false;
		const auto path = GetProfilesPath();
		if (path.empty()) return false;
		auto pending = path;
		pending += ".tmp";
		std::error_code ec;
		if (std::filesystem::exists(pending, ec) || ec) return false;
		for (const auto& candidate : { path, GetBackupPath(path) }) {
			const bool exists = std::filesystem::exists(candidate, ec);
			if (ec) return false;
			if (!exists) continue;
			nlohmann::json persisted;
			if (!TryLoadProfilesFile(candidate, persisted) || !compatible(persisted)) return false;
		}
		if (entry->contains("firstJoinRecovery") && entry->at("firstJoinRecovery") == recovery) return true;
		const auto previousProfiles = g_profiles;
		(*entry)["firstJoinRecovery"] = recovery;
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

		if (const auto active = ReadStoredResumeCredential(*entry, "resumeTokenProtected", "resumeExpiresAt")) {
			return active;
		}
		if (const auto activeToken = entry->find("resumeTokenProtected");
			activeToken != entry->end() && activeToken->is_string() && !activeToken->get_ref<const std::string&>().empty()) {
			LogStoreWarning("Stored server resume token could not be unprotected; continuing without a resume credential.");
		}

		const auto previous = ReadStoredResumeCredential(*entry, "resumeTokenPreviousProtected", "resumeExpiresAtPrevious");
		const auto previousToken = entry->find("resumeTokenPreviousProtected");
		const auto previousExpiry = entry->find("resumeExpiresAtPrevious");
		if (!previous || previousToken == entry->end() || previousExpiry == entry->end()) {
			return std::nullopt;
		}

		const auto previousProfiles = g_profiles;
		auto& mutableEntry = *entry;
		mutableEntry["resumeTokenProtected"] = *previousToken;
		mutableEntry["resumeExpiresAt"] = *previousExpiry;
		mutableEntry.erase("resumeTokenPreviousProtected");
		mutableEntry.erase("resumeExpiresAtPrevious");
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			LogStoreWarning("Failed to promote the retained server resume credential; keeping it for the next attempt.");
			return previous;
		}
		LogStoreWarning("Restored the retained server resume credential after the active record became unusable.");
		return previous;
	}

	std::optional<StoredResumeCredential> LookupPreviousResumeCredential(const std::string& a_serverKey)
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
		return ReadStoredResumeCredential(*entry, "resumeTokenPreviousProtected", "resumeExpiresAtPrevious");
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
		const auto activeToken = entry.find("resumeTokenProtected");
		const auto activeExpiry = entry.find("resumeExpiresAt");
		if (activeToken != entry.end() && activeToken->is_string() && !activeToken->get_ref<const std::string&>().empty() &&
			activeExpiry != entry.end() && activeExpiry->is_number() && !activeExpiry->is_boolean()) {
			const auto activePlaintext = ResumeTokenProtection::Unprotect(activeToken->get_ref<const std::string&>());
			if (activePlaintext && *activePlaintext != a_resumeToken) {
				entry["resumeTokenPreviousProtected"] = *activeToken;
				entry["resumeExpiresAtPrevious"] = *activeExpiry;
			}
		}
		entry["resumeTokenProtected"] = *protectedToken;
		entry["resumeExpiresAt"] = a_resumeExpiresAt;
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	bool ClearResumeCredential(const std::string& a_serverKey, const std::optional<std::string>& a_expectedToken)
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
		if (a_expectedToken) {
			const auto current = ReadStoredResumeCredential(*entry, "resumeTokenProtected", "resumeExpiresAt");
			if (!current || current->token != *a_expectedToken) {
				return false;
			}
		}
		const bool hasToken = entry->contains("resumeTokenProtected");
		const bool hasExpiry = entry->contains("resumeExpiresAt");
		const bool hasPrevious = entry->contains("resumeTokenPreviousProtected");
		if (!hasToken && !hasExpiry && !hasPrevious) {
			return false;
		}

		const auto previousProfiles = g_profiles;
		entry->erase("resumeTokenProtected");
		entry->erase("resumeExpiresAt");
		entry->erase("resumeTokenPreviousProtected");
		entry->erase("resumeExpiresAtPrevious");
		if (!SaveProfiles()) {
			g_profiles = previousProfiles;
			return false;
		}
		return true;
	}

	bool FinalizeResumeCredential(const std::string& a_serverKey)
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
		if (!entry->contains("resumeTokenPreviousProtected") && !entry->contains("resumeExpiresAtPrevious")) {
			return false;
		}

		const auto previousProfiles = g_profiles;
		entry->erase("resumeTokenPreviousProtected");
		entry->erase("resumeExpiresAtPrevious");
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
