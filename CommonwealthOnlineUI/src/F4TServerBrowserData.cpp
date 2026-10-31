#ifndef F4T_SERVER_BROWSER_DATA_TESTING
#include "pch.h"
#endif

#include "F4TServerBrowserData.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <format>

namespace F4T::ServerBrowser
{
	namespace
	{
		constexpr std::size_t kMaxDirectoryServers = 512;
		constexpr std::size_t kMaxServerIdLength = 256;
		constexpr std::size_t kMaxServerNameLength = 128;
		constexpr std::size_t kMaxServerDescriptionLength = 2048;
		constexpr std::size_t kMaxServerHostLength = 255;
		constexpr std::size_t kMaxServerModlistLength = 4096;
		constexpr std::size_t kMaxServerTags = 32;
		constexpr std::size_t kMaxServerTagLength = 64;
		constexpr const char* kDirectoryUnavailableMessage = "The server directory is unavailable right now. Use Direct Connect or the Local tab to join a server.";

		bool ValidateStringField(
			const nlohmann::json& a_json,
			const char* a_field,
			std::size_t a_maxLength,
			std::string& a_error)
		{
			if (!a_json.contains(a_field)) {
				return true;
			}

			const auto& value = a_json.at(a_field);
			if (!value.is_string()) {
				a_error = std::format("Server directory field '{}' must be a string.", a_field);
				return false;
			}
			if (value.get_ref<const std::string&>().size() > a_maxLength) {
				a_error = std::format("Server directory field '{}' is too long.", a_field);
				return false;
			}
			return true;
		}

		bool ValidateIntegerField(
			const nlohmann::json& a_json,
			const char* a_field,
			std::int64_t a_min,
			std::int64_t a_max,
			std::string& a_error)
		{
			if (!a_json.contains(a_field)) {
				return true;
			}

			const auto& value = a_json.at(a_field);
			if (!value.is_number_integer()) {
				a_error = std::format("Server directory field '{}' must be an integer.", a_field);
				return false;
			}
			const auto number = value.get<std::int64_t>();
			if (number < a_min || number > a_max) {
				a_error = std::format("Server directory field '{}' is outside its supported range.", a_field);
				return false;
			}
			return true;
		}

		bool ValidateServerEntryJson(const nlohmann::json& a_json, std::string& a_error)
		{
			if (!ValidateIntegerField(a_json, "port", 1, 65535, a_error) ||
				!ValidateIntegerField(a_json, "players", 0, 256, a_error) ||
				!ValidateIntegerField(a_json, "maxPlayers", 0, 256, a_error) ||
				!ValidateIntegerField(a_json, "ping", 0, 60000, a_error)) {
				return false;
			}
			if (!ValidateStringField(a_json, "id", kMaxServerIdLength, a_error) ||
				!ValidateStringField(a_json, "name", kMaxServerNameLength, a_error) ||
				!ValidateStringField(a_json, "region", 64, a_error) ||
				!ValidateStringField(a_json, "worldspace", 64, a_error) ||
				!ValidateStringField(a_json, "mode", 64, a_error) ||
				!ValidateStringField(a_json, "worldMode", 64, a_error) ||
				!ValidateStringField(a_json, "world_mode", 64, a_error) ||
				!ValidateStringField(a_json, "status", 64, a_error) ||
				!ValidateStringField(a_json, "description", kMaxServerDescriptionLength, a_error) ||
				!ValidateStringField(a_json, "host", kMaxServerHostLength, a_error) ||
				!ValidateStringField(a_json, "modlist", kMaxServerModlistLength, a_error) ||
				!ValidateStringField(a_json, "discord", 512, a_error)) {
				return false;
			}

			if (!a_json.contains("tags")) {
				return true;
			}

			const auto& tags = a_json.at("tags");
			if (!tags.is_array() || tags.size() > kMaxServerTags) {
				a_error = "Server directory tags are invalid or too numerous.";
				return false;
			}
			for (const auto& tag : tags) {
				if (!tag.is_string() || tag.get_ref<const std::string&>().size() > kMaxServerTagLength) {
					a_error = "Server directory tag is invalid or too long.";
					return false;
				}
			}
			return true;
		}

