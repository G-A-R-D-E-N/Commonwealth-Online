#pragma once

#include "F4TServerBrowserData.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace F4T::ServerBrowser
{
	class RecentServersStore
	{
	public:
		static constexpr std::size_t kMaxRecent = 8;

		static void Load();
		static void Save();
		static void Add(ServerEntry a_entry);
		static const std::vector<ServerEntry>& Get() { return s_servers; }
		static nlohmann::json ToJson();

	private:
		static std::filesystem::path GetStorePath();
		static ServerEntry Normalize(ServerEntry a_entry);
		static std::string EntryKey(const ServerEntry& a_entry);

		static std::vector<ServerEntry> s_servers;
	};
}
