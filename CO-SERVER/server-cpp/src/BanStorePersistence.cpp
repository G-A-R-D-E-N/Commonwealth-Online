#include "BanStore.h"

#include "F4TSteamId.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace CommonwealthOnline::Server
{
namespace
{
	using Json = nlohmann::json;

	std::string UniqueSuffix()
	{
		static std::atomic_uint64_t counter{ 1 };
		const auto now = static_cast<std::uint64_t>(
			std::chrono::steady_clock::now().time_since_epoch().count());
		return std::to_string(now) + "-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
	}

	#ifdef _WIN32
	std::runtime_error WindowsFailure(std::string_view a_operation, const std::filesystem::path& a_path)
	{
		return std::runtime_error(
			std::string{ a_operation } + " failed for '" + a_path.string() + "' (code " +
			std::to_string(static_cast<unsigned long>(GetLastError())) + ")");
	}

	void WriteSync(const std::filesystem::path& a_path, std::string_view a_contents)
	{
		const HANDLE handle = CreateFileW(
			a_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
		if (handle == INVALID_HANDLE_VALUE) throw WindowsFailure("CreateFileW", a_path);
		try {
			std::size_t written = 0;
			while (written < a_contents.size()) {
				const auto remaining = static_cast<DWORD>((std::min)(
					a_contents.size() - written,
					static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
				DWORD chunk = 0;
				if (!WriteFile(handle, a_contents.data() + written, remaining, &chunk, nullptr) || chunk == 0) {
					throw WindowsFailure("WriteFile", a_path);
				}
				written += chunk;
			}
			if (!FlushFileBuffers(handle)) throw WindowsFailure("FlushFileBuffers", a_path);
		} catch (...) {
			CloseHandle(handle);
			throw;
		}
		CloseHandle(handle);
	}

	void ReplaceFile(
		const std::filesystem::path& a_source,
		const std::filesystem::path& a_destination,
		bool* a_replaced)
	{
		if (!MoveFileExW(
				a_source.c_str(), a_destination.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			throw WindowsFailure("MoveFileExW", a_destination);
		}
		if (a_replaced != nullptr) *a_replaced = true;
	}
#else
	std::runtime_error PosixFailure(std::string_view a_operation, const std::filesystem::path& a_path)
	{
		return std::runtime_error(
			std::string{ a_operation } + " failed for '" + a_path.string() + "' (errno " + std::to_string(errno) + ")");
	}

	void WriteSync(const std::filesystem::path& a_path, std::string_view a_contents)
	{
		const int descriptor = open(a_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (descriptor < 0) throw PosixFailure("open", a_path);
		try {
			std::size_t written = 0;
			while (written < a_contents.size()) {
				const auto count = write(descriptor, a_contents.data() + written, a_contents.size() - written);
				if (count < 0) {
					if (errno == EINTR) continue;
					throw PosixFailure("write", a_path);
				}
				if (count == 0) throw std::runtime_error("write made no progress for bans file");
				written += static_cast<std::size_t>(count);
			}
			if (fsync(descriptor) != 0) throw PosixFailure("fsync", a_path);
		} catch (...) {
			close(descriptor);
			throw;
		}
		if (close(descriptor) != 0) throw PosixFailure("close", a_path);
	}

	void SyncDirectory(const std::filesystem::path& a_directory)
	{
		const int descriptor = open(a_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (descriptor < 0) throw PosixFailure("open directory", a_directory);
		const int result = fsync(descriptor);
		const int savedErrno = errno;
		close(descriptor);
		if (result != 0) {
			errno = savedErrno;
			throw PosixFailure("fsync directory", a_directory);
		}
	}

	void ReplaceFile(
		const std::filesystem::path& a_source,
		const std::filesystem::path& a_destination,
		bool* a_replaced)
	{
		if (rename(a_source.c_str(), a_destination.c_str()) != 0) throw PosixFailure("rename", a_destination);
		if (a_replaced != nullptr) *a_replaced = true;
		if (chmod(a_destination.c_str(), 0600) != 0) throw PosixFailure("chmod", a_destination);
		SyncDirectory(a_destination.parent_path());
	}
#endif
}

void BanStore::Load()
{
	_entries.clear();
	_steamEntries.clear();
	if (!std::filesystem::exists(_path)) return;
	std::ifstream input(_path, std::ios::binary);
	if (!input) throw std::runtime_error("Could not open ban file '" + _path.string() + "'.");
	Json root;
	try { input >> root; } catch (const Json::exception& exception) {
		throw std::runtime_error("Ban file is not valid JSON: " + std::string{ exception.what() });
	}
	if (!root.is_object()) throw std::runtime_error("Ban file has an invalid schema.");
	const auto banned = root.find("banned_ips");
	if (banned == root.end() || !banned->is_array()) throw std::runtime_error("Ban file has an invalid schema.");
	const auto bannedProfiles = root.find("banned_profiles");
	if (bannedProfiles != root.end() && !bannedProfiles->is_array()) throw std::runtime_error("Ban file has an invalid schema.");
	const auto bannedSteamIds = root.find("banned_steam_ids");
	if (bannedSteamIds != root.end() && !bannedSteamIds->is_array()) throw std::runtime_error("Ban file has an invalid schema.");
	std::size_t invalid = 0;
	for (const auto& item : *banned) {
		if (!item.is_object()) { ++invalid; continue; }
		const auto ipIt = item.find("ip");
		if (ipIt == item.end() || !ipIt->is_string()) { ++invalid; continue; }
		const auto normalized = BanStore::NormalizeAddress(ipIt->get_ref<const std::string&>());
		if (!normalized) { ++invalid; continue; }
		std::string reason;
		if (const auto node = item.find("reason"); node != item.end() && node->is_string()) {
			reason = node->get<std::string>();
			if (reason.size() > 1024) reason.resize(1024);
		}
		double bannedAt = 0.0;
		if (const auto node = item.find("bannedAt"); node != item.end() && !node->is_boolean() && node->is_number()) {
			try { const auto value = node->get<double>(); if (std::isfinite(value)) bannedAt = value; } catch (...) {}
		}
		BanEntry entry{ .ip = *normalized, .profileId = {}, .reason = std::move(reason), .bannedAt = bannedAt };
		const auto existing = std::find_if(_entries.begin(), _entries.end(), [&](const BanEntry& candidate) {
			return candidate.profileId.empty() && candidate.ip == entry.ip;
		});
		if (existing == _entries.end()) _entries.push_back(std::move(entry)); else *existing = std::move(entry);
	}
	if (bannedProfiles != root.end()) {
		for (const auto& item : *bannedProfiles) {
			if (!item.is_object()) { ++invalid; continue; }
			const auto profileIt = item.find("profileId");
			if (profileIt == item.end() || !profileIt->is_string()) { ++invalid; continue; }
			const auto normalized = BanStore::NormalizeProfileId(profileIt->get_ref<const std::string&>());
			if (!normalized) { ++invalid; continue; }
			std::string reason;
			if (const auto node = item.find("reason"); node != item.end() && node->is_string()) {
				reason = node->get<std::string>();
				if (reason.size() > 1024) reason.resize(1024);
			}
			double bannedAt = 0.0;
			if (const auto node = item.find("bannedAt"); node != item.end() && !node->is_boolean() && node->is_number()) {
				try { const auto value = node->get<double>(); if (std::isfinite(value)) bannedAt = value; } catch (...) {}
			}
			BanEntry entry{ .ip = {}, .profileId = *normalized, .reason = std::move(reason), .bannedAt = bannedAt };
			const auto existing = std::find_if(_entries.begin(), _entries.end(), [&](const BanEntry& candidate) {
				return !candidate.profileId.empty() && candidate.profileId == entry.profileId;
			});
			if (existing == _entries.end()) _entries.push_back(std::move(entry)); else *existing = std::move(entry);
		}
	}
	if (bannedSteamIds != root.end()) {
		for (const auto& item : *bannedSteamIds) {
			if (!item.is_object()) { ++invalid; continue; }
			const auto steamIt = item.find("steamId");
			if (steamIt == item.end() || !steamIt->is_string() || !F4T::SteamId::ParseIndividual(steamIt->get_ref<const std::string&>())) { ++invalid; continue; }
			std::string reason;
			if (const auto node = item.find("reason"); node != item.end() && node->is_string()) {
				reason = node->get<std::string>();
				if (reason.size() > 1024) reason.resize(1024);
			}
			double bannedAt = 0.0;
			if (const auto node = item.find("bannedAt"); node != item.end() && !node->is_boolean() && node->is_number()) {
				try { const auto value = node->get<double>(); if (std::isfinite(value)) bannedAt = value; } catch (...) {}
			}
			SteamBanEntry entry{ .steamId = steamIt->get<std::string>(), .reason = std::move(reason), .bannedAt = bannedAt };
			const auto existing = std::find_if(_steamEntries.begin(), _steamEntries.end(), [&](const SteamBanEntry& candidate) { return candidate.steamId == entry.steamId; });
			if (existing == _steamEntries.end()) _steamEntries.push_back(std::move(entry)); else *existing = std::move(entry);
		}
	}
	if (invalid != 0) throw std::runtime_error("Ban file contains " + std::to_string(invalid) + " invalid entries.");
}

void BanStore::Save(bool& a_committed)
{
	std::filesystem::create_directories(_path.parent_path());
	Json banned = Json::array();
	Json bannedProfiles = Json::array();
	Json bannedSteamIds = Json::array();
	for (const auto& entry : ListSteamIds()) bannedSteamIds.push_back(Json{{ "steamId", entry.steamId }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }});
	for (const auto& entry : List()) {
		if (!entry.profileId.empty()) bannedProfiles.push_back(Json{{ "profileId", entry.profileId }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }});
		else banned.push_back(Json{{ "ip", entry.ip }, { "reason", entry.reason }, { "bannedAt", entry.bannedAt }});
	}
	const auto tempPath = _path.parent_path() /
		("." + _path.filename().string() + "." + UniqueSuffix() + ".tmp");
	try {
		WriteSync(tempPath, Json{ { "banned_ips", std::move(banned) }, { "banned_profiles", std::move(bannedProfiles) }, { "banned_steam_ids", std::move(bannedSteamIds) } }.dump(2) + "\n");
		ReplaceFile(tempPath, _path, &a_committed);
		if (a_committed && _postCommitHook) {
			_postCommitHook();
		}
	} catch (...) {
		std::error_code ignored;
		std::filesystem::remove(tempPath, ignored);
		if (a_committed) {
			try {
				throw;
			} catch (const std::exception& exception) {
				_persistenceWarning = exception.what();
			} catch (...) {
				_persistenceWarning = "Ban state committed with an unspecified post-commit warning.";
			}
			return;
		}
		throw;
	}
}
}
