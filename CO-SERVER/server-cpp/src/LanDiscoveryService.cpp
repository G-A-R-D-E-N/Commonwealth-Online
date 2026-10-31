#include "LanDiscoveryService.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <utility>

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

namespace CommonwealthOnline::Server
{
	namespace
	{
#ifdef _WIN32
		using NativeSocket = SOCKET;
		constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
#else
		using NativeSocket = int;
		constexpr NativeSocket InvalidSocket = -1;
#endif
		constexpr auto InvalidEncodedSocket = (std::numeric_limits<std::uintptr_t>::max)();

		NativeSocket Decode(std::uintptr_t a_handle)
		{
			return a_handle == InvalidEncodedSocket ? InvalidSocket : static_cast<NativeSocket>(a_handle);
		}

		std::uintptr_t Encode(NativeSocket a_socket)
		{
			return a_socket == InvalidSocket ? InvalidEncodedSocket : static_cast<std::uintptr_t>(a_socket);
		}

		void CloseSocket(NativeSocket a_socket)
		{
#ifdef _WIN32
			closesocket(a_socket);
#else
			close(a_socket);
#endif
		}

		std::string SocketError(std::string_view a_operation)
		{
#ifdef _WIN32
			return std::string{ a_operation } + " failed with Winsock error " + std::to_string(WSAGetLastError());
#else
			return std::string{ a_operation } + " failed: " + std::strerror(errno);
#endif
		}

		constexpr auto SocketTimeoutMilliseconds = 100;

		bool ConfigureSocketTimeout(NativeSocket a_socket, int a_option, std::string_view a_operation, std::string& a_error)
		{
#ifdef _WIN32
			DWORD timeout = SocketTimeoutMilliseconds;
			const auto result = setsockopt(
				a_socket, SOL_SOCKET, a_option, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			const auto failed = result == SOCKET_ERROR;
#else
			timeval timeout{};
			timeout.tv_sec = SocketTimeoutMilliseconds / 1000;
			timeout.tv_usec = (SocketTimeoutMilliseconds % 1000) * 1000;
			const auto failed = setsockopt(a_socket, SOL_SOCKET, a_option, &timeout, sizeof(timeout)) != 0;
#endif
			if (failed) {
				a_error = SocketError(a_operation);
			}
			return !failed;
		}
	}

	LanDiscoveryService::LanDiscoveryService(
		std::string a_serverName,
		std::string a_serverDescription,
		std::uint16_t a_gamePort,
		std::size_t a_maximumPlayers,
		const std::atomic_size_t& a_activePlayers,
		bool a_passworded,
		std::uint16_t a_discoveryPort) :
		_serverName(std::move(a_serverName)),
		_serverDescription(std::move(a_serverDescription)),
		_gamePort(a_gamePort),
		_maximumPlayers(a_maximumPlayers),
		_activePlayers(a_activePlayers),
		_passworded(a_passworded),
		_requestedPort(a_discoveryPort),
		_socketHandle(InvalidEncodedSocket)
	{}

	bool LanDiscoveryService::IsLocalNetworkSource(std::uint32_t a_hostOrderAddress) noexcept
	{
		const auto first = (a_hostOrderAddress >> 24) & 0xFFu;
		const auto second = (a_hostOrderAddress >> 16) & 0xFFu;
		if (first == 127u) {
			return true;
		}
		if (first == 10u) {
			return true;
		}
		if (first == 192u && second == 168u) {
			return true;
		}
		if (first == 172u && second >= 16u && second <= 31u) {
			return true;
		}
		if (first == 169u && second == 254u) {
			return true;
		}
		return first == 100u && second >= 64u && second <= 127u;
	}

