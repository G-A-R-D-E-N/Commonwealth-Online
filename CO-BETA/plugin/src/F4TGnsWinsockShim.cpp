#include "pch.h"

#include "F4TGnsWinsockShim.h"
#include "F4TConnectionPolicy.h"
%:include "F4TCompatibilityShim.h"
#include "F4TGnsComponentReceive.h"
#include "F4TGnsProtocolAdapter.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TGnsVoice.h"
#include "F4TGnsWinsockShimState.h"
#include "F4TGnsWinsockShimInternal.h"
#include "F4TClientCompatibility.h"
#include "F4TClientIdentity.h"
#include "F4TClientModpackProbe.h"
#include "F4TLoadOrderSync.h"
#include "F4TReliableOutbox.h"
#include "F4TSecretWipe.h"
#include "F4TServerProfileStore.h"
#include "F4TWorldPolicyEnforcer.h"

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// The client must select exactly one build target (F4T_TARGET_CURRENT or
// F4T_TARGET_OG) so both DLLs obtain their identity metadata from the canonical
// co-core contract; building without a selection fails here instead of silently
// reporting an unspecified runtime.
static_assert(
    F4T::ClientCompatibility::kBuildTargetSelected,
    "Commonwealth Online client targets must define exactly one of F4T_TARGET_CURRENT or F4T_TARGET_OG");

namespace
{
	using namespace F4T::GnsWinsockShimState;

	constexpr auto kConnectTimeout = std::chrono::milliseconds{
		static_cast<std::int64_t>(F4T::ConnectionPolicy::DefaultLoopbackConnectSeconds * 1000.0)
	};
	constexpr auto kPollSleep = std::chrono::milliseconds{ 5 };

	std::mutex g_socketRegistryMutex;
	std::unordered_map<SOCKET, std::shared_ptr<SocketState>> g_socketRegistry;
	std::atomic<std::uintptr_t> g_nextSocket{ 0x434F000000000100ULL };

	static_assert(sizeof(SOCKET) >= sizeof(std::uintptr_t));

	std::shared_ptr<SocketState> FindSocket(SOCKET a_socket)
	{
		const std::scoped_lock lock(g_socketRegistryMutex);
		const auto found = g_socketRegistry.find(a_socket);
		return found == g_socketRegistry.end() ? nullptr : found->second;
	}

}

namespace F4T::GnsWinsockShim
{
	SOCKET Socket(int a_addressFamily, int a_type, int a_protocol) noexcept
	{
		if ((a_addressFamily != AF_INET && a_addressFamily != AF_INET6) || a_type != SOCK_STREAM || a_protocol != IPPROTO_TCP) {
			SetError(WSAEPROTONOSUPPORT);
			return INVALID_SOCKET;
		}

		auto state = std::make_shared<SocketState>();
		SOCKET socketValue = INVALID_SOCKET;
		{
			const std::scoped_lock lock(g_socketRegistryMutex);
			do { socketValue = static_cast<SOCKET>(g_nextSocket.fetch_add(1, std::memory_order_relaxed)); }
			while (socketValue == INVALID_SOCKET || g_socketRegistry.contains(socketValue));
			g_socketRegistry.emplace(socketValue, std::move(state));
		}
		ClearError();
		return socketValue;
	}