		bool ContainsIgnoreCase(std::string_view a_haystack, std::string_view a_needle)
		{
			if (a_needle.empty()) {
				return true;
			}

			std::string haystackLower(a_haystack);
			std::string needleLower(a_needle);
			std::ranges::transform(haystackLower, haystackLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::ranges::transform(needleLower, needleLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return haystackLower.find(needleLower) != std::string::npos;
		}
	}

	nlohmann::json ServerEntry::ToJson() const
	{
		auto json = nlohmann::json{
			{ "id", id },
			{ "name", name },
			{ "region", region },
			{ "players", players },
			{ "maxPlayers", maxPlayers },
			{ "ping", ping },
			{ "worldspace", worldspace },
			{ "mode", mode },
			{ "worldMode", worldMode },
			{ "status", status },
			{ "modded", modded },
			{ "passworded", passworded },
			{ "favorite", favorite },
			{ "description", description },
			{ "tags", tags }
		};

		if (!host.empty()) {
			json["host"] = host;
		}
		if (port > 0) {
			json["port"] = port;
		}
		if (!modlist.empty()) {
			json["modlist"] = modlist;
		}
		if (!discord.empty()) {
			json["discord"] = discord;
		}

		return json;
	}

	ServerEntry ServerEntry::FromJson(const nlohmann::json& a_json)
	{
		ServerEntry entry;
		entry.id = a_json.value("id", "");
		entry.name = a_json.value("name", "");
		entry.region = a_json.value("region", "");
		entry.worldspace = a_json.value("worldspace", "");
		entry.mode = a_json.value("mode", "");
		entry.worldMode = a_json.value("worldMode", "");
		if (entry.worldMode.empty()) {
			entry.worldMode = a_json.value("world_mode", "");
		}
		entry.status = a_json.value("status", "");
		entry.description = a_json.value("description", "");
		entry.host = a_json.value("host", "");
		entry.modlist = a_json.value("modlist", "");
		entry.discord = a_json.value("discord", "");
		entry.port = a_json.value("port", 0);
		entry.players = a_json.value("players", 0);
		entry.maxPlayers = a_json.value("maxPlayers", 0);
		entry.ping = a_json.value("ping", 0);
		entry.favorite = a_json.value("favorite", false);

		if (a_json.contains("passworded")) {
			entry.passworded = a_json.value("passworded", false);
		} else {
			entry.passworded = a_json.value("password", false);
		}

		if (a_json.contains("modded")) {
			entry.modded = a_json.value("modded", false);
		} else {
			entry.modded = !entry.modlist.empty();
		}

		if (a_json.contains("tags") && a_json.at("tags").is_array()) {
			for (const auto& tag : a_json.at("tags")) {
				if (tag.is_string()) {
					entry.tags.push_back(tag.get<std::string>());
				}
			}
		}

		return entry;
	}

	void ServerBrowserData::SetFilters(const BrowserFilters& a_filters)
	{
		m_filters = a_filters;
		ApplyFilters();
	}

	void ServerBrowserData::ResetFilters()
	{
		m_filters = BrowserFilters{};
		ApplyFilters();
	}

	bool ServerBrowserData::PassesFilters(const ServerEntry& a_entry) const
	{
		if (!m_filters.search.empty() &&
			!ContainsIgnoreCase(a_entry.name, m_filters.search) &&
			!ContainsIgnoreCase(a_entry.host, m_filters.search) &&
			!ContainsIgnoreCase(a_entry.description, m_filters.search)) {
			return false;
		}
		if (m_filters.region != "Any Region" && a_entry.region != m_filters.region) {
			return false;
		}
		if (m_filters.mode != "Any Mode" && a_entry.mode != m_filters.mode) {
			return false;
		}
		if (m_filters.worldspace != "Any Worldspace" && a_entry.worldspace != m_filters.worldspace) {
			return false;
		}
		if (m_filters.ping == "< 50 ms" && (a_entry.ping <= 0 || a_entry.ping >= 50)) {
			return false;
		}
		if (m_filters.ping == "< 100 ms" && (a_entry.ping <= 0 || a_entry.ping >= 100)) {
			return false;
		}
		if (m_filters.ping == "< 150 ms" && (a_entry.ping <= 0 || a_entry.ping >= 150)) {
			return false;
		}
		if (!m_filters.showFull && a_entry.status == "Full") {
			return false;
		}
		if (m_filters.hasPassword && !a_entry.passworded) {
			return false;
		}
		if (m_filters.modded && !a_entry.modded) {
			return false;
		}
		return true;
	}

	void ServerBrowserData::ApplyFilters()
	{
		m_filteredServers.clear();
		for (const auto& entry : m_allServers) {
			if (PassesFilters(entry)) {
				m_filteredServers.push_back(entry);
			}
		}
	}

	void ServerBrowserData::SetAllServers(std::vector<ServerEntry> a_servers)
	{
		m_allServers = std::move(a_servers);
		ApplyFilters();
	}

	void ServerBrowserData::SetLocalServers(std::vector<ServerEntry> a_servers)
	{
		m_localServers = std::move(a_servers);
	}

	void ServerBrowserData::ToggleFavorite(const std::string& a_serverId)
	{
		for (auto& entry : m_allServers) {
			if (entry.id == a_serverId) {
				entry.favorite = !entry.favorite;
				break;
			}
		}
		ApplyFilters();
	}

	const ServerEntry* ServerBrowserData::FindServer(const std::string& a_serverId) const
	{
		for (const auto& entry : m_allServers) {
			if (entry.id == a_serverId) {
				return std::addressof(entry);
			}
		}
		return nullptr;
	}

	const ServerEntry* ServerBrowserData::FindLocalServer(const std::string& a_serverId) const
	{
		for (const auto& entry : m_localServers) {
			if (entry.id == a_serverId) {
				return std::addressof(entry);
			}
		}
		return nullptr;
	}

	nlohmann::json ServerBrowserData::ToServerListJson() const
	{
		nlohmann::json servers = nlohmann::json::array();
		for (const auto& entry : m_filteredServers) {
			servers.push_back(entry.ToJson());
		}
		return servers;
	}

	nlohmann::json ServerBrowserData::ToAllServersJson() const
	{
		nlohmann::json servers = nlohmann::json::array();
		for (const auto& entry : m_allServers) {
			servers.push_back(entry.ToJson());
		}
		return servers;
	}

	std::vector<ServerEntry> ServerBrowserData::ParseDirectoryServers(const nlohmann::json& a_root, std::string& a_error)
	{
		a_error.clear();

		const nlohmann::json* serversNode = nullptr;
		if (a_root.is_array()) {
			serversNode = &a_root;
		} else if (a_root.is_object() && a_root.contains("servers") && a_root.at("servers").is_array()) {
			serversNode = &a_root.at("servers");
		} else {
			a_error = "Server directory JSON must contain a servers array.";
			return {};
		}

		if (serversNode->size() > kMaxDirectoryServers) {
			a_error = "Server directory contains too many servers.";
			return {};
		}

		std::vector<ServerEntry> servers;
		servers.reserve(serversNode->size());
		for (const auto& item : *serversNode) {
			if (!item.is_object()) {
				a_error = "Server directory contains a non-object entry.";
				return {};
			}
			if (!ValidateServerEntryJson(item, a_error)) {
				return {};
			}

			auto entry = ServerEntry::FromJson(item);
			if (entry.id.empty() && !entry.host.empty()) {
				entry.id = std::format("{}:{}", entry.host, entry.port > 0 ? entry.port : 7777);
			}
			if (entry.name.empty() && !entry.host.empty()) {
				entry.name = entry.host;
			}
			if (entry.status.empty()) {
				entry.status = "Open";
			}
			if (entry.port <= 0) {
				entry.port = 7777;
			}
			if (entry.host.empty()) {
				a_error = "Server directory entry is missing a host.";
				return {};
			}

			servers.push_back(std::move(entry));
		}

		return servers;
	}

	std::string ServerBrowserData::UserFacingDirectoryError(const std::string& a_error)
	{
		if (a_error.empty()) {
			return {};
		}
		return kDirectoryUnavailableMessage;
	}

	ServerBrowserData::DirectoryAttemptResult ServerBrowserData::EvaluateDirectoryAttempt(const DirectoryAttempt& a_attempt)
	{
		DirectoryAttemptResult result;
		if (!a_attempt.transportOk) {
			result.failed = true;
			result.error = a_attempt.transportError.empty() ? "Failed to download server directory." : a_attempt.transportError;
			return result;
		}

		try {
			const auto parsed = nlohmann::json::parse(a_attempt.body.empty() ? "{}" : a_attempt.body);
			auto servers = ParseDirectoryServers(parsed, result.error);
			if (!result.error.empty()) {
				result.failed = true;
				return result;
			}
			result.validList = true;
			result.servers = std::move(servers);
			return result;
		} catch (const std::exception& ex) {
			result.failed = true;
			result.error = std::format("Failed to parse server directory: {}", ex.what());
			return result;
		}
	}

	ServerBrowserData::DirectorySelection ServerBrowserData::AdvanceDirectorySelection(
		const DirectorySelection& a_current,
		const DirectoryAttemptResult& a_attempt)
	{
		auto next = a_current;
		if (next.settled) {
			return next;
		}
		if (a_attempt.failed || !a_attempt.validList) {
			if (!a_attempt.error.empty()) {
				next.error = a_attempt.error;
			} else if (next.error.empty()) {
				next.error = "Failed to download server directory.";
			}
			return next;
		}

		next.sawValidList = true;
		if (a_attempt.servers.empty()) {
			return next;
		}

		next.success = true;
		next.settled = true;
		next.error.clear();
		next.servers = a_attempt.servers;
		return next;
	}

	ServerBrowserData::DirectorySelection ServerBrowserData::FinishDirectorySelection(DirectorySelection a_current)
	{
		if (a_current.settled) {
			a_current.success = true;
			a_current.error.clear();
			return a_current;
		}
		if (a_current.sawValidList) {
			a_current.success = true;
			a_current.error.clear();
			a_current.servers.clear();
			return a_current;
		}
		a_current.success = false;
		a_current.servers.clear();
		if (a_current.error.empty()) {
			a_current.error = "Failed to download server directory.";
		}
		return a_current;
	}

	ServerBrowserData::DirectoryQueryResult ServerBrowserData::QueryDirectoryUrls(
		std::span<const char* const> a_urls,
		const std::function<DirectoryAttempt(const char* a_url, bool& a_cancelled)>& a_fetch)
	{
		DirectoryQueryResult result;
		DirectorySelection selection;
		std::string attemptNotes;
		for (const char* url : a_urls) {
			bool cancelled = false;
			const auto attempt = a_fetch(url, cancelled);
			if (cancelled) {
				result.cancelled = true;
				return result;
			}

			const auto evaluated = EvaluateDirectoryAttempt(attempt);
			if (evaluated.failed) {
				if (!attemptNotes.empty()) {
					attemptNotes += "; ";
				}
				attemptNotes += std::format("{} ({})", url, evaluated.error);
			}
			selection = AdvanceDirectorySelection(selection, evaluated);
			if (selection.settled) {
				break;
			}
		}

		selection = FinishDirectorySelection(std::move(selection));
		result.success = selection.success;
		result.servers = std::move(selection.servers);
		result.error = !attemptNotes.empty() ? std::move(attemptNotes) : std::move(selection.error);
		return result;
	}
}
