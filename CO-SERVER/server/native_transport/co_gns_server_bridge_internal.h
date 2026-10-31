#pragma once

#include "co_gns_server_bridge.h"

#include <steam/steamnetworkingsockets.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace co_gns::detail
{
    constexpr std::size_t kMaximumMessageBytes = 64uz * 1024uz;
    constexpr std::size_t kReceiveBatchSize = 64;
    inline std::mutex g_runtimeMutex;
    inline std::size_t g_runtimeReferenceCount = 0;
    class ServerBridge;
    inline std::mutex g_ownerMutex;
    inline std::unordered_map<HSteamListenSocket, ServerBridge*> g_listenOwners;
    inline std::unordered_map<HSteamNetConnection, ServerBridge*> g_connectionOwners;
    inline bool AcquireRuntime(std::string& error)
    {
        const std::scoped_lock lock(g_runtimeMutex);
        if (g_runtimeReferenceCount > 0) {
            ++g_runtimeReferenceCount;
            return true;
        }

        SteamNetworkingErrMsg errorMessage{};
        if (!GameNetworkingSockets_Init(nullptr, errorMessage)) {
            error = errorMessage[0] ? std::string{ errorMessage } : "GameNetworkingSockets_Init failed.";
            return false;
        }
        g_runtimeReferenceCount = 1;
        return true;
    }

    // "" and "0.0.0.0" listen on IPv4 only; "::" listens on IPv6 and, where the OS allows, IPv4 too.
    inline bool ParseHostAddress(std::string a_host, std::uint16_t a_port, SteamNetworkingIPAddr& a_out)
    {
        a_out.Clear();
        if (a_host.empty() || a_host == "0.0.0.0") {
            a_out.SetIPv4(0U, a_port);
            return true;
        }
        if (a_host.size() > 2 && a_host.front() == '[' && a_host.back() == ']') {
            a_host = a_host.substr(1, a_host.size() - 2);
        }
        if (!a_out.ParseString(a_host.c_str())) {
            return false;
        }
        a_out.m_port = a_port;
        return true;
    }

    inline bool FormatAddressText(const SteamNetworkingIPAddr& a_address, char* a_out, std::size_t a_size)
    {
        if (a_out == nullptr || a_size == 0) {
            return false;
        }
        std::array<char, SteamNetworkingIPAddr::k_cchMaxString> text{};
        if (a_address.IsIPv4()) {
            const auto ipv4 = a_address.GetIPv4();
            std::snprintf(text.data(), text.size(), "%u.%u.%u.%u",
                (ipv4 >> 24) & 0xFFU, (ipv4 >> 16) & 0xFFU, (ipv4 >> 8) & 0xFFU, ipv4 & 0xFFU);
        } else {
            a_address.ToString(text.data(), text.size(), false);
        }
        const auto length = std::strlen(text.data());
        if (length == 0 || length >= a_size) {
            return false;
        }
        std::memcpy(a_out, text.data(), length + 1);
        return true;
    }

    inline void ReleaseRuntime()
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

    struct QueuedEvent
    {
        co_gns_event metadata{};
        std::vector<std::uint8_t> payload;
    };

    class ServerBridge
    {
    public:
        ~ServerBridge()
        {
            Stop();
        }

        bool Start(const char* bindHost, std::uint16_t port, std::string& error)
        {
            if (port == 0) {
                error = "GNS listen port must be nonzero; GameNetworkingSockets does not support ephemeral port 0.";
                return false;
            }
            if (!AcquireRuntime(error)) {
                return false;
            }
            runtimeHeld_ = true;
            networking_ = SteamNetworkingSockets();
            if (networking_ == nullptr) {
                error = "SteamNetworkingSockets returned no server interface.";
                Stop();
                return false;
            }

            pollGroup_ = networking_->CreatePollGroup();
            if (pollGroup_ == k_HSteamNetPollGroup_Invalid) {
                error = "GameNetworkingSockets failed to create a server poll group.";
                Stop();
                return false;
            }

            SteamNetworkingIPAddr address{};
            const std::string host = bindHost != nullptr ? std::string{ bindHost } : std::string{};
            if (!ParseHostAddress(host, port, address)) {
                error = "GNS bind address must be a valid IPv4 or IPv6 address.";
                Stop();
                return false;
            }

            SteamNetworkingConfigValue_t option{};
            option.SetPtr(
                k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                reinterpret_cast<void*>(+ConnectionStatusChanged));
            listenSocket_ = networking_->CreateListenSocketIP(address, 1, std::addressof(option));
            if (listenSocket_ == k_HSteamListenSocket_Invalid) {
                const auto displayHost = host.empty() ? std::string{ "0.0.0.0" } : host;
                error = "GameNetworkingSockets failed to create a listen socket on " +
                        displayHost + ":" + std::to_string(port) + ".";
                Stop();
                return false;
            }

            {
                const std::scoped_lock ownerLock(g_ownerMutex);
                g_listenOwners[listenSocket_] = this;
            }

            SteamNetworkingIPAddr actualAddress{};
            if (!networking_->GetListenSocketAddress(listenSocket_, std::addressof(actualAddress))) {
                error = "GameNetworkingSockets could not report the bound listen address.";
                Stop();
                return false;
            }
            localPort_ = actualAddress.m_port;
            return true;
        }

        void Stop()
        {
            ISteamNetworkingSockets* networking = networking_;
            HSteamListenSocket listenSocket = listenSocket_;
            HSteamNetPollGroup pollGroup = pollGroup_;
            std::vector<HSteamNetConnection> connections;
            {
                const std::scoped_lock lock(mutex_);
                connections.assign(connections_.begin(), connections_.end());
                connections_.clear();
                connectedConnections_.clear();
                events_.clear();
                listenSocket_ = k_HSteamListenSocket_Invalid;
                pollGroup_ = k_HSteamNetPollGroup_Invalid;
                networking_ = nullptr;
                localPort_ = 0;
            }

            {
                const std::scoped_lock ownerLock(g_ownerMutex);
                if (listenSocket != k_HSteamListenSocket_Invalid) {
                    g_listenOwners.erase(listenSocket);
                }
                for (const auto connection : connections) {
                    g_connectionOwners.erase(connection);
                }
            }

            if (networking != nullptr) {
                for (const auto connection : connections) {
                    networking->CloseConnection(connection, 0, nullptr, false);
                }
                if (listenSocket != k_HSteamListenSocket_Invalid) {
                    networking->CloseListenSocket(listenSocket);
                }
                if (pollGroup != k_HSteamNetPollGroup_Invalid) {
                    networking->DestroyPollGroup(pollGroup);
                }
            }

            if (runtimeHeld_) {
                runtimeHeld_ = false;
                ReleaseRuntime();
            }
        }

        std::uint16_t LocalPort() const
        {
            const std::scoped_lock lock(mutex_);
            return localPort_;
        }

        std::uint32_t ConnectionCount() const
        {
            const std::scoped_lock lock(mutex_);
            return static_cast<std::uint32_t>(connectedConnections_.size());
        }

        void SetAdmissionCallback(co_gns_admission_callback a_callback, void* a_user)
        {
            const std::scoped_lock lock(mutex_);
            admission_ = a_callback;
            admissionUser_ = a_user;
        }

        void SetAddressAdmissionCallback(co_gns_address_admission_callback a_callback, void* a_user)
        {
            const std::scoped_lock lock(mutex_);
            addressAdmission_ = a_callback;
            addressAdmissionUser_ = a_user;
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

            bool queueIsEmpty = false;
            {
                const std::scoped_lock lock(mutex_);
                queueIsEmpty = events_.empty();
            }
            if (queueIsEmpty) {
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

        int Send(
            std::uint32_t connectionId,
            const void* payload,
            std::uint32_t payloadSize,
            std::uint32_t delivery)
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

            const auto connection = static_cast<HSteamNetConnection>(connectionId);
            ISteamNetworkingSockets* networking = nullptr;
            {
                const std::scoped_lock lock(mutex_);
                if (!connectedConnections_.contains(connection)) {
                    return CO_GNS_SEND_NOT_CONNECTED;
                }
                networking = networking_;
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

        int ConnectionStatus(std::uint32_t connectionId, co_gns_connection_status& a_status)
        {
            const auto connection = static_cast<HSteamNetConnection>(connectionId);
            ISteamNetworkingSockets* networking = nullptr;
            {
                const std::scoped_lock lock(mutex_);
                if (!connectedConnections_.contains(connection)) {
                    return 0;
                }
                networking = networking_;
            }
            if (networking == nullptr) {
                return 0;
            }

            SteamNetConnectionRealTimeStatus_t status{};
            if (networking->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK) {
                return 0;
            }
            const auto quality = (std::max)(0.0f, (std::min)(1.0f, status.m_flConnectionQualityRemote));
            a_status.ping_ms = status.m_nPing;
            a_status.loss_penalty_percent = (1.0f - quality) * 100.0f;
            a_status.pending_send_bytes = static_cast<std::uint32_t>(
                (std::max)(0, status.m_cbPendingUnreliable + status.m_cbPendingReliable));
            return 1;
        }

        int Disconnect(std::uint32_t connectionId, std::int32_t reason, const char* debug)
        {
            const auto connection = static_cast<HSteamNetConnection>(connectionId);
            ISteamNetworkingSockets* networking = nullptr;
            {
                const std::scoped_lock lock(mutex_);
                if (!connections_.contains(connection)) {
                    return 0;
                }
                connections_.erase(connection);
                connectedConnections_.erase(connection);
                networking = networking_;
            }
            {
                const std::scoped_lock ownerLock(g_ownerMutex);
                g_connectionOwners.erase(connection);
            }
            if (networking == nullptr) {
                return 0;
            }
            return networking->CloseConnection(connection, reason, debug, true) ? 1 : 0;
        }

    private:
        static ServerBridge* FindOwner(const SteamNetConnectionStatusChangedCallback_t& info)
        {
            const std::scoped_lock ownerLock(g_ownerMutex);
            const auto connectionOwner = g_connectionOwners.find(info.m_hConn);
            if (connectionOwner != g_connectionOwners.end()) {
                return connectionOwner->second;
            }
            const auto listenOwner = g_listenOwners.find(info.m_info.m_hListenSocket);
            return listenOwner == g_listenOwners.end() ? nullptr : listenOwner->second;
        }

        static void ConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
        {
            if (info == nullptr) {
                return;
            }
            if (auto* owner = FindOwner(*info); owner != nullptr) {
                owner->OnConnectionStatusChanged(*info);
            }
        }

        bool Admits(
            const SteamNetConnectionInfo_t& a_info,
            char* a_reason,
            std::size_t a_reasonSize,
            bool& a_applicationAdmitted) const
        {
            a_applicationAdmitted = false;
            co_gns_admission_callback callback = nullptr;
            void* user = nullptr;
            co_gns_address_admission_callback addressCallback = nullptr;
            void* addressUser = nullptr;
            {
                const std::scoped_lock lock(mutex_);
                callback = admission_;
                user = admissionUser_;
                addressCallback = addressAdmission_;
                addressUser = addressAdmissionUser_;
            }
            if (!a_info.m_addrRemote.IsIPv4()) {
                if (addressCallback == nullptr) {
                    if (callback == nullptr) {
                        return true;
                    }
                    std::snprintf(a_reason, a_reasonSize, "%s", "This server does not admit IPv6 connections.");
                    return false;
                }
                std::array<char, SteamNetworkingIPAddr::k_cchMaxString> address{};
                if (!FormatAddressText(a_info.m_addrRemote, address.data(), address.size())) {
                    std::snprintf(a_reason, a_reasonSize, "%s", "Unreadable peer address.");
                    return false;
                }
                const bool admitted = addressCallback(
                    addressUser,
                    address.data(),
                    a_info.m_addrRemote.m_port,
                    a_reason,
                    a_reasonSize) != 0;
                a_applicationAdmitted = admitted;
                return admitted;
            }
            if (callback == nullptr) {
                return true;
            }
            const bool admitted = callback(
                user,
                a_info.m_addrRemote.GetIPv4(),
                a_info.m_addrRemote.m_port,
                a_reason,
                a_reasonSize) != 0;
            a_applicationAdmitted = admitted;
            return admitted;
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
            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting &&
                info.m_info.m_hListenSocket == listenSocket_) {
                // Application admission runs before the transport accepts, so a
                // banned or rate-limited peer never gets a live GNS session.
                char refusal[128] = "Refused by server admission";
                bool applicationAdmitted = false;
                if (!Admits(info.m_info, refusal, sizeof(refusal), applicationAdmitted)) {
                    networking_->CloseConnection(info.m_hConn, 0, refusal, false);
                    return;
                }
                if (networking_->AcceptConnection(info.m_hConn) != k_EResultOK ||
                    !networking_->SetConnectionPollGroup(info.m_hConn, pollGroup_)) {
                    networking_->CloseConnection(info.m_hConn, 0, "Could not admit GNS connection", false);
                    if (applicationAdmitted) {
                        const std::scoped_lock lock(mutex_);
                        events_.push_back(QueuedEvent{
                            MakeEvent(
                                CO_GNS_EVENT_DISCONNECTED,
                                info.m_hConn,
                                0,
                                0,
                                "Transport admission failed after application reservation"),
                            {}
                        });
                    }
                    return;
                }
                {
                    const std::scoped_lock lock(mutex_);
                    connections_.insert(info.m_hConn);
                }
                {
                    const std::scoped_lock ownerLock(g_ownerMutex);
                    g_connectionOwners[info.m_hConn] = this;
                }
                return;
            }

            if (info.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
                const std::scoped_lock lock(mutex_);
                if (connections_.contains(info.m_hConn) && connectedConnections_.insert(info.m_hConn).second) {
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

            bool knownConnection = false;
            {
                const std::scoped_lock lock(mutex_);
                knownConnection = connections_.erase(info.m_hConn) > 0;
                connectedConnections_.erase(info.m_hConn);
                if (knownConnection) {
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
                g_connectionOwners.erase(info.m_hConn);
            }
            networking_->CloseConnection(info.m_hConn, 0, nullptr, false);
        }

        void PumpMessages()
        {
            ISteamNetworkingSockets* networking = nullptr;
            HSteamNetPollGroup pollGroup = k_HSteamNetPollGroup_Invalid;
            {
                const std::scoped_lock lock(mutex_);
                networking = networking_;
                pollGroup = pollGroup_;
            }
            if (networking == nullptr || pollGroup == k_HSteamNetPollGroup_Invalid) {
                return;
            }

            std::array<SteamNetworkingMessage_t*, kReceiveBatchSize> messages{};
            const auto count = networking->ReceiveMessagesOnPollGroup(
                pollGroup,
                messages.data(),
                static_cast<int>(messages.size()));
            if (count <= 0) {
                return;
            }

            for (int index = 0; index < count; ++index) {
                auto* message = messages[static_cast<std::size_t>(index)];
                if (message == nullptr) {
                    continue;
                }
                const auto size = message->m_cbSize > 0 ? static_cast<std::size_t>(message->m_cbSize) : 0uz;
                const auto connection = message->m_conn;
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
                    if (connections_.contains(connection)) {
                        events_.push_back(std::move(queued));
                    }
                }
                message->Release();
            }
        }

        mutable std::mutex mutex_;
        ISteamNetworkingSockets* networking_{ nullptr };
        co_gns_admission_callback admission_{ nullptr };
        void* admissionUser_{ nullptr };
        co_gns_address_admission_callback addressAdmission_{ nullptr };
        void* addressAdmissionUser_{ nullptr };
        HSteamListenSocket listenSocket_{ k_HSteamListenSocket_Invalid };
        HSteamNetPollGroup pollGroup_{ k_HSteamNetPollGroup_Invalid };
        std::unordered_set<HSteamNetConnection> connections_;
        std::unordered_set<HSteamNetConnection> connectedConnections_;
        std::deque<QueuedEvent> events_;
        std::uint16_t localPort_{ 0 };
        bool runtimeHeld_{ false };

    public:
        ISteamNetworkingSockets* Networking() const
        {
            const std::scoped_lock lock(mutex_);
            return networking_;
        }

        bool OwnsConnection(HSteamNetConnection connection) const
        {
            const std::scoped_lock lock(mutex_);
            return connections_.contains(connection);
        }
    };
}
