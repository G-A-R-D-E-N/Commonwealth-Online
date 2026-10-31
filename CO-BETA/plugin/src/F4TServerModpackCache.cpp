#include "F4TServerModpackCache.h"

#ifndef F4T_SERVER_MODPACK_CACHE_TESTING
#include "pch.h"
#include "F4TDebugLogging.h"
#include "F4TServerProfileStore.h"
#endif

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
	constexpr std::uint64_t kSchemaVersion = 1;
	constexpr std::size_t kMaximumServers = 64;
	constexpr std::size_t kMaximumBlobBytes = 2 * 1024 * 1024;
	constexpr std::size_t kMaximumFileBytes = 4 * 1024 * 1024;
	constexpr std::size_t kMaximumServerKeyBytes = 260;
	constexpr std::string_view kCacheFileName = "modpack-cache.json";

	std::mutex g_cacheMutex;
	nlohmann::json g_root = nlohmann::json::object();
	bool g_loaded = false;
	std::vector<std::string> g_order;
#ifdef F4T_SERVER_MODPACK_CACHE_TESTING
	std::optional<std::filesystem::path> g_testCachePath;
#endif

#ifdef F4T_SERVER_MODPACK_CACHE_TESTING
	void LogCacheWarning(std::string_view)
	{
	}
#else
	void LogCacheWarning(std::string_view a_message)
	{
		REX::WARN("[ServerModpackCache] {}", a_message);
	}
#endif

	std::filesystem::path CachePath()
	{
#ifdef F4T_SERVER_MODPACK_CACHE_TESTING
		if (g_testCachePath) {
			return *g_testCachePath;
		}
#endif
#ifndef F4T_SERVER_MODPACK_CACHE_TESTING
		return F4T::ServerProfileStore::GetStoreDirectory() / std::string{ kCacheFileName };
#else
		return std::filesystem::path{ kCacheFileName };
#endif
	}

	bool IsValidServerKey(std::string_view a_key) noexcept
	{
		if (a_key.empty() || a_key.size() > kMaximumServerKeyBytes) {
			return false;
		}
		for (const unsigned char character : a_key) {
			if (character < 0x20 || character >= 0x7f) {
				return false;
			}
		}
		return true;
	}

	nlohmann::json EmptyRoot()
	{
		return nlohmann::json{ { "version", kSchemaVersion }, { "servers", nlohmann::json::object() } };
	}

	void ResetRoot()
	{
		g_root = EmptyRoot();
		g_order.clear();
		g_loaded = true;
	}

	void Touch(const std::string& a_key)
	{
		std::erase(g_order, a_key);
		g_order.push_back(a_key);
	}

	void Load()
	{
		if (g_loaded) {
			return;
		}
		g_loaded = true;
		g_root = EmptyRoot();
		try {
			const auto path = CachePath();
			std::error_code error;
			const auto size = std::filesystem::file_size(path, error);
			if (error || size > kMaximumFileBytes) {
				return;
			}
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				return;
			}
			const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
			const auto parsed = nlohmann::json::parse(text, nullptr, false);
			if (parsed.is_discarded() || !parsed.is_object()) {
				return;
			}
			const auto version = parsed.find("version");
			const auto servers = parsed.find("servers");
			if (version == parsed.end() || !version->is_number_unsigned() || version->get<std::uint64_t>() != kSchemaVersion ||
				servers == parsed.end() || !servers->is_object() || servers->size() > kMaximumServers) {
				return;
			}
			for (const auto& [key, value] : servers->items()) {
				if (!IsValidServerKey(key) || !value.is_string()) {
					continue;
				}
				g_root["servers"][key] = value;
				g_order.push_back(key);
			}
		} catch (...) {
			g_root = EmptyRoot();
			g_order.clear();
		}
	}

	bool Save()
	{
		try {
			const auto path = CachePath();
			std::error_code error;
			if (!path.parent_path().empty()) {
				std::filesystem::create_directories(path.parent_path(), error);
				if (error) {
					return false;
				}
			}
			const auto text = g_root.dump();
			if (text.size() > kMaximumFileBytes) {
				return false;
			}
			auto temporary = path;
			temporary += ".tmp";
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output) {
					return false;
				}
				output.write(text.data(), static_cast<std::streamsize>(text.size()));
				output.flush();
				if (!output.good()) {
					output.close();
					std::filesystem::remove(temporary, error);
					return false;
				}
			}
			auto backup = path;
			backup += ".bak";
			std::filesystem::remove(backup, error);
			error.clear();
			std::filesystem::rename(path, backup, error);
			if (!error) {
				error.clear();
				std::filesystem::rename(temporary, path, error);
				if (error) {
					std::error_code restoreError;
					std::filesystem::rename(backup, path, restoreError);
					std::filesystem::remove(temporary, restoreError);
					return false;
				}
				std::filesystem::remove(backup, error);
				return true;
			}
			error.clear();
			std::filesystem::rename(temporary, path, error);
			return !error;
		} catch (...) {
			return false;
		}
	}
}

