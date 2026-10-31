#include "F4THttpGet.h"
#include "F4TServerBrowserData.h"
#include "F4TWorldMode.h"

#include <cassert>
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

	return 0;
}