	int Connect(SOCKET a_socket, const sockaddr* a_address, int a_addressLength) noexcept
	{
		try {
			const auto state = FindSocket(a_socket);
			if (!state) {
				SetError(WSAENOTSOCK);
				return SOCKET_ERROR;
			}
			if (!a_address || a_addressLength < static_cast<int>(sizeof(sockaddr_in))) {
				SetError(WSAEINVAL);
				return SOCKET_ERROR;
			}

			std::array<char, INET6_ADDRSTRLEN> host{};
			std::uint16_t remotePort = 0;
			if (a_address->sa_family == AF_INET) {
				const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(a_address);
				remotePort = ntohs(ipv4->sin_port);
				if (!InetNtopA(AF_INET, const_cast<IN_ADDR*>(std::addressof(ipv4->sin_addr)), host.data(), host.size())) {
					SetError(WSAEADDRNOTAVAIL);
					return SOCKET_ERROR;
				}
			} else if (a_address->sa_family == AF_INET6 && a_addressLength >= static_cast<int>(sizeof(sockaddr_in6))) {
				const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(a_address);
				remotePort = ntohs(ipv6->sin6_port);
				if (!InetNtopA(AF_INET6, const_cast<IN6_ADDR*>(std::addressof(ipv6->sin6_addr)), host.data(), host.size())) {
					SetError(WSAEADDRNOTAVAIL);
					return SOCKET_ERROR;
				}
			} else {
				SetError(WSAEAFNOSUPPORT);
				return SOCKET_ERROR;
			}
			if (remotePort == 0) {
				SetError(WSAEAFNOSUPPORT);
				return SOCKET_ERROR;
			}
			{
				const std::scoped_lock failureLock(state->failureMutex);
				state->connectFailure.reset();
			}

			std::string bridgeError;
			auto* api = GetBridgeApi(bridgeError);
			if (!api) {
				REX::WARN("Commonwealth Online GNS transport unavailable: {}", bridgeError);
				SetError(WSASYSNOTREADY);
				return SOCKET_ERROR;
			}

			state->host = host.data();
			state->port = remotePort;
			const std::scoped_lock bridgeLock(state->bridgeMutex);
			if (state->client || state->connected.load(std::memory_order_acquire)) {
				SetError(WSAEISCONN);
				return SOCKET_ERROR;
			}

			std::array<char, 512> errorBuffer{};
			GnsClientHandle client = nullptr;
			if (api->create(
					host.data(),
					remotePort,
					std::addressof(client),
					errorBuffer.data(),
					errorBuffer.size()) != 1 ||
				!client) {
				const auto errorText = errorBuffer[0] != '\0' ? errorBuffer.data() : "GNS client creation failed.";
				REX::WARN("Commonwealth Online GNS connection failed: {}", errorText);
				SetError(WSAECONNREFUSED);
				return SOCKET_ERROR;
			}
			state->client = client;
			state->closed.store(false, std::memory_order_release);
			state->outgoingTransformSnapshotSequence.store(0, std::memory_order_release);
			state->outgoingNpcSnapshotSequence.store(0, std::memory_order_release);
			state->incomingTransformSnapshotSequence.store(0, std::memory_order_release);
			state->incomingNpcSnapshotSequence.store(0, std::memory_order_release);
			{
				const std::scoped_lock protocolLock(state->protocolMutex);
				state->protocol.Reset();
				state->protocol.SetFormIdTranslator([](std::uint32_t a_playerId, std::uint32_t a_formId) {
					return F4T::LoadOrderSync::ToLocal(a_playerId, a_formId);
				});
			}

			std::array<std::uint8_t, kMaximumMessageBytes> payload{};
			const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
			bool explicitRefusal = false;
			while (std::chrono::steady_clock::now() < deadline) {
				GnsEvent event{};
				const auto pollResult = api->poll(
					client,
					std::addressof(event),
					payload.data(),
					static_cast<std::uint32_t>(payload.size()));
				if (pollResult < 0) {
					break;
				}
				if (pollResult == 0) {
					std::this_thread::sleep_for(kPollSleep);
					continue;
				}
				if (event.type == kEventConnected) {
					auto identity = F4T::ClientIdentity::TakeConnectionIdentity();
					const std::scoped_lock protocolLock(state->protocolMutex);
					if (identity && identity->metadata) {
						const auto serverKey = identity->serverKey;
						state->serverKey = serverKey;
						state->serverPassword = identity->serverPassword.value_or(std::string{});
						state->protocol.SetClientIdentity(
							F4T::GnsProtocolAdapter::ClientIdentity{ .profileId = identity->profileId, .metadata = *identity->metadata, .resumeToken = identity->resumeToken, .serverPassword = identity->serverPassword, .modpackFingerprint = identity->modpackFingerprint, .saveAttestation = identity->saveAttestation, .profileSecret = identity->profileSecret, .steamId = identity->steamId, .steamTicket = identity->steamTicket },
							[serverKey](const F4T::GnsProtocolAdapter::ReadyIdentity& a_identity, std::string& a_error) {
								return F4T::ClientIdentity::CommitReadyIdentity(serverKey, a_identity, a_error);
							});
					}
					if (identity && identity->serverPassword) {
						F4T::Secret::Wipe(*identity->serverPassword);
					}
					state->connected.store(true, std::memory_order_release);
					ClearError();
					return 0;
				}
				if (event.type == kEventDisconnected) {
					const auto debug = EventDebugText(event);
					RecordConnectFailure(state, ConnectFailure{
						.kind = ConnectFailureKind::AdmissionRefused,
						.transportReason = event.reason,
						.detail = debug
					});
					explicitRefusal = true;
					REX::WARN(
						"Commonwealth Online GNS connection closed during connect (reason={}, debug={}).",
						event.reason,
						debug.empty() ? "<none>" : debug);
					break;
				}
			}

			api->disconnect(client, 0, "connect failed");
			api->destroy(client);
			state->client = nullptr;
			state->connected.store(false, std::memory_order_release);
			SetError(explicitRefusal ? WSAECONNREFUSED : WSAETIMEDOUT);
			return SOCKET_ERROR;
		} catch (const std::exception& error) {
			REX::WARN("Commonwealth Online GNS connect exception: {}", error.what());
			SetError(WSASYSNOTREADY);
			return SOCKET_ERROR;
		} catch (...) {
			SetError(WSASYSNOTREADY);
			return SOCKET_ERROR;
		}
	}

