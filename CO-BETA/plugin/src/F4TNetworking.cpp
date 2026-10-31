#include "pch.h"

#include "F4TConnectionPolicy.h"

#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TNetworkingSocketLifecycle.h"
#include "F4TPvPCombatContract.h"
#include "F4TDebugLogging.h"
#include "F4TConsoleGuard.h"
#include "F4TChat.h"
#include "F4TFactions.h"
#include "F4TFriends.h"
#include "F4TInventorySync.h"
#include "F4TLoadOrderSync.h"
#include "F4TObjectSync.h"
#include "F4TQuestSync.h"
#include "F4TNetworkValidation.h"
#include "F4TReceivePolicy.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TSessionLaunch.h"
#include "F4TSettlementAccessClient.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TCharacterSlotsClient.h"
#include "F4TUIClient.h"
#include "F4TWorldStateSync.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace
{
	using namespace F4T::Networking::Detail;
	constexpr auto kLocalServerHost = "127.0.0.1";
	constexpr auto kLocalServerPort = 7777;
	constexpr auto kReconnectCooldown = 10s;
	constexpr auto kConnectTimeoutMilliseconds = 3000;
	constexpr auto kReceiveIdleSleep = 25ms;
	constexpr auto kMaximumReceiveBufferSize = 64uz * 1024uz;
	constexpr auto kSocketWriteWait = 250ms;
	constexpr auto kKeepAliveInterval = 15s;

	std::mutex g_diagnosticMutex;
	std::recursive_mutex g_connectionOperationMutex;
	std::atomic_uint64_t g_connectionGeneration{ 0 };
	bool g_winsockInitialized = false;
	auto g_lastConnectAttempt = std::chrono::steady_clock::time_point{};
	std::atomic_bool g_receiveThreadRunning{ false };
	std::thread g_receiveThread;
	std::mutex g_receiveThreadMutex;
	auto g_lastKeepAliveSendTime = std::chrono::steady_clock::time_point{};

	// Worst-case legitimate inbound is 255 remotes x 10 Hz ~= 2550 frames/s; 4096/s
	// and 4 MiB/s leave headroom; sustained windows disconnect.
	F4T::ReceivePolicy::Config g_inboundPolicyConfig{
		.maximumFrameBytes = kMaximumReceiveBufferSize,
		.budget = {
			.maximumPacketsPerWindow = 4096,
			.maximumBytesPerWindow = 4ULL * 1024ULL * 1024ULL,
			.maximumViolationWindows = 5,
			.windowSeconds = 1.0
		}
	};
	F4T::ReceivePolicy::State g_inboundPolicyState = F4T::ReceivePolicy::Initialize(0.0);


	double SteadySeconds()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void ResetInboundBudget()
	{
		g_inboundPolicyState = F4T::ReceivePolicy::Initialize(SteadySeconds());
	}

	bool WaitForSocketWritable(SOCKET a_socket, std::chrono::milliseconds a_timeout)
	{
		fd_set writeSet{};
		FD_ZERO(std::addressof(writeSet));
		FD_SET(a_socket, std::addressof(writeSet));

		timeval timeout{};
		timeout.tv_sec = static_cast<long>(a_timeout.count() / 1000);
		timeout.tv_usec = static_cast<long>((a_timeout.count() % 1000) * 1000);
		return select(0, nullptr, std::addressof(writeSet), nullptr, std::addressof(timeout)) > 0;
	}

	bool EnsureWinsockInitialized()
	{
		if (g_winsockInitialized) {
			return true;
		}

		WSADATA winsockData{};
		const auto startupResult = WSAStartup(MAKEWORD(2, 2), std::addressof(winsockData));
		if (startupResult != 0) {
			LogWarningWithLocalPlayerPrefix("Could not initialize Winsock for Commonwealth Online local networking.");
			return false;
		}

		g_winsockInitialized = true;
		return true;
	}

	bool SetNonBlocking(SOCKET a_socket)
	{
		u_long nonBlocking = 1;
		return ioctlsocket(a_socket, FIONBIO, std::addressof(nonBlocking)) == 0;
	}

	bool TryFinishNonBlockingConnect(SOCKET a_socket)
	{
		fd_set writeSet{};
		FD_ZERO(std::addressof(writeSet));
		FD_SET(a_socket, std::addressof(writeSet));

		timeval timeout{};
		timeout.tv_sec = kConnectTimeoutMilliseconds / 1000;
		timeout.tv_usec = (kConnectTimeoutMilliseconds % 1000) * 1000;

		const auto selectResult = select(0, nullptr, std::addressof(writeSet), nullptr, std::addressof(timeout));
		if (selectResult <= 0) {
			return false;
		}

		int socketError = 0;
		int socketErrorSize = sizeof(socketError);
		if (getsockopt(
				a_socket,
				SOL_SOCKET,
				SO_ERROR,
				reinterpret_cast<char*>(std::addressof(socketError)),
				std::addressof(socketErrorSize)) != 0) {
			return false;
		}

		return socketError == 0;
	}

	bool ShouldAttemptReconnect()
	{
		const auto now = std::chrono::steady_clock::now();
		if (g_lastConnectAttempt.time_since_epoch().count() != 0 && now - g_lastConnectAttempt < kReconnectCooldown) {
			return false;
		}

		g_lastConnectAttempt = now;
		return true;
	}

	void TrimPacketLine(std::string& a_line)
	{
		while (!a_line.empty() && (a_line.back() == '\r' || a_line.back() == ' ' || a_line.back() == '\t')) {
			a_line.pop_back();
		}
	}

	void ProcessReceiveBuffer(std::string& a_receiveBuffer)
	{
		std::size_t parsePosition = 0;
		while (true) {
			const auto step = F4T::ReceivePolicy::Next(
				a_receiveBuffer,
				parsePosition,
				g_inboundPolicyState,
				SteadySeconds(),
				g_inboundPolicyConfig);
			if (!step) {
				break;
			}

			parsePosition = step->nextParsePosition;
			if (step->disposition == F4T::ReceivePolicy::Disposition::CloseConnection) {
				LogWarningWithLocalPlayerPrefix("Closing connection: server violated the receive boundary.");
				CloseSocket();
				a_receiveBuffer.clear();
				if (g_receiveThreadRunning.exchange(false)) {
					F4T::UIClient::NotifyTransportDisconnected("Server violated the receive boundary.");
				}
				return;
			}
			if (step->disposition == F4T::ReceivePolicy::Disposition::DropFrame) {
				continue;
			}

			std::string line = a_receiveBuffer.substr(step->frameStart, step->frameLength);
			TrimPacketLine(line);
			HandlePacketLine(line);
			if (!g_receiveThreadRunning.load() || GetSocket() == INVALID_SOCKET) {
				g_inboundPolicyState.stopped = true;
				a_receiveBuffer.clear();
				return;
			}
		}
		if (parsePosition != 0) {
			a_receiveBuffer.erase(0, parsePosition);
		}
		if (F4T::ReceivePolicy::PendingOverflow(a_receiveBuffer, 0, kMaximumReceiveBufferSize)) {
			LogWarningWithLocalPlayerPrefix("Closing connection: server sent an oversized unterminated frame.");
			CloseSocket();
			a_receiveBuffer.clear();
			if (g_receiveThreadRunning.exchange(false)) {
				F4T::UIClient::NotifyTransportDisconnected("Server sent an oversized unterminated frame.");
			}
			return;
		}
	}

	void ReceiveLoop()
	{
		g_isReceiveThread = true;
		g_receiveLoopActive.store(true, std::memory_order_release);
		F4T::DebugLogging::Log("Networking", "receive thread started");
		std::array<char, 4096> receiveChunk{};
		std::string receiveBuffer;

		while (g_receiveThreadRunning.load()) {
			int bytesReceived = 0;
			const auto socket = GetSocket();
			if (socket == INVALID_SOCKET) {
				break;
			}

			bytesReceived = recv(socket, receiveChunk.data(), static_cast<int>(receiveChunk.size()), 0);

			if (bytesReceived > 0) {
				receiveBuffer.append(receiveChunk.data(), static_cast<std::size_t>(bytesReceived));
				ProcessReceiveBuffer(receiveBuffer);
				continue;
			}

			if (bytesReceived == 0) {
				LogWarningWithLocalPlayerPrefix("Commonwealth Online local server closed the connection.");
				CloseSocket();
				break;
			}

			const auto error = WSAGetLastError();
			if (error == WSAEWOULDBLOCK) {
				std::this_thread::sleep_for(kReceiveIdleSleep);
				continue;
			}

			if (g_receiveThreadRunning.load()) {
				LogWarningWithLocalPlayerPrefix("Lost connection to Commonwealth Online local server while receiving.");
			}
			CloseSocket();
			break;
		}

		if (g_receiveThreadRunning.exchange(false)) {
			F4T::UIClient::NotifyTransportDisconnected();
		}
		g_receiveLoopActive.store(false, std::memory_order_release);
		ClosePendingSocket();
		F4T::DebugLogging::Log("Networking", "receive thread stopped");
	}

	void StopReceiveThread()
	{
		if (g_isReceiveThread) {
			g_receiveThreadRunning.store(false);
			CloseSocket();
			return;
		}

		const std::scoped_lock lock(g_receiveThreadMutex);
		g_receiveThreadRunning.store(false);
		CloseSocket();

		if (g_receiveThread.joinable()) {
			g_receiveThread.join();
		}
	}

	void StartReceiveThread()
	{
		const std::scoped_lock lock(g_receiveThreadMutex);
		if (g_receiveThread.joinable()) {
			g_receiveThread.join();
		}

		g_receiveThreadRunning.store(true);
		g_receiveThread = std::thread(ReceiveLoop);
	}

	std::string TrimAsciiWhitespace(std::string_view a_text)
	{
		while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t' || a_text.front() == '\r' ||
		                           a_text.front() == '\n')) {
			a_text.remove_prefix(1);
		}
		while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r' ||
		                          a_text.back() == '\n')) {
			a_text.remove_suffix(1);
		}
		return std::string{ a_text };
	}

	bool NormalizeConnectEndpoint(std::string& a_host, std::uint16_t& a_port)
	{
		a_host = TrimAsciiWhitespace(a_host);
		return F4T::ConnectionPolicy::NormalizeConnectEndpoint(a_host, a_port);
	}

	bool TryConnectNonBlocking(SOCKET a_socket, const sockaddr* a_address, int a_addressLength)
	{
		const auto connectResult = connect(a_socket, a_address, a_addressLength);
		return connectResult == 0 ||
		       (WSAGetLastError() == WSAEWOULDBLOCK && TryFinishNonBlockingConnect(a_socket));
	}

	bool ResolveAndConnectIPv4(const std::string& a_host, std::uint16_t a_port, std::string& a_error)
	{
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		const auto portText = std::to_string(a_port);
		addrinfo* results = nullptr;
		const auto status = getaddrinfo(a_host.c_str(), portText.c_str(), std::addressof(hints), std::addressof(results));
		if (status != 0 || !results) {
			a_error = std::format(
				"Could not resolve server address '{}' ({})",
				a_host,
				gai_strerrorA(status));
			LogWarningWithLocalPlayerPrefix(a_error);
			return false;
		}

		SOCKET connectedSocket = INVALID_SOCKET;
		for (auto* entry = results; entry != nullptr; entry = entry->ai_next) {
			if (!entry->ai_addr || (entry->ai_family != AF_INET && entry->ai_family != AF_INET6)) {
				continue;
			}

			const auto candidate = socket(entry->ai_family, SOCK_STREAM, IPPROTO_TCP);
			if (candidate == INVALID_SOCKET) {
				continue;
			}

			if (!SetNonBlocking(candidate)) {
				closesocket(candidate);
				continue;
			}

			if (!TryConnectNonBlocking(candidate, entry->ai_addr, static_cast<int>(entry->ai_addrlen))) {
				closesocket(candidate);
				continue;
			}

			connectedSocket = candidate;
			break;
		}

		freeaddrinfo(results);

		if (connectedSocket == INVALID_SOCKET) {
			a_error = std::format("Connection failed — server offline or unreachable at {}:{}", a_host, a_port);
			LogWarningWithLocalPlayerPrefix(std::format(
				"Could not connect to Commonwealth Online server at {}:{}",
				a_host,
				a_port));
			return false;
		}

		{
			const std::scoped_lock lock(g_socketMutex);
			g_socket = connectedSocket;
		}

		return true;
	}

	bool ConnectToServerInternal(const char* a_host, std::uint16_t a_port, std::string& a_error)
	{
		const std::scoped_lock operationLock(g_connectionOperationMutex);
		a_error.clear();

		if (!a_host || a_host[0] == '\0') {
			a_error = "Server address is required.";
			return false;
		}

		auto host = std::string{ a_host };
		auto port = a_port > 0 ? a_port : static_cast<std::uint16_t>(kLocalServerPort);
		if (!NormalizeConnectEndpoint(host, port)) {
			a_error = "Server address is required.";
			return false;
		}

		if (GetSocket() != INVALID_SOCKET) {
			return true;
		}

		if (!ShouldAttemptReconnect()) {
			a_error = "Connection attempt was rate-limited. Try again shortly.";
			return false;
		}

		StopReceiveThread();
		F4T::RemotePlayerState::ClearAssignedPlayerId();
		F4T::RemotePlayerState::ClearRemotePlayers();
		F4T::Factions::Reset();
		F4T::Friends::Reset();
		F4T::SettlementAccessClient::Reset();
		F4T::SettlementCaptureClient::Reset();
		F4T::CharacterSlotsClient::Reset();
		F4T::Chat::Reset();
		F4T::ConsoleGuard::Reset();
		F4T::WorldStateSync::RequestReset();
		F4T::InventorySync::Reset();
		F4T::ObjectSync::Reset();
		F4T::QuestSync::ClearSession();
		F4T::LoadOrderSync::Reset();
		if (!EnsureWinsockInitialized()) {
			a_error = "Networking subsystem failed to initialize.";
			return false;
		}

		if (!ResolveAndConnectIPv4(host, port, a_error)) {
			return false;
		}

		F4T::ReplicationRuntime::NoteConnectionEstablished();
		ResetPacketDiagnostics();
		{
			const std::scoped_lock lock(g_diagnosticMutex);
			g_lastKeepAliveSendTime = {};
		}
		ResetInboundBudget();
		g_connectionGeneration.fetch_add(1);
		StartReceiveThread();

		LogStatusWithLocalPlayerPrefix(
			std::format("Connected to Commonwealth Online server at {}:{}.", host, port));
		F4T::DebugLogging::Log("Networking", "connected endpoint={}:{}", host, port);
		return true;
	}

}

