#include "BanStore.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#  include <sys/stat.h>
#endif

namespace
{
	std::filesystem::path ScratchDirectory()
	{
		const auto suffix = std::to_string(
			static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
		return std::filesystem::temp_directory_path() / ("co-ban-store-tests-" + suffix);
	}
}

int main()
{
	using CommonwealthOnline::Server::BanStore;

	assert(BanStore::NormalizeIpv4("127.0.0.1") == std::optional<std::string>{ "127.0.0.1" });
	assert(BanStore::NormalizeIpv4(" 127.000.000.001 ") == std::optional<std::string>{ "127.0.0.1" });
	assert(!BanStore::NormalizeIpv4("256.0.0.1"));
	assert(!BanStore::NormalizeIpv4("127.0.0"));
	assert(!BanStore::NormalizeIpv4("localhost"));

	const auto scratch = ScratchDirectory();
	std::filesystem::create_directories(scratch);
	const auto path = scratch / "bans.json";

	{
		BanStore store(path);
		assert(store.List().empty());
		const auto first = store.Ban("127.0.0.2", "reason", 10.5);
		assert(first.ip == "127.0.0.2");
		assert(first.profileId.empty());
		assert(first.reason == "reason");
		assert(first.bannedAt == 10.5);
		assert(store.Get("127.000.000.002").has_value());

		const auto profile = store.BanProfile(" profile-001 ", "profile reason", 11.5);
		assert(profile.ip.empty());
		assert(profile.profileId == "profile-001");
		assert(store.GetProfile("profile-001")->reason == "profile reason");

		const auto replacement = store.Ban("127.0.0.2", std::string(1100, 'x'), 20.0);
		assert(replacement.reason.size() == 1024);
		assert(store.List().size() == 2);

		(void)store.Ban("10.0.0.1", "second", 30.0);
		const auto list = store.List();
		assert(list.size() == 3);
		assert(list[0].ip == "10.0.0.1");
		assert(list[1].ip == "127.0.0.2");
		assert(list[2].profileId == "profile-001");
		assert(store.Unban("10.0.0.1"));
		assert(!store.Unban("10.0.0.1"));
	}

#ifndef _WIN32
	struct stat metadata{};
	assert(stat(path.c_str(), &metadata) == 0);
	assert((metadata.st_mode & 0777) == 0600);
#endif

	{
		BanStore reloaded(path);
		assert(reloaded.List().size() == 2);
		const auto entry = reloaded.Get("127.0.0.2");
		assert(entry && entry->reason.size() == 1024 && entry->bannedAt == 20.0);
		assert(reloaded.GetProfile("profile-001")->bannedAt == 11.5);
		assert(reloaded.UnbanProfile("profile-001"));
		assert(!reloaded.UnbanProfile("profile-001"));
	}

	assert(BanStore::NormalizeAddress(" 127.000.000.001 ") == std::optional<std::string>{ "127.0.0.1" });
	assert(BanStore::NormalizeAddress("2606:4700:10::5") == std::optional<std::string>{ "2606:4700:10:0::/64" });
	assert(BanStore::NormalizeAddress("[2606:4700:10::5]") == std::optional<std::string>{ "2606:4700:10:0::/64" });
	assert(BanStore::NormalizeAddress("2606:4700:10:0::/64") == std::optional<std::string>{ "2606:4700:10:0::/64" });
	assert(!BanStore::NormalizeAddress("localhost"));
	assert(!BanStore::NormalizeAddress("1::2::3"));

	{
		const auto ipv6Path = scratch / "ipv6.json";
		{
			BanStore store(ipv6Path);
			const auto entry = store.Ban("2606:4700:10::5", "rotating addresses", 50.0);
			assert(entry.ip == "2606:4700:10:0::/64");
			assert(store.Get("2606:4700:10:0:ffff::1").has_value());
			assert(!store.Get("2606:4700:11::1").has_value());
			assert(!store.Get("127.0.0.1").has_value());
		}
		BanStore reloaded(ipv6Path);
		assert(reloaded.Get("2606:4700:10::abcd").has_value());
		assert(reloaded.Unban("2606:4700:10::1"));
		assert(!reloaded.Get("2606:4700:10::abcd").has_value());
	}

	{
		const auto warningPath = scratch / "warnings.json";
		bool failPostCommit = false;
		BanStore store(warningPath, [&] {
			if (failPostCommit) {
				throw std::runtime_error("post-commit cleanup failed");
			}
		});
		failPostCommit = true;
		assert(store.Ban("127.0.0.9", "warning", 40.0).ip == "127.0.0.9");
		assert(store.TakePersistenceWarning().has_value());
		assert(store.Get("127.0.0.9").has_value());
		BanStore reloaded(warningPath);
		assert(reloaded.Get("127.0.0.9").has_value());
	}

	const auto legacyPath = scratch / "legacy.json";
	{
		std::ofstream legacy(legacyPath, std::ios::binary | std::ios::trunc);
		legacy << R"({"banned_ips":[{"ip":"127.0.0.9","reason":"legacy","bannedAt":3.0}]})";
	}
	{
		BanStore legacy(legacyPath);
		assert(legacy.Get("127.0.0.9")->reason == "legacy");
		assert(!legacy.GetProfile("profile-001"));
	}

	bool threw = false;
	try {
		BanStore store(path);
		(void)store.Ban("bad-ip", "", 1.0);
	} catch (...) {
		threw = true;
	}
	assert(threw);

	{
		std::ofstream invalid(path, std::ios::binary | std::ios::trunc);
		invalid << R"({"banned_ips":[{"ip":"999.0.0.1"}]})";
	}
	threw = false;
	try {
		BanStore invalid(path);
	} catch (...) {
		threw = true;
	}
	assert(threw);

	{
		std::ofstream invalidProfiles(path, std::ios::binary | std::ios::trunc);
		invalidProfiles << R"({"banned_ips":[],"banned_profiles":[{"profileId":"short"}]})";
	}
	threw = false;
	try {
		BanStore invalidProfiles(path);
	} catch (...) {
		threw = true;
	}
	assert(threw);

	std::filesystem::remove(path);
	{
		BanStore store(path);
		(void)store.Ban("10.0.0.9", "ip", 1.0);
		const auto steam = store.BanSteamId("76561198000000666", "steam reason", 2.0);
		assert(steam.steamId == "76561198000000666");
		assert(store.GetSteamId("76561198000000666")->reason == "steam reason");
		assert(!store.GetSteamId("76561198000000042"));
		assert(store.List().size() == 1);
		for (const char* invalidSteam : { "76561197960265728", "103582791429521408", "", " 76561198000000666" }) {
			bool rejected = false;
			try {
				(void)store.BanSteamId(invalidSteam, "x", 3.0);
			} catch (const std::invalid_argument&) {
				rejected = true;
			}
			assert(rejected);
		}
		(void)store.BanSteamId("76561198000000042", std::string(1100, 'y'), 4.0);
		assert(store.ListSteamIds().size() == 2);
		assert(store.ListSteamIds().front().steamId == "76561198000000042");
		assert(store.ListSteamIds().front().reason.size() == 1024);
	}
	{
		BanStore reloaded(path);
		assert(reloaded.ListSteamIds().size() == 2);
		assert(reloaded.GetSteamId("76561198000000666")->bannedAt == 2.0);
		assert(reloaded.Get("10.0.0.9").has_value());
		assert(reloaded.UnbanSteamId("76561198000000666"));
		assert(!reloaded.UnbanSteamId("76561198000000666"));
	}
	{
		BanStore reloaded(path);
		assert(reloaded.ListSteamIds().size() == 1);
		assert(!reloaded.GetSteamId("76561198000000666"));
	}
	{
		std::ofstream legacy(path, std::ios::binary | std::ios::trunc);
		legacy << R"({"banned_ips":[]})";
	}
	{
		BanStore legacy(path);
		assert(legacy.ListSteamIds().empty());
	}
	for (const char* invalidFile : {
			 R"({"banned_ips":[],"banned_steam_ids":{}})",
			 R"({"banned_ips":[],"banned_steam_ids":[{"steamId":"12345"}]})",
			 R"({"banned_ips":[],"banned_steam_ids":[{"steamId":76561198000000666}]})" }) {
		{
			std::ofstream invalidSteam(path, std::ios::binary | std::ios::trunc);
			invalidSteam << invalidFile;
		}
		threw = false;
		try {
			BanStore invalidSteam(path);
		} catch (...) {
			threw = true;
		}
		assert(threw);
	}

	std::filesystem::remove_all(scratch);
	return 0;
}
