#include "co_gns_server_bridge.h"
#include "test_udp_port.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    struct AdmissionLog
    {
        int ipv4Calls = 0;
        int addressCalls = 0;
        std::string lastAddress;
        bool admit = true;
    };

    int AdmitIpv4(void* a_user, std::uint32_t, std::uint16_t, char*, std::size_t)
    {
        static_cast<AdmissionLog*>(a_user)->ipv4Calls += 1;
        return 1;
    }

    int AdmitAddress(void* a_user, const char* a_address, std::uint16_t, char* a_reason, std::size_t a_reasonSize)
    {
        auto* log = static_cast<AdmissionLog*>(a_user);
        log->addressCalls += 1;
        log->lastAddress = a_address;
        if (!log->admit) {
            std::snprintf(a_reason, a_reasonSize, "%s", "banned");
        }
        return log->admit ? 1 : 0;
    }

    std::uint32_t ConnectOnce(co_gns_server_handle a_server, const char* a_host, std::uint16_t a_port)
    {
        co_gns_client_handle client = nullptr;
        std::array<char, 256> error{};
        if (co_gns_client_create(a_host, a_port, &client, error.data(), error.size()) != 1) {
            std::fprintf(stderr, "client create failed for %s: %s\n", a_host, error.data());
            return 0;
        }
        std::uint32_t serverConnection = 0;
        bool clientConnected = false;
        bool clientClosed = false;
        std::array<char, 64 * 1024> payload{};
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline && !clientClosed && (serverConnection == 0 || !clientConnected)) {
            co_gns_event event{};
            if (co_gns_server_poll(a_server, &event, payload.data(), static_cast<std::uint32_t>(payload.size())) == 1 &&
                event.type == CO_GNS_EVENT_CONNECTED) {
                serverConnection = event.connection_id;
            }
            co_gns_event clientEvent{};
            if (co_gns_client_poll(client, &clientEvent, payload.data(), static_cast<std::uint32_t>(payload.size())) == 1) {
                clientConnected = clientConnected || clientEvent.type == CO_GNS_EVENT_CONNECTED;
                clientClosed = clientEvent.type == CO_GNS_EVENT_DISCONNECTED;
            }
            std::this_thread::sleep_for(2ms);
        }
        co_gns_client_destroy(client);
        return clientConnected ? serverConnection : 0;
    }
}

int main()
{
    std::array<char, 256> error{};

    {
        co_gns_client_handle client = nullptr;
        assert(co_gns_client_create("0.0.0.0", 7777, &client, error.data(), error.size()) == 0);
        assert(co_gns_client_create("::", 7777, &client, error.data(), error.size()) == 0);
        assert(co_gns_client_create("not an address", 7777, &client, error.data(), error.size()) == 0);
        co_gns_server_handle bad = nullptr;
        assert(co_gns_server_create("nonsense", FindAvailableUdpPort(), &bad, error.data(), error.size()) == 0);
    }

    co_gns_server_handle server = nullptr;
    const auto port = FindAvailableUdpPort();
    if (co_gns_server_create("::1", port, &server, error.data(), error.size()) != 1) {
        std::fprintf(stderr, "SKIP: this host cannot listen on IPv6 loopback: %s\n", error.data());
        return 0;
    }

    AdmissionLog log;
    assert(co_gns_server_set_admission_callback(server, &AdmitIpv4, &log) == 1);
    assert(ConnectOnce(server, "::1", port) == 0 && "an IPv6 peer is refused when only the IPv4 admission check is set");
    assert(log.ipv4Calls == 0);

    assert(co_gns_server_set_address_admission_callback(server, &AdmitAddress, &log) == 1);
    log.admit = false;
    assert(ConnectOnce(server, "[::1]", port) == 0 && "the address admission check can refuse an IPv6 peer");
    assert(log.addressCalls >= 1);
    assert(log.lastAddress == "::1");

    log.admit = true;
    const auto connection = ConnectOnce(server, "::1", port);
    assert(connection != 0 && "an admitted IPv6 peer connects");

    std::array<char, 64> address{};
    std::uint16_t remotePort = 0;
    assert(co_gns_server_remote_address(server, connection, address.data(), address.size(), &remotePort) == 1);
    assert(std::string{ address.data() } == "::1");
    assert(remotePort != 0);
    std::array<char, 2> tiny{};
    assert(co_gns_server_remote_address(server, connection, tiny.data(), tiny.size(), &remotePort) == 0);
    std::uint32_t ipv4 = 0;
    assert(co_gns_server_remote_ipv4(server, connection, &ipv4, &remotePort) == 0 && "an IPv6 peer has no IPv4 address");
    co_gns_server_destroy(server);

    co_gns_server_handle dual = nullptr;
    const auto dualPort = FindAvailableUdpPort();
    assert(co_gns_server_create("::", dualPort, &dual, error.data(), error.size()) == 1);
    AdmissionLog dualLog;
    assert(co_gns_server_set_admission_callback(dual, &AdmitIpv4, &dualLog) == 1);
    assert(co_gns_server_set_address_admission_callback(dual, &AdmitAddress, &dualLog) == 1);
    const auto viaIpv4 = ConnectOnce(dual, "127.0.0.1", dualPort);
    if (viaIpv4 == 0) {
        std::fprintf(stderr, "NOTE: this OS does not accept IPv4 peers on an IPv6 :: listener\n");
    } else {
        assert(dualLog.ipv4Calls >= 1 && dualLog.addressCalls == 0 && "an IPv4 peer on :: still uses the IPv4 admission check");
        assert(co_gns_server_remote_ipv4(dual, viaIpv4, &ipv4, &remotePort) == 1);
        assert(ipv4 == 0x7F000001U);
        assert(co_gns_server_remote_address(dual, viaIpv4, address.data(), address.size(), &remotePort) == 1);
        assert(std::string{ address.data() } == "127.0.0.1");
    }
    co_gns_server_destroy(dual);

    std::puts("GNS IPv6 bridge tests passed");
    return 0;
}