	bool LanDiscoveryService::MayReplyTo(std::uint32_t a_hostOrderAddress, double a_nowSeconds)
	{
		if (!IsLocalNetworkSource(a_hostOrderAddress)) {
			return false;
		}

		if (a_nowSeconds < _replyWindowStartSeconds || a_nowSeconds - _replyWindowStartSeconds >= 1.0) {
			_replyWindowStartSeconds = a_nowSeconds;
			_repliesInWindow = 0;
		}
		if (_repliesInWindow >= MaximumRepliesPerSecond) {
			return false;
		}

		auto found = _lastReplyBySource.find(a_hostOrderAddress);
		if (found == _lastReplyBySource.end()) {
			if (_lastReplyBySource.size() >= MaximumTrackedDiscoverySources) {
				_lastReplyBySource.clear();
			}
			found = _lastReplyBySource.emplace(a_hostOrderAddress, SourceBudget{ a_nowSeconds, 0 }).first;
		}

		auto& budget = found->second;
		if (a_nowSeconds < budget.windowStartSeconds || a_nowSeconds - budget.windowStartSeconds >= 1.0) {
			budget.windowStartSeconds = a_nowSeconds;
			budget.replies = 0;
		}
		if (budget.replies >= MaximumRepliesPerSourcePerSecond) {
			return false;
		}

		++budget.replies;
		++_repliesInWindow;
		return true;
	}

	LanDiscoveryService::~LanDiscoveryService()
	{
		Stop();
	}

	void LanDiscoveryService::UpdateAdvertisement(std::string a_serverName, std::string a_serverDescription, std::size_t a_maximumPlayers, bool a_passworded)
	{
		const std::scoped_lock lock(_advertisementMutex);
		_serverName = std::move(a_serverName);
		_serverDescription = std::move(a_serverDescription);
		_maximumPlayers = a_maximumPlayers;
		_passworded = a_passworded;
	}

	bool LanDiscoveryService::Start(std::string& a_error)
	{
		if (_running.load(std::memory_order_acquire)) {
			a_error.clear();
			return true;
		}
#ifdef _WIN32
		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
			a_error = "WSAStartup failed for LAN discovery";
			return false;
		}
		_socketRuntimeHeld = true;
#endif

		const auto socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socketHandle == InvalidSocket) {
			a_error = SocketError("LAN discovery socket");
#ifdef _WIN32
			if (_socketRuntimeHeld) {
				WSACleanup();
				_socketRuntimeHeld = false;
			}
#endif
			return false;
		}

		int enabled = 1;
		(void)setsockopt(
			socketHandle,
			SOL_SOCKET,
			SO_BROADCAST,
#ifdef _WIN32
			reinterpret_cast<const char*>(&enabled),
#else
			&enabled,
#endif
			sizeof(enabled));

		if (!ConfigureSocketTimeout(socketHandle, SO_RCVTIMEO, "LAN discovery receive timeout", a_error) ||
			!ConfigureSocketTimeout(socketHandle, SO_SNDTIMEO, "LAN discovery send timeout", a_error)) {
			CloseSocket(socketHandle);
#ifdef _WIN32
			if (_socketRuntimeHeld) {
				WSACleanup();
				_socketRuntimeHeld = false;
			}
#endif
			return false;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		address.sin_port = htons(_requestedPort);
		if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
			a_error = SocketError("LAN discovery bind");
			CloseSocket(socketHandle);
#ifdef _WIN32
			if (_socketRuntimeHeld) {
				WSACleanup();
				_socketRuntimeHeld = false;
			}
#endif
			return false;
		}