	int IoctlSocket(SOCKET a_socket, long a_command, u_long* a_argument) noexcept
	{
		if (!FindSocket(a_socket)) {
			SetError(WSAENOTSOCK);
			return SOCKET_ERROR;
		}
		if (a_command != FIONBIO || !a_argument) {
			SetError(WSAEINVAL);
			return SOCKET_ERROR;
		}
		ClearError();
		return 0;
	}

	int Send(SOCKET a_socket, const char* a_buffer, int a_length, int) noexcept
	{
		try {
			if (!a_buffer || a_length <= 0) {
				SetError(WSAEINVAL);
				return SOCKET_ERROR;
			}
			const auto state = FindSocket(a_socket);
			if (!state) {
				SetError(WSAENOTSOCK);
				return SOCKET_ERROR;
			}
			if (!state->connected.load(std::memory_order_acquire) || state->closed.load(std::memory_order_acquire)) {
				SetError(WSAENOTCONN);
				return SOCKET_ERROR;
			}

			std::vector<F4T::GnsProtocolAdapter::OutboundMessage> messages;
			{
				const std::scoped_lock protocolLock(state->protocolMutex);
				messages = state->protocol.AdaptOutbound(
					std::string_view{ a_buffer, static_cast<std::size_t>(a_length) });
			}

			if (messages.empty()) {
				ClearError();
				return a_length;
			}
			if (!SendAdaptedMessages(state, messages)) {
				return SOCKET_ERROR;
			}
			return a_length;
		} catch (const std::exception& error) {
			REX::WARN("Commonwealth Online GNS send exception: {}", error.what());
			SetError(WSAECONNABORTED);
			return SOCKET_ERROR;
		} catch (...) {
			SetError(WSAECONNABORTED);
			return SOCKET_ERROR;
		}
	}

