#include "pch.h"

#include "F4TRecentServers.h"

#include "F4TUserPaths.h"

#include <algorithm>
#include <format>
#include <fstream>

namespace F4T::ServerBrowser
{
	std::vector<ServerEntry> RecentServersStore::s_servers;

	namespace
	{
		constexpr const char* kLogPrefix = "[COUI] [RecentServers]";
	}

	std::filesystem::path RecentServersStore::GetStorePath()
	{
		const auto documents = F4T::UserPaths::CommonwealthOnlineDocumentsDirectory();
		if (documents.empty()) {
			return {};
		}
		return documents / "recent-servers.json";
	}

	std::string RecentServersStore::EntryKey(const ServerEntry& a_entry)
	{
		if (!a_entry.host.empty()) {
			const auto port = a_entry.port > 0 ? a_entry.port : 7777;
			return std::format("{}:{}", a_entry.host, port);
		}
		return a_entry.id;
	}

	ServerEntry RecentServersStore::Normalize(ServerEntry a_entry)
	{
		if (a_entry.port <= 0) {
			a_entry.port = 7777;
		}
		if (a_entry.host.empty() && a_entry.id.starts_with("recent:")) {
			const auto rest = a_entry.id.substr(7);
			const auto colon = rest.rfind(':');
			if (colon != std::string::npos) {
				a_entry.host = rest.substr(0, colon);
				try {
					a_entry.port = std::stoi(rest.substr(colon + 1));
				} catch (...) {
					a_entry.port = 7777;
				}
			}
		}
		if (a_entry.id.empty() && !a_entry.host.empty()) {
			a_entry.id = std::format("recent:{}:{}", a_entry.host, a_entry.port);
		}
		if (a_entry.name.empty() && !a_entry.host.empty()) {
			a_entry.name = std::format("{}:{}", a_entry.host, a_entry.port);
		}
		return a_entry;
	}

	void RecentServersStore::Load()
	{
		s_servers.clear();

		const auto path = GetStorePath();
		if (path.empty() || !std::filesystem::exists(path)) {
			return;
		}

		try {
			std::ifstream in(path, std::ios::binary);
			if (!in) {
				REX::WARN("{} Failed to open {}", kLogPrefix, path.string());
				return;
			}

			nlohmann::json root;
			in >> root;

			const nlohmann::json* list = nullptr;
			if (root.is_array()) {
				list = &root;
			} else if (root.is_object() && root.contains("servers") && root.at("servers").is_array()) {
				list = &root.at("servers");
			} else {
				REX::WARN("{} Invalid recent-servers.json shape", kLogPrefix);
				return;
			}

			for (const auto& item : *list) {
				if (!item.is_object()) {
					continue;
				}
				auto entry = Normalize(ServerEntry::FromJson(item));
				if (entry.host.empty() && entry.id.empty()) {
					continue;
				}
				s_servers.push_back(std::move(entry));
				if (s_servers.size() >= kMaxRecent) {
					break;
				}
			}

			REX::INFO("{} Loaded {} recent server(s) from {}", kLogPrefix, s_servers.size(), path.string());
		} catch (const std::exception& ex) {
			REX::WARN("{} Failed to load recent servers: {}", kLogPrefix, ex.what());
			s_servers.clear();
		}
	}

	void RecentServersStore::Save()
	{
		const auto path = GetStorePath();
		if (path.empty()) {
			REX::WARN("{} USERPROFILE unavailable; cannot persist recent servers", kLogPrefix);
			return;
		}

		try {
			std::filesystem::create_directories(path.parent_path());

			nlohmann::json root{
				{ "version", 1 },
				{ "servers", ToJson() }
			};

			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out) {
				REX::WARN("{} Failed to write {}", kLogPrefix, path.string());
				return;
			}
			out << root.dump(2);
			REX::INFO("{} Saved {} recent server(s) to {}", kLogPrefix, s_servers.size(), path.string());
		} catch (const std::exception& ex) {
			REX::WARN("{} Failed to save recent servers: {}", kLogPrefix, ex.what());
		}
	}

	void RecentServersStore::Add(ServerEntry a_entry)
	{
		a_entry = Normalize(std::move(a_entry));
		const auto key = EntryKey(a_entry);
		if (key.empty()) {
			return;
		}

		s_servers.erase(
			std::remove_if(
				s_servers.begin(),
				s_servers.end(),
				[&](const ServerEntry& existing) {
					return EntryKey(existing) == key || (!a_entry.id.empty() && existing.id == a_entry.id);
				}),
			s_servers.end());

		s_servers.insert(s_servers.begin(), std::move(a_entry));
		if (s_servers.size() > kMaxRecent) {
			s_servers.resize(kMaxRecent);
		}

		Save();
	}

	nlohmann::json RecentServersStore::ToJson()
	{
		nlohmann::json servers = nlohmann::json::array();
		for (const auto& entry : s_servers) {
			servers.push_back(entry.ToJson());
		}
		return servers;
	}
}
