#include "F4TDebugLogging.h"
#include "F4TLanDiscovery.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
	std::mutex g_logMutex;
	std::vector<std::string> g_logLines;

	std::vector<std::string> TakeLog()
	{
		const std::scoped_lock lock(g_logMutex);
		auto lines = std::move(g_logLines);
		g_logLines.clear();
		return lines;
	}

	std::size_t CountLines(const std::vector<std::string>& a_lines, std::string_view a_text)
	{
		return static_cast<std::size_t>(std::ranges::count_if(a_lines, [a_text](const std::string& a_line) {
			return a_line.find(a_text) != std::string::npos;
		}));
	}

	SOCKET BindLoopbackUdp(std::uint16_t& a_port)
	{
		const auto socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		assert(socketHandle != INVALID_SOCKET);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		assert(bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
		int length = sizeof(address);
		assert(getsockname(socketHandle, reinterpret_cast<sockaddr*>(&address), &length) == 0);
		a_port = ntohs(address.sin_port);
		DWORD timeout = 50;
		assert(setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0);
		return socketHandle;
	}

	void TestScanLogsRequestAndResponse()
	{
		std::uint16_t port{};
		const auto responder = BindLoopbackUdp(port);
		std::atomic_bool stopping{};
		std::atomic_int requests{};
		std::thread stub{ [&] {
			std::array<char, 2048> buffer{};
			const auto reply = nlohmann::json{
				{ "type", "discoverResponse" },
				{ "protocol", "commonwealth-online" },
				{ "version", 1 },
				{ "name", "Stub Responder" },
				{ "description", "LAN discovery test" },
				{ "port", 7777 },
				{ "players", 1 },
				{ "maxPlayers", 8 }
			}.dump();
			while (!stopping.load()) {
				sockaddr_in source{};
				int sourceLength = sizeof(source);
				const auto count = recvfrom(responder, buffer.data(), static_cast<int>(buffer.size()), 0,
					reinterpret_cast<sockaddr*>(&source), &sourceLength);
				if (count <= 0) {
					continue;
				}
				++requests;
				for (int copy = 0; copy < 2; ++copy) {
					(void)sendto(responder, reply.data(), static_cast<int>(reply.size()), 0,
						reinterpret_cast<const sockaddr*>(&source), sourceLength);
				}
			}
		} };

		std::string error;
		const auto servers = F4T::LanDiscovery::Scan(error, port);
		stopping.store(true);
		stub.join();
		closesocket(responder);

		assert(error.empty());
		assert(requests.load() >= 1);
		assert(std::ranges::count_if(servers, [](const auto& a_server) {
			return a_server.host == "127.0.0.1" && a_server.port == 7777 && a_server.name == "Stub Responder";
		}) == 1);

		const auto lines = TakeLog();
		assert(CountLines(lines, std::format("discover sent target=127.0.0.1:{} adapter=loopback", port)) == 1);
		assert(CountLines(lines, std::format("response from=127.0.0.1:{} gamePort=7777 name=Stub Responder ping=", port)) == 1);
		assert(CountLines(lines, "response from=") == 1);
		assert(CountLines(lines, "no response within") == 0);
		assert(CountLines(lines, "scan finished servers=1") == 1);
	}

	void TestScanLogsTimeout()
	{
		std::uint16_t port{};
		const auto silent = BindLoopbackUdp(port);
		std::string error;
		const auto servers = F4T::LanDiscovery::Scan(error, port);
		closesocket(silent);

		assert(error.empty());
		assert(servers.empty());
		const auto lines = TakeLog();
		assert(CountLines(lines, std::format("discover sent target=127.0.0.1:{} adapter=loopback", port)) == 1);
		assert(CountLines(lines, "response from=") == 0);
		assert(CountLines(lines, "no response within 300ms from ") == 1);
	}
}

namespace F4T::DebugLogging
{
	void Initialize() {}
	void SetEnabled(bool) {}
	bool IsEnabled() { return true; }

	std::string SanitizeLogText(std::string_view a_text, std::size_t a_maxLength)
	{
		return std::string{ a_text.substr(0, a_maxLength) };
	}

	void Write(std::string_view a_component, std::string_view a_message)
	{
		const std::scoped_lock lock(g_logMutex);
		g_logLines.push_back(std::format("[{}] {}", a_component, a_message));
	}
}

int main()
{
	WSADATA data{};
	assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);
	TestScanLogsRequestAndResponse();
	TestScanLogsTimeout();
	WSACleanup();
	return 0;
}
