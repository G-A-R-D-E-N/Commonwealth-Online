#include "ServerConfiguration.h"
#include "F4TModpackManifest.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

int main()
{
	using namespace CommonwealthOnline::Server::Configuration;
	using F4T::ServerConfiguration::RequiredModpackError;
	using Json = nlohmann::json;

	const auto defaults = Parse(Json::object());
	assert(defaults.bindHost == "0.0.0.0");
	assert(defaults.port == 7777);
	assert(defaults.adminPort == 7779);
	assert(defaults.serverName == "Commonwealth Online Server");
	assert(defaults.maximumPlayers == 16);
	assert(defaults.requireV2Identity);
	assert(defaults.enablePersistence);
	assert(defaults.enableChat);
	assert(defaults.enableFactions);
	assert(defaults.enableObjectState);
	assert(defaults.enableQuestSync);
	assert(defaults.friendlyFire);
	assert(!defaults.factionFriendlyFire);
	assert(defaults.resumeTokenLifetimeHours == 720);
	assert(defaults.worldMode == "coop-story");
	assert(defaults.doorsShared);
	assert(defaults.vats == "disabled");
	assert(defaults.worldPolicyPath == "world-policy.json");
	assert(Validate(defaults).empty());

	const auto worldPolicy = Parse(Json{
		{ "world_mode", "free-roam" },
		{ "doors_shared", false },
		{ "world_policy_path", "policy/world-policy.json" }
	});
	assert(worldPolicy.worldMode == "free-roam");
	assert(!worldPolicy.doorsShared);
	assert(worldPolicy.worldPolicyPath == "policy/world-policy.json");
	assert(Validate(worldPolicy).empty());

	assert(Parse(Json{ { "questMode", "free-roam" } }).worldMode == "free-roam");
	assert(Parse(Json{ { "quest_mode", "coop-story" } }).worldMode == "coop-story");

	const auto camelCase = Parse(Json{
		{ "worldMode", "free-roam" },
		{ "doorsShared", false },
		{ "worldPolicyPath", "policy/world-policy.json" }
	});
	assert(camelCase.worldMode == "free-roam");
	assert(!camelCase.doorsShared);
	assert(camelCase.worldPolicyPath == "policy/world-policy.json");
	assert(Validate(camelCase).empty());

	assert(Validate(Parse(Json{ { "worldMode", " Free-Roam " } })).empty());
	assert(Validate(Parse(Json{ { "worldMode", "FREE-ROAM" } })).empty());
	assert(Parse(Json{ { "doorsShared", false }, { "doors_shared", true } }).doorsShared == false);
	assert(Parse(Json{ { "vats", "real-time" } }).vats == "real-time");
	assert(Parse(Json{ { "vats_mode", "vanilla" } }).vats == "vanilla");
	assert(Parse(Json{ { "vats", "disabled" }, { "vats_mode", "vanilla" } }).vats == "disabled");
	assert(Validate(Parse(Json{ { "vats", " Real-Time " } })).empty());
	try {
		(void)Parse(Json{ { "vats", "slow-mo" } });
		assert(false);
	} catch (const std::invalid_argument&) {
	}
	auto badVats = defaults;
	badVats.vats = "slow-mo";
	assert(!Validate(badVats).empty());

	auto badWorldMode = defaults;
	badWorldMode.worldMode = "sandbox";
	assert(!Validate(badWorldMode).empty());
	auto emptyPolicyPath = defaults;
	emptyPolicyPath.worldPolicyPath = "   ";
	assert(!Validate(emptyPolicyPath).empty());

	Options mandatoryIdentity = defaults;
	mandatoryIdentity.requireV2Identity = false;
	const auto mandatoryErrors = Validate(mandatoryIdentity);
	assert(!mandatoryErrors.empty());

	Json configured{
		{ "host", "127.0.0.1" },
		{ "port", "7788" },
		{ "adminPort", 7790 },
		{ "serverName", "Native Test" },
		{ "serverDescription", "Configuration parity" },
		{ "maxPlayers", 16 },
		{ "requireV2Identity", "yes" },
		{ "enablePersistence", 1 },
		{ "enableChat", 1 },
		{ "durableStatePath", "state/custom.json" },
		{ "requiredClientVersion", " 1.2.0 " },
		{ "requiredFalloutRuntimeVersion", "1.11.240" },
		{ "requiredContentManifestFingerprint", std::string(64, 'A') },
		{ "requiredCapabilities", Json::array({ "interest-v1", "player-state-v1" }) },
		{ "resumeTokenLifetimeHours", "48" }
	};
	const auto parsed = Parse(configured);
	assert(parsed.bindHost == "127.0.0.1");
	assert(parsed.port == 7788);
	assert(parsed.adminPort == 7790);
	assert(parsed.serverName == "Native Test");
	assert(parsed.serverDescription == "Configuration parity");
	assert(parsed.maximumPlayers == 16);
	assert(parsed.requireV2Identity);
	assert(parsed.enablePersistence);
	assert(parsed.enableChat);
	assert(Parse(Json{ { "enable_chat", true } }).enableChat);
	assert(!Parse(Json{ { "enable_chat", false } }).enableChat);
	assert(Parse(Json{ { "enable_persistence", false } }).enablePersistence == false);
	assert(Parse(Json{ { "enable_factions", false } }).enableFactions == false);
	assert(Parse(Json{ { "enable_quest_sync", true } }).enableQuestSync);
	assert(!Parse(Json{ { "enable_quest_sync", false } }).enableQuestSync);
	assert(!Parse(Json{ { "enableQuestSync", false } }).enableQuestSync);
	assert(defaults.steamFriendsOnly && parsed.steamFriendsOnly);
	assert(!Parse(Json{ { "steam_friends_only", false } }).steamFriendsOnly);
	assert(!Parse(Json{ { "steamFriendsOnly", false } }).steamFriendsOnly);
	assert(Parse(Json{ { "steam_friends_only", true } }).steamFriendsOnly);
	assert(parsed.durableStatePath == "state/custom.json");
	assert(parsed.requiredClientVersion == std::optional<std::string>{ "1.2.0" });
	assert(parsed.requiredFalloutRuntimeVersion == std::optional<std::string>{ "1.11.240" });
	assert(parsed.requiredCapabilities.size() == 2);
	assert(parsed.resumeTokenLifetimeHours == 48);
	assert(Validate(parsed).empty());

	const auto requirements = IdentityRequirements(parsed);
	assert(requirements.clientVersion == parsed.requiredClientVersion);
	assert(requirements.falloutRuntimeVersion == parsed.requiredFalloutRuntimeVersion);
	assert(requirements.contentManifestFingerprint == parsed.requiredContentManifestFingerprint);
	assert(requirements.capabilities == parsed.requiredCapabilities);
	assert(requirements.resumeLifetimeSeconds == 48.0 * 60.0 * 60.0);

	Options sessionCapacity = defaults;
	sessionCapacity.maximumPlayers = 16;
	assert(Validate(sessionCapacity).empty());
	sessionCapacity.maximumPlayers = 17;
	assert(!Validate(sessionCapacity).empty());

	for (const auto* host : { "::", "::1", "2606:4700:10::1", "[2606:4700:10::1]" }) {
		Options ipv6 = defaults;
		ipv6.bindHost = host;
		assert(Validate(ipv6).empty());
	}
	for (const auto* host : { "1::2::3", "::ffff:1.2.3.4", "0.0.0", "" }) {
		Options badHost = defaults;
		badHost.bindHost = host;
		assert(!Validate(badHost).empty());
	}

	Options invalid = defaults;
	invalid.bindHost = "localhost";
	invalid.port = 0;
	invalid.adminPort = 0;
	invalid.serverName.clear();
	invalid.serverDescription = std::string(257, 'x');
	invalid.maximumPlayers = 17;
	invalid.enablePersistence = true;
	invalid.requireV2Identity = false;
	invalid.durableStatePath.clear();
	invalid.resumeTokenLifetimeHours = 0;
	invalid.requiredClientVersion = std::string(65, 'x');
	invalid.requiredFalloutRuntimeVersion = std::string{ "bad\x01version" };
	invalid.requiredContentManifestFingerprint = std::string(64, 'z');
	invalid.requiredCapabilities = { "interest-v1", "interest-v1", "" };
	const auto errors = Validate(invalid);
	assert(errors.size() >= 12);

	Options conflictingPorts = defaults;
	conflictingPorts.adminPort = conflictingPorts.port;
	const auto conflictErrors = Validate(conflictingPorts);
	assert(!conflictErrors.empty());

	bool threw = false;
	try {
		(void)Parse(Json{ { "max_players", "not-a-number" } });
	} catch (const std::invalid_argument&) {
		threw = true;
	}
	assert(threw);

	threw = false;
	try {
		(void)Parse(Json{ { "require_v2_identity", "maybe" } });
	} catch (const std::invalid_argument&) {
		threw = true;
	}
	assert(threw);

	threw = false;
	try {
		(void)Parse(Json{ { "required_capabilities", Json::array({ "ok", 3 }) } });
	} catch (const std::invalid_argument&) {
		threw = true;
	}
	assert(threw);

	const auto discoveryDefaults = Parse(Json::object());
	assert(discoveryDefaults.enableLanDiscovery);
	assert(discoveryDefaults.lanDiscoveryPort == 7778);

	const auto discoveryConfigured = Parse(Json{
		{ "lan_discovery_port", 7900 },
		{ "lan_discovery_enabled", false }
	});
	assert(discoveryConfigured.lanDiscoveryPort == 7900);
	assert(!discoveryConfigured.enableLanDiscovery);
	assert(Validate(discoveryConfigured).empty());

	auto discoveryClash = Parse(Json::object());
	discoveryClash.lanDiscoveryPort = discoveryClash.port;
	assert(!Validate(discoveryClash).empty());

	discoveryClash.enableLanDiscovery = false;
	assert(Validate(discoveryClash).empty());

	const auto listingDefaults = Parse(Json::object());
	assert(listingDefaults.publicListing);
	assert(listingDefaults.directoryUrl == "https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory");

	const auto listingOptedOut = Parse(Json{ { "public_listing", false } });
	assert(!listingOptedOut.publicListing);
	const auto listingOptedOutAlias = Parse(Json{ { "publicListing", false } });
	assert(!listingOptedOutAlias.publicListing);

	const auto listingConfigured = Parse(Json{
		{ "public_listing", true },
		{ "directory_url", "https://directory.example.com/api/v1/servers" }
	});
	assert(listingConfigured.publicListing);
	assert(Validate(listingConfigured).empty());

	auto insecureListing = listingConfigured;
	insecureListing.directoryUrl = "http://directory.example.com/api/v1/servers";
	assert(!Validate(insecureListing).empty());
	insecureListing.publicListing = false;
	assert(Validate(insecureListing).empty());

	auto listedMetadata = Parse(Json{ { "public_listing", true } });
	listedMetadata.serverName = "   ";
	assert(!Validate(listedMetadata).empty());
	listedMetadata.serverName = "Tabbed\tName";
	assert(!Validate(listedMetadata).empty());
	listedMetadata.serverName = "Trailing newline\n";
	assert(!Validate(listedMetadata).empty());
	listedMetadata.serverName = "Trailing tab\t";
	assert(!Validate(listedMetadata).empty());
	listedMetadata.serverName = "Nomad's Game";
	assert(Validate(listedMetadata).empty());
	listedMetadata.serverName = "Nomad's Game  ";
	assert(Validate(listedMetadata).empty());
	listedMetadata.serverDescription = std::string{ "Bell\x7f" };
	assert(!Validate(listedMetadata).empty());
	listedMetadata.serverDescription = std::string{ "Next\xc2\x85Line" };
	assert(!Validate(listedMetadata).empty());
	listedMetadata.serverDescription = "Trailing newline\n";
	assert(!Validate(listedMetadata).empty());
	listedMetadata.serverDescription = "Trailing tab\t";
	assert(!Validate(listedMetadata).empty());
	listedMetadata.serverDescription = "Survival co-op";
	assert(Validate(listedMetadata).empty());
	listedMetadata.serverDescription = "Survival co-op  ";
	assert(Validate(listedMetadata).empty());

	const auto listedNameMessage = Validate(Parse(Json{ { "public_listing", true }, { "server_name", "Bad\nName" } }));
	assert(!listedNameMessage.empty());
	assert(listedNameMessage.front().find("public_listing") != std::string::npos);

	auto unlistedMetadata = listedMetadata;
	unlistedMetadata.publicListing = false;
	unlistedMetadata.serverName = "   ";
	assert(Validate(unlistedMetadata).empty());
	unlistedMetadata.serverName = "Tabbed\tName";
	assert(Validate(unlistedMetadata).empty());
	unlistedMetadata.serverName = "Trailing newline\n";
	assert(Validate(unlistedMetadata).empty());
	unlistedMetadata.serverDescription = std::string{ "Bell\x7f" };
	assert(Validate(unlistedMetadata).empty());
	unlistedMetadata.serverDescription = "Trailing tab\t";
	assert(Validate(unlistedMetadata).empty());

	assert(IsAllowedDirectoryUrl("http://127.0.0.1:3000/api/v1/servers"));
	assert(IsAllowedDirectoryUrl("http://localhost:3000/api/v1/servers"));
	assert(!IsAllowedDirectoryUrl("http://localhost.evil.example/api"));
	assert(!IsAllowedDirectoryUrl("https://"));
	assert(!IsAllowedDirectoryUrl("https://host/with space"));
	assert(!IsAllowedDirectoryUrl("ftp://host/"));
	assert(!IsAllowedDirectoryUrl("http://127.0.0.1:80@evil.example/x"));
	assert(!IsAllowedDirectoryUrl("http://127.0.0.1:1@evil.example"));
	assert(!IsAllowedDirectoryUrl("https://user@x.supabase.co/"));
	assert(IsAllowedDirectoryUrl("http://127.0.0.1:8080/functions/v1/server-directory"));
	assert(IsAllowedDirectoryUrl("https://oeajjfquvmdxqqtiotfa.supabase.co/functions/v1/server-directory"));

	const auto hourly = Parse(Json{ { "resume_token_lifetime_hours", 2 } });
	assert(!hourly.resumeTokenLifetimeSeconds.has_value());
	assert(IdentityRequirements(hourly).resumeLifetimeSeconds == 7200.0);

	const auto seconds = Parse(Json{
		{ "resume_token_lifetime_hours", 2 },
		{ "resume_token_lifetime_seconds", 5 }
	});
	assert(seconds.resumeTokenLifetimeSeconds.has_value() && *seconds.resumeTokenLifetimeSeconds == 5);
	assert(IdentityRequirements(seconds).resumeLifetimeSeconds == 5.0);
	assert(Validate(seconds).empty());

	const auto nullAlias = Parse(Json{ { "admin_port", nullptr }, { "adminPort", 7790 } });
	assert(nullAlias.adminPort == 7790);

	const auto adminList = Parse(Json{ { "admin_profiles", Json::array({ "co-admin-0001", " co-admin-0002 " }) } });
	assert(adminList.adminProfiles.size() == 2);
	assert(adminList.adminProfiles.front() == "co-admin-0001");
	assert(adminList.adminProfiles.back() == "co-admin-0002");
	assert(Validate(adminList).empty());

	const auto configuredAdminSecret = Parse(Json{ { "admin_profiles", Json::array({ "co-admin-0001:operator-secret" }) } });
	assert(configuredAdminSecret.adminProfiles.size() == 1);
	const auto adminSecretErrors = Validate(configuredAdminSecret);
	assert(adminSecretErrors.size() == 1);
	assert(adminSecretErrors.front().find(":secret") != std::string::npos);

	threw = false;
	try {
		(void)Parse(Json{ { "port", "+7788" } });
	} catch (const std::invalid_argument&) {
		threw = true;
	}
	assert(threw);

	auto utf8 = defaults;
	utf8.requiredClientVersion = std::string{};
	for (int index = 0; index < 33; ++index) utf8.requiredClientVersion->append("\xC3\xA9");
	assert(!Validate(utf8).empty());

	auto outOfRange = seconds;
	outOfRange.resumeTokenLifetimeSeconds = 0;
	assert(!Validate(outOfRange).empty());
	outOfRange.resumeTokenLifetimeSeconds = 8760u * 60u * 60u + 1u;
	assert(!Validate(outOfRange).empty());

	const Json catalog = Json::array({ Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5 } } });
	const auto inventoryOptions = Parse(Json{ { "enable_persistence", true }, { "starting_inventory", catalog } });
	assert(inventoryOptions.startingInventory == catalog);
	assert(Validate(inventoryOptions).empty());
	assert(!Validate(Parse(Json{ { "enable_persistence", false }, { "starting_inventory", Json::array() } })).empty());
	assert(Validate(Parse(Json{ { "starting_inventory", Json::array() } })).empty());
	for (const auto& bad : { Json::object(), Json::array({ Json{ { "plugin", "../test.esm" }, { "localFormId", "123" }, { "count", 1 } } }),
		Json::array({ Json{ { "plugin", "test.esm" }, { "localFormId", "0" }, { "count", 1 } } }),
		Json::array({ Json{ { "plugin", "test.esm" }, { "localFormId", "123" }, { "count", 65536 } } }),
		Json::array({ Json{ { "plugin", "test.esm" }, { "localFormId", "123" }, { "count", -1 } } }) }) {
		threw = false;
		try { (void)Parse(Json{ { "starting_inventory", bad } }); }
		catch (const std::invalid_argument&) { threw = true; }
		assert(threw);
	}
	Json duplicate = catalog;
	duplicate.push_back(Json{ { "plugin", "test.ESM" }, { "localFormId", "00000123" }, { "count", 1 } });
	threw = false;
	try { (void)Parse(Json{ { "starting_inventory", duplicate } }); }
	catch (const std::invalid_argument&) { threw = true; }
	assert(threw);
	Json oversized = Json::array();
	for (unsigned int index = 1; index <= 256; ++index) {
		oversized.push_back(Json{ { "plugin", std::string(124, 'a') + ".esm" },
			{ "localFormId", std::to_string(index) }, { "count", 65535 } });
	}
	threw = false;
	try { (void)Parse(Json{ { "enable_persistence", true }, { "starting_inventory", oversized } }); }
	catch (const std::invalid_argument&) { threw = true; }
	assert(threw);
	assert(Parse(Json{ { "enableFactions", true } }).enableFactions);
	assert(Parse(Json{ { "enable_factions", "yes" } }).enableFactions);
	assert(!Parse(Json{ { "friendly_fire", false } }).friendlyFire);
	assert(!Parse(Json{ { "friendlyFire", "off" } }).friendlyFire);
	assert(Parse(Json{ { "friendly_fire", "yes" } }).friendlyFire);
	assert(!Parse(Json{ { "friendly_fire", 0 } }).friendlyFire);
	assert(Parse(Json{ { "faction_friendly_fire", true } }).factionFriendlyFire);
	assert(Parse(Json{ { "factionFriendlyFire", "on" } }).factionFriendlyFire);
	assert(Parse(Json{ { "faction_friendly_fire", "yes" } }).factionFriendlyFire);
	assert(!Parse(Json{ { "faction_friendly_fire", 0 } }).factionFriendlyFire);
	bool factionFriendlyFireRejected = false;
	try { (void)Parse(Json{ { "faction_friendly_fire", "maybe" } }); }
	catch (const std::invalid_argument&) { factionFriendlyFireRejected = true; }
	assert(factionFriendlyFireRejected);
	bool friendlyFireRejected = false;
	try { (void)Parse(Json{ { "friendly_fire", "maybe" } }); }
	catch (const std::invalid_argument&) { friendlyFireRejected = true; }
	assert(friendlyFireRejected);
	bool factionsRejected = false;
	try { (void)Parse(Json{ { "enable_factions", "maybe" } }); }
	catch (const std::invalid_argument&) { factionsRejected = true; }
	assert(factionsRejected);
	assert(!defaults.serverPassword.has_value());
	const auto passwordOptions = Parse(Json{ { "server_password", "abcd" } });
	assert(passwordOptions.serverPassword == std::optional<std::string>{ "abcd" });
	assert(Validate(passwordOptions).empty());
	assert(IdentityRequirements(passwordOptions).serverPasswordSha256 ==
		std::optional<std::string>{ "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589" });
	assert(!IdentityRequirements(defaults).serverPasswordSha256.has_value());
	assert(Validate(Parse(Json{ { "serverPassword", std::string(64, '~') } })).empty());
	assert(!Validate(Parse(Json{ { "server_password", "" } })).empty());
	assert(!Validate(Parse(Json{ { "server_password", "abc" } })).empty());
	assert(!Validate(Parse(Json{ { "server_password", std::string(65, 'a') } })).empty());
	assert(!Validate(Parse(Json{ { "server_password", "ab\x01" "cd" } })).empty());
	assert(!Validate(Parse(Json{ { "server_password", "ab\x7f" "cd" } })).empty());
	assert(Validate(Parse(Json{ { "adminProfiles", Json::array({ "co-admin" }) } })).empty());
	assert(!Validate(Parse(Json{ { "admin_profiles", Json::array({ "co-admin", "co-admin" }) } })).empty());
	Json tooManyAdmins = Json::array();
	for (std::size_t index = 0; index < 65; ++index) {
		tooManyAdmins.push_back("admin-" + std::to_string(index));
	}
	assert(!Validate(Parse(Json{ { "admin_profiles", tooManyAdmins } })).empty());
	assert(!Validate(Parse(Json{ { "admin_profiles", Json::array({ "co-admin:operator-secret" }) } })).empty());

	threw = false;
	try {
		(void)Parse(Json{ { "admin_profiles", Json::array({ "ok", 3 }) } });
	} catch (const std::invalid_argument&) {
		threw = true;
	}
	assert(threw);

	const auto modpackDirectory = std::filesystem::temp_directory_path() / "co-server-configuration-modpack-tests";
	std::filesystem::remove_all(modpackDirectory);
	std::filesystem::create_directories(modpackDirectory);
	const auto writeFile = [&](std::string_view a_name, const std::string& a_text) {
		std::ofstream output(modpackDirectory / std::string{ a_name }, std::ios::binary);
		output << a_text;
	};
	const auto manifestWithNatives = [](std::size_t a_count) {
		std::string text = R"({"version":1,"entries":[)";
		for (std::size_t index = 0; index < a_count; ++index) {
			if (index > 0) text += ',';
			text += R"({"kind":"native","name":"F4SE/Plugins/mod-)" + std::to_string(index) + R"(.dll","sha256":")" + std::string(64, 'e') + R"("})";
		}
		return text + "]}";
	};
	const auto rejects = [&](const Json& a_root) {
		try {
			(void)Parse(a_root, modpackDirectory);
		} catch (const std::invalid_argument&) {
			return true;
		}
		return false;
	};

	const std::string validManifest = manifestWithNatives(1);
	writeFile("modpack.json", validManifest);
	const auto modpacked = Parse(Json{ { "required_modpack_path", "modpack.json" } }, modpackDirectory);
	const auto expectedManifest = F4T::ModpackManifest::ParseManifest(validManifest);
	assert(expectedManifest);
	assert(modpacked.requiredModpackFingerprint == F4T::ModpackManifest::ComputeFingerprint(*expectedManifest));
	assert(modpacked.requiredModpackManifestJson == F4T::ModpackManifest::SerializeManifest(*expectedManifest));
	assert(IdentityRequirements(modpacked).modpackFingerprint == modpacked.requiredModpackFingerprint);
	assert(IdentityRequirements(modpacked).modpackManifestJson == modpacked.requiredModpackManifestJson);
	assert(Validate(modpacked).empty());
	assert(!defaults.requiredModpackFingerprint.has_value());
	assert(!IdentityRequirements(defaults).modpackFingerprint.has_value());
	const auto namedModpack = Parse(Json{ { "required_modpack_path", "modpack.json" }, { "modpack_name", "Main List" } }, modpackDirectory);
	assert(namedModpack.modpackName == "Main List");
	assert(IdentityRequirements(namedModpack).modpackName == "Main List");
	assert(!IdentityRequirements(modpacked).modpackName.has_value());
	assert(Validate(namedModpack).empty());
	assert(!Validate(Parse(Json{ { "modpack_name", "Main List" } })).empty());
	assert(Validate(Parse(Json{ { "required_modpack_path", "modpack.json" }, { "modpackName", std::string(80, 'M') } }, modpackDirectory)).empty());
	assert(!Validate(Parse(Json{ { "required_modpack_path", "modpack.json" }, { "modpack_name", std::string(81, 'M') } }, modpackDirectory)).empty());
	assert(!Validate(Parse(Json{ { "required_modpack_path", "modpack.json" }, { "modpack_name", "<b>Main</b>" } }, modpackDirectory)).empty());
	const auto absolute = Parse(Json{ { "required_modpack_path", (modpackDirectory / "modpack.json").string() } }, "unused-base");
	assert(absolute.requiredModpackFingerprint == modpacked.requiredModpackFingerprint);
	assert(absolute.requiredModpackManifestJson == modpacked.requiredModpackManifestJson);
	const auto withLink = Parse(Json{ { "required_modpack_path", "modpack.json" }, { "required_modpack_url", "https://example.com/modpack" } }, modpackDirectory);
	assert(withLink.requiredModpackUrl == "https://example.com/modpack");
	assert(IdentityRequirements(withLink).modpackUrl == "https://example.com/modpack");
	assert(Validate(withLink).empty());
	assert(!Validate(Parse(Json{ { "required_modpack_url", "ftp://example.com/modpack" } })).empty());
	assert(!Validate(Parse(Json{ { "required_modpack_url", "http://example.com/modpack" } })).empty());
	assert(Validate(Parse(Json{ { "required_modpack_url", "http://127.0.0.1/modpack" } })).empty());
	assert(!defaults.allowModpackDownloads);
	assert(defaults.modpackDownloadConcurrency == 4);
	const auto downloads = Parse(Json{
		{ "required_modpack_path", "modpack.json" },
		{ "allow_modpack_downloads", true },
		{ "modpack_download_dir", "files" },
		{ "modpack_downloadable", Json::array({ "MyMod.esp", "MyMod - Main.ba2" }) },
		{ "modpack_download_concurrency", 2 },
		{ "modpack_download_bytes_per_hour_per_address", 1024 } }, modpackDirectory);
	assert(downloads.allowModpackDownloads);
	assert(downloads.modpackDownloadDirectory == (modpackDirectory / "files").string());
	assert(downloads.modpackDownloadConcurrency == 2 && downloads.modpackDownloadBytesPerHourPerAddress == 1024);
	assert(Validate(downloads).empty());
	assert(Validate(Parse(Json{ { "required_modpack_path", "modpack.json" }, { "allow_modpack_downloads", true }, { "modpack_download_dir", "files" }, { "modpack_downloadable", Json::array({ "*" }) } }, modpackDirectory)).empty());
	assert(!Validate(Parse(Json{ { "allow_modpack_downloads", true }, { "modpack_download_dir", "files" }, { "modpack_downloadable", Json::array({ "*" }) } }, modpackDirectory)).empty());
	assert(!Validate(Parse(Json{ { "required_modpack_path", "modpack.json" }, { "allow_modpack_downloads", true }, { "modpack_downloadable", Json::array({ "*" }) } }, modpackDirectory)).empty());
	assert(!Validate(Parse(Json{ { "required_modpack_path", "modpack.json" }, { "allow_modpack_downloads", true }, { "modpack_download_dir", "files" } }, modpackDirectory)).empty());
	assert(!Validate(Parse(Json{ { "modpack_downloadable", Json::array({ "*", "MyMod.esp" }) } })).empty());
	assert(!Validate(Parse(Json{ { "modpack_downloadable", Json::array({ "F4SE/Plugins/x.dll" }) } })).empty());
	assert(!Validate(Parse(Json{ { "modpack_downloadable", Json::array({ "Fallout4.esm" }) } })).empty());
	assert(!Validate(Parse(Json{ { "modpack_download_concurrency", 17 } })).empty());
	assert(!Validate(Parse(Json{ { "modpack_download_bytes_per_hour_per_address", 42949672961 } })).empty());
	assert(!RequiredModpackError(Json::object(), modpackDirectory).has_value());
	assert(RequiredModpackError(Json{ { "required_modpack_path", "missing.json" } }, modpackDirectory).has_value());
	assert(!RequiredModpackError(Json{ { "required_modpack_path", "modpack.json" } }, modpackDirectory).has_value());
	assert(RequiredModpackError(Json{ { "required_modpack_path", 7 } }, modpackDirectory).has_value());
	writeFile("moderate.json", manifestWithNatives(300));
	assert(!rejects(Json{ { "requiredModpackPath", "moderate.json" } }));
	writeFile("invalid.json", "{");
	assert(rejects(Json{ { "required_modpack_path", "invalid.json" } }));
	assert(rejects(Json{ { "required_modpack_path", "missing.json" } }));
	assert(rejects(Json{ { "required_modpack_path", 7 } }));
	writeFile("large.json", manifestWithNatives(1000));
	const auto large = Parse(Json{ { "required_modpack_path", "large.json" } }, modpackDirectory);
	assert(large.requiredModpackFingerprint.has_value());
	assert(large.requiredModpackManifestJson.has_value());
	writeFile("huge.json", validManifest + std::string(2 * 1024 * 1024, ' '));
	assert(rejects(Json{ { "required_modpack_path", "huge.json" } }));

	assert(defaults.questMode == F4T::QuestProtocol::Mode::CoopStory);
	assert(defaults.questOverrides.empty());
	const auto quests = Parse(Json{
		{ "quest_sync_mode", "free-roam" },
		{ "quest_sync_overrides", Json::array({ Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "policy", "solo" } } }) }
	});
	assert(quests.questMode == F4T::QuestProtocol::Mode::FreeRoam);
	assert(quests.questOverrides.size() == 1);
	assert(quests.questOverrides.front().quest.plugin == "fallout4.esm");
	assert(quests.questOverrides.front().policy == F4T::QuestProtocol::Override::Solo);
	assert(Validate(quests).empty());
	assert(Parse(Json{ { "questSyncMode", "faction-war" } }).questMode == F4T::QuestProtocol::Mode::FactionWar);
	assert(rejects(Json{ { "quest_sync_mode", "story" } }));
	assert(rejects(Json{ { "quest_sync_mode", 3 } }));
	assert(rejects(Json{ { "quest_sync_overrides", Json::object() } }));
	assert(rejects(Json{ { "quest_sync_overrides", Json::array({ Json{ { "quest", "bogus" }, { "policy", "solo" } } }) } }));
	assert(rejects(Json{ { "quest_sync_overrides", Json::array({ Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "policy", "off" } } }) } }));
	const auto duplicated = Parse(Json{
		{ "quest_sync_overrides", Json::array({
			Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "policy", "solo" } },
			Json{ { "quest", "0a1b2c:FALLOUT4.ESM" }, { "policy", "blocked" } }
		}) }
	});
	const auto duplicateErrors = Validate(duplicated);
	assert(duplicateErrors.size() == 1);
	assert(duplicateErrors.front().find("must not repeat 0A1B2C:fallout4.esm") != std::string::npos);
	assert(Parse(Json{ { "quest_mode", "free-roam" } }).questMode == F4T::QuestProtocol::Mode::CoopStory);
	assert(Parse(Json{ { "questMode", "free-roam" } }).questMode == F4T::QuestProtocol::Mode::CoopStory);
	assert(Parse(Json{ { "quest_overrides", Json::array() } }).questOverrides.empty());

	const auto factionDefaults = Parse(Json::object());
	assert(factionDefaults.factions.maximumFactions == 512);
	assert(factionDefaults.factions.maximumMembers == 16);
	assert(factionDefaults.factions.inviteLifetimeSeconds == 60.0);
	assert(factionDefaults.factions.inviteWindowSeconds == 60.0);
	assert(factionDefaults.factions.invitesPerWindow == 8);
	assert(factionDefaults.factions.pendingInvites == 256);
	assert(factionDefaults.factions.pendingInvitesPerTarget == 16);
	assert(factionDefaults.factions.rosterRequestIntervalSeconds == 1.0);
	assert(factionDefaults.factions.listRequestIntervalSeconds == 1.0);
	assert(factionDefaults.factions.nameRosterIntervalSeconds == 1.0);
	assert(factionDefaults.factions.mutationCapacity == 4.0);
	assert(factionDefaults.factions.mutationRefillPerSecond == 1.0);
	assert(!factionDefaults.questStoryOrderPath);
	assert(!factionDefaults.questStoryOrder);
	const auto downedDefaults = F4T::ServerConfiguration::DownedConfig(defaults);
	assert(downedDefaults.downedSeconds == 60);
	assert(downedDefaults.reviveHealthFraction == 0.5);
	assert(downedDefaults.reviveRangeMeters == 3.0);
	assert(downedDefaults.reviveChannelSeconds == 3.0);
	assert(downedDefaults.respawnGraceSeconds == 8.0);
	assert(downedDefaults.stimpaksPerRevive == 1);

	const auto tuned = Parse(Json{
		{ "factions", Json{ { "max_factions", 4 }, { "max_members", 3 }, { "invite_lifetime_seconds", 30.0 }, { "pending_invites", 9 } } },
		{ "downed", Json{ { "seconds", 45 }, { "revive_channel_seconds", 5 }, { "revive_health_fraction", 0.25 }, { "revive_range_meters", 2.5 }, { "respawn_grace_seconds", 12.0 }, { "stimpaks_per_revive", 2 } } }
	});
	assert(Validate(tuned).empty());
	assert(tuned.factions.maximumFactions == 4);
	assert(tuned.factions.maximumMembers == 3);
	assert(tuned.factions.inviteLifetimeSeconds == 30.0);
	assert(tuned.factions.pendingInvites == 9);
	assert(tuned.factions.invitesPerWindow == 8);
	const auto tunedDowned = F4T::ServerConfiguration::DownedConfig(tuned);
	assert(tunedDowned.downedSeconds == 45);
	assert(tunedDowned.reviveChannelSeconds == 5.0);
	assert(tunedDowned.reviveHealthFraction == 0.25);
	assert(tunedDowned.reviveRangeMeters == 2.5);
	assert(tunedDowned.respawnGraceSeconds == 12.0);
	assert(tunedDowned.stimpaksPerRevive == 2);
	assert(Parse(Json{ { "downed_seconds", 30 } }).downedSeconds == 30);
	assert(Parse(Json{ { "downed_seconds", 30 }, { "downed", Json{ { "seconds", 45 } } } }).downedSeconds == 45);

	const auto invalidTuning = Validate(Parse(Json{
		{ "factions", Json{ { "max_factions", 0 }, { "max_members", 65 }, { "mutation_refill_per_second", 0.0 } } },
		{ "downed", Json{ { "revive_health_fraction", 1.5 }, { "revive_range_meters", 0.0 }, { "respawn_grace_seconds", 601 }, { "stimpaks_per_revive", 100 } } }
	}));
	assert(invalidTuning.size() == 7);
	assert(rejects(Json{ { "factions", 3 } }));
	assert(rejects(Json{ { "downed", Json::array() } }));
	assert(rejects(Json{ { "questStoryOrderPath", "missing-quest-order.json" } }));

	std::filesystem::remove_all(modpackDirectory);
	return 0;
}
