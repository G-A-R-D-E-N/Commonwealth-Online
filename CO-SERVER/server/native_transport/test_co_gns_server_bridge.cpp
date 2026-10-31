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

    HSteamNetConnection g_clientConnection = k_HSteamNetConnection_Invalid;
    bool g_clientConnected = false;
    bool g_clientFailed = false;

    void ClientConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
    {
        if (info == nullptr || info->m_hConn != g_clientConnection) {
            return;
        }
        switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connected:
            g_clientConnected = true;
            break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            g_clientFailed = info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
            if (auto* networking = SteamNetworkingSockets(); networking != nullptr) {
                networking->CloseConnection(info->m_hConn, 0, nullptr, false);
            }
            g_clientConnection = k_HSteamNetConnection_Invalid;
            g_clientConnected = false;
            break;
        default:
            break;
        }
    }

    struct PolledEvent
    {
        co_gns_event event{};
        std::string payload;
    };

    struct ClientMessage
    {
        std::string payload;
        int flags{};
    };

    bool LiveGnsRequired()
    {
        const auto* value = std::getenv("CO_REQUIRE_LIVE_GNS");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }

    std::optional<PolledEvent> PollBridge(co_gns_server_handle server)
    {
        std::array<char, 64 * 1024> payload{};
        co_gns_event event{};
        const auto result = co_gns_server_poll(
            server,
            &event,
            payload.data(),
            static_cast<std::uint32_t>(payload.size()));
        assert(result >= 0);
        if (result == 0) {
            return std::nullopt;
        }
        PolledEvent out{};
        out.event = event;
        if (event.type == CO_GNS_EVENT_MESSAGE) {
            out.payload.assign(payload.data(), event.payload_size);
        }
        return out;
    }

    template <class Predicate>
    bool WaitUntil(co_gns_server_handle server, Predicate&& predicate, std::chrono::milliseconds timeout = 3s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            PollBridge(server);
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(2ms);
        }
        return false;
    }

    std::optional<PolledEvent> WaitForEvent(
        co_gns_server_handle server,
        std::uint32_t eventType,
        std::chrono::milliseconds timeout = 3s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            auto event = PollBridge(server);
            if (event && event->event.type == eventType) {
                return event;
            }
            std::this_thread::sleep_for(2ms);
        }
        return std::nullopt;
    }

    std::optional<ClientMessage> WaitForClientMessage(
        co_gns_server_handle server,
        ISteamNetworkingSockets* networking,
        std::chrono::milliseconds timeout = 3s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            PollBridge(server);
            ISteamNetworkingMessage* message = nullptr;
            const auto count = networking->ReceiveMessagesOnConnection(g_clientConnection, &message, 1);
            if (count > 0 && message != nullptr) {
                ClientMessage result{};
                result.flags = message->m_nFlags;
                if (message->m_cbSize > 0 && message->m_pData != nullptr) {
                    result.payload.assign(
                        static_cast<const char*>(message->m_pData),
                        static_cast<std::size_t>(message->m_cbSize));
                }
                message->Release();
                return result;
            }
            assert(count >= 0);
            std::this_thread::sleep_for(2ms);
        }
        return std::nullopt;
    }
}

