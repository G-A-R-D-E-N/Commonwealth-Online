#include "co_gns_server_bridge_internal.h"

#include <steam/steamnetworkingsockets.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace co_gns::detail;

namespace
{
    class ClientBridge;

    std::unordered_map<HSteamNetConnection, ClientBridge*> g_clientOwners;

    void WriteError(char* buffer, std::size_t bufferSize, const std::string& message)
    {
        if (buffer == nullptr || bufferSize == 0) {
            return;
        }
        std::snprintf(buffer, bufferSize, "%s", message.c_str());
    }

    class ClientBridge
    {
    public:
        ~ClientBridge()
        {
            Stop();
        }

        bool Start(const char* remoteHost, std::uint16_t port, std::string& error)
        {
            if (remoteHost == nullptr || remoteHost[0] == '\0' || port == 0) {
                error = "GNS remote address and port are required.";
                return false;
            }
            if (!AcquireRuntime(error)) {
                return false;
            }
            runtimeHeld_ = true;
            networking_ = SteamNetworkingSockets();
            if (networking_ == nullptr) {
                error = "SteamNetworkingSockets returned no client interface.";
                Stop();
                return false;
            }

            SteamNetworkingIPAddr address{};
            if (!ParseHostAddress(remoteHost, port, address) || address.IsIPv6AllZeros() ||
                (address.IsIPv4() && address.GetIPv4() == 0U)) {
                error = "GNS remote address must be a valid IPv4 or IPv6 address.";
                Stop();
                return false;
            }

            SteamNetworkingConfigValue_t option{};
            option.SetPtr(
                k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                reinterpret_cast<void*>(+ConnectionStatusChanged));
            connection_ = networking_->ConnectByIPAddress(address, 1, std::addressof(option));
            if (connection_ == k_HSteamNetConnection_Invalid) {
                error = "GameNetworkingSockets failed to create a client connection to " +
                        std::string{ remoteHost } + ":" + std::to_string(port) + ".";
                Stop();
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
            if (networking != nullptr && connection != k_HSteamNetConnection_Invalid) {
                networking->CloseConnection(connection, 0, nullptr, false);
            }
            if (runtimeHeld_) {
                runtimeHeld_ = false;
                ReleaseRuntime();
            }
        }

        int Poll(co_gns_event& outEvent, void* payloadBuffer, std::uint32_t payloadCapacity)
        {
            ISteamNetworkingSockets* networking = nullptr;
            {
                const std::scoped_lock lock(mutex_);
                networking = networking_;
            }
            if (networking == nullptr) {
                return -1;
            }

            networking->RunCallbacks();

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
                std::memset(std::addressof(outEvent), 0, sizeof(outEvent));
                return 0;
            }
            const auto& next = events_.front();
            outEvent = next.metadata;
            // Metadata (including payload_size) is already copied out, so drop the
            // undeliverable event and advance before returning -2. Otherwise a
            // caller that does not treat -2 as fatal would re-read the same stuck
            // event on every poll and never make progress.
            if (outEvent.type == CO_GNS_EVENT_MESSAGE && next.payload.size() > payloadCapacity) {
                events_.pop_front();
                return -2;
            }
            if (outEvent.type == CO_GNS_EVENT_MESSAGE && !next.payload.empty()) {
                if (payloadBuffer == nullptr) {
                    events_.pop_front();
                    return -2;
                }
                std::memcpy(payloadBuffer, next.payload.data(), next.payload.size());
            }
            events_.pop_front();
            return 1;
        }