		sockaddr_in actual{};
#ifdef _WIN32
		int actualLength = sizeof(actual);
#else
		socklen_t actualLength = sizeof(actual);
#endif
		if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&actual), &actualLength) != 0) {
			a_error = SocketError("LAN discovery getsockname");
			CloseSocket(socketHandle);
#ifdef _WIN32
			if (_socketRuntimeHeld) {
				WSACleanup();
				_socketRuntimeHeld = false;
			}
#endif
			return false;
		}

		_socketHandle.store(Encode(socketHandle), std::memory_order_release);
		_lastReplyBySource.clear();
		_repliesInWindow = 0;
		_replyWindowStartSeconds = 0.0;
		_localPort.store(ntohs(actual.sin_port), std::memory_order_release);
		_running.store(true, std::memory_order_release);
		_thread = std::thread(&LanDiscoveryService::Run, this);
		a_error.clear();
		return true;
	}

	void LanDiscoveryService::Stop()
	{
		_running.store(false, std::memory_order_release);
#ifdef _WIN32
		const auto socketHandle = Decode(_socketHandle.exchange(InvalidEncodedSocket, std::memory_order_acq_rel));
		if (socketHandle != InvalidSocket) {
			CloseSocket(socketHandle);
		}
		if (_thread.joinable()) {
			_thread.join();
		}
#else
		if (_thread.joinable()) {
			_thread.join();
		}
		const auto socketHandle = Decode(_socketHandle.exchange(InvalidEncodedSocket, std::memory_order_acq_rel));
		if (socketHandle != InvalidSocket) {
			CloseSocket(socketHandle);
		}
#endif
		_localPort.store(0, std::memory_order_release);
#ifdef _WIN32
		if (_socketRuntimeHeld) {
			WSACleanup();
			_socketRuntimeHeld = false;
		}
#endif
	}

	std::uint16_t LanDiscoveryService::LocalPort() const noexcept
	{
		return _localPort.load(std::memory_order_acquire);
	}

	void LanDiscoveryService::Run()
	{
		std::array<char, 2048> buffer{};
		const auto started = std::chrono::steady_clock::now();
		while (_running.load(std::memory_order_acquire)) {
			const auto nowSeconds = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - started).count();
			sockaddr_in remote{};
#ifdef _WIN32
			int remoteLength = sizeof(remote);
			const auto count = recvfrom(
				Decode(_socketHandle.load(std::memory_order_acquire)), buffer.data(), static_cast<int>(buffer.size()), 0,
				reinterpret_cast<sockaddr*>(&remote), &remoteLength);
			if (count == SOCKET_ERROR) {
				const auto error = WSAGetLastError();
				if (!_running.load(std::memory_order_acquire)) {
					break;
				}
				if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK || error == WSAEINTR) {
					continue;
				}
				continue;
			}
#else
			socklen_t remoteLength = sizeof(remote);
			const auto count = recvfrom(
				Decode(_socketHandle.load(std::memory_order_acquire)), buffer.data(), buffer.size(), 0,
				reinterpret_cast<sockaddr*>(&remote), &remoteLength);
			if (count < 0) {
				if (!_running.load(std::memory_order_acquire)) {
					break;
				}
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
					continue;
				}
				continue;
			}
#endif
			if (count <= 0) {
				continue;
			}
			if (!IsLocalNetworkSource(ntohl(remote.sin_addr.s_addr))) {
				continue;
			}

			try {
				const auto request = nlohmann::json::parse(std::string{ buffer.data(), static_cast<std::size_t>(count) }, nullptr, false);
				if (!request.is_object() ||
					request.value("type", std::string{}) != "discover" ||
					request.value("protocol", std::string{}) != "commonwealth-online") {
					continue;
				}
				if (!MayReplyTo(ntohl(remote.sin_addr.s_addr), nowSeconds)) {
					continue;
				}
				std::string advertisedName;
				std::string advertisedDescription;
				std::size_t advertisedMaximumPlayers{};
				bool advertisedPassworded{};
				{
					const std::scoped_lock lock(_advertisementMutex);
					advertisedName = _serverName;
					advertisedDescription = _serverDescription;
					advertisedMaximumPlayers = _maximumPlayers;
					advertisedPassworded = _passworded;
				}
				const nlohmann::json response{
					{ "type", "discoverResponse" },
					{ "protocol", "commonwealth-online" },
					{ "version", 1 },
					{ "name", advertisedName },
					{ "description", advertisedDescription },
					{ "port", _gamePort },
					{ "players", _activePlayers.load(std::memory_order_acquire) },
					{ "maxPlayers", advertisedMaximumPlayers },
					{ "passworded", advertisedPassworded }
				};
				const auto text = response.dump();
				(void)sendto(
					Decode(_socketHandle.load(std::memory_order_acquire)),
					text.data(),
#ifdef _WIN32
					static_cast<int>(text.size()),
#else
					text.size(),
#endif
					0,
					reinterpret_cast<const sockaddr*>(&remote),
					remoteLength);
			} catch (...) {
			}
		}
	}
}
