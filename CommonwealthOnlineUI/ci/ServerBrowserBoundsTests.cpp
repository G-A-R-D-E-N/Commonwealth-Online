#include "F4THttpGet.h"
#include "F4TMenuWatchdog.h"
#include "F4TServerBrowserData.h"
#include "F4TWorldMode.h"

#include <cassert>
#include <chrono>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using F4T::ServerBrowser::ServerBrowserData;
	using F4T::ServerBrowser::ServerEntry;

	std::string BadgeText(const ServerEntry& a_entry)
	{
		return std::string{ COUI::WorldMode::Label(COUI::WorldMode::Normalize(a_entry.worldMode)) };
	}

	void Require(bool a_condition)
	{
		assert(a_condition);
	}

	nlohmann::json ValidServer()
	{
		return {
			{ "id", std::string(256, 'i') },
			{ "name", std::string(128, 'n') },
			{ "description", std::string(2048, 'd') },
			{ "host", "127.0.0.1" },
			{ "modlist", std::string(4096, 'm') },
			{ "tags", nlohmann::json::array({ std::string(64, 't') }) },
			{ "port", 65535 },
			{ "players", 0 },
			{ "maxPlayers", 256 },
			{ "ping", 60000 }
		};
	}

	void RequireRejected(nlohmann::json a_root)
	{
		std::string error;
		Require(ServerBrowserData::ParseDirectoryServers(a_root, error).empty());
		Require(!error.empty());
	}
}