        int Send(const void* payload, std::uint32_t payloadSize, std::uint32_t delivery)
        {
            if (payload == nullptr || payloadSize == 0) {
                return CO_GNS_SEND_ERROR;
            }
            if (payloadSize > kMaximumMessageBytes) {
                return CO_GNS_SEND_TOO_LARGE;
            }
            if (delivery != CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED && delivery != CO_GNS_DELIVERY_RELIABLE_ORDERED) {
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
            if (networking == nullptr) {
                return CO_GNS_SEND_NOT_CONNECTED;
            }

            const int flags = delivery == CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED ?
                              k_nSteamNetworkingSend_UnreliableNoDelay :
                              k_nSteamNetworkingSend_ReliableNoNagle;
            const auto result = networking->SendMessageToConnection(
                connection,
                payload,
                payloadSize,
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

        int Disconnect(std::int32_t reason, const char* debug)
        {
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
            if (networking == nullptr) {
                return 0;
            }
            return networking->CloseConnection(connection, reason, debug, false) ? 1 : 0;
        }

    private:
        static ClientBridge* FindOwner(HSteamNetConnection connection)
        {
            const std::scoped_lock ownerLock(g_ownerMutex);
            const auto owner = g_clientOwners.find(connection);
            return owner == g_clientOwners.end() ? nullptr : owner->second;
        }

        static void ConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
        {
            if (info == nullptr) {
                return;
            }
            if (auto* owner = FindOwner(info->m_hConn); owner != nullptr) {
                owner->OnConnectionStatusChanged(*info);
            }
        }

        static co_gns_event MakeEvent(
            std::uint32_t type,
            HSteamNetConnection connection,
            std::int32_t reason,
            std::uint32_t payloadSize,
            const char* debug)
        {
            co_gns_event event{};
            event.type = type;
            event.connection_id = static_cast<std::uint32_t>(connection);
            event.reason = reason;
            event.payload_size = payloadSize;
            if (debug != nullptr && debug[0] != '\0') {
                std::snprintf(event.debug, sizeof(event.debug), "%s", debug);
            }
            return event;
        }

        void OnConnectionStatusChanged(const SteamNetConnectionStatusChangedCallback_t& info)
        {
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
                const std::scoped_lock lock(mutex_);
                if (connection_ == info.m_hConn && !connected_) {
                    connected_ = true;
                    events_.push_back(QueuedEvent{
                        MakeEvent(CO_GNS_EVENT_CONNECTED, info.m_hConn, 0, 0, nullptr),
                        {}
                    });
                }
                return;
            }

            if (info.m_info.m_eState != k_ESteamNetworkingConnectionState_ClosedByPeer &&
                info.m_info.m_eState != k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
                return;
            }

            while (networking_ != nullptr && ReceiveBatch(networking_, info.m_hConn) > 0) {}

            bool knownConnection = false;
            {
                const std::scoped_lock lock(mutex_);
                if (connection_ == info.m_hConn) {
                    knownConnection = true;
                    connection_ = k_HSteamNetConnection_Invalid;
                    connected_ = false;
                    events_.push_back(QueuedEvent{
                        MakeEvent(
                            CO_GNS_EVENT_DISCONNECTED,
                            info.m_hConn,
                            info.m_info.m_eEndReason,
                            0,
                            info.m_info.m_szEndDebug),
                        {}
                    });
                }
            }
            if (!knownConnection) {
                return;
            }
            {
                const std::scoped_lock ownerLock(g_ownerMutex);
                g_clientOwners.erase(info.m_hConn);
            }
            networking_->CloseConnection(info.m_hConn, 0, nullptr, false);
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
            if (networking == nullptr || connection == k_HSteamNetConnection_Invalid) {
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
                if (message == nullptr) {
                    continue;
                }
                const auto size = message->m_cbSize > 0 ? static_cast<std::size_t>(message->m_cbSize) : 0uz;
                QueuedEvent queued{};
                if (size > kMaximumMessageBytes) {
                    queued.metadata = MakeEvent(
                        CO_GNS_EVENT_OVERSIZE_MESSAGE,
                        connection,
                        0,
                        static_cast<std::uint32_t>((std::min)(size, static_cast<std::size_t>(UINT32_MAX))),
                        "Inbound GNS message exceeded 64 KiB");
                } else {
                    queued.metadata = MakeEvent(
                        CO_GNS_EVENT_MESSAGE,
                        connection,
                        0,
                        static_cast<std::uint32_t>(size),
                        nullptr);
                    if (size > 0 && message->m_pData != nullptr) {
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

        mutable std::mutex mutex_;
        ISteamNetworkingSockets* networking_{ nullptr };
        HSteamNetConnection connection_{ k_HSteamNetConnection_Invalid };
        std::deque<QueuedEvent> events_;
        bool connected_{ false };
        bool runtimeHeld_{ false };
    };
}

extern "C"
{
    int co_gns_server_create(
        const char* bind_host,
        uint16_t port,
        co_gns_server_handle* out_handle,
        char* error_buffer,
        size_t error_buffer_size)
    {
        if (out_handle == nullptr) {
            WriteError(error_buffer, error_buffer_size, "out_handle is required.");
            return 0;
        }
        *out_handle = nullptr;
        auto* server = new ServerBridge();
        std::string error;
        if (!server->Start(bind_host, port, error)) {
            delete server;
            WriteError(error_buffer, error_buffer_size, error);
            return 0;
        }
        *out_handle = server;
        WriteError(error_buffer, error_buffer_size, "");
        return 1;
    }

    void co_gns_server_destroy(co_gns_server_handle handle)
    {
        delete static_cast<ServerBridge*>(handle);
    }

    uint16_t co_gns_server_local_port(co_gns_server_handle handle)
    {
        const auto* server = static_cast<ServerBridge*>(handle);
        return server == nullptr ? 0 : server->LocalPort();
    }

    uint32_t co_gns_server_connection_count(co_gns_server_handle handle)
    {
        const auto* server = static_cast<ServerBridge*>(handle);
        return server == nullptr ? 0 : server->ConnectionCount();
    }

    int co_gns_server_poll(
        co_gns_server_handle handle,
        co_gns_event* out_event,
        void* payload_buffer,
        uint32_t payload_capacity)
    {
        auto* server = static_cast<ServerBridge*>(handle);
        if (server == nullptr || out_event == nullptr) {
            return -1;
        }
        return server->Poll(*out_event, payload_buffer, payload_capacity);
    }

    int co_gns_server_send(
        co_gns_server_handle handle,
        uint32_t connection_id,
        const void* payload,
        uint32_t payload_size,
        uint32_t delivery)
    {
        auto* server = static_cast<ServerBridge*>(handle);
        return server == nullptr ? CO_GNS_SEND_ERROR :
               server->Send(connection_id, payload, payload_size, delivery);
    }

    int co_gns_server_set_admission_callback(
        co_gns_server_handle handle,
        co_gns_admission_callback callback,
        void* user)
    {
        auto* server = static_cast<ServerBridge*>(handle);
        if (server == nullptr) {
            return 0;
        }
        server->SetAdmissionCallback(callback, user);
        return 1;
    }

    int co_gns_server_set_address_admission_callback(
        co_gns_server_handle handle,
        co_gns_address_admission_callback callback,
        void* user)
    {
        auto* server = static_cast<ServerBridge*>(handle);
        if (server == nullptr) {
            return 0;
        }
        server->SetAddressAdmissionCallback(callback, user);
        return 1;
    }

    int co_gns_server_disconnect(
        co_gns_server_handle handle,
        uint32_t connection_id,
        int32_t reason,
        const char* debug)
    {
        auto* server = static_cast<ServerBridge*>(handle);
        return server == nullptr ? 0 : server->Disconnect(connection_id, reason, debug);
    }

    int co_gns_client_create(
        const char* remote_host,
        uint16_t port,
        co_gns_client_handle* out_handle,
        char* error_buffer,
        size_t error_buffer_size)
    {
        if (out_handle == nullptr) {
            WriteError(error_buffer, error_buffer_size, "out_handle is required.");
            return 0;
        }
        *out_handle = nullptr;
        auto* client = new ClientBridge();
        std::string error;
        if (!client->Start(remote_host, port, error)) {
            delete client;
            WriteError(error_buffer, error_buffer_size, error);
            return 0;
        }
        *out_handle = client;
        WriteError(error_buffer, error_buffer_size, "");
        return 1;
    }

    void co_gns_client_destroy(co_gns_client_handle handle)
    {
        delete static_cast<ClientBridge*>(handle);
    }

    int co_gns_client_poll(
        co_gns_client_handle handle,
        co_gns_event* out_event,
        void* payload_buffer,
        uint32_t payload_capacity)
    {
        auto* client = static_cast<ClientBridge*>(handle);
        if (client == nullptr || out_event == nullptr) {
            return -1;
        }
        return client->Poll(*out_event, payload_buffer, payload_capacity);
    }

    int co_gns_client_send(
        co_gns_client_handle handle,
        const void* payload,
        uint32_t payload_size,
        uint32_t delivery)
    {
        auto* client = static_cast<ClientBridge*>(handle);
        return client == nullptr ? CO_GNS_SEND_ERROR : client->Send(payload, payload_size, delivery);
    }

    int co_gns_client_disconnect(
        co_gns_client_handle handle,
        int32_t reason,
        const char* debug)
    {
        auto* client = static_cast<ClientBridge*>(handle);
        return client == nullptr ? 0 : client->Disconnect(reason, debug);
    }
}
