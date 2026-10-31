#include "co_gns_server_bridge.h"
#include "test_udp_port.h"

#include <steam/steamnetworkingsockets.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    HSteamNetConnection g_clientConnection = k_HSteamNetConnection_Invalid;
    bool g_clientConnected = false;

    bool LiveGnsRequired()
    {
        const auto* value = std::getenv("CO_REQUIRE_LIVE_GNS");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }

    void ClientConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
    {
        if (info == nullptr || info->m_hConn != g_clientConnection) {
            return;
        }
        if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
            g_clientConnected = true;
        }
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
    assert(server != nullptr);
    const auto serverPort = co_gns_server_local_port(server);
    assert(serverPort != 0);
    assert(serverPort == requestedPort);

    auto* networking = SteamNetworkingSockets();
    assert(networking != nullptr);
    SteamNetworkingIPAddr serverAddress{};
    serverAddress.Clear();
    serverAddress.SetIPv4(0x7F000001U, serverPort);
    SteamNetworkingConfigValue_t option{};
    option.SetPtr(
        k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        reinterpret_cast<void*>(+ClientConnectionStatusChanged));
    g_clientConnection = networking->ConnectByIPAddress(serverAddress, 1, &option);
    assert(g_clientConnection != k_HSteamNetConnection_Invalid);

    std::uint32_t serverConnectionId = 0;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline && (serverConnectionId == 0 || !g_clientConnected)) {
        co_gns_event event{};
        std::array<char, 64 * 1024> payload{};
        const auto pollResult = co_gns_server_poll(
            server,
            &event,
            payload.data(),
            static_cast<std::uint32_t>(payload.size()));
        assert(pollResult >= 0);
        if (pollResult == 1 && event.type == CO_GNS_EVENT_CONNECTED) {
            serverConnectionId = event.connection_id;
        }
        std::this_thread::sleep_for(2ms);
    }

    assert(g_clientConnected);
    assert(serverConnectionId != 0);
    std::uint32_t remoteIpv4 = 0;
    std::uint16_t remotePort = 0;
    assert(co_gns_server_remote_ipv4(server, serverConnectionId, &remoteIpv4, &remotePort) == 1);
    assert(remoteIpv4 == 0x7F000001U);
    assert(remotePort != 0);

    std::uint32_t missingIpv4 = 0;
    std::uint16_t missingPort = 0;
    assert(co_gns_server_remote_ipv4(server, serverConnectionId + 100000U, &missingIpv4, &missingPort) == 0);

    co_gns_server_handle otherServer = nullptr;
    assert(co_gns_server_create("127.0.0.1", FindAvailableUdpPort(), &otherServer, error.data(), error.size()) == 1);
    std::uint32_t foreignIpv4 = 0;
    std::uint16_t foreignPort = 0;
    assert(co_gns_server_remote_ipv4(otherServer, serverConnectionId, &foreignIpv4, &foreignPort) == 0);
    co_gns_server_destroy(otherServer);

    networking->CloseConnection(g_clientConnection, 0, nullptr, false);
    g_clientConnection = k_HSteamNetConnection_Invalid;
    co_gns_server_destroy(server);
    return 0;
}
