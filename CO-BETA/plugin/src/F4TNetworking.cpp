#include "pch.h"
#include "F4TConnectionPolicy.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TNetworkingSocketLifecycle.h"
#include "F4TPvPCombatContract.h"
#include "F4TDebugLogging.h"
#include "F4TConsoleGuard.h"
#include "F4TChat.h"
#include "F4TAdminClient.h"
#include "F4TFactions.h"
#include "F4TFriends.h"
#include "F4TMapPresence.h"
#include "F4TInventorySync.h"
#include "F4TJoinFlow.h"
#include "F4TLoadOrderSync.h"
#include "F4TObjectSync.h"
#include "F4TParkedSuitSync.h"
#include "F4TQuestSync.h"
#include "F4TStateBagSync.h"
#include "F4TSurvivalNeedsClient.h"
#include "F4TTradeClient.h"
#include "F4TVendorTradeClient.h"
#include "F4TNetworkValidation.h"
#include "F4TReceivePolicy.h"
#include "F4TReconnect.h"
#include "F4TRemoteNpcState.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TSessionLaunch.h"
#include "F4TSettlementAccessClient.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TSettlementRaidClient.h"
#include "F4TWorkshopReplicationClient.h"
#include "F4TCharacterSlotsClient.h"
#include "F4TSessionWorldState.h"
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
	constexpr auto kReceiveIdleSleep = 25ms;
	constexpr auto kMaximumReceiveBufferSize = 64uz * 1024uz;
	constexpr auto kSocketWriteWait = 250ms;
	constexpr auto kKeepAliveInterval = 15s;

	std::mutex g_diagnosticMutex;
	std::recursive_timed_mutex g_connectionOperationMutex;
	std::atomic_uint64_t g_connectionGeneration{ 0 };
	std::atomic_uint64_t g_deferredReceiveDisconnect{ 0 };
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

	bool IsKeepAliveAfkExempt()
	{
		// constraint: LooksMenu has no MENU_NAME, so it is matched by name like SPECIALMenu.
		return F4T::ConnectionPolicy::AfkExemptFromClientState(
			F4T::JoinFlow::IsActive(),
			F4T::SessionWorldState::IsLoadingScreenActive(),
			F4T::SessionWorldState::IsMenuOpen("LooksMenu"));
	}

	std::string BuildKeepAlivePacket()
	{
		nlohmann::json packet{ { "type", "keepAlive" } };
		if (IsKeepAliveAfkExempt()) {
			packet["afkExempt"] = true;
		}
		return packet.dump();
	}

	void ResetInboundBudget()
	{
		g_inboundPolicyState = F4T::ReceivePolicy::Initialize(SteadySeconds());
	}

	bool EnsureWinsockInitialized()
	{
		// constraint: Winsock is started once and never cleaned up; the OS releases it at process exit.
		static std::mutex winsockMutex;
		const std::scoped_lock winsockLock(winsockMutex);
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
			try {
				HandlePacketLine(line);
			} catch (const std::exception& a_error) {
				LogThrottledWarning("packet_handler_exception", std::format("Dropping a server packet that failed to process: {}", F4T::DebugLogging::SanitizeLogText(a_error.what(), 128)));
			} catch (...) {
				LogThrottledWarning("packet_handler_exception", "Dropping a server packet that failed to process.");
			}
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

	void CloseSocketAfterLossSave()
	{
		F4T::Networking::CloseAfterLossSave(g_receiveThreadRunning, CloseSocket);
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
				CloseSocketAfterLossSave();
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
			CloseSocketAfterLossSave();
			break;
		}

		if (g_receiveThreadRunning.exchange(false) && !F4T::UIClient::TryReconnectAfterTransportLoss()) {
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

	void DeferReceiveDisconnect()
	{
		g_deferredReceiveDisconnect.store(g_connectionGeneration.load());
		g_receiveThreadRunning.store(false);
		CloseSocket();
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
			a_error = std::format("Could not resolve server address '{}' ({})", a_host, gai_strerrorA(status));
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
			LogWarningWithLocalPlayerPrefix(
				std::format("Could not connect to Commonwealth Online server at {}:{}", a_host, a_port));
			return false;
		}

		{
			const std::scoped_lock lock(g_sendMutex, g_socketMutex);
			F4T::ReplicationRuntime::NoteConnectionEstablished();
			g_socket = connectedSocket;
		}

		return true;
	}

	bool ConnectToServerInternal(const char* a_host, std::uint16_t a_port, std::string& a_error)
	{
		const std::scoped_lock operationLock(g_connectionOperationMutex);
		F4T::ParkedSuitSync::OnDisconnected();
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
		g_connectionGeneration.fetch_add(1);
		F4T::RemotePlayerState::ClearAssignedPlayerId();
		F4T::RemotePlayerState::ClearRemotePlayers();
		F4T::Factions::Reset();
		F4T::Admin::Reset();
		F4T::Friends::Reset();
		if (!F4T::Networking::IsResumeAttempt()) F4T::TradeClient::Reset();
		F4T::VendorTradeClient::Reset();
		F4T::MapPresence::Clear();
		F4T::SettlementAccessClient::Reset();
		F4T::SettlementCaptureClient::Reset();
		F4T::SettlementRaidClient::Reset();
		F4T::WorkshopReplicationClient::Reset();
		if (!F4T::Networking::IsResumeAttempt()) F4T::CharacterSlotsClient::Reset();
		F4T::Chat::Reset();
		F4T::ConsoleGuard::Reset();
		F4T::WorldStateSync::RequestReset();
		F4T::InventorySync::Reset();
		F4T::ObjectSync::Reset();
		F4T::QuestSync::ClearSession();
		F4T::StateBagSync::Reset();
		F4T::LoadOrderSync::Reset();
		F4T::SurvivalNeedsClient::Reset();
		if (!EnsureWinsockInitialized()) {
			a_error = "Networking subsystem failed to initialize.";
			return false;
		}

		if (!ResolveAndConnectIPv4(host, port, a_error)) {
			return false;
		}

		ResetPacketDiagnostics();
		{
			const std::scoped_lock lock(g_diagnosticMutex);
			g_lastKeepAliveSendTime = {};
		}
		ResetInboundBudget();
		StartReceiveThread();

		LogStatusWithLocalPlayerPrefix(
			std::format("Connected to Commonwealth Online server at {}:{}.", host, port));
		F4T::DebugLogging::Log("Networking", "connected endpoint={}:{}", host, port);
		return true;
	}
}