int main()
{
	std::string error;
	std::string body;
	Require(F4T::Http::AppendResponseChunk(body, std::string(F4T::Http::kMaxResponseBytes, 'x'), error));
	Require(body.size() == F4T::Http::kMaxResponseBytes);
	Require(!F4T::Http::AppendResponseChunk(body, "x", error));
	Require(body.size() == F4T::Http::kMaxResponseBytes);

	std::string validError;
	auto validJson = ValidServer();
	validJson["tags"] = nlohmann::json::array();
	for (int index = 0; index < 32; ++index) validJson["tags"].push_back(std::string(64, 't'));
	const auto valid = ServerBrowserData::ParseDirectoryServers(nlohmann::json::array({ validJson }), validError);
	Require(valid.size() == 1);
	Require(ServerBrowserData::ParseDirectoryServers(nlohmann::json::array(), validError).empty());

	for (const auto& [field, length] : {
			std::pair{ "id", 257 },
			std::pair{ "name", 129 },
			std::pair{ "description", 2049 },
			std::pair{ "host", 256 },
			std::pair{ "modlist", 4097 },
			std::pair{ "region", 65 },
			std::pair{ "worldspace", 65 },
			std::pair{ "mode", 65 },
			std::pair{ "worldMode", 65 },
			std::pair{ "world_mode", 65 },
			std::pair{ "status", 65 },
			std::pair{ "lastSeen", 65 },
			std::pair{ "discord", 513 } }) {
		auto oversized = ValidServer();
		oversized[field] = std::string(length, 'x');
		RequireRejected(nlohmann::json::array({ oversized }));
	}

	auto tooManyServers = nlohmann::json::array();
	for (int index = 0; index < 513; ++index) tooManyServers.push_back(ValidServer());
	RequireRejected(tooManyServers);

	auto fullDirectory = nlohmann::json::array();
	for (int index = 0; index < 512; ++index) fullDirectory.push_back(ValidServer());
	std::string fullError;
	Require(ServerBrowserData::ParseDirectoryServers(nlohmann::json{ { "servers", fullDirectory } }, fullError).size() == 512);
	Require(fullError.empty());
	fullDirectory.push_back(ValidServer());
	Require(ServerBrowserData::ParseDirectoryServers(nlohmann::json{ { "servers", fullDirectory } }, fullError).empty());
	Require(!fullError.empty());

	for (const auto value : { 0, 65536 }) {
		auto invalid = ValidServer();
		invalid["port"] = value;
		RequireRejected(nlohmann::json::array({ invalid }));
	}
	for (const auto value : { -1, 257 }) {
		auto invalid = ValidServer();
		invalid["players"] = value;
		RequireRejected(nlohmann::json::array({ invalid }));
	}

	auto oversizedTags = ValidServer();
	oversizedTags["tags"] = nlohmann::json::array();
	for (int index = 0; index < 33; ++index) oversizedTags["tags"].push_back("tag");
	RequireRejected(nlohmann::json::array({ oversizedTags }));

	auto oversizedTag = ValidServer();
	oversizedTag["tags"] = nlohmann::json::array({ std::string(65, 'x') });
	RequireRejected(nlohmann::json::array({ oversizedTag }));

	auto wrongLive = ValidServer();
	wrongLive["live"] = 7;
	RequireRejected(nlohmann::json::array({ wrongLive }));

	auto partiallyInvalid = nlohmann::json::array({ ValidServer(), ValidServer() });
	partiallyInvalid[1]["host"] = "";
	RequireRejected(partiallyInvalid);

	const auto liveDirectory = nlohmann::json{
		{ "version", 2 },
		{ "heartbeatIntervalSeconds", 30 },
		{ "ttlSeconds", 90 },
		{ "count", 2 },
		{ "servers", nlohmann::json::array({
			nlohmann::json{
				{ "id", "live-0123456789abcdef" },
				{ "name", "Live Server" },
				{ "host", "203.0.113.10" },
				{ "port", 7777 },
				{ "address", "203.0.113.10:7777" },
				{ "players", 3 },
				{ "maxPlayers", 8 },
				{ "status", "Open" },
				{ "password", false },
				{ "live", true },
				{ "lastSeen", "2026-09-23T00:00:00.000Z" },
				{ "compatibility", { { "falloutRuntime", "1.10.163" }, { "capabilities", nlohmann::json::array({ "protocol-v2" }) } } } },
			nlohmann::json{
				{ "id", "curated-1" },
				{ "name", "Curated" },
				{ "host", "servers.example.com" },
				{ "port", 7777 },
				{ "password", true },
				{ "live", false } } }) }
	};
	std::string liveError;
	const auto liveServers = ServerBrowserData::ParseDirectoryServers(liveDirectory, liveError);
	Require(liveError.empty());
	Require(liveServers.size() == 2);
	Require(liveServers[0].host == "203.0.113.10");
	Require(liveServers[0].players == 3 && liveServers[0].maxPlayers == 8);
	Require(!liveServers[0].passworded);
	Require(liveServers[1].passworded);

	Require(ServerBrowserData::UserFacingDirectoryError(liveError).empty());
	Require(ServerBrowserData::UserFacingDirectoryError("HTTP 404 from server directory.") == "The server directory is unavailable right now. Use Direct Connect or the Local tab to join a server.");
	const auto supabaseDirectory = nlohmann::json{
		{ "version", 2 },
		{ "updatedAt", "2026-10-11T00:00:00.000Z" },
		{ "heartbeatIntervalSeconds", 30 },
		{ "ttlSeconds", 90 },
		{ "servers", nlohmann::json::array({ nlohmann::json{ { "name", "Supabase Server" }, { "host", "198.51.100.7" }, { "port", 7778 }, { "address", "198.51.100.7:7778" } } }) }
	};
	std::string supabaseError;
	const auto supabaseServers = ServerBrowserData::ParseDirectoryServers(supabaseDirectory, supabaseError);
	Require(supabaseError.empty());
	Require(supabaseServers.size() == 1);
	Require(supabaseServers[0].host == "198.51.100.7" && supabaseServers[0].port == 7778);

	for (const auto& [mode, badge] : {
			std::pair{ "coop-story", "Co-op story" },
			std::pair{ "free-roam", "Free roam" },
			std::pair{ " Free-Roam ", "Free roam" } }) {
		auto listed = ValidServer();
		listed["worldMode"] = mode;
		std::string listedError;
		const auto entries = ServerBrowserData::ParseDirectoryServers(nlohmann::json::array({ listed }), listedError);
		Require(listedError.empty());
		Require(entries.size() == 1);
		Require(entries.front().worldMode == mode);
		Require(BadgeText(entries.front()) == badge);
	}

	auto snakeCase = ValidServer();
	snakeCase["world_mode"] = "free-roam";
	std::string snakeError;
	const auto snakeEntries = ServerBrowserData::ParseDirectoryServers(nlohmann::json::array({ snakeCase }), snakeError);
	Require(snakeError.empty());
	Require(snakeEntries.size() == 1);
	Require(BadgeText(snakeEntries.front()) == "Free roam");

	auto categoryOnly = ValidServer();
	categoryOnly["mode"] = "Roleplay";
	std::string categoryError;
	const auto categoryEntries = ServerBrowserData::ParseDirectoryServers(nlohmann::json::array({ categoryOnly }), categoryError);
	Require(categoryError.empty());
	Require(categoryEntries.size() == 1);
	Require(categoryEntries.front().worldMode.empty());
	Require(BadgeText(categoryEntries.front()).empty());

	auto wrongType = ValidServer();
	wrongType["worldMode"] = 7;
	RequireRejected(nlohmann::json::array({ wrongType }));

	ServerEntry remembered;
	remembered.host = "203.0.113.10";
	remembered.port = 7777;
	remembered.worldMode = "free-roam";
	Require(ServerEntry::FromJson(remembered.ToJson()).worldMode == "free-roam");
	Require(BadgeText(ServerEntry::FromJson(remembered.ToJson())) == "Free roam");
	Require(BadgeText(ServerEntry{}).empty());
	const auto official = nlohmann::json{
		{ "version", 1 },
		{ "updatedAt", nullptr },
		{ "servers", nlohmann::json::array({ nlohmann::json{
			{ "id", "commonwealth-online-server-one" },
			{ "name", "Commonwealth Online Server One" },
			{ "host", "servers.commonwealth-online.com" },
			{ "port", 7777 },
			{ "description", "Commonwealth Online Server One" },
			{ "region", "Oceania" },
			{ "tags", nlohmann::json::array({ "commonwealth", "online", "server", "one" }) },
			{ "password", false },
			{ "modlist", "" },
			{ "discord", "" } } }) }
	}.dump();

	auto select = [](std::vector<ServerBrowserData::DirectoryAttempt> a_attempts) {
		std::vector<std::string> labels;
		std::vector<const char*> urls;
		labels.reserve(a_attempts.size());
		for (std::size_t i = 0; i < a_attempts.size(); ++i) {
			labels.push_back("https://directory-" + std::to_string(i) + ".example/servers/server.json");
		}
		urls.reserve(labels.size());
		for (const auto& label : labels) {
			urls.push_back(label.c_str());
		}
		std::size_t index = 0;
		return ServerBrowserData::QueryDirectoryUrls(urls, [&](const char*, bool& cancelled) {
			cancelled = false;
			return a_attempts.at(index++);
		});
	};

	const auto liveThenCurated = select({
		{ true, R"({"version":2,"servers":[]})", {} },
		{ true, official, {} }
	});
	Require(liveThenCurated.success);
	Require(liveThenCurated.error.empty());
	Require(liveThenCurated.servers.size() == 1);
	Require(liveThenCurated.servers[0].host == "servers.commonwealth-online.com");
	Require(liveThenCurated.servers[0].port == 7777);
	Require(!liveThenCurated.servers[0].passworded);
	Require(!liveThenCurated.servers[0].live);

	const auto liveEntries = select({
		{ true, R"({"servers":[{"name":"Live","host":"203.0.113.10","port":7777}]})", {} },
		{ true, official, {} }
	});
	Require(liveEntries.success);
	Require(liveEntries.servers.size() == 2);
	Require(liveEntries.servers[0].host == "203.0.113.10");
	Require(liveEntries.servers[1].host == "servers.commonwealth-online.com");

	const auto staticAfterFailure = select({
		{ false, {}, "HTTP 404 from server directory." },
		{ true, official, {} }
	});
	Require(staticAfterFailure.success);
	Require(staticAfterFailure.servers.size() == 1);
	Require(staticAfterFailure.servers[0].name == "Commonwealth Online Server One");

	const auto bothEmpty = select({
		{ true, R"({"servers":[]})", {} },
		{ true, R"({"servers":[]})", {} }
	});
	Require(bothEmpty.success);
	Require(bothEmpty.servers.empty());
	Require(bothEmpty.error.empty());

	const auto invalidThenCurated = select({
		{ true, "not-json", {} },
		{ true, official, {} }
	});
	Require(invalidThenCurated.success);
	Require(invalidThenCurated.servers.size() == 1);

	const auto bothUnavailable = select({
		{ false, {}, "HTTP 404 from server directory." },
		{ false, {}, "HTTP 404 from server directory." }
	});
	Require(!bothUnavailable.success);
	Require(bothUnavailable.servers.empty());
	Require(ServerBrowserData::UserFacingDirectoryError(bothUnavailable.error) == "The server directory is unavailable right now. Use Direct Connect or the Local tab to join a server.");

	const auto emptyLiveOnly = select({ { true, R"({"servers":[]})", {} } });
	Require(emptyLiveOnly.success);
	Require(emptyLiveOnly.servers.empty());

	const auto liveHeartbeat = select({
		{ true, R"({"version":2,"heartbeatIntervalSeconds":30,"ttlSeconds":90,"servers":[{"name":"Live","host":"203.0.113.10","port":7777,"players":3,"maxPlayers":8,"lastSeen":"2026-09-23T00:00:00.000Z"}]})", {} },
		{ true, official, {} }
	});
	Require(liveHeartbeat.success);
	Require(liveHeartbeat.servers.size() == 2);
	Require(liveHeartbeat.servers[0].live);
	Require(liveHeartbeat.servers[0].players == 3);
	Require(liveHeartbeat.servers[0].lastSeen == "2026-09-23T00:00:00.000Z");
	Require(!liveHeartbeat.servers[1].live);
	Require(liveHeartbeat.servers[1].lastSeen.empty());

	const auto liveWinsDuplicate = select({
		{ true, R"({"version":2,"heartbeatIntervalSeconds":30,"ttlSeconds":90,"servers":[{"name":"Official","host":"SERVERS.commonwealth-online.com","port":7777,"players":5,"maxPlayers":8}]})", {} },
		{ true, official, {} }
	});
	Require(liveWinsDuplicate.success);
	Require(liveWinsDuplicate.servers.size() == 1);
	Require(liveWinsDuplicate.servers[0].live);
	Require(liveWinsDuplicate.servers[0].players == 5);
	Require(liveWinsDuplicate.servers[0].description == "Commonwealth Online Server One");

	const auto curatedDuplicateKeepsCurated = select({
		{ true, R"({"servers":[{"name":"Duplicate","host":"servers.commonwealth-online.com","port":7777,"players":2}]})", {} },
		{ true, official, {} }
	});
	Require(curatedDuplicateKeepsCurated.servers.size() == 1);
	Require(curatedDuplicateKeepsCurated.servers[0].name == "Duplicate");

	const auto explicitNotLive = select({
		{ true, R"({"version":2,"heartbeatIntervalSeconds":30,"ttlSeconds":90,"servers":[{"name":"Curated","host":"198.51.100.9","port":7777,"live":false}]})", {} }
	});
	Require(explicitNotLive.servers.size() == 1);
	Require(!explicitNotLive.servers[0].live);

	auto makeList = [](int a_count, int a_startIndex) {
		auto list = nlohmann::json::array();
		for (int offset = 0; offset < a_count; ++offset) {
			const int index = a_startIndex + offset;
			list.push_back(nlohmann::json{
				{ "name", std::format("Server {}", index) },
				{ "host", std::format("198.51.{}.{}", index / 256, index % 256) },
				{ "port", 7000 + (index % 500) } });
		}
		return list;
	};

	const auto cappedMerge = select({
		{ true, nlohmann::json{ { "servers", makeList(400, 0) } }.dump(), {} },
		{ true, nlohmann::json{ { "servers", makeList(400, 1000) } }.dump(), {} }
	});
	Require(cappedMerge.success);
	Require(cappedMerge.servers.size() == 512);

	const auto duplicatedMerge = select({
		{ true, nlohmann::json{ { "servers", makeList(512, 0) } }.dump(), {} },
		{ true, nlohmann::json{ { "servers", makeList(512, 0) } }.dump(), {} }
	});
	Require(duplicatedMerge.success);
	Require(duplicatedMerge.servers.size() == 512);

	const auto liveDirectoryPayload = [&](const nlohmann::json& a_servers) {
		return nlohmann::json{
			{ "version", 2 },
			{ "heartbeatIntervalSeconds", 30 },
			{ "ttlSeconds", 90 },
			{ "servers", a_servers } }.dump();
	};
	const auto websitePayload = nlohmann::json{
		{ "version", 1 },
		{ "servers", nlohmann::json::array({
			nlohmann::json{ { "id", "official-usa" }, { "name", "Commonwealth Online Official USA" }, { "host", "195.26.244.234" }, { "port", 7777 } },
			nlohmann::json{ { "id", "official-usa-2" }, { "name", "Commonwealth Online USA 2" }, { "host", "195.26.244.234" }, { "port", 7787 } } }) } }.dump();

	const auto fullLivePlusWebsite = select({
		{ true, liveDirectoryPayload(makeList(512, 0)), {} },
		{ true, websitePayload, {} }
	});
	Require(fullLivePlusWebsite.success);
	Require(fullLivePlusWebsite.servers.size() == 512);
	bool foundFirstOfficial = false;
	bool foundSecondOfficial = false;
	int liveCount = 0;
	for (const auto& entry : fullLivePlusWebsite.servers) {
		if (entry.host == "195.26.244.234" && entry.port == 7777) {
			foundFirstOfficial = true;
		}
		if (entry.host == "195.26.244.234" && entry.port == 7787) {
			foundSecondOfficial = true;
		}
		if (entry.live) {
			++liveCount;
		}
	}
	Require(foundFirstOfficial);
	Require(foundSecondOfficial);
	Require(liveCount == 510);

	auto websiteDuplicate = nlohmann::json{
		{ "version", 1 },
		{ "servers", nlohmann::json::array({
			nlohmann::json{ { "id", "live-duplicate" }, { "name", "Live Duplicate" }, { "host", "198.51.0.0" }, { "port", 7000 } },
			nlohmann::json{ { "id", "official-usa" }, { "name", "Commonwealth Online Official USA" }, { "host", "195.26.244.234" }, { "port", 7777 } } }) } }.dump();
	const auto duplicateWebsiteSlot = select({
		{ true, liveDirectoryPayload(makeList(512, 0)), {} },
		{ true, websiteDuplicate, {} }
	});
	Require(duplicateWebsiteSlot.success);
	Require(duplicateWebsiteSlot.servers.size() == 512);
	bool foundDuplicate = false;
	bool foundExtraOfficial = false;
	for (const auto& entry : duplicateWebsiteSlot.servers) {
		if (entry.host == "198.51.0.0" && entry.port == 7000) {
			foundDuplicate = true;
		}
		if (entry.host == "195.26.244.234" && entry.port == 7777) {
			foundExtraOfficial = true;
		}
	}
	Require(foundDuplicate);
	Require(foundExtraOfficial);

	auto tailDuplicateLive = makeList(512, 0);
	tailDuplicateLive[511] = nlohmann::json{
		{ "name", "Live Official USA" },
		{ "host", "195.26.244.234" },
		{ "port", 7777 },
		{ "players", 6 }
	};
	const auto tailDuplicateWebsite = select({
		{ true, liveDirectoryPayload(tailDuplicateLive), {} },
		{ true, websitePayload, {} }
	});
	Require(tailDuplicateWebsite.success);
	Require(tailDuplicateWebsite.servers.size() == 512);
	bool foundTailOfficial = false;
	bool foundTailSecondOfficial = false;
	for (const auto& entry : tailDuplicateWebsite.servers) {
		if (entry.host == "195.26.244.234" && entry.port == 7777) {
			foundTailOfficial = true;
			Require(entry.live);
		}
		if (entry.host == "195.26.244.234" && entry.port == 7787) {
			foundTailSecondOfficial = true;
		}
	}
	Require(foundTailOfficial);
	Require(foundTailSecondOfficial);

	using F4T::ServerBrowser::ReachabilityCache;
	using F4T::ServerBrowser::ReachabilityResult;
	namespace ReachabilityState = F4T::ServerBrowser::ReachabilityState;

	ReachabilityCache cooldown;
	Require(cooldown.Size() == 0);
	Require(!cooldown.Lookup("example.com", 7777, 100.0).has_value());
	cooldown.Record("Example.COM", 7777, ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, 42 }, 100.0);
	Require(cooldown.Size() == 1);
	const auto insideCooldown = cooldown.Lookup("example.com", 7777, 129.9);
	Require(insideCooldown.has_value());
	Require(insideCooldown->state == ReachabilityState::kAnswering);
	Require(insideCooldown->milliseconds == 42);
	Require(!cooldown.Lookup("example.com", 7777, 100.0 + ReachabilityCache::kCooldownSeconds).has_value());
	Require(!cooldown.Lookup("example.com", 7778, 100.0).has_value());
	cooldown.Record("[2001:db8::1]", 7787, ReachabilityResult{ std::string{ ReachabilityState::kNoAnswer }, "refused", 0 }, 100.0);
	Require(cooldown.Lookup("2001:db8::1", 7787, 101.0).has_value());
	Require(cooldown.Lookup("2001:db8::1", 7787, 101.0)->reason == "refused");
	cooldown.Clear("example.com", 7777);
	Require(!cooldown.Lookup("example.com", 7777, 100.0).has_value());
	Require(cooldown.Size() == 1);
	cooldown.Reset();
	Require(cooldown.Size() == 0);

	ServerBrowserData reachabilityData;
	ServerEntry first;
	first.id = "first";
	first.host = "Example.com";
	first.port = 7777;
	ServerEntry second;
	second.id = "second";
	second.host = "198.51.100.7";
	second.port = 7787;
	ServerEntry third;
	third.id = "third";
	third.host = "198.51.100.8";
	third.port = 7787;
	reachabilityData.SetAllServers({ first, second, third });
	Require(reachabilityData.SetReachability(
		"example.COM",
		7777,
		ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, 12 }));
	Require(reachabilityData.GetAllServers()[0].reachability == ReachabilityState::kAnswering);
	Require(reachabilityData.GetAllServers()[0].reachabilityMs == 12);
	Require(!reachabilityData.SetReachability(
		"203.0.113.9",
		7777,
		ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, 1 }));
	reachabilityData.SetReachability(
		"198.51.100.7",
		7787,
		ReachabilityResult{ std::string{ ReachabilityState::kChecking }, {}, 0 });
	Require(reachabilityData.ClearCheckingReachability("not checked in this refresh") == 1);
	Require(reachabilityData.GetAllServers()[1].reachability == ReachabilityState::kUnchecked);
	Require(reachabilityData.GetAllServers()[1].reachabilityReason == "not checked in this refresh");
	Require(reachabilityData.GetAllServers()[0].reachability == ReachabilityState::kAnswering);
	Require(reachabilityData.GetAllServers()[2].reachability.empty());

	ReachabilityCache epochs;
	Require(epochs.Epoch("example.com", 7777) == 0);
	Require(epochs.IsCurrent("example.com", 7777, 0));
	epochs.Record("Example.COM", 7777, ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, 5 }, 100.0);
	Require(epochs.IsCurrent("example.com", 7777, 0));
	epochs.Clear("example.com", 7777);
	Require(epochs.Epoch("example.com", 7777) == 1);
	Require(!epochs.IsCurrent("example.com", 7777, 0));
	Require(epochs.IsCurrent("example.com", 7777, 1));
	Require(epochs.Epoch("198.51.100.9", 7777) == 0);
	epochs.Reset();
	Require(epochs.Epoch("example.com", 7777) == 0);

	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Wrong password."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("This server needs a password."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Server requires client version 2.0.0."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Server requires Fallout runtime 1.11.240."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Your modpack does not match this server."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Server is full."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("modpack_mismatch"));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Server rejected the session (steam_identity_mismatch)."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Server rejected the session."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Server refused the connection: Too many connection attempts."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Server refused the connection."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("The host refused the connection."));
	Require(F4T::ServerBrowser::JoinFailureProvesAnswer("Server refused the connection (GNS reason 1000)."));
	Require(!F4T::ServerBrowser::JoinFailureProvesAnswer("Could not reach the server (GNS reason 4001)."));
	Require(!F4T::ServerBrowser::JoinFailureProvesAnswer("Server refused the connection (GNS reason 4001)."));
	Require(!F4T::ServerBrowser::JoinFailureProvesAnswer("Could not reach the server: ProblemDetectedLocally."));
	Require(!F4T::ServerBrowser::JoinFailureProvesAnswer("Connection cancelled."));
	Require(!F4T::ServerBrowser::JoinFailureProvesAnswer("Connection ended before the join could complete."));
	Require(!F4T::ServerBrowser::JoinFailureProvesAnswer("Retry failed."));
	Require(!F4T::ServerBrowser::JoinFailureProvesAnswer("Server not found."));
	Require(!F4T::ServerBrowser::JoinFailureProvesAnswer(""));

	// Invariant: Retry must not be offered for refusals a retry cannot fix.
	Require(!F4T::ServerBrowser::JoinFailureRetryable("The resume token is invalid for this profile."));
	Require(!F4T::ServerBrowser::JoinFailureRetryable("The server does not recognize this resume credential."));
	Require(!F4T::ServerBrowser::JoinFailureRetryable("This character has starting inventory or progress on the server, so it cannot be discarded here. Keep your saves."));
	Require(!F4T::ServerBrowser::JoinFailureRetryable("Your interrupted first character could not be identified (x) for profile p."));
	Require(!F4T::ServerBrowser::JoinFailureRetryable("Server rejected the session (unknown_resume): the server has no record of your saved session."));
	Require(!F4T::ServerBrowser::JoinFailureRetryable("Server rejected the session (unknown_resume)"));
	Require(!F4T::ServerBrowser::JoinFailureRetryable("Server rejected the session (invalid_resume): stale."));
	Require(F4T::ServerBrowser::JoinFailureRetryable("Server rejected the session (invalid_resume_ack): Reconnect with your previous token."));
	Require(F4T::ServerBrowser::JoinFailureRetryable("Your character could not be identified by Steam."));
	Require(F4T::ServerBrowser::JoinFailureRetryable("Could not reach the server (GNS reason 4001)."));
	Require(F4T::ServerBrowser::JoinFailureRetryable("Wrong password."));
	Require(F4T::ServerBrowser::JoinFailureRetryable(""));

	// Invariant: the unfocused watchdog must fire after the 2 s limit and restart its count on any focus.
	{
		using F4T::PrismaUI::Internal::VanillaMenuUnfocusedTooLong;
		const auto start = std::chrono::steady_clock::time_point{} + std::chrono::seconds{ 100 };
		auto since = std::chrono::steady_clock::time_point{};
		Require(!VanillaMenuUnfocusedTooLong(true, since, start));
		Require(!VanillaMenuUnfocusedTooLong(true, since, start + std::chrono::milliseconds{ 1999 }));
		Require(VanillaMenuUnfocusedTooLong(true, since, start + std::chrono::seconds{ 2 }));
		Require(!VanillaMenuUnfocusedTooLong(false, since, start + std::chrono::seconds{ 3 }));
		Require(!VanillaMenuUnfocusedTooLong(true, since, start + std::chrono::seconds{ 4 }));
	}

	Require(F4T::ServerBrowser::AddressKey(" [2001:DB8::1] ", 0) == F4T::ServerBrowser::AddressKey("2001:db8::1", 7777));
	Require(F4T::ServerBrowser::AddressKey("Example.COM", 7777) == "example.com:7777");
	Require(F4T::ServerBrowser::AddressKey("[ ::1 ]", 0) == F4T::ServerBrowser::AddressKey("::1", 7777));
	Require(F4T::ServerBrowser::AddressKey("[]", 7777) == "[]:7777");

	std::vector<ServerEntry> bracketedLive;
	bracketedLive.push_back(ServerEntry{ .host = " [2001:db8::1] ", .port = 7777, .live = true });
	std::vector<ServerEntry> bareCurated;
	bareCurated.push_back(ServerEntry{ .description = "curated", .host = "2001:db8::1", .port = 7777 });
	const auto bracketedMerge = ServerBrowserData::MergeDirectoryLists({ bracketedLive, bareCurated });
	Require(bracketedMerge.size() == 1);
	Require(bracketedMerge[0].live);
	Require(bracketedMerge[0].description == "curated");

	std::vector<ServerEntry> zeroPort;
	zeroPort.push_back(ServerEntry{ .host = "198.51.100.11", .port = 0, .live = true });
	std::vector<ServerEntry> defaultPort;
	defaultPort.push_back(ServerEntry{ .description = "curated", .host = "198.51.100.11", .port = 7777 });
	const auto defaultPortMerge = ServerBrowserData::MergeDirectoryLists({ zeroPort, defaultPort });
	Require(defaultPortMerge.size() == 1);
	Require(defaultPortMerge[0].description == "curated");
	Require(defaultPortMerge[0].port == 7777);

	const auto answeredBadge = F4T::ServerBrowser::JoinFailureBadgeFor("Wrong password.");
	Require(answeredBadge.answered);
	Require(answeredBadge.result.state == ReachabilityState::kAnswering);
	Require(answeredBadge.result.reason.empty());
	Require(answeredBadge.result.milliseconds == 0);
	const auto refusedBadge = F4T::ServerBrowser::JoinFailureBadgeFor("Connection cancelled.");
	Require(!refusedBadge.answered);
	Require(refusedBadge.result.state == ReachabilityState::kUnchecked);
	Require(refusedBadge.result.reason == F4T::ServerBrowser::kJoinFailureBadgeReason);

	std::vector<ServerEntry> listedPorts;
	listedPorts.push_back(ServerEntry{ .host = " 198.51.100.21 ", .port = 0 });
	listedPorts.push_back(ServerEntry{ .host = "198.51.100.22", .port = 5000 });
	listedPorts.push_back(ServerEntry{ .host = "198.51.100.23", .port = -1 });
	listedPorts.push_back(ServerEntry{ .host = "198.51.100.24", .port = 7777 });
	F4T::ServerBrowser::NormalizeListedEntries(listedPorts);
	Require(listedPorts[0].host == "198.51.100.21");
	Require(listedPorts[0].port == 7777);
	Require(listedPorts[1].port == 5000);
	Require(listedPorts[2].port == 7777);
	Require(listedPorts[3].port == 7777);

	ServerBrowserData badgeData;
	ServerEntry badgeEntry;
	badgeEntry.id = "badge";
	badgeEntry.host = "198.51.100.31";
	badgeEntry.port = 7777;
	badgeData.SetAllServers({ badgeEntry });
	badgeData.SetReachability("198.51.100.31", 7777, ReachabilityResult{ std::string{ ReachabilityState::kNoAnswer }, "timed out", 900 });
	Require(badgeData.GetAllServers()[0].reachability == ReachabilityState::kNoAnswer);
	Require(badgeData.SetReachability("198.51.100.31", 7777, answeredBadge.result));
	Require(badgeData.GetAllServers()[0].reachability == ReachabilityState::kAnswering);
	Require(badgeData.GetAllServers()[0].reachabilityReason.empty());
	Require(badgeData.GetAllServers()[0].reachabilityMs == 0);
	badgeData.SetReachability("198.51.100.31", 7777, ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, 42 });
	Require(badgeData.GetAllServers()[0].reachability == ReachabilityState::kAnswering);
	Require(badgeData.GetAllServers()[0].reachabilityMs == 42);
	badgeData.SetReachability("198.51.100.31", 7777, refusedBadge.result);
	Require(badgeData.GetAllServers()[0].reachability == ReachabilityState::kUnchecked);
	Require(badgeData.GetAllServers()[0].reachabilityReason == F4T::ServerBrowser::kJoinFailureBadgeReason);
	Require(badgeData.GetAllServers()[0].reachabilityMs == 0);

	ServerEntry measuredEntry;
	measuredEntry.reachability = std::string{ ReachabilityState::kAnswering };
	measuredEntry.reachabilityMs = 42;
	Require(F4T::ServerBrowser::PreserveAnsweredMilliseconds(&measuredEntry, answeredBadge.result).milliseconds == 42);
	measuredEntry.reachability = std::string{ ReachabilityState::kNoAnswer };
	Require(F4T::ServerBrowser::PreserveAnsweredMilliseconds(&measuredEntry, answeredBadge.result).milliseconds == 0);
	Require(F4T::ServerBrowser::PreserveAnsweredMilliseconds(nullptr, answeredBadge.result).milliseconds == 0);

	const auto cachedBadge = F4T::ServerBrowser::CachedOrJoinFailureBadge(
		ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, 42 });
	Require(cachedBadge.state == ReachabilityState::kAnswering);
	Require(cachedBadge.milliseconds == 42);
	const auto emptyBadge = F4T::ServerBrowser::CachedOrJoinFailureBadge(std::nullopt);
	Require(emptyBadge.state == ReachabilityState::kUnchecked);
	Require(emptyBadge.reason == F4T::ServerBrowser::kJoinFailureBadgeReason);
	Require(emptyBadge.milliseconds == 0);

	ReachabilityCache answerCache;
	answerCache.Record("198.51.100.31", 7777, ReachabilityResult{ std::string{ ReachabilityState::kNoAnswer }, "timed out", 900 }, 100.0);
	answerCache.NoteAnswering("198.51.100.31", 7777, ReachabilityResult{ std::string{ ReachabilityState::kAnswering }, {}, 42 }, 100.5);
	const auto upgradedAnswer = answerCache.Lookup("198.51.100.31", 7777, 101.0);
	Require(upgradedAnswer.has_value());
	Require(upgradedAnswer->state == ReachabilityState::kAnswering);
	Require(upgradedAnswer->milliseconds == 42);
	Require(answerCache.Epoch("198.51.100.31", 7777) == 1);
	Require(!answerCache.IsCurrent("198.51.100.31", 7777, 0));

	const char* orderedUrls[] = {
		"https://live.example/directory",
		"https://static.example/servers/server.json",
		"https://unused.example/servers/server.json"
	};
	std::vector<std::string> fetched;
	const auto warned = ServerBrowserData::QueryDirectoryUrls(orderedUrls, [&](const char* url, bool& cancelled) {
		cancelled = false;
		fetched.emplace_back(url);
		if (fetched.size() == 1) {
			return ServerBrowserData::DirectoryAttempt{ false, {}, "HTTP 404 from server directory." };
		}
		return ServerBrowserData::DirectoryAttempt{ true, official, {} };
	});
	Require(warned.success);
	Require(!warned.cancelled);
	Require(warned.servers.size() == 1);
	Require(warned.servers[0].host == "servers.commonwealth-online.com");
	Require(warned.error.find("https://live.example/directory") != std::string::npos);
	Require(warned.error.find("HTTP 404 from server directory.") != std::string::npos);
	Require(fetched.size() == 3);
	Require(fetched[0] == orderedUrls[0]);
	Require(fetched[1] == orderedUrls[1]);
	Require(fetched[2] == orderedUrls[2]);

	fetched.clear();
	const auto cancelled = ServerBrowserData::QueryDirectoryUrls(orderedUrls, [&](const char* url, bool& cancelledFlag) {
		fetched.emplace_back(url);
		cancelledFlag = true;
		return ServerBrowserData::DirectoryAttempt{};
	});
	Require(cancelled.cancelled);
	Require(!cancelled.success);
	Require(cancelled.servers.empty());
	Require(fetched.size() == 1);
	Require(fetched[0] == orderedUrls[0]);

	ServerEntry listedServer;
	listedServer.id = "listed";
	listedServer.host = "198.51.100.41";
	listedServer.port = 7777;
	std::vector<ServerEntry> listedList{ listedServer };
	ServerEntry savedById;
	savedById.id = "listed";
	savedById.host = "203.0.113.1";
	savedById.port = 7777;
	ServerEntry savedMissing;
	savedMissing.id = "gone";
	savedMissing.name = "Gone";
	savedMissing.host = "198.51.100.99";
	savedMissing.port = 7777;
	F4T::ServerBrowser::ApplySavedFavorites(listedList, { savedById, savedMissing });
	Require(listedList.size() == 2);
	Require(listedList[0].favorite);
	Require(listedList[1].savedOnly);
	Require(listedList[1].favorite);
	Require(listedList[1].id == "gone");
	Require(listedList[1].reachability == ReachabilityState::kUnchecked);

	ServerEntry savedByAddress;
	savedByAddress.id = "renamed";
	savedByAddress.host = "198.51.100.41";
	savedByAddress.port = 7777;
	std::vector<ServerEntry> endpointList{ listedServer };
	F4T::ServerBrowser::ApplySavedFavorites(endpointList, { savedByAddress });
	Require(endpointList.size() == 1);
	Require(endpointList[0].favorite);
	Require(!endpointList[0].savedOnly);

	ServerBrowserData favoriteData;
	favoriteData.SetAllServers(listedList);
	const auto offlineToggle = favoriteData.ToggleFavorite("gone");
	Require(offlineToggle.has_value());
	Require(!offlineToggle->favorite);
	Require(favoriteData.GetAllServers().size() == 1);
	Require(favoriteData.FindServer("gone") == nullptr);
	const auto listedToggle = favoriteData.ToggleFavorite("listed");
	Require(listedToggle.has_value());
	Require(!listedToggle->favorite);
	Require(favoriteData.GetAllServers().size() == 1);
	Require(!favoriteData.ToggleFavorite("unknown").has_value());

	Require(listedList[1].ToJson().value("savedOnly", false));
	Require(!listedList[0].ToJson().contains("savedOnly"));

	std::string favoriteError;
	const nlohmann::json validFavorite{ { "id", "listed" }, { "name", "Listed" }, { "host", "198.51.100.41" }, { "port", 7777 } };
	Require(F4T::ServerBrowser::ValidateFavoriteServerJson(validFavorite, favoriteError));
	Require(F4T::ServerBrowser::ValidateFavoriteServerJson(nlohmann::json{ { "host", "198.51.100.41" } }, favoriteError));
	Require(!F4T::ServerBrowser::ValidateFavoriteServerJson(nlohmann::json::array({ 1 }), favoriteError));
	Require(!F4T::ServerBrowser::ValidateFavoriteServerJson(nlohmann::json{ { "host", "h" }, { "port", 0 } }, favoriteError));
	Require(!F4T::ServerBrowser::ValidateFavoriteServerJson(nlohmann::json{ { "host", "h" }, { "port", 65536 } }, favoriteError));
	Require(!F4T::ServerBrowser::ValidateFavoriteServerJson(nlohmann::json{ { "id", std::string(257, 'i') } }, favoriteError));
	Require(!F4T::ServerBrowser::ValidateFavoriteServerJson(nlohmann::json{ { "name", std::string(129, 'n') } }, favoriteError));
	Require(!F4T::ServerBrowser::ValidateFavoriteServerJson(nlohmann::json{ { "host", std::string(256, 'h') } }, favoriteError));
	Require(!F4T::ServerBrowser::ValidateFavoriteServerJson(nlohmann::json{ { "name", 5 } }, favoriteError));

	return 0;
}
