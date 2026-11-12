#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace F4T::ServerModpackCache
{
	std::filesystem::path GetCachePath();

	std::optional<std::string> Lookup(const std::string& a_serverKey);
	bool Store(const std::string& a_serverKey, const std::string& a_blob);
	bool Clear(const std::string& a_serverKey);

#ifdef F4T_SERVER_MODPACK_CACHE_TESTING
	void SetCachePathForTesting(std::filesystem::path a_path);
#endif
}
