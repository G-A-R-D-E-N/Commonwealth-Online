#include "LanDiscoveryService.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

	struct CapturedLog
	{
		std::mutex mutex;
		std::vector<std::string> lines;

		CommonwealthOnline::Server::LanDiscoveryService::LogSink Sink()
		{
			return [this](std::string_view a_line) {
				const std::scoped_lock lock(mutex);
				lines.emplace_back(a_line);
			};
		}

		std::vector<std::string> Snapshot()
		{
			const std::scoped_lock lock(mutex);
			return lines;
		}

		std::size_t Count(std::string_view a_text)
		{
			std::size_t count{};
			for (const auto& line : Snapshot()) {
				count += line.find(a_text) != std::string::npos ? 1 : 0;
			}
			return count;
		}

		bool WaitFor(std::string_view a_text)
		{
			for (int attempt = 0; attempt < 200; ++attempt) {
				if (Count(a_text) != 0) {
					return true;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			return false;
		}
	};

	const std::string DiscoverRequest = R"({"type":"discover","protocol":"commonwealth-online"})";

	void TestDropReasonLogs()
	{
		using CommonwealthOnline::Server::LanDiscoveryService;
		std::atomic_size_t players{ 0 };

		CapturedLog publicLog;
		LanDiscoveryService publicSource{ "Log Test", "", 7777, 8, players, false, 0, publicLog.Sink() };
		assert(!publicSource.HandleDatagram(0x19010203u, 50000, DiscoverRequest, 0.0));
		assert(!publicSource.HandleDatagram(0x1A010203u, 50001, DiscoverRequest, 0.0));
		auto lines = publicLog.Snapshot();
		assert(lines.size() == 2);
		assert(lines[0].find("packet from 25.1.2.3:50000 dropped: source is not a loopback, private") != std::string::npos);
		assert(lines[1].find("packet from 26.1.2.3:50001 dropped: source is not") != std::string::npos);

		CapturedLog junkLog;
		LanDiscoveryService junk{ "Log Test", "", 7777, 8, players, false, 0, junkLog.Sink() };
		assert(!junk.HandleDatagram(0xC0A80105u, 50000, R"({"type":5,"protocol":"commonwealth-online"})", 0.0));
		assert(!junk.HandleDatagram(0xC0A80105u, 50000, "not json", 0.0));
		assert(junkLog.Count("packet from 192.168.1.5:50000 dropped: not a commonwealth-online discover request") == 2);

		CapturedLog sourceLog;
		LanDiscoveryService perSource{ "Log Test", "", 7777, 8, players, false, 0, sourceLog.Sink() };
		for (std::uint32_t reply = 0; reply < LanDiscoveryService::MaximumRepliesPerSourcePerSecond; ++reply) {
			const auto response = perSource.HandleDatagram(0xC0A80105u, 50000, DiscoverRequest, 0.0);
			assert(response && nlohmann::json::parse(*response).value("name", std::string{}) == "Log Test");
		}
		assert(sourceLog.Snapshot().empty());
		assert(!perSource.HandleDatagram(0xC0A80105u, 50000, DiscoverRequest, 0.0));
		lines = sourceLog.Snapshot();
		assert(lines.size() == 1);
		assert(lines[0] == "LAN discovery: request from 192.168.1.5:50000 dropped: per-source reply limit of 4 per second reached");
		assert(perSource.HandleDatagram(0xC0A80105u, 50000, DiscoverRequest, 1.0));

		CapturedLog serverLog;
		LanDiscoveryService serverWide{ "Log Test", "", 7777, 8, players, false, 0, serverLog.Sink() };
		std::size_t replies{};
		for (std::uint32_t source = 0; replies < LanDiscoveryService::MaximumRepliesPerSecond; ++source) {
			for (std::uint32_t reply = 0; reply < LanDiscoveryService::MaximumRepliesPerSourcePerSecond &&
				replies < LanDiscoveryService::MaximumRepliesPerSecond; ++reply, ++replies) {
				assert(serverWide.HandleDatagram(0x0A000001u + source, 50000, DiscoverRequest, 0.0));
			}
		}
		assert(serverLog.Snapshot().empty());
		assert(!serverWide.HandleDatagram(0x0A0000FFu, 50002, DiscoverRequest, 0.0));
		lines = serverLog.Snapshot();
		assert(lines.size() == 1);
		assert(lines[0] == "LAN discovery: request from 10.0.0.255:50002 dropped: server reply limit of 32 per second reached");

		for (int extra = 0; extra < 20; ++extra) {
			assert(!serverWide.HandleDatagram(0x0A0000FFu, 50002, DiscoverRequest, 0.5));
		}
		lines = serverLog.Snapshot();
		assert(lines.size() == LanDiscoveryService::MaximumLogLinesPerSecond);
		assert(!serverWide.HandleDatagram(0x19010203u, 50003, DiscoverRequest, 1.5));
		lines = serverLog.Snapshot();
		assert(lines.size() == LanDiscoveryService::MaximumLogLinesPerSecond + 2);
		assert(lines[LanDiscoveryService::MaximumLogLinesPerSecond] == "LAN discovery: 13 more request lines suppressed");
		assert(lines.back().find("packet from 25.1.2.3:50003 dropped") != std::string::npos);
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

	TestDropReasonLogs();

	std::atomic_size_t players{ 2 };
	CapturedLog discoveryLog;
	LanDiscoveryService discovery{ "Discovery Test", "Native discovery", 7777, 8, players, false, 0, discoveryLog.Sink() };
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

	assert(discoveryLog.WaitFor("LAN discovery: replied to 127.0.0.1:"));
	assert(discoveryLog.Count("with game port 7777") == 1);

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
