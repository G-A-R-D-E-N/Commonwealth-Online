#include "co_gns_server_bridge.h"

#include <steam/steamnetworkingsockets.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>

namespace
{
    bool LiveGnsRequired()
    {
        const auto* value = std::getenv("CO_REQUIRE_LIVE_GNS");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }
}

int main()
{
    SteamNetworkingErrMsg error{};
    if (!GameNetworkingSockets_Init(nullptr, error)) {
        std::fprintf(stderr, "%s: GameNetworkingSockets_Init failed: %s\n", LiveGnsRequired() ? "FAIL" : "SKIP", error);
        return LiveGnsRequired() ? 1 : 0;
    }

    float packetLoss = -1.0f;
    float packetReorder = -1.0f;
    assert(co_gns_test_set_network_conditions(0.0f, 0.0f) == 1);
    assert(co_gns_test_get_network_conditions(&packetLoss, &packetReorder) == 1);
    assert(packetLoss == 0.0f);
    assert(packetReorder == 0.0f);

    assert(co_gns_test_set_network_conditions(25.0f, 50.0f) == 1);
    assert(co_gns_test_get_network_conditions(&packetLoss, &packetReorder) == 1);
    assert(packetLoss == 25.0f);
    assert(packetReorder == 50.0f);

    assert(co_gns_test_set_network_conditions(-1.0f, 0.0f) == 0);
    assert(co_gns_test_get_network_conditions(&packetLoss, &packetReorder) == 1);
    assert(packetLoss == 25.0f);
    assert(packetReorder == 50.0f);

    assert(co_gns_test_set_network_conditions(0.0f, 101.0f) == 0);
    assert(co_gns_test_get_network_conditions(&packetLoss, &packetReorder) == 1);
    assert(packetLoss == 25.0f);
    assert(packetReorder == 50.0f);

    assert(co_gns_test_set_network_conditions(0.0f, 0.0f) == 1);
    assert(co_gns_test_get_network_conditions(&packetLoss, &packetReorder) == 1);
    assert(packetLoss == 0.0f);
    assert(packetReorder == 0.0f);

    GameNetworkingSockets_Kill();
    return 0;
}
