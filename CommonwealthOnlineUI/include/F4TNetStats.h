#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace F4T::NetStats
{
	inline std::mutex g_mutex;
	inline std::string g_latestJson;

	inline void SetLatestJson(std::string_view a_json)
	{
		const std::scoped_lock lock(g_mutex);
		g_latestJson.assign(a_json);
	}

	inline std::string LatestJson()
	{
		const std::scoped_lock lock(g_mutex);
		return g_latestJson;
	}
}
