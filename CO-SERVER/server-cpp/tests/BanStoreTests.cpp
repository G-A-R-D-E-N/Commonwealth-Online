#include "BanStore.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
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
	assert(BanStore::NormalizeAddress("::ffff:7f00:1") == std::optional<std::string>{ "127.0.0.1" });
	assert(BanStore::NormalizeAddress("[::FFFF:C633:6432]") == std::optional<std::string>{ "198.51.100.50" });
	assert(BanStore::NormalizeAddress("0:0:0:0:0:ffff:c633:6432") == std::optional<std::string>{ "198.51.100.50" });
	assert(BanStore::NormalizeAddress("::ffff:198.51.100.50") == std::optional<std::string>{ "198.51.100.50" });
	assert(!BanStore::NormalizeAddress("::ffff:7f00"));
	assert(!BanStore::NormalizeAddress("::ffff:1:2:3"));

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

	{
		std::filesystem::remove(path);
		BanStore expiring(path);
		const auto now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
		(void)expiring.Ban("10.0.0.10", "expired", 1.0, now - 10.0);
		(void)expiring.Ban("10.0.0.11", "live", 1.0, now + 600.0);
		(void)expiring.Ban("10.0.0.12", "permanent", 1.0);
		(void)expiring.BanProfile("profile-expired", "expired", 1.0, now - 10.0);
		(void)expiring.BanSteamId("76561198000000777", "expired", 1.0, now - 10.0);
		assert(!expiring.Get("10.0.0.10").has_value());
		assert(expiring.Get("10.0.0.11").has_value());
		assert(expiring.Get("10.0.0.12").has_value());
		assert(!expiring.GetProfile("profile-expired").has_value());
		assert(!expiring.GetSteamId("76561198000000777").has_value());
		assert(expiring.List().size() == 2);
		assert(expiring.ListSteamIds().empty());
	}
	{
		BanStore reloaded(path);
		assert(reloaded.Get("10.0.0.11").has_value());
		assert(!reloaded.Get("10.0.0.10").has_value());
		assert(reloaded.List().size() == 2);
	}
	{
		std::ofstream legacyExpiry(path, std::ios::binary | std::ios::trunc);
		legacyExpiry << R"({"banned_ips":[{"ip":"10.0.0.13","reason":"old","bannedAt":1.0}]})";
	}
	{
		BanStore legacyExpiry(path);
		assert(legacyExpiry.Get("10.0.0.13").has_value());
		assert(legacyExpiry.Get("10.0.0.13")->expiresAt == 0.0);
	}

	{
		std::filesystem::remove(path);
		BanStore bounds(path);
		const auto rejects = [](auto a_call) {
			try {
				a_call();
			} catch (const std::invalid_argument&) {
				return true;
			}
			return false;
		};
		for (const double invalid : { std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
			assert(rejects([&] { (void)bounds.Ban("10.0.0.30", "invalid", 1.0, invalid); }));
			assert(rejects([&] { (void)bounds.BanProfile("profile-bounds", "invalid", 1.0, invalid); }));
			assert(rejects([&] { (void)bounds.BanSteamId("76561198000000901", "invalid", 1.0, invalid); }));
		}
		assert(!bounds.Get("10.0.0.30").has_value());
		(void)bounds.Ban("10.0.0.31", "permanent", 1.0);
		assert(bounds.Ban("10.0.0.31", "short", 1.0, 4070908800.0).reason == "permanent");
		assert(bounds.Get("10.0.0.31")->expiresAt == 0.0);
		(void)bounds.Ban("10.0.0.32", "long", 1.0, 4102444800.0);
		assert(bounds.Ban("10.0.0.32", "short", 1.0, 4070908800.0).reason == "long");
		assert(bounds.Get("10.0.0.32")->expiresAt == 4102444800.0);
		assert(bounds.Ban("10.0.0.32", "longer", 1.0, 4113000000.0).reason == "longer");
		(void)bounds.Ban("10.0.0.33", "temporary", 1.0, 4070908800.0);
		assert(bounds.Ban("10.0.0.33", "permanent", 1.0).expiresAt == 0.0);
		(void)bounds.BanProfile("profile-bounds", "long", 1.0, 4102444800.0);
		assert(bounds.BanProfile("profile-bounds", "short", 1.0, 4070908800.0).reason == "long");
		(void)bounds.BanSteamId("76561198000000901", "long", 1.0, 4102444800.0);
		assert(bounds.BanSteamId("76561198000000901", "short", 1.0, 4070908800.0).reason == "long");
	}
	{
		std::ofstream nonFinite(path, std::ios::binary | std::ios::trunc);
		nonFinite << R"({"banned_ips":[{"ip":"10.0.0.34","reason":"x","bannedAt":1.0,"expiresAt":1e400}]})";
	}
	threw = false;
	try {
		BanStore nonFiniteStore(path);
	} catch (const std::runtime_error& exception) {
		const std::string message{ exception.what() };
		threw = message.find("non-finite") != std::string::npos || message.find("not valid JSON") != std::string::npos;
	}
	assert(threw);

	std::filesystem::remove_all(scratch);
	return 0;
}