namespace F4T::Networking::Detail
{
	bool SendPacketBytes(std::string_view a_packet)
	{
		if (a_packet.empty()) {
			return false;
		}

		const auto deadline = std::chrono::steady_clock::now() + kSocketWriteWait;
		std::size_t sentTotal = 0;
		const std::scoped_lock sendLock(g_sendMutex);
		const auto activeSocket = GetSocket();
		if (activeSocket == INVALID_SOCKET) {
			return false;
		}

		DetachedSocket failed;
		while (sentTotal < a_packet.size()) {
			const auto remaining = a_packet.size() - sentTotal;
			const auto chunkSize = static_cast<int>((std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<int>::max)())));
			const auto sent = send(activeSocket, a_packet.data() + sentTotal, chunkSize, 0);
			if (sent > 0) {
				sentTotal += static_cast<std::size_t>(sent);
				continue;
			}

			const auto error = WSAGetLastError();
			if (sent == SOCKET_ERROR && error == WSAEWOULDBLOCK && std::chrono::steady_clock::now() < deadline) {
				const auto remainingWait = std::chrono::duration_cast<std::chrono::milliseconds>(
					deadline - std::chrono::steady_clock::now());
				if (remainingWait.count() > 0 && WaitForSocketWritable(activeSocket, remainingWait)) {
					continue;
				}
			}

