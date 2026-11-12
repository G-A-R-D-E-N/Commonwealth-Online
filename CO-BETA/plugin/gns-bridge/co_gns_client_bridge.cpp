#include "co_gns_client_bridge.h"
#include "co_gns_auth_protocol.h"

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	constexpr std::size_t kDefaultMaximumMessageBytes = 256u * 1024u;
	constexpr std::size_t kHardMaximumMessageBytes = 512u * 1024u;
	constexpr std::size_t kMinimumMaximumMessageBytes = 64u * 1024u;
	std::atomic<std::size_t> g_maximumMessageBytes{ kDefaultMaximumMessageBytes };

	[[nodiscard]] std::size_t MaximumMessageBytes() noexcept
	{
		return g_maximumMessageBytes.load(std::memory_order_relaxed);
	}

	[[nodiscard]] bool SetMaximumMessageBytes(std::size_t a_bytes) noexcept
	{
		if (a_bytes < kMinimumMaximumMessageBytes || a_bytes > kHardMaximumMessageBytes) {
			return false;
		}
		g_maximumMessageBytes.store(a_bytes, std::memory_order_relaxed);
		return true;
	}

	constexpr std::size_t kReceiveBatchSize = 64u;
	constexpr std::size_t kMaximumHeldFrames = kReceiveBatchSize;

	struct QueuedEvent
	{
		co_gns_event metadata{};
		std::vector<std::uint8_t> payload;
	};

	class ClientBridge;

	std::mutex g_runtimeMutex;
	std::size_t g_runtimeReferenceCount = 0;
	std::mutex g_callbackDispatchMutex;
	std::mutex g_ownerMutex;
	std::unordered_map<HSteamNetConnection, ClientBridge*> g_clientOwners;

	void WriteError(char* a_buffer, std::size_t a_bufferSize, const std::string& a_message)
	{
		if (!a_buffer || a_bufferSize == 0) {
			return;
		}
		std::snprintf(a_buffer, a_bufferSize, "%s", a_message.c_str());
	}

	bool AcquireRuntime(std::string& a_error)
	{
		const std::scoped_lock lock(g_runtimeMutex);
		if (g_runtimeReferenceCount > 0) {
			++g_runtimeReferenceCount;
			return true;
		}

		SteamNetworkingErrMsg errorMessage{};
		if (!GameNetworkingSockets_Init(nullptr, errorMessage)) {
			a_error = errorMessage[0] ? std::string{ errorMessage } : "GameNetworkingSockets_Init failed.";
			return false;
		}
		g_runtimeReferenceCount = 1;
		return true;
	}

	void ReleaseRuntime()
	{
		const std::scoped_lock lock(g_runtimeMutex);
		if (g_runtimeReferenceCount == 0) {
			return;
		}
		--g_runtimeReferenceCount;
		if (g_runtimeReferenceCount == 0) {
			GameNetworkingSockets_Kill();
		}
	}

	class ClientBridge
	{
	public:
		~ClientBridge()
		{
			Stop();
		}

		bool Start(const char* a_remoteHost, std::uint16_t a_port, std::string& a_error)
		{
			if (!a_remoteHost || a_remoteHost[0] == '\0' || a_port == 0) {
				a_error = "GNS remote address and port are required.";
				return false;
			}
			if (!AcquireRuntime(a_error)) {
				return false;
			}
			runtimeHeld_ = true;
			networking_ = SteamNetworkingSockets();
			if (!networking_) {
				a_error = "SteamNetworkingSockets returned no client interface.";
				Stop();
				return false;
			}

			SteamNetworkingIPAddr address{};
			address.Clear();
			std::string host{ a_remoteHost };
			if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
				host = host.substr(1, host.size() - 2);
			}
			if (!address.ParseString(host.c_str()) || address.IsIPv6AllZeros() ||
				(address.IsIPv4() && address.GetIPv4() == 0U)) {
				a_error = "GNS remote address must be a valid IPv4 or IPv6 address.";
				Stop();
				return false;
			}
			address.m_port = a_port;
			pinKey_ = FormatPinKey(host, a_port);

			SteamNetworkingConfigValue_t option{};
			option.SetPtr(
				k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
				reinterpret_cast<void*>(+ConnectionStatusChanged));

			const std::scoped_lock callbackLock(g_callbackDispatchMutex);
			connection_ = networking_->ConnectByIPAddress(address, 1, std::addressof(option));
			if (connection_ == k_HSteamNetConnection_Invalid) {
				a_error = "GameNetworkingSockets failed to create a client connection to " +
					std::string{ a_remoteHost } + ":" + std::to_string(a_port) + ".";
				StopWithoutCallbackLock();
				return false;
			}
			{
				const std::scoped_lock ownerLock(g_ownerMutex);
				g_clientOwners[connection_] = this;
			}
			return true;
		}

		void Stop()
		{
			const std::scoped_lock callbackLock(g_callbackDispatchMutex);
			StopWithoutCallbackLock();
		}

		int Poll(co_gns_event& a_outEvent, void* a_payloadBuffer, std::uint32_t a_payloadCapacity)
		{
			ISteamNetworkingSockets* networking = nullptr;
			{
				const std::scoped_lock lock(mutex_);
				networking = networking_;
			}
			if (!networking) {
				return -1;
			}

			{
				const std::scoped_lock callbackLock(g_callbackDispatchMutex);
				networking->RunCallbacks();
			}

			bool shouldPump = false;
			{
				const std::scoped_lock lock(mutex_);
				shouldPump = events_.empty() && connected_ && connection_ != k_HSteamNetConnection_Invalid;
			}
			if (shouldPump) {
				PumpMessages();
			}

			AdvanceAuthentication();

			const std::scoped_lock lock(mutex_);
			if (events_.empty()) {
				std::memset(std::addressof(a_outEvent), 0, sizeof(a_outEvent));
				return 0;
			}

			const auto& next = events_.front();
			a_outEvent = next.metadata;
			if (a_outEvent.type == CO_GNS_EVENT_MESSAGE && next.payload.size() > a_payloadCapacity) {
				return -2;
			}
			if (a_outEvent.type == CO_GNS_EVENT_MESSAGE && !next.payload.empty()) {
				if (!a_payloadBuffer) {
					return -2;
				}
				std::memcpy(a_payloadBuffer, next.payload.data(), next.payload.size());
			}
			events_.pop_front();
			return 1;
		}

		int GetStats(co_gns_connection_stats& a_out)
		{
			ISteamNetworkingSockets* networking = nullptr;
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			{
				const std::scoped_lock lock(mutex_);
				networking = networking_;
				connection = connection_;
			}
			if (!networking || connection == k_HSteamNetConnection_Invalid) {
				return 0;
			}

			SteamNetConnectionRealTimeStatus_t status{};
			if (networking->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK) {
				return 0;
			}
			a_out.ping_ms = status.m_nPing;
			a_out.quality_local = status.m_flConnectionQualityLocal;
			a_out.quality_remote = status.m_flConnectionQualityRemote;
			a_out.in_bytes_per_sec = status.m_flInBytesPerSec;
			a_out.out_bytes_per_sec = status.m_flOutBytesPerSec;
			a_out.in_packets_per_sec = status.m_flInPacketsPerSec;
			a_out.out_packets_per_sec = status.m_flOutPacketsPerSec;
			a_out.pending_bytes = (std::max)(0, status.m_cbPendingUnreliable + status.m_cbPendingReliable);
			return 1;
		}

		int Send(const void* a_payload, std::uint32_t a_payloadSize, std::uint32_t a_delivery)
		{
			if (!a_payload || a_payloadSize == 0) {
				return CO_GNS_SEND_ERROR;
			}
			if (a_payloadSize > MaximumMessageBytes()) {
				return CO_GNS_SEND_TOO_LARGE;
			}
			if (a_delivery != CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED &&
				a_delivery != CO_GNS_DELIVERY_RELIABLE_ORDERED) {
				return CO_GNS_SEND_ERROR;
			}

			ISteamNetworkingSockets* networking = nullptr;
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			{
				const std::scoped_lock lock(mutex_);
				if (!connected_ || connection_ == k_HSteamNetConnection_Invalid ||
					authState_ != AuthState::Authenticated) {
					return CO_GNS_SEND_NOT_CONNECTED;
				}
				networking = networking_;
				connection = connection_;
			}
			if (!networking) {
				return CO_GNS_SEND_NOT_CONNECTED;
			}

			const int flags = a_delivery == CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED ?
				k_nSteamNetworkingSend_UnreliableNoDelay :
				k_nSteamNetworkingSend_ReliableNoNagle;
			const auto result = networking->SendMessageToConnection(
				connection,
				a_payload,
				a_payloadSize,
				flags,
				nullptr);
			switch (result) {
			case k_EResultOK:
				return CO_GNS_SEND_SENT;
			case k_EResultIgnored:
				return CO_GNS_SEND_DROPPED;
			case k_EResultLimitExceeded:
				return CO_GNS_SEND_BACKPRESSURE;
			case k_EResultNoConnection:
			case k_EResultInvalidState:
				return CO_GNS_SEND_NOT_CONNECTED;
			default:
				return CO_GNS_SEND_ERROR;
			}
		}

		int Disconnect(std::int32_t a_reason, const char* a_debug)
		{
			const std::scoped_lock callbackLock(g_callbackDispatchMutex);
			ISteamNetworkingSockets* networking = nullptr;
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			{
				const std::scoped_lock lock(mutex_);
				connection = connection_;
				if (connection == k_HSteamNetConnection_Invalid) {
					return 0;
				}
				networking = networking_;
				connection_ = k_HSteamNetConnection_Invalid;
				connected_ = false;
			}
			{
				const std::scoped_lock ownerLock(g_ownerMutex);
				g_clientOwners.erase(connection);
			}
			if (!networking) {
				return 0;
			}
			return networking->CloseConnection(connection, a_reason, a_debug, false) ? 1 : 0;
		}

	private:
		static std::string FormatPinKey(std::string a_host, std::uint16_t a_port)
		{
			if (a_host.find(':') != std::string::npos) {
				a_host = "[" + a_host + "]";
			}
			return a_host + ":" + std::to_string(a_port);
		}

		static ClientBridge* FindOwner(HSteamNetConnection a_connection)
		{
			const std::scoped_lock ownerLock(g_ownerMutex);
			const auto found = g_clientOwners.find(a_connection);
			return found == g_clientOwners.end() ? nullptr : found->second;
		}

		static void ConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* a_info)
		{
			if (!a_info) {
				return;
			}
			if (auto* owner = FindOwner(a_info->m_hConn)) {
				owner->OnConnectionStatusChanged(*a_info);
			}
		}

		static co_gns_event MakeEvent(
			std::uint32_t a_type,
			HSteamNetConnection a_connection,
			std::int32_t a_reason,
			std::uint32_t a_payloadSize,
			const char* a_debug)
		{
			co_gns_event event{};
			event.type = a_type;
			event.connection_id = static_cast<std::uint32_t>(a_connection);
			event.reason = a_reason;
			event.payload_size = a_payloadSize;
			if (a_debug && a_debug[0] != '\0') {
				std::snprintf(event.debug, sizeof(event.debug), "%s", a_debug);
			}
			return event;
		}

		static QueuedEvent MakeFrameEvent(HSteamNetConnection a_connection, const void* a_data, std::size_t a_size)
		{
			QueuedEvent queued{};
			if (a_size > MaximumMessageBytes()) {
				const auto boundedSize = (std::min)(
					a_size,
					static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()));
				queued.metadata = MakeEvent(
					CO_GNS_EVENT_OVERSIZE_MESSAGE,
					a_connection,
					0,
					static_cast<std::uint32_t>(boundedSize),
					"Inbound GNS message exceeded maximum message size");
			} else {
				queued.metadata = MakeEvent(
					CO_GNS_EVENT_MESSAGE,
					a_connection,
					0,
					static_cast<std::uint32_t>(a_size),
					nullptr);
				if (a_size > 0 && a_data != nullptr) {
					const auto* begin = static_cast<const std::uint8_t*>(a_data);
					queued.payload.assign(begin, begin + a_size);
				}
			}
			return queued;
		}

		void OnConnectionStatusChanged(const SteamNetConnectionStatusChangedCallback_t& a_info)
		{
			if (a_info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
				const std::scoped_lock lock(mutex_);
				if (connection_ == a_info.m_hConn && !connected_) {
					connected_ = true;
					authState_ = AuthState::HelloPending;
					authNonce_ = co_gns::auth::RandomNonce();
					ResetAuthentication();
					authDeadline_ = std::chrono::steady_clock::now() + co_gns::auth::kAuthTimeout;
				}
				return;
			}

			if (a_info.m_info.m_eState != k_ESteamNetworkingConnectionState_ClosedByPeer &&
				a_info.m_info.m_eState != k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
				return;
			}

			while (networking_ && ReceiveBatch(networking_, a_info.m_hConn) > 0) {}

			bool knownConnection = false;
			{
				const std::scoped_lock lock(mutex_);
				if (connection_ == a_info.m_hConn) {
					knownConnection = true;
					connection_ = k_HSteamNetConnection_Invalid;
					connected_ = false;
					authState_ = AuthState::Idle;
					ResetAuthentication();
					events_.push_back(QueuedEvent{
						MakeEvent(
							CO_GNS_EVENT_DISCONNECTED,
							a_info.m_hConn,
							a_info.m_info.m_eEndReason,
							0,
							a_info.m_info.m_szEndDebug),
						{}
					});
				}
			}
			if (!knownConnection) {
				return;
			}
			{
				const std::scoped_lock ownerLock(g_ownerMutex);
				g_clientOwners.erase(a_info.m_hConn);
			}
			if (networking_) {
				networking_->CloseConnection(a_info.m_hConn, 0, nullptr, false);
			}
		}

		void PumpMessages()
		{
			ISteamNetworkingSockets* networking = nullptr;
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			{
				const std::scoped_lock lock(mutex_);
				networking = networking_;
				connection = connection_;
			}
			if (!networking || connection == k_HSteamNetConnection_Invalid) {
				return;
			}
			ReceiveBatch(networking, connection);
		}

		int ReceiveBatch(ISteamNetworkingSockets* networking, HSteamNetConnection connection)
		{
			std::array<SteamNetworkingMessage_t*, kReceiveBatchSize> messages{};
			const auto count = networking->ReceiveMessagesOnConnection(
				connection,
				messages.data(),
				static_cast<int>(messages.size()));
			if (count <= 0) {
				return 0;
			}

			for (int index = 0; index < count; ++index) {
				auto* message = messages[static_cast<std::size_t>(index)];
				if (!message) {
					continue;
				}

				const auto size = message->m_cbSize > 0 ? static_cast<std::size_t>(message->m_cbSize) : 0u;
				bool authenticated = false;
				{
					const std::scoped_lock lock(mutex_);
					authenticated = authState_ == AuthState::Authenticated;
				}
				if (!authenticated) {
					bool buffered = false;
					if (size > 0 && size <= MaximumMessageBytes() && message->m_pData != nullptr) {
						const auto* begin = static_cast<const std::uint8_t*>(message->m_pData);
						const std::scoped_lock lock(mutex_);
						if (connection_ == connection && authState_ == AuthState::AwaitingReply) {
							if (!authReplyReceived_) {
								authReply_.assign(begin, begin + size);
								authReplyReceived_ = true;
								buffered = true;
							} else if (authHeldFrames_.size() < kMaximumHeldFrames) {
								authHeldFrames_.push_back(MakeFrameEvent(connection, begin, size));
								buffered = true;
							}
						}
					}
					if (!buffered) {
						const std::scoped_lock lock(mutex_);
						if (connection_ == connection && authState_ != AuthState::Authenticated) {
							authUnexpectedFrame_ = true;
						}
					}
					message->Release();
					continue;
				}

				QueuedEvent queued = MakeFrameEvent(connection, message->m_pData, size);

				{
					const std::scoped_lock lock(mutex_);
					if (connection_ == connection) {
						events_.push_back(std::move(queued));
					}
				}
				message->Release();
			}
			return count;
		}

		void StopWithoutCallbackLock()
		{
			ISteamNetworkingSockets* networking = nullptr;
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			{
				const std::scoped_lock lock(mutex_);
				networking = networking_;
				connection = connection_;
				networking_ = nullptr;
				connection_ = k_HSteamNetConnection_Invalid;
				connected_ = false;
				authState_ = AuthState::Idle;
				ResetAuthentication();
				events_.clear();
			}
			if (connection != k_HSteamNetConnection_Invalid) {
				const std::scoped_lock ownerLock(g_ownerMutex);
				g_clientOwners.erase(connection);
			}
			if (networking && connection != k_HSteamNetConnection_Invalid) {
				networking->CloseConnection(connection, 0, nullptr, false);
			}
			if (runtimeHeld_) {
				runtimeHeld_ = false;
				ReleaseRuntime();
			}
		}

		enum class AuthState
		{
			Idle,
			HelloPending,
			AwaitingReply,
			Authenticated,
			Failed
		};

		void ResetAuthentication()
		{
			authReply_.clear();
			authReplyReceived_ = false;
			authUnexpectedFrame_ = false;
			authHeldFrames_.clear();
		}

		void AdvanceAuthentication()
		{
			AuthState state = AuthState::Idle;
			co_gns::auth::Nonce nonce{};
			std::vector<std::uint8_t> reply;
			bool replyReceived = false;
			std::string pinKey;
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			ISteamNetworkingSockets* networking = nullptr;
			bool unexpectedFrame = false;
			bool expired = false;
			{
				const std::scoped_lock lock(mutex_);
				state = authState_;
				nonce = authNonce_;
				reply = std::move(authReply_);
				authReply_.clear();
				replyReceived = authReplyReceived_;
				pinKey = pinKey_;
				connection = connection_;
				networking = networking_;
				unexpectedFrame = authUnexpectedFrame_;
				expired = std::chrono::steady_clock::now() >= authDeadline_;
			}
			if (!networking || connection == k_HSteamNetConnection_Invalid) {
				return;
			}
			if (state == AuthState::Idle || state == AuthState::Authenticated || state == AuthState::Failed) {
				return;
			}

			if (unexpectedFrame) {
				FailAuthentication("server authentication failed: the server sent application data before proving its identity");
				return;
			}

			if (state == AuthState::HelloPending) {
				const auto hello = co_gns::auth::BuildHello(nonce);
				const auto result = networking->SendMessageToConnection(
					connection,
					hello.data(),
					static_cast<std::uint32_t>(hello.size()),
					k_nSteamNetworkingSend_ReliableNoNagle,
					nullptr);
				if (result != k_EResultOK) {
					FailAuthentication("server authentication failed: could not send the identity challenge");
					return;
				}
				const std::scoped_lock lock(mutex_);
				if (authState_ == AuthState::HelloPending) {
					authState_ = AuthState::AwaitingReply;
					authDeadline_ = std::chrono::steady_clock::now() + co_gns::auth::kAuthTimeout;
				}
				return;
			}

			if (replyReceived) {
				co_gns::auth::PublicKey publicKey{};
				co_gns::auth::Signature signature{};
				if (!co_gns::auth::ParseReply(reply.data(), reply.size(), publicKey, signature)) {
					FailAuthentication("server authentication failed: the server sent an unusable identity proof");
					return;
				}
				if (!co_gns::auth::VerifyNonce(publicKey, nonce, signature)) {
					FailAuthentication("server authentication failed: the identity proof signature is invalid");
					return;
				}
				std::string pinError;
				const auto pin = co_gns::auth::CheckPin(pinKey, publicKey, pinError);
				if (pin == co_gns::auth::PinResult::Mismatch) {
					FailAuthentication("server authentication failed: " + pinError);
					return;
				}
				std::string notice = "server identity ";
				notice += co_gns::auth::ToHex(publicKey);
				if (pin == co_gns::auth::PinResult::FirstUse) {
					notice += " pinned on first use";
					co_gns::auth::Log("[co-gns] " + notice + " for " + pinKey);
				}
				const std::scoped_lock lock(mutex_);
				if (connection_ != connection || authState_ != AuthState::AwaitingReply) {
					return;
				}
				authState_ = AuthState::Authenticated;
				events_.push_back(QueuedEvent{
					MakeEvent(CO_GNS_EVENT_CONNECTED, connection, 0, 0, notice.c_str()),
					{}
				});
				for (auto& frame : authHeldFrames_) {
					events_.push_back(std::move(frame));
				}
				authHeldFrames_.clear();
				return;
			}

			if (expired) {
				FailAuthentication("server authentication failed: no identity proof arrived; the server may be outdated");
			}
		}

		void FailAuthentication(const std::string& a_debug)
		{
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			ISteamNetworkingSockets* networking = nullptr;
			{
				const std::scoped_lock lock(mutex_);
				if (authState_ == AuthState::Failed || authState_ == AuthState::Authenticated) {
					return;
				}
				authState_ = AuthState::Failed;
				authHeldFrames_.clear();
				connection = connection_;
				networking = networking_;
			}
			if (!networking || connection == k_HSteamNetConnection_Invalid) {
				return;
			}
			co_gns::auth::Log("[co-gns] " + a_debug);
			const std::scoped_lock callbackLock(g_callbackDispatchMutex);
			networking->CloseConnection(connection, co_gns::auth::kAuthFailureReason, a_debug.c_str(), false);
		}

		mutable std::mutex mutex_;
		ISteamNetworkingSockets* networking_{ nullptr };
		HSteamNetConnection connection_{ k_HSteamNetConnection_Invalid };
		std::deque<QueuedEvent> events_;
		bool connected_{ false };
		bool runtimeHeld_{ false };
		AuthState authState_{ AuthState::Idle };
		co_gns::auth::Nonce authNonce_{};
		std::vector<std::uint8_t> authReply_;
		bool authReplyReceived_{ false };
		bool authUnexpectedFrame_{ false };
		std::vector<QueuedEvent> authHeldFrames_;
		std::chrono::steady_clock::time_point authDeadline_{};
		std::string pinKey_;
	};

	static_assert(sizeof(co_gns_event) == 144);
}

