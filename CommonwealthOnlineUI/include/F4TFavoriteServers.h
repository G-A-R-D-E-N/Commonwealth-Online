#pragma once

#include "F4TServerBrowserData.h"

#include <filesystem>
#include <vector>

namespace F4T::ServerBrowser
{
	class FavoriteServersStore
	{
	public:
		static void Load();
		static void Add(const ServerEntry& a_entry);
		static void Remove(const ServerEntry& a_entry);
		static const std::vector<ServerEntry>& Get() { return s_servers; }

	private:
		static void Save();
		static std::filesystem::path GetStorePath();

		static std::vector<ServerEntry> s_servers;
	};
}