			if (sentTotal > 0 || error != WSAEWOULDBLOCK) {
				const std::scoped_lock lock(g_socketMutex);
				if (g_socket == activeSocket) {
					failed = DetachSocketLocked();
				}
			}
			break;
		}
		if (failed.toShutdown != INVALID_SOCKET) {
			FinishDetachedSocket(failed);
			return false;
		}
		return sentTotal == a_packet.size();
	}

}

namespace F4T::Networking
{
	using namespace Detail;
	std::string GetLocalPlayerLogPrefix()
	{
		const auto assignedPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
		if (!assignedPlayerId) {
			return "[LocalPlayerId=unassigned]";
		}

		return "[LocalPlayerId=" + std::to_string(*assignedPlayerId) + "]";
	}

	bool ConnectAndValidateServer(const char* a_host, std::uint16_t a_port, std::string& a_error)
	{
		std::unique_lock operationLock(g_connectionOperationMutex);
		if (!a_host || a_host[0] == '\0') {
			a_error = "Server address is required.";
			return false;
		}

		auto host = std::string{ a_host };
		auto port = a_port > 0 ? a_port : static_cast<std::uint16_t>(kLocalServerPort);
		if (!NormalizeConnectEndpoint(host, port)) {
			a_error = "Server address is required.";
			return false;
		}

		if (IsConnectedToServer()) {
			DisconnectFromLocalServer();
		}

		g_lastConnectAttempt = std::chrono::steady_clock::time_point{};

		if (!ConnectToServerInternal(host.c_str(), port, a_error)) {
			if (a_error.empty()) {
				a_error = std::format("Connection failed — server offline or unreachable at {}:{}", host, port);
			} else if (host == kLocalServerHost && port == kLocalServerPort &&
			           a_error.find("Could not resolve") == std::string::npos) {
				a_error = "Could not connect to the local server on this PC.";
			}
			return false;
		}

		const auto connectionGeneration = g_connectionGeneration.load();
		operationLock.unlock();
		constexpr auto kWelcomeTimeout = std::chrono::milliseconds{
			static_cast<std::int64_t>(F4T::ConnectionPolicy::DefaultWelcomeWaitSeconds * 1000.0)
		};
		const auto deadline = std::chrono::steady_clock::now() + kWelcomeTimeout;
		while (std::chrono::steady_clock::now() < deadline) {
			{
				const std::scoped_lock lock(g_connectionOperationMutex);
				if (g_connectionGeneration.load() != connectionGeneration || !IsConnectedToServer()) {
					a_error = "Connection ended before the server handshake completed.";
					return false;
				}
				if (F4T::RemotePlayerState::GetAssignedPlayerId().has_value()) {
					return true;
				}
			}

			std::this_thread::sleep_for(25ms);
		}

		operationLock.lock();
		if (g_connectionGeneration.load() == connectionGeneration) {
			DisconnectFromLocalServer();
		}
		a_error = "Server did not respond with a valid Commonwealth Online handshake.";
		return false;
	}

