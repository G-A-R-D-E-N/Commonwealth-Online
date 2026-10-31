#include "LanDiscoveryService.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

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
#ifdef _WIN32
	using NativeSocket = SOCKET;
	constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
#else
	using NativeSocket = int;
	constexpr NativeSocket InvalidSocket = -1;
#endif

	void CloseSocket(NativeSocket a_socket)
	{
#ifdef _WIN32
		closesocket(a_socket);
#else
		close(a_socket);
#endif
	}

	nlohmann::json Discover(std::uint16_t a_port)
	{
		const auto client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		assert(client != InvalidSocket);
#ifdef _WIN32
		DWORD timeout = 2000;
		assert(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0);
#else
		timeval timeout{};
		timeout.tv_sec = 2;
		assert(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
#endif

		sockaddr_in destination{};
		destination.sin_family = AF_INET;
		destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		destination.sin_port = htons(a_port);
		const auto request = nlohmann::json{
			{ "type", "discover" },
			{ "protocol", "commonwealth-online" }
		}.dump();
		const auto sent = sendto(
			client,
			request.data(),
#ifdef _WIN32
			static_cast<int>(request.size()),
#else
			request.size(),
#endif
			0,
			reinterpret_cast<const sockaddr*>(&destination),
			sizeof(destination));
		assert(sent > 0);

		std::array<char, 4096> buffer{};
		sockaddr_in source{};
#ifdef _WIN32
		int sourceLength = sizeof(source);
		const auto count = recvfrom(client, buffer.data(), static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&source), &sourceLength);
		assert(count != SOCKET_ERROR && count > 0);
#else
		socklen_t sourceLength = sizeof(source);
		const auto count = recvfrom(client, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&source), &sourceLength);
		assert(count > 0);
#endif
		CloseSocket(client);
		return nlohmann::json::parse(std::string{ buffer.data(), static_cast<std::size_t>(count) });
	}

	void SendJunk(std::uint16_t a_port, int a_packets)
	{
		const auto client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		assert(client != InvalidSocket);
		sockaddr_in destination{};
		destination.sin_family = AF_INET;
		destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		destination.sin_port = htons(a_port);
		const std::string junk[] = { "not json", R"({"type":"discover","protocol":"other"})", R"({"type":5})", "[]" };
		for (int index = 0; index < a_packets; ++index) {
			const auto& payload = junk[index % 4];
			(void)sendto(
				client,
				payload.data(),
#ifdef _WIN32
				static_cast<int>(payload.size()),
#else
				payload.size(),
#endif
				0,
				reinterpret_cast<const sockaddr*>(&destination),
				sizeof(destination));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		CloseSocket(client);
	}
}

int main()
{
	using CommonwealthOnline::Server::LanDiscoveryService;

	std::atomic_size_t players{ 2 };
	LanDiscoveryService discovery{ "Discovery Test", "Native discovery", 7777, 8, players, false, 0 };
	std::string error;
	assert(discovery.Start(error));
	assert(error.empty());
	assert(discovery.LocalPort() != 0);

	auto response = Discover(discovery.LocalPort());
	assert(response.value("type", std::string{}) == "discoverResponse");
	assert(response.value("protocol", std::string{}) == "commonwealth-online");
	assert(response.value("version", 0) == 1);
	assert(response.value("name", std::string{}) == "Discovery Test");
	assert(response.value("description", std::string{}) == "Native discovery");
	assert(response.value("port", 0) == 7777);
	assert(response.value("players", 0) == 2);
	assert(response.value("maxPlayers", 0) == 8);
	assert(response.value("passworded", true) == false);

	players.store(5);
	response = Discover(discovery.LocalPort());
	assert(response.value("players", 0) == 5);

	LanDiscoveryService junkTarget{ "Junk Test", "Native discovery", 7777, 8, players, false, 0 };
	assert(junkTarget.Start(error));
	SendJunk(junkTarget.LocalPort(), 2 * static_cast<int>(LanDiscoveryService::MaximumRepliesPerSecond));
	assert(Discover(junkTarget.LocalPort()).value("name", std::string{}) == "Junk Test");
	junkTarget.Stop();

	LanDiscoveryService locked{ "Locked Test", "Native discovery", 7777, 8, players, true, 0 };
	assert(locked.Start(error));
	const auto lockedPort = locked.LocalPort();
	assert(lockedPort != 0);
	assert(Discover(locked.LocalPort()).value("passworded", false) == true);
	locked.Stop();
	locked.Stop();

	LanDiscoveryService restarted{ "Locked Test", "Native discovery", 7777, 8, players, true, lockedPort };
	assert(restarted.Start(error));
	assert(restarted.LocalPort() == lockedPort);
	assert(Discover(restarted.LocalPort()).value("passworded", false) == true);
	restarted.Stop();

	LanDiscoveryService busy{ "Busy Test", "Native discovery", 7777, 8, players, false, 0 };
	assert(busy.Start(error));
	const auto busyPort = busy.LocalPort();
	assert(busyPort != 0);
	std::atomic_bool floodStopping{};
	std::thread flooder{ [&floodStopping, busyPort] {
		const auto client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (client == InvalidSocket) {
			return;
		}
		sockaddr_in destination{};
		destination.sin_family = AF_INET;
		destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		destination.sin_port = htons(busyPort);
		const auto request = nlohmann::json{
			{ "type", "discover" },
			{ "protocol", "commonwealth-online" }
		}.dump();
		while (!floodStopping.load()) {
			(void)sendto(
				client,
				request.data(),
#ifdef _WIN32
				static_cast<int>(request.size()),
#else
				request.size(),
#endif
				0,
				reinterpret_cast<const sockaddr*>(&destination),
				sizeof(destination));
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		CloseSocket(client);
	} };
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	busy.Stop();
	assert(busy.LocalPort() == 0);
	floodStopping.store(true);
	flooder.join();

	discovery.Stop();
	assert(discovery.LocalPort() == 0);
	using CommonwealthOnline::Server::LanDiscoveryService;
	assert(LanDiscoveryService::IsLocalNetworkSource(0x7F000001u));
	assert(LanDiscoveryService::IsLocalNetworkSource(0x0A000001u));
	assert(LanDiscoveryService::IsLocalNetworkSource(0xC0A80001u));
	assert(LanDiscoveryService::IsLocalNetworkSource(0xAC100001u));
	assert(LanDiscoveryService::IsLocalNetworkSource(0xAC1F0001u));
	assert(LanDiscoveryService::IsLocalNetworkSource(0xA9FE0001u));
	assert(LanDiscoveryService::IsLocalNetworkSource(0x64400001u));
	assert(!LanDiscoveryService::IsLocalNetworkSource(0x08080808u));
	assert(!LanDiscoveryService::IsLocalNetworkSource(0xAC0F0001u));
	assert(!LanDiscoveryService::IsLocalNetworkSource(0xAC200001u));
	assert(!LanDiscoveryService::IsLocalNetworkSource(0x01020304u));

	return 0;
}