	int Recv(SOCKET a_socket, char* a_buffer, int a_length, int) noexcept
	{
		try {
			if (!a_buffer || a_length <= 0) {
				SetError(WSAEINVAL);
				return SOCKET_ERROR;
			}
			const auto state = FindSocket(a_socket);
			if (!state) {
				SetError(WSAENOTSOCK);
				return SOCKET_ERROR;
			}
			if (!FlushReliableOutbox(state)) {
				return SOCKET_ERROR;
			}
			if (const auto pending = ServePendingReceive(state, a_buffer, a_length); pending > 0) {
				ClearError();
				return pending;
			}
			if (state->closed.load(std::memory_order_acquire)) {
				ClearError();
				return 0;
			}

			std::string bridgeError;
			auto* api = GetBridgeApi(bridgeError);
			if (!api) {
				SetError(WSASYSNOTREADY);
				return SOCKET_ERROR;
			}

			std::array<std::uint8_t, kMaximumMessageBytes> payload{};
			for (std::size_t attempts = 0; attempts < 128; ++attempts) {
				GnsEvent event{};
				int pollResult = 0;
				{
					const std::scoped_lock bridgeLock(state->bridgeMutex);
					if (!state->client || state->closed.load(std::memory_order_acquire)) {
						ClearError();
						return 0;
					}
					pollResult = api->poll(
						state->client,
						std::addressof(event),
						payload.data(),
						static_cast<std::uint32_t>(payload.size()));
				}

				if (pollResult == 0) {
					SetError(WSAEWOULDBLOCK);
					return SOCKET_ERROR;
				}
				if (pollResult < 0) {
					SetError(pollResult == -2 ? WSAEMSGSIZE : WSAECONNABORTED);
					return SOCKET_ERROR;
				}

				if (event.type == kEventConnected) {
					state->connected.store(true, std::memory_order_release);
					continue;
				}
				if (event.type == kEventDisconnected) {
					bool protocolReady = false;
					{
						const std::scoped_lock protocolLock(state->protocolMutex);
						protocolReady = state->protocol.IsReady();
					}
					const auto debug = EventDebugText(event);
					if (!protocolReady) {
						RecordConnectFailure(state, ConnectFailure{
							.kind = ConnectFailureKind::AdmissionRefused,
							.transportReason = event.reason,
							.detail = debug
						});
					}
					state->connected.store(false, std::memory_order_release);
					state->closed.store(true, std::memory_order_release);
					F4T::DebugLogging::Log(
						"Networking",
						"GNS disconnected reason={} debug={}",
						event.reason,
						debug.empty() ? "<none>" : debug);
					ClearError();
					return 0;
				}
				if (event.type == kEventOversizeMessage) {
					SetError(WSAEMSGSIZE);
					return SOCKET_ERROR;
				}
				if (event.type != kEventMessage || event.payloadSize == 0 || event.payloadSize > payload.size()) {
					continue;
				}

				if (F4T::GnsVoice::TryHandleInbound(state, std::span<const std::uint8_t>{ payload.data(), event.payloadSize })) { continue; }
				const auto classified = F4T::ComponentReceive::ClassifyReceive(*state, std::span<const std::uint8_t>{ payload.data(), event.payloadSize });
				if (!classified.Accepted()) {
					DisconnectForProtocolViolation(state, "malformed or unsupported receive payload");
					ClearError();
					return 0;
				}
				if (classified.classification == F4T::Compatibility::ReceiveClassification::Component) {
					const auto componentError = F4T::ComponentReceive::HandleComponentFrame(classified);
					if (componentError.empty()) { continue; }
					DisconnectForProtocolViolation(state, componentError);
					ClearError();
					return 0;
				}

				const std::size_t payloadOffset = classified.legacyPayloadOffset;
				const std::size_t payloadSize = event.payloadSize - payloadOffset;
				if (classified.legacyHeader.has_value()) {
					auto& incomingSequence = IncomingSnapshotSequence(*state, classified.legacyHeader->family);
					const auto lastSequence = incomingSequence.load(std::memory_order_acquire);
					if (!F4T::GnsSnapshotEnvelope::IsNewer(classified.legacyHeader->sequence, lastSequence)) {
						continue;
					}
					incomingSequence.store(classified.legacyHeader->sequence, std::memory_order_release);
				}

				F4T::GnsProtocolAdapter::InboundResult adapted;
				std::optional<F4T::WorldPolicy::Policy> worldPolicy;
				{
					const std::scoped_lock protocolLock(state->protocolMutex);
					adapted = state->protocol.AdaptInbound(std::string_view{
						reinterpret_cast<const char*>(payload.data() + payloadOffset),
						payloadSize });
					if (state->protocol.IsReady()) {
						WipeServerPassword(state->serverPassword);
					}
					worldPolicy = state->protocol.GetWorldPolicy();
				}
				if (worldPolicy) {
					F4T::WorldPolicyEnforcer::AcceptPolicy(*worldPolicy);
				}
				if (adapted.protocolViolation) {
					DisconnectForProtocolViolation(state, adapted.error);
					ClearError();
					return 0;
				}
				if (adapted.rejection) {
					DisconnectForSessionRejection(state, *adapted.rejection);
					ClearError();
					return 0;
				}
				if (adapted.localHandshakeFailure) {
					DisconnectForLocalHandshakeFailure(state, adapted.error);
					ClearError();
					return 0;
				}
				if (!adapted.controlMessages.empty() && !SendAdaptedMessages(state, adapted.controlMessages)) {
					return SOCKET_ERROR;
				}
				for (auto& message : adapted.legacyMessages) {
					QueueLegacyMessage(state, std::move(message));
				}
				if (const auto received = ServePendingReceive(state, a_buffer, a_length); received > 0) {
					ClearError();
					return received;
				}
			}

			SetError(WSAEWOULDBLOCK);
			return SOCKET_ERROR;
		} catch (const std::exception& error) {
			REX::WARN("Commonwealth Online GNS receive exception: {}", error.what());
			SetError(WSAECONNABORTED);
			return SOCKET_ERROR;
		} catch (...) {
			SetError(WSAECONNABORTED);
			return SOCKET_ERROR;
		}
	}

