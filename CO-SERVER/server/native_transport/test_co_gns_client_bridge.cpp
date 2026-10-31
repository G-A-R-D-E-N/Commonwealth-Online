#include "co_gns_server_bridge.h"
#include "test_udp_port.h"

#include <steam/steamnetworkingsockets.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    HSteamListenSocket g_rawListenSocket = k_HSteamListenSocket_Invalid;
    HSteamNetConnection g_rawServerConnection = k_HSteamNetConnection_Invalid;
    bool g_rawServerConnected = false;

    struct PolledEvent
    {
        co_gns_event event{};
        std::string payload;
    };

    struct RawMessage
    {
        std::string payload;
        int flags{};
    };

    bool LiveGnsRequired()
    {
        const auto* value = std::getenv("CO_REQUIRE_LIVE_GNS");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }

    void RawServerConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
    {
        if (info == nullptr || info->m_info.m_hListenSocket != g_rawListenSocket) {
            return;
        }
        auto* networking = SteamNetworkingSockets();
        assert(networking != nullptr);
        if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting) {
            assert(networking->AcceptConnection(info->m_hConn) == k_EResultOK);
            return;
        }
        if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
            g_rawServerConnection = info->m_hConn;
            g_rawServerConnected = true;
            return;
        }
        if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
            info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            if (g_rawServerConnection == info->m_hConn) {
                g_rawServerConnection = k_HSteamNetConnection_Invalid;
                g_rawServerConnected = false;
            }
            networking->CloseConnection(info->m_hConn, 0, nullptr, false);
        }
    }

    std::optional<PolledEvent> PollServer(co_gns_server_handle server)
    {
        std::array<char, 64 * 1024> payload{};
        co_gns_event event{};
        const auto result = co_gns_server_poll(server, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
        assert(result >= 0);
        if (result == 0) return std::nullopt;
        PolledEvent out{};
        out.event = event;
        if (event.type == CO_GNS_EVENT_MESSAGE) out.payload.assign(payload.data(), event.payload_size);
        return out;
    }

    std::optional<PolledEvent> PollClient(co_gns_client_handle client)
    {
        std::array<char, 64 * 1024> payload{};
        co_gns_event event{};
        const auto result = co_gns_client_poll(client, &event, payload.data(), static_cast<std::uint32_t>(payload.size()));
        assert(result >= 0);
        if (result == 0) return std::nullopt;
        PolledEvent out{};
        out.event = event;
        if (event.type == CO_GNS_EVENT_MESSAGE) out.payload.assign(payload.data(), event.payload_size);
        return out;
    }

    bool WaitConnected(co_gns_server_handle server, co_gns_client_handle client, std::uint32_t& serverConnectionId)
    {
        bool serverConnected = false;
        bool clientConnected = false;
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto event = PollServer(server); event && event->event.type == CO_GNS_EVENT_CONNECTED) {
                serverConnected = true;
                serverConnectionId = event->event.connection_id;
            }
            if (auto event = PollClient(client); event && event->event.type == CO_GNS_EVENT_CONNECTED) {
                clientConnected = true;
            }
            if (serverConnected && clientConnected) return true;
            std::this_thread::sleep_for(2ms);
        }
        return false;
    }

    bool WaitRawConnected(co_gns_client_handle client)
    {
        bool clientConnected = false;
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto event = PollClient(client); event && event->event.type == CO_GNS_EVENT_CONNECTED) {
                clientConnected = true;
            }
            if (clientConnected && g_rawServerConnected && g_rawServerConnection != k_HSteamNetConnection_Invalid) return true;
            std::this_thread::sleep_for(2ms);
        }
        return false;
    }

    std::optional<PolledEvent> WaitServerEvent(
        co_gns_server_handle server,
        co_gns_client_handle client,
        std::uint32_t type)
    {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto event = PollServer(server); event && event->event.type == type) return event;
            PollClient(client);
            std::this_thread::sleep_for(2ms);
        }
        return std::nullopt;
    }

    std::optional<PolledEvent> WaitClientEvent(
        co_gns_server_handle server,
        co_gns_client_handle client,
        std::uint32_t type)
    {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            PollServer(server);
            if (auto event = PollClient(client); event && event->event.type == type) return event;
            std::this_thread::sleep_for(2ms);
        }
        return std::nullopt;
    }

    std::optional<RawMessage> WaitRawServerMessage(co_gns_client_handle client)
    {
        auto* networking = SteamNetworkingSockets();
        assert(networking != nullptr);
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            PollClient(client);
            if (g_rawServerConnection != k_HSteamNetConnection_Invalid) {
                ISteamNetworkingMessage* message = nullptr;
                const auto count = networking->ReceiveMessagesOnConnection(g_rawServerConnection, &message, 1);
                assert(count >= 0);
                if (count > 0 && message != nullptr) {
                    RawMessage result{};
                    result.flags = message->m_nFlags;
                    if (message->m_cbSize > 0 && message->m_pData != nullptr) {
                        result.payload.assign(
                            static_cast<const char*>(message->m_pData),
                            static_cast<std::size_t>(message->m_cbSize));
                    }
                    message->Release();
                    return result;
                }
            }
            std::this_thread::sleep_for(2ms);
        }
        return std::nullopt;
    }
}

