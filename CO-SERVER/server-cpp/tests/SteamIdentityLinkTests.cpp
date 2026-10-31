#include "SteamIdentityLink.h"

#include "AdminControlServer.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace
{
	using CommonwealthOnline::Server::AdminActionResult;
	using CommonwealthOnline::Server::AdminControlServer;
	using CommonwealthOnline::Server::SteamHostLinkPublication;
	using CommonwealthOnline::Server::SteamIdentityLink;
	using CommonwealthOnline::Server::SteamIdSource;
	using Json = nlohmann::json;

	constexpr std::uint32_t kLoopback = 0x7F000001u;
	constexpr std::uint32_t kLoopbackOther = 0x7F000102u;
	constexpr std::uint32_t kLan = 0xC0A80105u;
	constexpr auto kGuest = "76561198000000042";
	constexpr auto kOtherGuest = "76561198000000099";
	constexpr auto kHost = "76561198000000007";

#ifdef _WIN32
	using NativeSocket = SOCKET;
	constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
	void CloseSocket(NativeSocket a_socket) { closesocket(a_socket); }
#else
	using NativeSocket = int;
	constexpr NativeSocket InvalidSocket = -1;
	void CloseSocket(NativeSocket a_socket) { close(a_socket); }
#endif

	Json SendRequest(std::uint16_t a_port, const Json& a_request)
	{
		const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		assert(client != InvalidSocket);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(a_port);
		assert(connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
		const auto payload = a_request.dump() + "\n";
		std::size_t sent = 0;
		while (sent < payload.size()) {
#ifdef _WIN32
			const auto count = send(client, payload.data() + sent, static_cast<int>(payload.size() - sent), 0);
#else
			const auto count = send(client, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
#endif
			assert(count > 0);
			sent += static_cast<std::size_t>(count);
		}
		std::string line;
		std::array<char, 1024> buffer{};
		while (line.find('\n') == std::string::npos) {
#ifdef _WIN32
			const auto count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
			const auto count = recv(client, buffer.data(), buffer.size(), 0);
#endif
			assert(count > 0);
			line.append(buffer.data(), static_cast<std::size_t>(count));
		}
		CloseSocket(client);
		return Json::parse(line.substr(0, line.find('\n')));
	}

	void TestTunnelBindingResolvesOnlyOnLoopback()
	{
		SteamIdentityLink link;
		assert(link.Bind(40000, kGuest));
		const auto bound = link.Resolve(kLoopback, 40000, {});
		assert(bound && bound->steamId == kGuest && bound->source == SteamIdSource::SteamTunnel && !bound->passwordRequired);
		assert(link.Resolve(kLoopbackOther, 40000, {}).has_value());
		assert(!link.Resolve(kLan, 40000, {}).has_value());
		assert(!link.Resolve(kLoopback, 40001, {}).has_value());
		assert(!link.Resolve(kLoopback, 40001, kGuest).has_value());

		const auto spoofed = link.Resolve(kLoopback, 40000, kOtherGuest);
		assert(spoofed && spoofed->steamId == kGuest);

		assert(link.Unbind(40000));
		assert(!link.Unbind(40000));
		assert(!link.Resolve(kLoopback, 40000, {}).has_value());
	}

	void TestBindRejectsInvalidInput()
	{
		SteamIdentityLink link;
		assert(!link.Bind(0, kGuest));
		assert(!link.Bind(40000, "76561197960265728"));
		assert(!link.Bind(40000, "103582791429521408"));
		assert(!link.Bind(40000, ""));
		assert(link.BindingCount() == 0);

		for (std::uint16_t port = 1; port <= F4T::SteamHostLink::kMaximumBindings; ++port) {
			assert(link.Bind(port, kGuest));
		}
		assert(!link.Bind(static_cast<std::uint16_t>(F4T::SteamHostLink::kMaximumBindings + 1), kGuest));
		assert(link.Bind(1, kOtherGuest));
		assert(link.Resolve(kLoopback, 1, {})->steamId == kOtherGuest);
	}

	void TestHostSelfNeedsLoopbackAndExactClaim()
	{
		SteamIdentityLink link;
		assert(!link.Resolve(kLoopback, 50000, kHost).has_value());
		assert(link.SetHost(kHost));
		const auto host = link.Resolve(kLoopback, 50000, kHost);
		assert(host && host->steamId == kHost && host->source == SteamIdSource::HostSelf);
		assert(!link.Resolve(kLoopback, 50000, kGuest).has_value());
		assert(!link.Resolve(kLoopback, 50000, {}).has_value());
		assert(!link.Resolve(kLan, 50000, kHost).has_value());

		assert(link.Bind(40000, kGuest));
		const auto boundWins = link.Resolve(kLoopback, 40000, kHost);
		assert(boundWins && boundWins->steamId == kGuest && boundWins->source == SteamIdSource::SteamTunnel);

		assert(!link.SetHost("12345"));
		assert(link.Resolve(kLoopback, 50000, kHost).has_value());
		assert(link.SetHost(""));
		assert(!link.Resolve(kLoopback, 50000, kHost).has_value());

		assert(link.SetHost(kHost));
		link.Clear();
		assert(link.BindingCount() == 0);
		assert(!link.Resolve(kLoopback, 50000, kHost).has_value());
	}

	void TestHandleRequestCommands()
	{
		SteamIdentityLink link;
		assert(link.HandleRequest(Json{ { "cmd", "steam_bind" }, { "port", 40000 }, { "steamId", kGuest }, { "id", 7 } }).at("ok") == true);
		assert(link.HandleRequest(Json{ { "cmd", "steam_bind" }, { "port", 40000 }, { "steamId", kGuest }, { "id", 7 } }).at("id") == 7);
		assert(link.BindingCount() == 1);
		for (const Json& port : { Json(0), Json(-1), Json(70000), Json("40001"), Json(true), Json(40001.5), Json(nullptr) }) {
			assert(link.HandleRequest(Json{ { "cmd", "steam_bind" }, { "port", port }, { "steamId", kGuest } }).at("ok") == false);
		}
		assert(link.HandleRequest(Json{ { "cmd", "steam_bind" }, { "port", 40001 }, { "steamId", 76561198000000042ull } }).at("ok") == false);
		assert(link.HandleRequest(Json{ { "cmd", "steam_bind" }, { "port", 40001 } }).at("ok") == false);
		assert(link.BindingCount() == 1);

		const auto unbound = link.HandleRequest(Json{ { "cmd", "steam_unbind" }, { "port", 40000 } });
		assert(unbound.at("ok") == true);
		assert(link.BindingCount() == 0);
		assert(link.HandleRequest(Json{ { "cmd", "steam_unbind" } }).at("ok") == false);

		assert(link.HandleRequest(Json{ { "cmd", "steam_host" }, { "steamId", kHost } }).at("ok") == true);
		assert(link.Resolve(kLoopback, 50000, kHost).has_value());
		assert(link.HandleRequest(Json{ { "cmd", "steam_host" }, { "steamId", "bogus" } }).at("ok") == false);
		assert(link.HandleRequest(Json{ { "cmd", "steam_clear" } }).at("ok") == true);
		assert(!link.Resolve(kLoopback, 50000, kHost).has_value());

		assert(link.HandleRequest(Json{ { "cmd", "steam_bind" }, { "port", 40002 }, { "steamId", kGuest }, { "passwordRequired", true } }).at("ok") == true);
		assert(link.Resolve(kLoopback, 40002, {})->passwordRequired);
		assert(link.HandleRequest(Json{ { "cmd", "steam_bind" }, { "port", 40003 }, { "steamId", kGuest }, { "passwordRequired", false } }).at("ok") == true);
		assert(!link.Resolve(kLoopback, 40003, {})->passwordRequired);
		assert(link.HandleRequest(Json{ { "cmd", "steam_bind" }, { "port", 40004 }, { "steamId", kGuest }, { "passwordRequired", 1 } }).at("ok") == false);
		assert(!link.Resolve(kLoopback, 40004, {}).has_value());
		assert(link.SetHost(kHost) && !link.Resolve(kLoopback, 50000, kHost)->passwordRequired);

		auto policy = link.HandleRequest(Json{ { "cmd", "steam_policy" }, { "id", 9 } });
		assert(policy.at("ok") == true && policy.at("id") == 9);
		assert(policy.at("data").at("friendsOnly") == true && policy.at("data").at("serverPasswordSet") == false);
		link.SetPolicy(F4T::SteamAdmission::HostPolicy{ false, true });
		policy = link.HandleRequest(Json{ { "cmd", "steam_policy" } });
		assert(policy.at("data").at("friendsOnly") == false && policy.at("data").at("serverPasswordSet") == true);

		assert(link.HandleRequest(Json{ { "cmd", "shutdown" } }).at("ok") == false);
		assert(link.HandleRequest(Json{ { "cmd", "kick" }, { "playerId", 1 } }).at("ok") == false);
		assert(link.HandleRequest(Json::object()).at("ok") == false);
	}

	void TestLiveSettingsCommands()
	{
		SteamIdentityLink link;
		assert(link.HandleRequest(Json{ { "cmd", "apply_settings" }, { "document", Json::object() } }).at("ok") == false);
		assert(link.HandleRequest(Json{ { "cmd", "apply_settings" } }).at("ok") == false);
		assert(link.HandleRequest(Json{ { "cmd", "server_config" } }).at("ok") == false);

		Json seen;
		link.SetApplySettingsHandler([&seen](const Json& a_document) {
			seen = a_document;
			return AdminActionResult{ true, "Server settings applied live.", Json{ { "applied", Json::array({ "enableChat" }) } } };
		});
		const auto accepted = link.HandleRequest(Json{ { "cmd", "apply_settings" }, { "document", Json{ { "enable_chat", false } } }, { "id", 11 } });
		assert(accepted.at("ok") == true);
		assert(accepted.at("id") == 11);
		assert(accepted.at("data").at("applied").at(0) == "enableChat");
		assert(seen.at("enable_chat") == false);

		link.SetApplySettingsHandler([](const Json&) { return AdminActionResult{ false, "Factions cannot be switched.", nullptr }; });
		const auto refused = link.HandleRequest(Json{ { "cmd", "apply_settings" }, { "document", Json::object() } });
		assert(refused.at("ok") == false);
		assert(refused.at("error") == "Factions cannot be switched.");

		link.SetServerConfigProvider([] { return Json{ { "configPath", "C:/game/Data/server/commonwealth-server.json" } }; });
		const auto config = link.HandleRequest(Json{ { "cmd", "server_config" } });
		assert(config.at("ok") == true);
		assert(config.at("data").at("configPath") == "C:/game/Data/server/commonwealth-server.json");

		link.SetApplySettingsHandler(nullptr);
		link.SetServerConfigProvider(nullptr);
		assert(link.HandleRequest(Json{ { "cmd", "apply_settings" }, { "document", Json::object() } }).at("ok") == false);
		assert(link.HandleRequest(Json{ { "cmd", "server_config" } }).at("ok") == false);
	}

	void TestPublicationWritesAndRemovesLinkFiles()
	{
		const auto directory = std::filesystem::temp_directory_path() / "co-steam-host-link-test";
		std::filesystem::remove_all(directory);
		const auto portPath = F4T::SteamHostLink::PortPath(directory, 7777);
		const auto tokenPath = F4T::SteamHostLink::TokenPath(directory, 7777);
		std::filesystem::create_directories(directory);
		std::ofstream(tokenPath) << "stale-token";
		assert(tokenPath.filename() == "7777.token");
		{
			SteamHostLinkPublication publication;
			std::string error;
			assert(publication.Publish(portPath, tokenPath, 51234, error));
			std::ifstream input(portPath);
			std::uint32_t port = 0;
			input >> port;
			assert(port == 51234);
			assert(!std::filesystem::exists(std::filesystem::path{ portPath }.concat(".tmp")));
			assert(std::filesystem::exists(tokenPath));
		}
		assert(!std::filesystem::exists(portPath));
		assert(!std::filesystem::exists(tokenPath));
		std::filesystem::remove_all(directory);
	}

	void TestStaleLinkFilesAreRemoved()
	{
		const auto directory = std::filesystem::temp_directory_path() / "co-steam-host-link-stale";
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		std::ofstream(F4T::SteamHostLink::PortPath(directory, 7777)) << 51234 << '\n';
		std::ofstream(F4T::SteamHostLink::TokenPath(directory, 7777)) << "stale-token";

		F4T::SteamHostLink::RemoveLinkFiles(directory, 7777);
		assert(!std::filesystem::exists(F4T::SteamHostLink::PortPath(directory, 7777)));
		assert(!std::filesystem::exists(F4T::SteamHostLink::TokenPath(directory, 7777)));

		std::filesystem::remove_all(directory);
	}

	void TestAuthenticatedLinkChannelRoundTrip()
	{
		SteamIdentityLink link;
		AdminControlServer server{
			0,
			"link-token-for-tests",
			{},
			[&link](const Json& a_request) { return link.HandleRequest(a_request); }
		};
		std::string error;
		assert(server.Start(error));
		const auto port = server.LocalPort();
		assert(port != 0);

		const auto unauthorized = SendRequest(port, Json{ { "adminToken", "wrong" }, { "cmd", "steam_bind" }, { "port", 40000 }, { "steamId", kGuest } });
		assert(unauthorized.at("ok") == false);
		const auto missingToken = SendRequest(port, Json{ { "cmd", "steam_bind" }, { "port", 40000 }, { "steamId", kGuest } });
		assert(missingToken.at("ok") == false);
		assert(link.BindingCount() == 0);

		const auto bound = SendRequest(port, Json{ { "adminToken", "link-token-for-tests" }, { "command", "STEAM_BIND" }, { "port", 40000 }, { "steamId", kGuest } });
		assert(bound.at("ok") == true);
		assert(link.Resolve(kLoopback, 40000, {})->steamId == kGuest);

		const auto adminCommand = SendRequest(port, Json{ { "adminToken", "link-token-for-tests" }, { "cmd", "shutdown" } });
		assert(adminCommand.at("ok") == false);
		CommonwealthOnline::Server::AdminCommand queued;
		assert(!server.TryPop(queued));
		server.Stop();
	}
}

int main()
{
	TestTunnelBindingResolvesOnlyOnLoopback();
	TestBindRejectsInvalidInput();
	TestHostSelfNeedsLoopbackAndExactClaim();
	TestHandleRequestCommands();
	TestLiveSettingsCommands();
	TestPublicationWritesAndRemovesLinkFiles();
	TestStaleLinkFilesAreRemoved();
	TestAuthenticatedLinkChannelRoundTrip();
	return 0;
}
