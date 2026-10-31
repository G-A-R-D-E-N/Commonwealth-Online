#include "pch.h"

#include "F4TLanDiscovery.h"
#include "F4TDebugLogging.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
	constexpr auto kProtocolName = "commonwealth-online";
	constexpr auto kDiscoverPort = 7778;
	constexpr auto kDefaultGamePort = 7777;
	constexpr auto kUdpListenDuration = 300ms;
	constexpr auto kMaxHostsPerSubnet = 1024;

	bool g_winsockInitialized = false;

	struct LanSubnet
	{
		std::string localIp;
		std::uint32_t networkHostOrder{ 0 };
		std::uint32_t broadcastHostOrder{ 0 };
	};

	bool EnsureWinsockInitialized()
	{
		if (g_winsockInitialized) {
			return true;
		}

		WSADATA winsockData{};
		if (WSAStartup(MAKEWORD(2, 2), std::addressof(winsockData)) != 0) {
			return false;
		}

		g_winsockInitialized = true;
		return true;
	}

	std::optional<std::string> IPv4ToString(const IN_ADDR& a_addr)
	{
		std::array<char, INET_ADDRSTRLEN> buffer{};
		if (!InetNtopA(AF_INET, std::addressof(a_addr), buffer.data(), static_cast<DWORD>(buffer.size()))) {
			return std::nullopt;
		}

		return std::string{ buffer.data() };
	}

	std::string HostOrderToIPv4(std::uint32_t a_host)
	{
		in_addr address{};
		address.S_un.S_addr = htonl(a_host);
		return IPv4ToString(address).value_or("0.0.0.0");
	}

	std::vector<LanSubnet> GetLanSubnets()
	{
		std::vector<LanSubnet> subnets;
		std::unordered_set<std::uint32_t> seenNetworks;

		ULONG bufferSize = 0;
		if (GetAdaptersAddresses(
				AF_INET,
				GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
				nullptr,
				nullptr,
				std::addressof(bufferSize)) != ERROR_BUFFER_OVERFLOW) {
			return subnets;
		}

		std::vector<std::byte> buffer(bufferSize);
		auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
		if (GetAdaptersAddresses(
				AF_INET,
				GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
				nullptr,
				addresses,
				std::addressof(bufferSize)) != NO_ERROR) {
			return subnets;
		}

		for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
			if (adapter->OperStatus != IfOperStatusUp) {
				continue;
			}

			for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
				if (unicast->Address.lpSockaddr == nullptr ||
					unicast->Address.lpSockaddr->sa_family != AF_INET) {
					continue;
				}

				const auto* sockaddr = reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr);
				const auto localIp = IPv4ToString(sockaddr->sin_addr);
				if (!localIp || localIp->starts_with("127.") || localIp->starts_with("169.254.")) {
					continue;
				}

				auto prefixLength = static_cast<int>(unicast->OnLinkPrefixLength);
				if (prefixLength <= 0 || prefixLength > 32) {
					prefixLength = 24;
				}

				const auto ipHost = ntohl(sockaddr->sin_addr.S_un.S_addr);
				const auto mask = prefixLength == 32 ?
				                      0xFFFFFFFFu :
				                      (~0u << (32 - prefixLength));
				auto network = ipHost & mask;
				auto broadcast = network | ~mask;

				if ((broadcast - network) > kMaxHostsPerSubnet) {
					network = ipHost & 0xFFFFFF00u;
					broadcast = network | 0xFFu;
				}

				if (!seenNetworks.insert(network).second) {
					continue;
				}

				subnets.push_back(LanSubnet{
					.localIp = *localIp,
					.networkHostOrder = network,
					.broadcastHostOrder = broadcast
				});
			}
		}

		return subnets;
	}

	bool SetSocketNonBlocking(SOCKET a_socket, bool a_nonBlocking)
	{
		u_long mode = a_nonBlocking ? 1 : 0;
		return ioctlsocket(a_socket, FIONBIO, std::addressof(mode)) == 0;
	}

	bool SendDiscoverRequest(SOCKET a_socket, const sockaddr_in& a_target)
	{
		static constexpr auto kRequest =
			R"({"type":"discover","protocol":"commonwealth-online","version":1})";

		const auto sent = sendto(
			a_socket,
			kRequest,
			static_cast<int>(std::strlen(kRequest)),
			0,
			reinterpret_cast<const sockaddr*>(std::addressof(a_target)),
			sizeof(a_target));

		return sent > 0;
	}

	bool MakeDiscoverTarget(sockaddr_in& a_target, std::string_view a_host)
	{
		a_target = {};
		a_target.sin_family = AF_INET;
		a_target.sin_port = htons(kDiscoverPort);
		return inet_pton(AF_INET, std::string{ a_host }.c_str(), std::addressof(a_target.sin_addr)) == 1;
	}

	std::optional<F4T::LanDiscovery::DiscoveredServer> ParseDiscoverResponse(
		std::string_view a_json,
		std::string_view a_host,
		int a_pingMs)
	{
		try {
			auto json = nlohmann::json::parse(a_json, nullptr, false);
			if (json.is_discarded() || !json.is_object()) {
				return std::nullopt;
			}
			if (!json.contains("type") || !json["type"].is_string() ||
				json["type"].get<std::string>() != "discoverResponse") {
				return std::nullopt;
			}
			if (!json.contains("protocol") || !json["protocol"].is_string() ||
				json["protocol"].get<std::string>() != kProtocolName) {
				return std::nullopt;
			}

			const auto port = json.contains("port") && json["port"].is_number_integer() ?
				json["port"].get<std::int64_t>() : static_cast<std::int64_t>(kDefaultGamePort);
			const auto players = json.contains("players") && json["players"].is_number_integer() ?
				json["players"].get<std::int64_t>() : 0;
			const auto maxPlayers = json.contains("maxPlayers") && json["maxPlayers"].is_number_integer() ?
				json["maxPlayers"].get<std::int64_t>() : 16;
			if (port < 1 || port > 65535 || players < 0 || players > 100000 || maxPlayers < 1 || maxPlayers > 100000) {
				return std::nullopt;
			}

			F4T::LanDiscovery::DiscoveredServer server;
			server.host = std::string{ a_host };
			server.port = static_cast<std::uint16_t>(port);
			if (json.contains("name") && json["name"].is_string()) {
				server.name = json["name"].get<std::string>().substr(0, 64);
			}
			if (json.contains("description") && json["description"].is_string()) {
				server.description = json["description"].get<std::string>().substr(0, 256);
			}
			server.players = static_cast<int>(players);
			server.maxPlayers = static_cast<int>(maxPlayers);
			server.ping = (std::max)(0, a_pingMs);
			return server;
		} catch (const std::exception&) {
			return std::nullopt;
		}
	}

	std::string MakeServerKey(std::string_view a_host, std::uint16_t a_port)
	{
		return std::format("{}:{}", a_host, a_port);
	}

	void CollectUdpResponses(
		SOCKET a_socket,
		std::chrono::steady_clock::time_point a_sendStartedAt,
		std::chrono::steady_clock::time_point a_deadline,
		std::unordered_map<std::string, F4T::LanDiscovery::DiscoveredServer>& a_discovered)
	{
		std::array<char, 2048> receiveBuffer{};

		while (std::chrono::steady_clock::now() < a_deadline) {
			fd_set readSet{};
			FD_ZERO(std::addressof(readSet));
			FD_SET(a_socket, std::addressof(readSet));

			const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
				a_deadline - std::chrono::steady_clock::now());
			if (remaining.count() <= 0) {
				break;
			}

			timeval timeout{};
			timeout.tv_sec = static_cast<long>(remaining.count() / 1'000'000);
			timeout.tv_usec = static_cast<long>(remaining.count() % 1'000'000);

			const auto selectResult = select(0, std::addressof(readSet), nullptr, nullptr, std::addressof(timeout));
			if (selectResult <= 0) {
				break;
			}

			sockaddr_in senderAddress{};
			int senderAddressSize = sizeof(senderAddress);
			const auto received = recvfrom(
				a_socket,
				receiveBuffer.data(),
				static_cast<int>(receiveBuffer.size()),
				0,
				reinterpret_cast<sockaddr*>(std::addressof(senderAddress)),
				std::addressof(senderAddressSize));

			if (received <= 0) {
				continue;
			}

			const auto senderHost = IPv4ToString(senderAddress.sin_addr);
			if (!senderHost) {
				continue;
			}

			const auto pingMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - a_sendStartedAt).count());

			const auto response = ParseDiscoverResponse(
				std::string_view{ receiveBuffer.data(), static_cast<std::size_t>(received) },
				*senderHost,
				pingMs);
			if (!response) {
				continue;
			}

			a_discovered.emplace(MakeServerKey(response->host, response->port), *response);
		}
	}
}

