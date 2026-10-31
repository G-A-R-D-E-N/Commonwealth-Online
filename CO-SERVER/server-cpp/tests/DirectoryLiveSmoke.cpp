#include "DirectoryRegistrar.h"

#include <atomic>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	using namespace CommonwealthOnline::Server;

	if (argc < 2) {
		std::cerr << "usage: CommonwealthOnlineServerDirectoryLiveSmoke DIRECTORY_URL\n";
		return 2;
	}

	std::atomic_size_t players{ 2 };
	auto sender = CreatePlatformDirectorySender();
	DirectoryRegistrar registrar{
		argv[1],
		DirectoryListing{ .name = "Directory Live Smoke", .description = "smoke", .port = 7777, .maximumPlayers = 8, .capabilities = { "protocol-v2" } },
		players,
		sender
	};

	auto fail = [&](std::string_view a_step) {
		std::cerr << a_step << " failed: " << ToJson(registrar.Status()).dump() << '\n';
		return 1;
	};

	(void)registrar.Tick();
	if (registrar.Status().state != DirectoryState::Registered) return fail("register");
	const auto serverId = registrar.Status().serverId;

	players = 5;
	(void)registrar.Tick();
	if (registrar.Status().state != DirectoryState::Registered) return fail("heartbeat");

	const auto listing = sender("GET", argv[1], {}, {});
	const auto parsed = nlohmann::json::parse(listing.body, nullptr, false);
	bool found = false;
	if (parsed.is_object() && parsed.contains("servers") && parsed["servers"].is_array()) {
		for (const auto& server : parsed["servers"]) {
			if (server.value("id", std::string{}) == serverId) {
				found = server.value("players", -1) == 5 && server.value("live", false);
			}
		}
	}
	if (!found) {
		std::cerr << "listing did not contain the live heartbeat: " << listing.body << '\n';
		return 1;
	}

	registrar.Unregister();
	const auto after = nlohmann::json::parse(sender("GET", argv[1], {}, {}).body, nullptr, false);
	for (const auto& server : after.value("servers", nlohmann::json::array())) {
		if (server.value("id", std::string{}) == serverId) {
			std::cerr << "listing survived unregister\n";
			return 1;
		}
	}

	std::cout << "directory live smoke passed (" << serverId << ")\n";
	return 0;
}