namespace F4T::ServerModpackCache
{
	std::filesystem::path GetCachePath()
	{
		const std::scoped_lock lock(g_cacheMutex);
		return CachePath();
	}

	std::optional<std::string> Lookup(const std::string& a_serverKey)
	{
		if (!IsValidServerKey(a_serverKey)) {
			return std::nullopt;
		}
		const std::scoped_lock lock(g_cacheMutex);
		Load();
		const auto servers = g_root.find("servers");
		if (servers == g_root.end() || !servers->is_object()) {
			return std::nullopt;
		}
		const auto entry = servers->find(a_serverKey);
		if (entry == servers->end() || !entry->is_string()) {
			return std::nullopt;
		}
		Touch(a_serverKey);
		try {
			const auto& text = entry->get_ref<const std::string&>();
			return text.size() <= kMaximumBlobBytes ? std::optional{ text } : std::nullopt;
		} catch (...) {
			return std::nullopt;
		}
	}

	bool Store(const std::string& a_serverKey, const std::string& a_blob)
	{
		if (!IsValidServerKey(a_serverKey) || a_blob.empty() || a_blob.size() > kMaximumBlobBytes) {
			return false;
		}
		const auto parsed = nlohmann::json::parse(a_blob, nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object()) {
			return false;
		}
		const std::scoped_lock lock(g_cacheMutex);
		Load();
		const auto previousRoot = g_root;
		const auto previousOrder = g_order;
		if (!g_root.contains("servers") || !g_root["servers"].is_object()) {
			g_root["servers"] = nlohmann::json::object();
		}
		auto& servers = g_root["servers"];
		if (!servers.contains(a_serverKey) && servers.size() >= kMaximumServers) {
			std::erase_if(g_order, [&](const std::string& a_key) { return !servers.contains(a_key); });
			servers.erase(g_order.empty() ? servers.begin().key() : g_order.front());
			if (!g_order.empty()) {
				g_order.erase(g_order.begin());
			}
		}
		servers[a_serverKey] = a_blob;
		Touch(a_serverKey);
		if (g_root.dump().size() > kMaximumFileBytes || !Save()) {
			g_root = previousRoot;
			g_order = previousOrder;
			LogCacheWarning("Could not save the modpack check cache; the next join will probe again.");
			return false;
		}
		return true;
	}

	bool Clear(const std::string& a_serverKey)
	{
		if (!IsValidServerKey(a_serverKey)) {
			return false;
		}
		const std::scoped_lock lock(g_cacheMutex);
		Load();
		if (!g_root.contains("servers") || !g_root["servers"].is_object()) {
			return true;
		}
		auto& servers = g_root["servers"];
		if (!servers.contains(a_serverKey)) {
			return true;
		}
		const auto previousRoot = g_root;
		const auto previousOrder = g_order;
		servers.erase(a_serverKey);
		std::erase(g_order, a_serverKey);
		if (!Save()) {
			g_root = previousRoot;
			g_order = previousOrder;
			return false;
		}
		return true;
	}

#ifdef F4T_SERVER_MODPACK_CACHE_TESTING
	void SetCachePathForTesting(std::filesystem::path a_path)
	{
		const std::scoped_lock lock(g_cacheMutex);
		g_testCachePath = std::move(a_path);
		g_root = EmptyRoot();
		g_order.clear();
		g_loaded = false;
	}
#endif
}