	void DisconnectFromLocalServer()
	{
		std::unique_lock operationLock(g_connectionOperationMutex, std::defer_lock);
		if (!g_isReceiveThread) {
			operationLock.lock();
		}
		F4T::DebugLogging::Log("Networking", "disconnect requested");
		StopReceiveThread();
		g_connectionGeneration.fetch_add(1);
		F4T::RemotePlayerState::ClearAssignedPlayerId();
		F4T::RemotePlayerState::ClearRemotePlayers();
		F4T::Factions::Reset();
		F4T::Friends::Reset();
		F4T::SettlementAccessClient::Reset();
		F4T::SettlementCaptureClient::Reset();
		F4T::CharacterSlotsClient::Reset();
		F4T::Chat::Reset();
		F4T::ConsoleGuard::Reset();
		F4T::WorldStateSync::RequestReset();
		F4T::InventorySync::Reset();
		F4T::ObjectSync::Reset();
		F4T::QuestSync::ClearSession();
		F4T::LoadOrderSync::Reset();
		ResetPacketDiagnostics();
		{
			const std::scoped_lock lock(g_diagnosticMutex);
			g_lastKeepAliveSendTime = {};
		}
		ResetInboundBudget();

		if (g_winsockInitialized) {
			WSACleanup();
			g_winsockInitialized = false;
		}
	}

	bool IsConnectedToServer()
	{
		return GetSocket() != INVALID_SOCKET;
	}

	std::uint64_t GetConnectionGeneration()
	{
		return g_connectionGeneration.load();
	}

	void UpdateConnectionMaintenance()
	{
		if (!IsConnectedToServer()) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		{
			const std::scoped_lock lock(g_diagnosticMutex);
			if (g_lastKeepAliveSendTime.time_since_epoch().count() != 0 &&
				now - g_lastKeepAliveSendTime < kKeepAliveInterval) {
				return;
			}
		}

		static constexpr std::string_view packet = "{\"type\":\"keepAlive\"}";
		if (SendPacketBytes(packet)) {
			const std::scoped_lock lock(g_diagnosticMutex);
			g_lastKeepAliveSendTime = now;
		}
	}

}
