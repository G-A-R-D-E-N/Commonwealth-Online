#include "F4TSaveLineageStore.h"

#include "F4TContentManifest.h"
#include "F4TServerProfileStore.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
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
	constexpr std::size_t kMaxAttestedSavesPerServer = 32;
	constexpr std::size_t kMaxServerKeys = 64;
	constexpr std::size_t kServerSecretHexLength = 32;
	constexpr std::uintmax_t kMaxLineageFileBytes = 1024 * 1024;

	bool IsServerSecret(std::string_view a_value) noexcept
	{
		if (a_value.size() != kServerSecretHexLength) {
			return false;
		}
		for (const char ch : a_value) {
			if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
				return false;
			}
		}
		return true;
	}

	std::mutex g_storeMutex;

	std::filesystem::path GetLineagePath()
	{
		auto directory = F4T::ServerProfileStore::GetStoreDirectory();
		if (directory.empty()) {
			return {};
		}
		return directory / "save-lineage.json";
	}

	std::filesystem::path GetBackupPath(const std::filesystem::path& a_path)
	{
		auto backup = a_path;
		backup += ".bak";
		return backup;
	}

	bool TryLoadLineageFile(const std::filesystem::path& a_path, nlohmann::json& a_out)
	{
		std::error_code ec;
		const auto fileSize = std::filesystem::file_size(a_path, ec);
		if (ec || fileSize > kMaxLineageFileBytes) {
			return false;
		}

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

	nlohmann::json LoadLineageFile()
	{
		const auto path = GetLineagePath();
		nlohmann::json loaded;
		if (!path.empty() && (TryLoadLineageFile(path, loaded) || TryLoadLineageFile(GetBackupPath(path), loaded))) {
			return loaded;
		}
		return nlohmann::json::object();
	}

	bool SaveLineageFile(const nlohmann::json& a_lineage)
	{
		const auto path = GetLineagePath();
		if (path.empty()) {
			return false;
		}

		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) {
			return false;
		}

		auto tempPath = path;
		tempPath += ".tmp";
		const auto backupPath = GetBackupPath(path);
		{
			std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				return false;
			}
			output << a_lineage.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
			output.close();
			if (output.fail()) {
				std::filesystem::remove(tempPath, ec);
				return false;
			}
		}

		if (!CommitFileToDisk(tempPath)) {
			std::filesystem::remove(tempPath, ec);
			return false;
		}

		if (std::filesystem::exists(path, ec)) {
			nlohmann::json current;
			if (TryLoadLineageFile(path, current)) {
				std::filesystem::remove(backupPath, ec);
				ec.clear();
				std::filesystem::rename(path, backupPath, ec);
				if (ec) {
					std::filesystem::remove(tempPath, ec);
					return false;
				}
			} else {
				std::filesystem::remove(path, ec);
				if (ec) {
					std::filesystem::remove(tempPath, ec);
					return false;
				}
			}
		}

		ec.clear();
		std::filesystem::rename(tempPath, path, ec);
		if (ec) {
			std::filesystem::remove(tempPath, ec);
			return false;
		}
		return true;
	}

	nlohmann::json& ServerAttestedSaves(nlohmann::json& a_lineage, const std::string& a_serverKey)
	{
		if (!a_lineage.contains("servers") || !a_lineage["servers"].is_object()) {
			a_lineage["servers"] = nlohmann::json::object();
		}
		auto& server = a_lineage["servers"][a_serverKey];
		if (!server.is_object()) {
			server = nlohmann::json::object();
		}
		if (!server.contains("attested") || !server["attested"].is_array()) {
			server["attested"] = nlohmann::json::array();
		}
		return server["attested"];
	}

	bool TryParseAttestedSave(const nlohmann::json& a_entry, F4T::SaveProfileLogic::AttestedSave& a_out)
	{
		if (!a_entry.is_object()) {
			return false;
		}

		const auto saveFile = a_entry.find("saveFile");
		const auto sha256Hex = a_entry.find("sha256");
		const auto attestedAt = a_entry.find("attestedAt");
		if (saveFile == a_entry.end() || !saveFile->is_string() || saveFile->get<std::string>().empty() ||
			sha256Hex == a_entry.end() || !sha256Hex->is_string() ||
			!F4T::ContentManifest::IsValidFingerprint(sha256Hex->get<std::string>()) ||
			attestedAt == a_entry.end() || !attestedAt->is_number_unsigned()) {
			return false;
		}

		a_out.saveFile = saveFile->get<std::string>();
		a_out.sha256Hex = sha256Hex->get<std::string>();
		a_out.attestedAtUnix = attestedAt->get<std::uint64_t>();
		return true;
	}

	std::uint64_t NewestAttestedAt(const nlohmann::json& a_server)
	{
		std::uint64_t newest = 0;
		const auto attested = a_server.find("attested");
		if (attested == a_server.end() || !attested->is_array()) {
			return newest;
		}
		for (const auto& entry : *attested) {
			F4T::SaveProfileLogic::AttestedSave save;
			if (TryParseAttestedSave(entry, save) && save.attestedAtUnix > newest) {
				newest = save.attestedAtUnix;
			}
		}
		return newest;
	}

	void TrimServerKeys(nlohmann::json& a_lineage, const std::string& a_serverKey)
	{
		auto& servers = a_lineage["servers"];
		while (servers.size() > kMaxServerKeys) {
			auto oldest = servers.end();
			std::uint64_t oldestAt = 0;
			for (auto server = servers.begin(); server != servers.end(); ++server) {
				if (server.key() == a_serverKey) {
					continue;
				}
				const auto newest = NewestAttestedAt(server.value());
				if (oldest == servers.end() || newest < oldestAt) {
					oldest = server;
					oldestAt = newest;
				}
			}
			if (oldest == servers.end()) {
				return;
			}
			const auto oldestKey = oldest.key();
			servers.erase(oldestKey);
		}
	}
}

