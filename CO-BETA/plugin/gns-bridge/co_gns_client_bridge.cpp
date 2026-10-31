#include "co_gns_client_bridge.h"

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

#include <algorithm>
#include <array>
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
	constexpr std::size_t kMaximumMessageBytes = 64u * 1024u;
	constexpr std::size_t kReceiveBatchSize = 64u;

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

		int Send(const void* a_payload, std::uint32_t a_payloadSize, std::uint32_t a_delivery)
		{
			if (!a_payload || a_payloadSize == 0) {
				return CO_GNS_SEND_ERROR;
			}
			if (a_payloadSize > kMaximumMessageBytes) {
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
				if (!connected_ || connection_ == k_HSteamNetConnection_Invalid) {
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

		void OnConnectionStatusChanged(const SteamNetConnectionStatusChangedCallback_t& a_info)
		{
			if (a_info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
				const std::scoped_lock lock(mutex_);
				if (connection_ == a_info.m_hConn && !connected_) {
					connected_ = true;
					events_.push_back(QueuedEvent{
						MakeEvent(CO_GNS_EVENT_CONNECTED, a_info.m_hConn, 0, 0, nullptr),
						{}
					});
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
				QueuedEvent queued{};
				if (size > kMaximumMessageBytes) {
					const auto boundedSize = (std::min)(
						size,
						static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()));
					queued.metadata = MakeEvent(
						CO_GNS_EVENT_OVERSIZE_MESSAGE,
						connection,
						0,
						static_cast<std::uint32_t>(boundedSize),
						"Inbound GNS message exceeded 64 KiB");
				} else {
					queued.metadata = MakeEvent(
						CO_GNS_EVENT_MESSAGE,
						connection,
						0,
						static_cast<std::uint32_t>(size),
						nullptr);
					if (size > 0 && message->m_pData) {
						const auto* begin = static_cast<const std::uint8_t*>(message->m_pData);
						queued.payload.assign(begin, begin + size);
					}
				}

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

		mutable std::mutex mutex_;
		ISteamNetworkingSockets* networking_{ nullptr };
		HSteamNetConnection connection_{ k_HSteamNetConnection_Invalid };
		std::deque<QueuedEvent> events_;
		bool connected_{ false };
		bool runtimeHeld_{ false };
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
}