int main()
{
    co_gns_server_handle invalidServer = nullptr;
    std::array<char, 256> invalidError{};
    assert(co_gns_server_create("127.0.0.1", 0, &invalidServer, invalidError.data(), invalidError.size()) == 0);
    assert(invalidServer == nullptr);
    assert(std::string{ invalidError.data() } ==
           "GNS listen port must be nonzero; GameNetworkingSockets does not support ephemeral port 0.");

    co_gns_server_handle server = nullptr;
    std::array<char, 256> error{};
    const auto requestedPort = FindAvailableUdpPort();
    const int createResult = co_gns_server_create("127.0.0.1", requestedPort, &server, error.data(), error.size());
    if (createResult != 1) {
        std::fprintf(stderr, "%s: environment cannot create a GNS listen socket: %s\n", LiveGnsRequired() ? "FAIL" : "SKIP", error.data());
        return LiveGnsRequired() ? 1 : 0;
    }
    assert(server != nullptr);
    const auto port = co_gns_server_local_port(server);
    assert(port != 0);
    assert(port == requestedPort);

    auto* networking = SteamNetworkingSockets();
    assert(networking != nullptr);
    SteamNetworkingIPAddr serverAddress{};
    serverAddress.Clear();
    serverAddress.SetIPv4(0x7f000001U, port);
    SteamNetworkingConfigValue_t option{};
    option.SetPtr(
        k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        reinterpret_cast<void*>(+ClientConnectionStatusChanged));
    g_clientConnection = networking->ConnectByIPAddress(serverAddress, 1, &option);
    assert(g_clientConnection != k_HSteamNetConnection_Invalid);

    const auto connectedEvent = WaitForEvent(server, CO_GNS_EVENT_CONNECTED);
    assert(connectedEvent.has_value());
    const auto serverConnectionId = connectedEvent->event.connection_id;
    assert(serverConnectionId != 0);
    assert(WaitUntil(server, []() { return g_clientConnected && !g_clientFailed; }));
    assert(co_gns_server_connection_count(server) == 1);

    const std::string reliable = R"({"type":"playerState","characterName":"Nomad"})";
    assert(networking->SendMessageToConnection(
        g_clientConnection,
        reliable.data(),
        static_cast<std::uint32_t>(reliable.size()),
        k_nSteamNetworkingSend_ReliableNoNagle,
        nullptr) == k_EResultOK);
    const auto messageEvent = WaitForEvent(server, CO_GNS_EVENT_MESSAGE);
    assert(messageEvent.has_value());
    assert(messageEvent->event.connection_id == serverConnectionId);
    assert(messageEvent->payload == reliable);
    assert(messageEvent->payload.find('\n') == std::string::npos);

    const std::string reliableResponse = R"({"type":"sessionReady","playerId":1})";
    assert(co_gns_server_send(
        server,
        serverConnectionId,
        reliableResponse.data(),
        static_cast<std::uint32_t>(reliableResponse.size()),
        CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_SENT);
    const auto clientReliable = WaitForClientMessage(server, networking);
    assert(clientReliable.has_value());
    assert(clientReliable->payload == reliableResponse);
    assert((clientReliable->flags & k_nSteamNetworkingSend_Reliable) != 0);

    const std::string snapshotResponse = R"({"type":"transform","snapshotSequence":8,"x":1})";
    assert(co_gns_server_send(
        server,
        serverConnectionId,
        snapshotResponse.data(),
        static_cast<std::uint32_t>(snapshotResponse.size()),
        CO_GNS_DELIVERY_UNRELIABLE_SEQUENCED) == CO_GNS_SEND_SENT);
    const auto clientSnapshot = WaitForClientMessage(server, networking);
    assert(clientSnapshot.has_value());
    assert(clientSnapshot->payload == snapshotResponse);
    assert((clientSnapshot->flags & k_nSteamNetworkingSend_Reliable) == 0);

    const std::string tooLarge((64 * 1024) + 1, 'x');
    assert(co_gns_server_send(
        server,
        serverConnectionId,
        tooLarge.data(),
        static_cast<std::uint32_t>(tooLarge.size()),
        CO_GNS_DELIVERY_RELIABLE_ORDERED) == CO_GNS_SEND_TOO_LARGE);
    assert(networking->SendMessageToConnection(
        g_clientConnection,
        tooLarge.data(),
        static_cast<std::uint32_t>(tooLarge.size()),
        k_nSteamNetworkingSend_ReliableNoNagle,
        nullptr) == k_EResultOK);
    const auto oversizeEvent = WaitForEvent(server, CO_GNS_EVENT_OVERSIZE_MESSAGE);
    assert(oversizeEvent.has_value());
    assert(oversizeEvent->event.connection_id == serverConnectionId);
    assert(oversizeEvent->event.payload_size == tooLarge.size());

    assert(co_gns_server_disconnect(server, serverConnectionId, 1000, "test complete") == 1);
    assert(WaitUntil(server, []() { return g_clientConnection == k_HSteamNetConnection_Invalid; }));
    assert(!g_clientFailed);

    co_gns_server_destroy(server);
    return 0;
}