namespace F4T::SaveLineageStore
{
	std::vector<F4T::SaveProfileLogic::AttestedSave> LookupAttestedSaves(const std::string& a_serverKey)
	{
		std::vector<F4T::SaveProfileLogic::AttestedSave> saves;
		try {
			const std::scoped_lock lock(g_storeMutex);
			const auto lineage = LoadLineageFile();
			const auto servers = lineage.find("servers");
			if (servers == lineage.end() || !servers->is_object()) {
				return saves;
			}

			const auto server = servers->find(a_serverKey);
			if (server == servers->end() || !server->is_object()) {
				return saves;
			}

			const auto attested = server->find("attested");
			if (attested == server->end() || !attested->is_array()) {
				return saves;
			}

			for (const auto& entry : *attested) {
				F4T::SaveProfileLogic::AttestedSave save;
				if (TryParseAttestedSave(entry, save)) {
					saves.push_back(std::move(save));
				}
			}
			if (saves.size() > kMaxAttestedSavesPerServer) {
				saves.erase(saves.begin(), saves.end() - static_cast<std::ptrdiff_t>(kMaxAttestedSavesPerServer));
			}
			return saves;
		} catch (...) {
			return {};
		}
	}

	bool RecordAttestedSave(const std::string& a_serverKey, const F4T::SaveProfileLogic::AttestedSave& a_save)
	{
		if (a_serverKey.empty() || a_save.saveFile.empty() || !F4T::ContentManifest::IsValidFingerprint(a_save.sha256Hex)) {
			return false;
		}

		try {
			const std::scoped_lock lock(g_storeMutex);
			auto lineage = LoadLineageFile();
			auto& attested = ServerAttestedSaves(lineage, a_serverKey);
			nlohmann::json kept = nlohmann::json::array();
			for (const auto& entry : attested) {
				F4T::SaveProfileLogic::AttestedSave existing;
				if (TryParseAttestedSave(entry, existing) &&
					!F4T::SaveProfileLogic::EqualsIgnoreCase(existing.saveFile, a_save.saveFile)) {
					kept.push_back(entry);
				}
			}
			kept.push_back(nlohmann::json{
				{ "saveFile", a_save.saveFile },
				{ "sha256", a_save.sha256Hex },
				{ "attestedAt", a_save.attestedAtUnix } });
			while (kept.size() > kMaxAttestedSavesPerServer) {
				kept.erase(kept.begin());
			}
			attested = std::move(kept);
			TrimServerKeys(lineage, a_serverKey);
			return SaveLineageFile(lineage);
		} catch (...) {
			return false;
		}
	}

	std::optional<std::string> LookupServerSecret(const std::string& a_serverKey)
	{
		if (a_serverKey.empty()) {
			return std::nullopt;
		}
		try {
			const std::scoped_lock lock(g_storeMutex);
			const auto lineage = LoadLineageFile();
			const auto servers = lineage.find("servers");
			if (servers == lineage.end() || !servers->is_object()) {
				return std::nullopt;
			}
			const auto server = servers->find(a_serverKey);
			if (server == servers->end() || !server->is_object()) {
				return std::nullopt;
			}
			const auto secret = server->find("secret");
			if (secret == server->end() || !secret->is_string() || secret->get_ref<const std::string&>().empty()) {
				return std::nullopt;
			}
			return secret->get<std::string>();
		} catch (...) {
			return std::nullopt;
		}
	}

	bool StoreServerSecret(const std::string& a_serverKey, const std::string& a_serverSecret)
	{
		if (a_serverKey.empty() || !IsServerSecret(a_serverSecret)) {
			return false;
		}
		try {
			const std::scoped_lock lock(g_storeMutex);
			auto lineage = LoadLineageFile();
			(void)ServerAttestedSaves(lineage, a_serverKey);
			auto& server = lineage["servers"][a_serverKey];
			if (server.contains("secret") && server["secret"] == a_serverSecret) {
				return true;
			}
			server["secret"] = a_serverSecret;
			return SaveLineageFile(lineage);
		} catch (...) {
			return false;
		}
	}
}