extern "C"
{
	int co_gns_client_create(
		const char* a_remoteHost,
		std::uint16_t a_port,
		co_gns_client_handle* a_outHandle,
		char* a_errorBuffer,
		std::size_t a_errorBufferSize)
	{
		if (!a_outHandle) {
			WriteError(a_errorBuffer, a_errorBufferSize, "out_handle is required.");
			return 0;
		}
		*a_outHandle = nullptr;

		auto* client = new ClientBridge();
		std::string error;
		if (!client->Start(a_remoteHost, a_port, error)) {
			delete client;
			WriteError(a_errorBuffer, a_errorBufferSize, error);
			return 0;
		}
		*a_outHandle = client;
		WriteError(a_errorBuffer, a_errorBufferSize, "");
		return 1;
	}

	void co_gns_client_destroy(co_gns_client_handle a_handle)
	{
		delete static_cast<ClientBridge*>(a_handle);
	}

	int co_gns_client_poll(
		co_gns_client_handle a_handle,
		co_gns_event* a_outEvent,
		void* a_payloadBuffer,
		std::uint32_t a_payloadCapacity)
	{
		auto* client = static_cast<ClientBridge*>(a_handle);
		if (!client || !a_outEvent) {
			return -1;
		}
		return client->Poll(*a_outEvent, a_payloadBuffer, a_payloadCapacity);
	}

	int co_gns_client_send(
		co_gns_client_handle a_handle,
		const void* a_payload,
		std::uint32_t a_payloadSize,
		std::uint32_t a_delivery)
	{
		auto* client = static_cast<ClientBridge*>(a_handle);
		return client ? client->Send(a_payload, a_payloadSize, a_delivery) : CO_GNS_SEND_ERROR;
	}

	int co_gns_client_disconnect(
		co_gns_client_handle a_handle,
		std::int32_t a_reason,
		const char* a_debug)
	{
		auto* client = static_cast<ClientBridge*>(a_handle);
		return client ? client->Disconnect(a_reason, a_debug) : 0;
	}

	int co_gns_client_get_stats(
		co_gns_client_handle a_handle,
		co_gns_connection_stats* a_outStats)
	{
		auto* client = static_cast<ClientBridge*>(a_handle);
		if (!client || !a_outStats) {
			return 0;
		}
		return client->GetStats(*a_outStats);
	}

	int co_gns_set_maximum_message_bytes(std::size_t maximum_message_bytes)
	{
		return SetMaximumMessageBytes(maximum_message_bytes) ? 1 : 0;
	}

	std::size_t co_gns_maximum_message_bytes(void)
	{
		return MaximumMessageBytes();
	}
}
