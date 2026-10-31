#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace F4T::ServerBrowser
{
	struct ServerEntry
	{
		std::string id;
		std::string name;
		std::string region;
		std::string worldspace;
		std::string mode;
		std::string worldMode;
		std::string status;
		std::string description;
		std::string host;
		std::string modlist;
		std::string discord;
		std::string lastSeen;
		std::string reachability;
		std::string reachabilityReason;
		std::vector<std::string> tags;
		int         port{ 0 };
		int         players{ 0 };
		int         maxPlayers{ 0 };
		int         ping{ 0 };
		int         reachabilityMs{ 0 };
		bool        modded{ false };
		bool        passworded{ false };
		bool        favorite{ false };
		bool        live{ false };

		nlohmann::json ToJson() const;
		static ServerEntry FromJson(const nlohmann::json& a_json);
	};

	struct BrowserFilters
	{
		std::string search;
		std::string region{ "Any Region" };
		std::string mode{ "Any Mode" };
		std::string worldspace{ "Any Worldspace" };
		std::string ping{ "Any" };
		bool        showFull{ false };
		bool        hasPassword{ false };
		bool        modded{ false };
	};

	class ServerBrowserData
	{
	public:
		void SetFilters(const BrowserFilters& a_filters);
		void ResetFilters();
		void ApplyFilters();
		void SetAllServers(std::vector<ServerEntry> a_servers);
		void SetLocalServers(std::vector<ServerEntry> a_servers);
		void ToggleFavorite(const std::string& a_serverId);
		const ServerEntry* FindServer(const std::string& a_serverId) const;
		const ServerEntry* FindLocalServer(const std::string& a_serverId) const;
		const std::vector<ServerEntry>& GetAllServers() const { return m_allServers; }
		const std::vector<ServerEntry>& GetLocalServers() const { return m_localServers; }
		const std::vector<ServerEntry>& GetFilteredServers() const { return m_filteredServers; }
		const BrowserFilters& GetFilters() const { return m_filters; }
		std::string GetConnectionStatus() const { return m_connectionStatus; }
		void SetConnectionStatus(std::string a_status) { m_connectionStatus = std::move(a_status); }
		bool IsRefreshing() const { return m_isRefreshing; }
		void SetRefreshing(bool a_refreshing) { m_isRefreshing = a_refreshing; }
		nlohmann::json ToServerListJson() const;
		nlohmann::json ToAllServersJson() const;
		static std::vector<ServerEntry> ParseDirectoryServers(const nlohmann::json& a_root, std::string& a_error);
		static std::string UserFacingDirectoryError(const std::string& a_error);

		struct DirectoryAttempt
		{
			bool transportOk{ false };
			std::string body;
			std::string transportError;
		};

		struct DirectoryAttemptResult
		{
			bool failed{ false };
			bool validList{ false };
			std::string error;
			std::vector<ServerEntry> servers;
		};

		static DirectoryAttemptResult EvaluateDirectoryAttempt(const DirectoryAttempt& a_attempt);
		static std::vector<ServerEntry> MergeDirectoryLists(const std::vector<std::vector<ServerEntry>>& a_lists);

		struct DirectoryQueryResult
		{
			bool cancelled{ false };
			bool success{ false };
			std::string error;
			std::vector<ServerEntry> servers;
		};

		static DirectoryQueryResult QueryDirectoryUrls(
			std::span<const char* const> a_urls,
			const std::function<DirectoryAttempt(const char* a_url, bool& a_cancelled)>& a_fetch);

	private:
		bool PassesFilters(const ServerEntry& a_entry) const;

		std::vector<ServerEntry> m_allServers;
		std::vector<ServerEntry> m_localServers;
		std::vector<ServerEntry> m_filteredServers;
		BrowserFilters           m_filters;
		std::string              m_connectionStatus{ "Online" };
		bool                     m_isRefreshing{ false };
	};
}