	int Shutdown(SOCKET a_socket, int) noexcept
	{
		try {
			const auto state = FindSocket(a_socket);
			if (!state) {
				SetError(WSAENOTSOCK);
				return SOCKET_ERROR;
			}
			state->cancelRequested.store(true, std::memory_order_release);
			state->closed.store(true, std::memory_order_release);
			state->connected.store(false, std::memory_order_release);
			ClearError();
			return 0;
		} catch (...) {
			SetError(WSAECONNABORTED);
			return SOCKET_ERROR;
		}
	}

	int CloseSocket(SOCKET a_socket) noexcept
	{
		try {
			std::shared_ptr<SocketState> state;
			{
				const std::scoped_lock lock(g_socketRegistryMutex);
				const auto found = g_socketRegistry.find(a_socket);
				if (found == g_socketRegistry.end()) {
					SetError(WSAENOTSOCK);
					return SOCKET_ERROR;
				}
				state = found->second;
				g_socketRegistry.erase(found);
			}

			if (auto failure = TakeStateConnectFailure(state)) {
				PreserveThreadConnectFailure(std::move(*failure));
			}
			state->cancelRequested.store(true, std::memory_order_release);
			state->closed.store(true, std::memory_order_release);
			state->connected.store(false, std::memory_order_release);
			std::string bridgeError;
			auto* api = GetBridgeApi(bridgeError);
			const std::scoped_lock bridgeLock(state->bridgeMutex);
			state->reliableOutbox.Clear();
			if (state->client && api) {
				api->disconnect(state->client, 0, "client disconnect");
				api->destroy(state->client);
				state->client = nullptr;
			}
			ClearError();
			return 0;
		} catch (...) {
			SetError(WSAECONNABORTED);
			return SOCKET_ERROR;
		}
	}

	int LastError() noexcept
	{
		return g_lastError;
	}

	std::optional<ConnectFailure> TakeConnectFailure(SOCKET a_socket) noexcept
	{
		try {
			const auto state = FindSocket(a_socket);
			return state ? TakeStateConnectFailure(state) : std::nullopt;
		} catch (...) {
			return std::nullopt;
		}
	}

	std::optional<ConnectFailure> TakeCurrentThreadConnectFailure() noexcept
	{
		auto failure = std::move(g_currentThreadConnectFailure);
		g_currentThreadConnectFailure.reset();
		return failure;
	}

	std::optional<NpcAuthorityAssignment> GetNpcAuthorityForScope(
		std::string_view a_cellId,
		std::string_view a_worldspaceId) noexcept
	{
		try {
			std::vector<std::shared_ptr<SocketState>> states;
			{
				const std::scoped_lock lock(g_socketRegistryMutex);
				states.reserve(g_socketRegistry.size());
				for (const auto& [_, state] : g_socketRegistry) {
					states.push_back(state);
				}
			}

			for (const auto& state : states) {
				if (!state || state->closed.load(std::memory_order_acquire)) {
					continue;
				}
				const std::scoped_lock protocolLock(state->protocolMutex);
				if (const auto authority = state->protocol.GetNpcAuthority(a_cellId, a_worldspaceId)) {
					return NpcAuthorityAssignment{ authority->playerId, authority->epoch };
				}
			}
		} catch (...) {
		}
		return std::nullopt;
	}
}