namespace F4T::Networking::Detail
{
	bool SendPacketBytesForGeneration(std::string_view a_packet, std::uint64_t a_generation)
	{
		std::unique_lock lock(g_connectionOperationMutex, std::defer_lock);
		if (g_isReceiveThread) {
			if (!lock.try_lock()) return false;
		} else {
			lock.lock();
		}
		return g_connectionGeneration.load() == a_generation && SendPacketBytes(a_packet);
	}

	bool SendPacketBytes(std::string_view a_packet)
	{
		return SendPacketBytes(a_packet, std::nullopt);
	}

	bool SendPacketBytes(std::string_view a_packet, std::optional<std::uint64_t> a_generation)
	{
		if (a_packet.empty() || F4T::Networking::IsLossSaveHeld()) {
			return false;
		}

		const auto deadline = std::chrono::steady_clock::now() + kSocketWriteWait;
		std::size_t sentTotal = 0;
		const std::scoped_lock sendLock(g_sendMutex);
		const auto activeSocket = GetSocket();
		if (activeSocket == INVALID_SOCKET || (a_generation &&
			(*a_generation == 0 || *a_generation != F4T::ReplicationRuntime::CurrentGeneration()))) {
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

	void InvalidateReplicationSession()
	{
		const std::scoped_lock sendLock(g_sendMutex);
		F4T::ReplicationRuntime::NoteConnectionEstablished();
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

	bool ConnectAndValidateServer(const char* a_host, std::uint16_t a_port, std::string& a_error, std::uint64_t* a_generationOut)
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
		const auto welcomeWait = std::chrono::milliseconds{
			static_cast<std::int64_t>(F4T::ConnectionPolicy::LimitsForHost(host).welcomeWaitSeconds * 1000.0)
		};
		const auto deadline = std::chrono::steady_clock::now() + welcomeWait;
		while (std::chrono::steady_clock::now() < deadline) {
			{
				const std::scoped_lock lock(g_connectionOperationMutex);
				if (g_connectionGeneration.load() != connectionGeneration || !IsConnectedToServer()) {
					a_error = "Connection ended before the server handshake completed.";
					return false;
				}
				if (F4T::RemotePlayerState::GetAssignedPlayerId().has_value()) {
					if (a_generationOut) *a_generationOut = connectionGeneration;
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
		if (g_isReceiveThread) {
			if (!operationLock.try_lock()) {
				// constraint: the lock holder may be joining this thread; never block here, the game-thread tick finishes the disconnect.
				DeferReceiveDisconnect();
				return;
			}
		} else {
			operationLock.lock();
		}
		F4T::Reconnect::Forget();
		F4T::DebugLogging::Log("Networking", "disconnect requested");
		StopReceiveThread();
		g_connectionGeneration.fetch_add(1);
		F4T::RemotePlayerState::ClearAssignedPlayerId();
		F4T::RemotePlayerState::ClearRemotePlayers();
		F4T::Factions::Reset();
		F4T::Admin::Reset();
		F4T::Friends::Reset();
		if (!F4T::Networking::IsResumeAttempt()) F4T::TradeClient::Reset();
		F4T::VendorTradeClient::Reset();
		F4T::MapPresence::Clear();
		F4T::SettlementAccessClient::Reset();
		F4T::SettlementCaptureClient::Reset();
		F4T::SettlementRaidClient::Reset();
		F4T::WorkshopReplicationClient::Reset();
		if (!F4T::Networking::IsResumeAttempt()) F4T::CharacterSlotsClient::Reset();
		F4T::Chat::Reset();
		F4T::ConsoleGuard::Reset();
		F4T::WorldStateSync::RequestReset();
		F4T::InventorySync::Reset();
		F4T::ObjectSync::Reset();
		F4T::ParkedSuitSync::OnDisconnected();
		F4T::QuestSync::ClearSession();
		F4T::StateBagSync::Reset();
		F4T::LoadOrderSync::Reset();
		F4T::SurvivalNeedsClient::Reset();
		ResetPacketDiagnostics();
		{
			const std::scoped_lock lock(g_diagnosticMutex);
			g_lastKeepAliveSendTime = {};
		}
		ResetInboundBudget();
	}

	bool DisconnectFromLocalServerIfGeneration(std::uint64_t a_generation, const std::function<bool()>& a_permitted)
	{
		const std::scoped_lock lock(g_connectionOperationMutex);
		if (g_connectionGeneration.load() == a_generation && a_permitted()) {
			DisconnectFromLocalServer();
			return true;
		}
		return false;
	}

	void RunDeferredReceiveDisconnect()
	{
		if (g_deferredReceiveDisconnect.load() == 0) {
			return;
		}

		std::unique_lock lock(g_connectionOperationMutex, std::try_to_lock);
		if (!lock.owns_lock()) {
			return;
		}

		const auto generation = g_deferredReceiveDisconnect.exchange(0);
		if (generation != 0 && g_connectionGeneration.load() == generation) {
			DisconnectFromLocalServer();
		}
	}

	bool IsConnectedToServer()
	{
		return GetSocket() != INVALID_SOCKET;
	}

	void DisconnectForShutdown()
	{
		// constraint: runs at process exit; a stuck lock holder must not hang exit, so give up after the timeout.
		const std::unique_lock operationLock(g_connectionOperationMutex, std::chrono::seconds{ 2 });
		if (!operationLock.owns_lock()) return;
		F4T::Reconnect::Forget();
		StopReceiveThread();
		g_connectionGeneration.fetch_add(1);
	}

	std::uint64_t GetConnectionGeneration()
	{
		return g_connectionGeneration.load();
	}

	bool TryRunForConnectionGeneration(std::uint64_t a_generation, const std::function<void()>& a_work)
	{
		std::unique_lock lock(g_connectionOperationMutex, std::try_to_lock);
		if (!lock.owns_lock() || a_generation == 0 || g_connectionGeneration.load() != a_generation || !IsConnectedToServer()) return false;
		a_work();
		return true;
	}

	void UpdateConnectionMaintenance()
	{
		RunDeferredReceiveDisconnect();
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

		if (SendPacketBytes(BuildKeepAlivePacket())) {
			const std::scoped_lock lock(g_diagnosticMutex);
			g_lastKeepAliveSendTime = now;
		}
	}
}
