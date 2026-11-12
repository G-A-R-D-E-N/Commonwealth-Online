#include "pch.h"

#include "F4TFavoriteServers.h"

#include "F4TUserPaths.h"

#include <algorithm>
#include <fstream>

namespace F4T::ServerBrowser
{
	std::vector<ServerEntry> FavoriteServersStore::s_servers;

	namespace
	{
		constexpr const char* kLogPrefix = "[COUI] [FavoriteServers]";
		constexpr std::uintmax_t kMaxFavoriteFileBytes = 256 * 1024;
		constexpr std::size_t kMaxFavoriteServers = 256;

		void QuarantineStore(const std::filesystem::path& a_path, const char* a_reason)
		{
			auto bad = a_path;
			bad += ".bad";
			std::error_code ec;
			std::filesystem::remove(bad, ec);
			std::filesystem::rename(a_path, bad, ec);
			if (ec) {
				REX::WARN("{} Could not move {} aside ({}): {}", kLogPrefix, a_path.string(), a_reason, ec.message());
				return;
			}
			REX::WARN("{} Moved {} aside to {} ({})", kLogPrefix, a_path.string(), bad.string(), a_reason);
		}
	}

	std::filesystem::path FavoriteServersStore::GetStorePath()
	{
		const auto documents = F4T::UserPaths::CommonwealthOnlineDocumentsDirectory();
		if (documents.empty()) {
			return {};
		}
		return documents / "favorite-servers.json";
	}

	void FavoriteServersStore::Load()
	{
		s_servers.clear();

		const auto path = GetStorePath();
		if (path.empty() || !std::filesystem::exists(path)) {
			return;
		}

		try {
			if (std::filesystem::file_size(path) > kMaxFavoriteFileBytes) {
				QuarantineStore(path, "larger than the favorite file limit");
				return;
			}

			nlohmann::json root;
			{
				std::ifstream in(path, std::ios::binary);
				if (!in) {
					REX::WARN("{} Failed to open {}", kLogPrefix, path.string());
					return;
				}
				in >> root;
			}
			if (!root.is_object() || !root.contains("servers") || !root.at("servers").is_array()) {
				QuarantineStore(path, "invalid shape");
				return;
			}

			for (const auto& item : root.at("servers")) {
				if (s_servers.size() >= kMaxFavoriteServers) {
					REX::WARN("{} Stopped loading at {} favorite servers", kLogPrefix, kMaxFavoriteServers);
					break;
				}
				try {
					std::string error;
					if (!ValidateFavoriteServerJson(item, error)) {
						REX::WARN("{} Skipped a favorite: {}", kLogPrefix, error);
						continue;
					}
					auto entry = ServerEntry::FromJson(item);
					if (entry.host.empty() && entry.id.empty()) {
						continue;
					}
					s_servers.push_back(std::move(entry));
				} catch (const std::exception& ex) {
					REX::WARN("{} Skipped a favorite: {}", kLogPrefix, ex.what());
				}
			}

			REX::INFO("{} Loaded {} favorite server(s) from {}", kLogPrefix, s_servers.size(), path.string());
		} catch (const std::exception& ex) {
			REX::WARN("{} Failed to load favorite servers: {}", kLogPrefix, ex.what());
			QuarantineStore(path, "unreadable");
			s_servers.clear();
		}
	}

	void FavoriteServersStore::Save()
	{
		const auto path = GetStorePath();
		if (path.empty()) {
			REX::WARN("{} USERPROFILE unavailable; cannot persist favorite servers", kLogPrefix);
			return;
		}

		try {
			std::filesystem::create_directories(path.parent_path());

			nlohmann::json servers = nlohmann::json::array();
			for (const auto& entry : s_servers) {
				servers.push_back(nlohmann::json{
					{ "id", entry.id },
					{ "name", entry.name },
					{ "host", entry.host },
					{ "port", entry.port }
				});
			}
			const nlohmann::json root{
				{ "version", 1 },
				{ "servers", std::move(servers) }
			};
			const auto text = root.dump(2);

			auto temp = path;
			temp += ".tmp";
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			out << text;
			out.close();
			if (out.fail()) {
				REX::WARN("{} Failed to write {}", kLogPrefix, temp.string());
				return;
			}

			std::error_code ec;
			std::filesystem::rename(temp, path, ec);
			if (ec) {
				REX::WARN("{} Failed to replace {}: {}", kLogPrefix, path.string(), ec.message());
				std::filesystem::remove(temp, ec);
			}
		} catch (const std::exception& ex) {
			REX::WARN("{} Failed to save favorite servers: {}", kLogPrefix, ex.what());
		}
	}

	void FavoriteServersStore::Add(const ServerEntry& a_entry)
	{
		const auto known = std::ranges::any_of(s_servers, [&](const ServerEntry& a_existing) { return IsSameServer(a_existing, a_entry); });
		if (known) {
			return;
		}
		if (s_servers.size() >= kMaxFavoriteServers) {
			REX::WARN("{} Favorite list is full at {} servers; {} is not saved", kLogPrefix, kMaxFavoriteServers, a_entry.host);
			return;
		}
		s_servers.push_back(a_entry);
		Save();
	}

	void FavoriteServersStore::Remove(const ServerEntry& a_entry)
	{
		const auto removed = std::erase_if(s_servers, [&](const ServerEntry& a_existing) { return IsSameServer(a_existing, a_entry); });
		if (removed > 0) {
			Save();
		}
	}
}
