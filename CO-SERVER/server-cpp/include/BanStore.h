#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct BanEntry
	{
		std::string ip;
		std::string profileId;
		std::string reason;
		double bannedAt{};
	};

	struct SteamBanEntry
	{
		std::string steamId;
		std::string reason;
		double bannedAt{};
	};

	class BanStore
	{
	public:
		using PostCommitHook = std::function<void()>;

		explicit BanStore(std::filesystem::path a_path, PostCommitHook a_postCommitHook = {});

		[[nodiscard]] std::optional<std::string> TakePersistenceWarning();

		[[nodiscard]] std::optional<BanEntry> Get(std::string_view a_ip) const;
		[[nodiscard]] std::optional<BanEntry> GetProfile(std::string_view a_profileId) const;
		[[nodiscard]] std::vector<BanEntry> List() const;
		[[nodiscard]] BanEntry Ban(std::string_view a_ip, std::string_view a_reason, double a_bannedAt);
		[[nodiscard]] bool Unban(std::string_view a_ip);
		[[nodiscard]] BanEntry BanProfile(std::string_view a_profileId, std::string_view a_reason, double a_bannedAt);
		[[nodiscard]] bool UnbanProfile(std::string_view a_profileId);
		[[nodiscard]] std::optional<SteamBanEntry> GetSteamId(std::string_view a_steamId) const;
		[[nodiscard]] std::vector<SteamBanEntry> ListSteamIds() const;
		[[nodiscard]] SteamBanEntry BanSteamId(std::string_view a_steamId, std::string_view a_reason, double a_bannedAt);
		[[nodiscard]] bool UnbanSteamId(std::string_view a_steamId);

		[[nodiscard]] static std::optional<std::string> NormalizeIpv4(std::string_view a_ip);
		// IPv4 as dotted text; any IPv6 address (or a "prefix::/64") as its /64, since IPv6 hosts rotate addresses.
		[[nodiscard]] static std::optional<std::string> NormalizeAddress(std::string_view a_address);
		[[nodiscard]] static std::optional<std::string> NormalizeProfileId(std::string_view a_profileId);

	private:
		void Load();
		void Save(bool& a_committed);

		std::filesystem::path _path;
		std::vector<BanEntry> _entries;
		std::vector<SteamBanEntry> _steamEntries;
		PostCommitHook _postCommitHook;
		std::optional<std::string> _persistenceWarning;
	};
}