int main()
{
    co_gns_server_handle server = nullptr;
    std::array<char, 256> error{};
    const auto requestedPort = FindAvailableUdpPort();
    if (co_gns_server_create("127.0.0.1", requestedPort, &server, error.data(), error.size()) != 1) {
        std::fprintf(stderr, "%s: environment cannot create a GNS listen socket: %s\n", LiveGnsRequired() ? "FAIL" : "SKIP", error.data());
        return LiveGnsRequired() ? 1 : 0;
    }

    const auto port = co_gns_server_local_port(server);
    assert(port != 0);
    assert(port == requestedPort);

    co_gns_client_handle client = nullptr;
    error.fill(0);
    assert(co_gns_client_create("127.0.0.1", port, &client, error.data(), error.size()) == 1);
    assert(client != nullptr);

    std::uint32_t serverConnectionId = 0;
    assert(WaitConnected(server, client, serverConnectionId));
    assert(serverConnectionId != 0);
    assert(co_gns_server_connection_count(server) == 1);
    assert(co_gns_test_set_network_conditions(0.0f, 0.0f) == 1);
    assert(co_gns_test_set_network_conditions(-1.0f, 0.0f) == 0);
    assert(co_gns_test_set_network_conditions(0.0f, 101.0f) == 0);

    float packetLoss = -1.0f;
    float packetReorder = -1.0f;
    assert(co_gns_test_get_network_conditions(&packetLoss, &packetReorder) == 1);
    assert(packetLoss == 0.0f);
    assert(packetReorder == 0.0f);
    assert(co_gns_test_set_network_conditions(25.0f, 50.0f) == 1);
    assert(co_gns_test_get_network_conditions(&packetLoss, &packetReorder) == 1);
    assert(packetLoss == 25.0f);
    assert(packetReorder == 50.0f);
    assert(co_gns_test_set_network_conditions(0.0f, 0.0f) == 1);

    const std::string reliable = R"({"type":"hello","protocolVersion":2})";
    assert(co_gns_client_send(
        client,
        reliable.data(),
        static_cast<std::uint32_t>(reliable.size()),
        CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT);
    const auto serverMessage = WaitServerEvent(server, client, CO_GNS_EVENT_MESSAGE);
    assert(serverMessage.has_value());
    assert(serverMessage->event.connection_id == serverConnectionId);
    assert(serverMessage->payload == reliable);

    const std::string snapshot = R"({"type":"transform","x":1})";
    assert(co_gns_server_send(
        server,
        serverConnectionId,
        snapshot.data(),
        static_cast<std::uint32_t>(snapshot.size()),
        CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED) == CO_GNS_SEND_SENT);
    const auto clientMessage = WaitClientEvent(server, client, CO_GNS_EVENT_MESSAGE);
    assert(clientMessage.has_value());
    assert(clientMessage->payload == snapshot);

    const std::string tooLarge((64 * 1024) + 1, 'x');
    assert(co_gns_client_send(
        client,
        tooLarge.data(),
        static_cast<std::uint32_t>(tooLarge.size()),
        CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_TOO_LARGE);

    assert(co_gns_client_disconnect(client, 1000, "reconnect") == 1);
    const auto disconnected = WaitServerEvent(server, client, CO_GNS_EVENT_DISCONNECTED);
    assert(disconnected.has_value());
    assert(disconnected->event.connection_id == serverConnectionId);
    co_gns_client_destroy(client);

    client = nullptr;
    error.fill(0);
    assert(co_gns_client_create("127.0.0.1", port, &client, error.data(), error.size()) == 1);
    serverConnectionId = 0;
    assert(WaitConnected(server, client, serverConnectionId));
    assert(co_gns_server_disconnect(server, serverConnectionId, 1000, "done") == 1);
    const auto clientDisconnected = WaitClientEvent(server, client, CO_GNS_EVENT_DISCONNECTED);
    assert(clientDisconnected.has_value());
    co_gns_client_destroy(client);
    client = nullptr;

    auto* networking = SteamNetworkingSockets();
    assert(networking != nullptr);
    const auto rawPort = FindAvailableUdpPort();
    SteamNetworkingIPAddr rawAddress{};
    rawAddress.Clear();
    rawAddress.SetIPv4(0x7f000001U, rawPort);
    SteamNetworkingConfigValue_t rawOption{};
    rawOption.SetPtr(
        k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        reinterpret_cast<void*>(+RawServerConnectionStatusChanged));
    g_rawListenSocket = networking->CreateListenSocketIP(rawAddress, 1, &rawOption);
    assert(g_rawListenSocket != k_HSteamListenSocket_Invalid);
    SteamNetworkingIPAddr actualRawAddress{};
    assert(networking->GetListenSocketAddress(g_rawListenSocket, &actualRawAddress));
    assert(actualRawAddress.m_port != 0);
    assert(actualRawAddress.m_port == rawPort);

    error.fill(0);
    assert(co_gns_client_create("127.0.0.1", actualRawAddress.m_port, &client, error.data(), error.size()) == 1);
    assert(WaitRawConnected(client));

    const std::string clientReliable = R"({"type":"playerState","characterName":"Reliable"})";
    assert(co_gns_client_send(
        client,
        clientReliable.data(),
        static_cast<std::uint32_t>(clientReliable.size()),
        CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT);
    const auto rawReliable = WaitRawServerMessage(client);
    assert(rawReliable.has_value());
    assert(rawReliable->payload == clientReliable);
    assert((rawReliable->flags & k_nSteamNetworkingSend_Reliable) != 0);

    const std::string clientSnapshot = R"({"type":"transform","x":2})";
    assert(co_gns_client_send(
        client,
        clientSnapshot.data(),
        static_cast<std::uint32_t>(clientSnapshot.size()),
        CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED) == CO_GNS_SEND_SENT);
    const auto rawSnapshot = WaitRawServerMessage(client);
    assert(rawSnapshot.has_value());
    assert(rawSnapshot->payload == clientSnapshot);
    assert((rawSnapshot->flags & k_nSteamNetworkingSend_Reliable) == 0);

    co_gns_client_destroy(client);
    client = nullptr;
    if (g_rawServerConnection != k_HSteamNetConnection_Invalid) {
        networking->CloseConnection(g_rawServerConnection, 0, nullptr, false);
        g_rawServerConnection = k_HSteamNetConnection_Invalid;
    }
    networking->CloseListenSocket(g_rawListenSocket);
    g_rawListenSocket = k_HSteamListenSocket_Invalid;
    co_gns_server_destroy(server);
    return 0;
}
