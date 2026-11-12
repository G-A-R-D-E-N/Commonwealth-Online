#include "BanStore.h"
#include "SteamAdmin.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using CommonwealthOnline::Server::AdminCommandHooks;
	using CommonwealthOnline::Server::BanStore;
	using CommonwealthOnline::Server::SteamAdmin;
	using CommonwealthOnline::Server::SteamAdminConnection;
	using CommonwealthOnline::Server::SteamAdminDependencies;
	using CommonwealthOnline::Server::SteamIdSource;

	constexpr std::string_view kProfileId = "profile-steam-admin";
	constexpr std::string_view kTunnelSteamId = "76561198000000666";
	constexpr std::string_view kWebApiSteamId = "76561198000000042";

	std::filesystem::path ScratchDirectory()
	{
		const auto suffix = std::to_string(
			static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
		return std::filesystem::temp_directory_path() / ("co-steam-admin-tests-" + suffix);
	}

	SteamAdminDependencies MakeDependencies(BanStore& a_bans, std::function<double()> a_clock)
	{
		SteamAdminDependencies dependencies;
		dependencies.bans = &a_bans;
		dependencies.connections = [] { return std::vector<SteamAdminConnection>{}; };
		dependencies.endSession = [](std::uint32_t, std::string_view, std::string_view) {};
		dependencies.withPersistenceWarning = [](std::string a_message, std::optional<std::string>) { return a_message; };
		dependencies.unixTimeSeconds = std::move(a_clock);
		return dependencies;
	}
}

int main()
{
	const auto scratch = ScratchDirectory();
	std::filesystem::create_directories(scratch);

	{
		BanStore bans(scratch / "bans-all.json");
		auto admin = SteamAdmin::Create(nlohmann::json{}, MakeDependencies(bans, [] { return 200.0; }));
		assert(admin);
		admin->RecordIdentity(1, kProfileId, "Tester", kTunnelSteamId, SteamIdSource::SteamTunnel, 100.0);
		admin->RecordIdentity(2, kProfileId, "Tester", kWebApiSteamId, SteamIdSource::WebApi, 100.0);
		assert(admin->ProfileSteamIds(kProfileId).size() == 2);

		AdminCommandHooks hooks;
		admin->InstallHooks(hooks);

		const auto banned = hooks.banProfile(kProfileId, "cheating");
		assert(banned.ok);
		assert(banned.data.at("steamIdsFailed").empty());
		assert(bans.GetProfile(kProfileId).has_value());
		assert(bans.GetSteamId(kTunnelSteamId).has_value());
		assert(bans.GetSteamId(kWebApiSteamId)->reason == "cheating");

		const auto unbanned = hooks.unbanProfile(kProfileId);
		assert(unbanned.ok);
		assert(unbanned.data.at("steamIdsStillBanned").size() == 2);
		assert(!bans.GetProfile(kProfileId).has_value());
		assert(hooks.unbanSteamId(kTunnelSteamId).ok);
		assert(hooks.unbanSteamId(kWebApiSteamId).ok);
		assert(!bans.GetSteamId(kTunnelSteamId).has_value());
		assert(!bans.GetSteamId(kWebApiSteamId).has_value());
	}

	{
		BanStore bans(scratch / "bans-clock-fault.json");
		int clockCalls = 0;
		auto admin = SteamAdmin::Create(nlohmann::json{}, MakeDependencies(bans, [&clockCalls] {
			++clockCalls;
			return clockCalls == 2 ? std::numeric_limits<double>::quiet_NaN() : 200.0;
		}));
		assert(admin);
		admin->RecordIdentity(1, kProfileId, "Tester", kTunnelSteamId, SteamIdSource::SteamTunnel, 100.0);
		admin->RecordIdentity(2, kProfileId, "Tester", kWebApiSteamId, SteamIdSource::WebApi, 100.0);

		AdminCommandHooks hooks;
		admin->InstallHooks(hooks);

		const auto banned = hooks.banProfile(kProfileId, "cheating");
		assert(banned.ok);
		const auto& failed = banned.data.at("steamIdsFailed");
		assert(failed.size() == 1);
		const auto failedId = failed[0].get<std::string>();
		const std::string survivorId = failedId == kTunnelSteamId ? std::string{ kWebApiSteamId } : std::string{ kTunnelSteamId };
		assert(banned.message.find("Failed to ban Steam ID " + failedId + ".") != std::string::npos);
		assert(bans.GetProfile(kProfileId).has_value());
		assert(!bans.GetSteamId(failedId).has_value());
		assert(bans.GetSteamId(survivorId).has_value());

		const auto unbanned = hooks.unbanProfile(kProfileId);
		assert(unbanned.ok);
		const auto& stillBanned = unbanned.data.at("steamIdsStillBanned");
		assert(stillBanned.size() == 1);
		assert(stillBanned[0].get<std::string>() == survivorId);
		assert(hooks.unbanSteamId(survivorId).ok);
	}

	std::filesystem::remove_all(scratch);
	return 0;
}
