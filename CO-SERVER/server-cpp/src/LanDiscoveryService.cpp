#include "LanDiscoveryService.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
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

		std::string FormatSource(std::uint32_t a_hostOrderAddress, std::uint16_t a_port)
		{
			return std::to_string((a_hostOrderAddress >> 24) & 0xFFu) + '.' +
				std::to_string((a_hostOrderAddress >> 16) & 0xFFu) + '.' +
				std::to_string((a_hostOrderAddress >> 8) & 0xFFu) + '.' +
				std::to_string(a_hostOrderAddress & 0xFFu) + ':' + std::to_string(a_port);
		}

		bool IsStringField(const nlohmann::json& a_object, const char* a_key, std::string_view a_expected)
		{
			const auto found = a_object.find(a_key);
			return found != a_object.end() && found->is_string() && found->get_ref<const std::string&>() == a_expected;
		}

		void WriteStandardOutput(std::string_view a_line)
		{
			std::cout << std::string{ a_line } + '\n' << std::flush;
		}
	}

	LanDiscoveryService::LanDiscoveryService(
		std::string a_serverName,
		std::string a_serverDescription,
		std::uint16_t a_gamePort,
		std::size_t a_maximumPlayers,
		const std::atomic_size_t& a_activePlayers,
		bool a_passworded,
		std::uint16_t a_discoveryPort,
		LogSink a_log) :
		_serverName(std::move(a_serverName)),
		_serverDescription(std::move(a_serverDescription)),
		_gamePort(a_gamePort),
		_maximumPlayers(a_maximumPlayers),
		_activePlayers(a_activePlayers),
		_passworded(a_passworded),
		_requestedPort(a_discoveryPort),
		_socketHandle(InvalidEncodedSocket),
		_log(a_log ? std::move(a_log) : LogSink{ WriteStandardOutput })
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

	LanDiscoveryService::ReplyBudget LanDiscoveryService::TakeReplyBudget(std::uint32_t a_hostOrderAddress, double a_nowSeconds)
	{
		if (a_nowSeconds < _replyWindowStartSeconds || a_nowSeconds - _replyWindowStartSeconds >= 1.0) {
			_replyWindowStartSeconds = a_nowSeconds;
			_repliesInWindow = 0;
		}
		if (_repliesInWindow >= MaximumRepliesPerSecond) {
			return ReplyBudget::ServerLimit;
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
			return ReplyBudget::SourceLimit;
		}

		++budget.replies;
		++_repliesInWindow;
		return ReplyBudget::Available;
	}

	void LanDiscoveryService::LogRequest(double a_nowSeconds, std::string_view a_line)
	{
		if (a_nowSeconds < _logWindowStartSeconds || a_nowSeconds - _logWindowStartSeconds >= 1.0) {
			if (_suppressedLogLines != 0) {
				_log("LAN discovery: " + std::to_string(_suppressedLogLines) + " more request lines suppressed");
			}
			_logWindowStartSeconds = a_nowSeconds;
			_logLinesInWindow = 0;
			_suppressedLogLines = 0;
		}
		if (_logLinesInWindow >= MaximumLogLinesPerSecond) {
			++_suppressedLogLines;
			return;
		}
		++_logLinesInWindow;
		_log(a_line);
	}

	std::optional<std::string> LanDiscoveryService::HandleDatagram(
		std::uint32_t a_hostOrderAddress,
		std::uint16_t a_sourcePort,
		std::string_view a_payload,
		double a_nowSeconds)
	{
		const auto source = FormatSource(a_hostOrderAddress, a_sourcePort);
		if (!IsLocalNetworkSource(a_hostOrderAddress)) {
			LogRequest(a_nowSeconds, "LAN discovery: packet from " + source +
				" dropped: source is not a loopback, private, link-local or CGNAT (100.64/10) address");
			return std::nullopt;
		}

		const auto request = nlohmann::json::parse(a_payload, nullptr, false);
		if (!request.is_object() ||
			!IsStringField(request, "type", "discover") ||
			!IsStringField(request, "protocol", "commonwealth-online")) {
			LogRequest(a_nowSeconds, "LAN discovery: packet from " + source +
				" dropped: not a commonwealth-online discover request");
			return std::nullopt;
		}

		switch (TakeReplyBudget(a_hostOrderAddress, a_nowSeconds)) {
		case ReplyBudget::ServerLimit:
			LogRequest(a_nowSeconds, "LAN discovery: request from " + source + " dropped: server reply limit of " +
				std::to_string(MaximumRepliesPerSecond) + " per second reached");
			return std::nullopt;
		case ReplyBudget::SourceLimit:
			LogRequest(a_nowSeconds, "LAN discovery: request from " + source + " dropped: per-source reply limit of " +
				std::to_string(MaximumRepliesPerSourcePerSecond) + " per second reached");
			return std::nullopt;
		case ReplyBudget::Available:
			break;
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
		return response.dump();
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
		_logWindowStartSeconds = 0.0;
		_logLinesInWindow = 0;
		_suppressedLogLines = 0;
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

			const auto sourceAddress = ntohl(remote.sin_addr.s_addr);
			const auto sourcePort = ntohs(remote.sin_port);
			std::optional<std::string> reply;
			try {
				reply = HandleDatagram(
					sourceAddress, sourcePort, std::string_view{ buffer.data(), static_cast<std::size_t>(count) }, nowSeconds);
			} catch (const std::exception& a_error) {
				LogRequest(nowSeconds, "LAN discovery: request from " + FormatSource(sourceAddress, sourcePort) +
					" dropped: " + a_error.what());
				continue;
			}
			if (!reply) {
				continue;
			}
			const auto sent = sendto(
				Decode(_socketHandle.load(std::memory_order_acquire)),
				reply->data(),
#ifdef _WIN32
				static_cast<int>(reply->size()),
#else
				reply->size(),
#endif
				0,
				reinterpret_cast<const sockaddr*>(&remote),
				remoteLength);
#ifdef _WIN32
			const auto sendFailed = sent == SOCKET_ERROR;
#else
			const auto sendFailed = sent < 0;
#endif
			if (sendFailed) {
				const auto error = SocketError("sendto");
				LogRequest(nowSeconds, "LAN discovery: reply to " + FormatSource(sourceAddress, sourcePort) + " failed: " + error);
			} else {
				LogRequest(nowSeconds, "LAN discovery: replied to " + FormatSource(sourceAddress, sourcePort) +
					" with game port " + std::to_string(_gamePort));
			}
		}
	}
}