namespace F4T::LanDiscovery
{
	std::vector<DiscoveredServer> Scan(std::string& a_error)
	{
		a_error.clear();
		F4T::DebugLogging::Log("LanDiscovery", "scan started");

		if (!EnsureWinsockInitialized()) {
			a_error = "Could not initialize Winsock for LAN discovery.";
			return {};
		}

		const auto discoverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (discoverSocket == INVALID_SOCKET) {
			a_error = "Could not create UDP socket for LAN discovery.";
			return {};
		}

		const BOOL broadcastEnabled = TRUE;
		if (setsockopt(
				discoverSocket,
				SOL_SOCKET,
				SO_BROADCAST,
				reinterpret_cast<const char*>(std::addressof(broadcastEnabled)),
				sizeof(broadcastEnabled)) != 0) {
			closesocket(discoverSocket);
			a_error = "Could not enable LAN discovery broadcast.";
			return {};
		}

		sockaddr_in bindAddress{};
		bindAddress.sin_family = AF_INET;
		bindAddress.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
		bindAddress.sin_port = 0;
		if (bind(discoverSocket, reinterpret_cast<const sockaddr*>(std::addressof(bindAddress)), sizeof(bindAddress)) != 0) {
			closesocket(discoverSocket);
			a_error = "Could not bind LAN discovery socket.";
			return {};
		}

		if (!SetSocketNonBlocking(discoverSocket, true)) {
			closesocket(discoverSocket);
			a_error = "Could not configure LAN discovery socket.";
			return {};
		}

		const auto subnets = GetLanSubnets();
		F4T::DebugLogging::Log("LanDiscovery", "eligible IPv4 subnets={}", subnets.size());

		sockaddr_in target{};
		const auto sendStartedAt = std::chrono::steady_clock::now();

		if (MakeDiscoverTarget(target, "127.0.0.1")) {
			SendDiscoverRequest(discoverSocket, target);
		}
		if (MakeDiscoverTarget(target, "255.255.255.255")) {
			SendDiscoverRequest(discoverSocket, target);
		}

		for (const auto& subnet : subnets) {
			const auto broadcastIp = HostOrderToIPv4(subnet.broadcastHostOrder);
			if (MakeDiscoverTarget(target, broadcastIp)) {
				SendDiscoverRequest(discoverSocket, target);
			}
		}

		std::unordered_map<std::string, DiscoveredServer> discovered;
		const auto udpDeadline = sendStartedAt + kUdpListenDuration;
		CollectUdpResponses(discoverSocket, sendStartedAt, udpDeadline, discovered);
		closesocket(discoverSocket);

		std::vector<DiscoveredServer> results;
		results.reserve(discovered.size());
		for (auto& [_, server] : discovered) {
			results.push_back(std::move(server));
		}

		std::ranges::sort(results, [](const DiscoveredServer& a_lhs, const DiscoveredServer& a_rhs) {
			if (a_lhs.ping != a_rhs.ping) {
				return a_lhs.ping < a_rhs.ping;
			}

			return a_lhs.host < a_rhs.host;
		});

		F4T::DebugLogging::Log("LanDiscovery", "scan finished servers={}", results.size());
		return results;
	}
}
