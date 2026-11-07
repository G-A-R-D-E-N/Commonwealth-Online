#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace F4T::ServerBrowser
{
	namespace ReachabilityState
	{
		inline constexpr std::string_view kChecking = "checking";
		inline constexpr std::string_view kAnswering = "answering";
		inline constexpr std::string_view kNoAnswer = "no_answer";
		inline constexpr std::string_view kUnchecked = "unchecked";
	}

	struct ReachabilityResult
	{
		std::string state;
		std::string reason;
		int         milliseconds{};
	};

	struct ReachabilityUpdate
	{
		std::string        host;
		int                port{};
		std::uint64_t      epoch{};
		ReachabilityResult result;
	};

	class ReachabilityCache
	{
	public:
		static constexpr double kCooldownSeconds = 30.0;

		void                              Record(const std::string& a_host, int a_port, ReachabilityResult a_result, double a_nowSeconds);
		std::optional<ReachabilityResult> Lookup(const std::string& a_host, int a_port, double a_nowSeconds) const;
		void                              Clear(const std::string& a_host, int a_port);
		void                              Reset();
		void                              NoteAnswering(const std::string& a_host, int a_port, ReachabilityResult a_result, double a_nowSeconds);
		std::uint64_t                     Epoch(const std::string& a_host, int a_port) const;
		bool                              IsCurrent(const std::string& a_host, int a_port, std::uint64_t a_epoch) const;
		std::size_t                       Size() const { return m_entries.size(); }

	private:
		struct Entry
		{
			ReachabilityResult result;
			double              recordedAtSeconds{};
		};

		std::unordered_map<std::string, Entry>         m_entries;
		std::unordered_map<std::string, std::uint64_t> m_epochs;
	};

	bool JoinFailureProvesAnswer(std::string_view a_reason);

	inline constexpr std::string_view kJoinFailureBadgeReason = "the last check did not match the join";

	struct JoinFailureBadge
	{
		bool               answered{};
		ReachabilityResult result;
	};

	JoinFailureBadge JoinFailureBadgeFor(std::string_view a_reason);

	std::string AddressKey(std::string_view a_host, int a_port);

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

	void NormalizeListedEntries(std::vector<ServerEntry>& a_servers);

	ReachabilityResult PreserveAnsweredMilliseconds(const ServerEntry* a_listed, ReachabilityResult a_badge);

	ReachabilityResult CachedOrJoinFailureBadge(std::optional<ReachabilityResult> a_cached);

	std::string TrimAsciiWhitespace(std::string_view a_text);

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
		bool SetReachability(const std::string& a_host, int a_port, const ReachabilityResult& a_result);
		std::size_t ClearCheckingReachability(std::string_view a_reason);
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
