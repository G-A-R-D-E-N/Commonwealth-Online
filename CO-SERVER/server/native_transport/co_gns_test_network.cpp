#include "co_gns_server_bridge.h"

#include <steam/isteamnetworkingutils.h>

#include <cmath>

namespace
{
    bool ReadGlobalFloat(
        ISteamNetworkingUtils* utils,
        ESteamNetworkingConfigValue value,
        float& output)
    {
        std::size_t size = sizeof(output);
        ESteamNetworkingConfigDataType type = k_ESteamNetworkingConfig_Float;
        const auto result = utils->GetConfigValue(
            value,
            k_ESteamNetworkingConfig_Global,
            0,
            &type,
            &output,
            &size);
        return (result == k_ESteamNetworkingGetConfigValue_OK ||
                result == k_ESteamNetworkingGetConfigValue_OKInherited) &&
               type == k_ESteamNetworkingConfig_Float &&
               size == sizeof(output);
    }
}

extern "C"
{
    int co_gns_test_set_network_conditions(float packet_loss_percent, float packet_reorder_percent)
    {
        if (!std::isfinite(packet_loss_percent) || !std::isfinite(packet_reorder_percent) ||
            packet_loss_percent < 0.0f || packet_loss_percent > 100.0f ||
            packet_reorder_percent < 0.0f || packet_reorder_percent > 100.0f) {
            return 0;
        }

        auto* utils = SteamNetworkingUtils();
        if (utils == nullptr) {
            return 0;
        }

        float previousLoss = 0.0f;
        float previousReorder = 0.0f;
        if (!ReadGlobalFloat(utils, k_ESteamNetworkingConfig_FakePacketLoss_Send, previousLoss) ||
            !ReadGlobalFloat(utils, k_ESteamNetworkingConfig_FakePacketReorder_Send, previousReorder)) {
            return 0;
        }

        if (!utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, packet_loss_percent)) {
            return 0;
        }
        if (!utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketReorder_Send, packet_reorder_percent)) {
            utils->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, previousLoss);
            return 0;
        }
        return 1;
    }

    int co_gns_test_get_network_conditions(float* out_packet_loss_percent, float* out_packet_reorder_percent)
    {
        if (out_packet_loss_percent == nullptr || out_packet_reorder_percent == nullptr) {
            return 0;
        }
        auto* utils = SteamNetworkingUtils();
        if (utils == nullptr) {
            return 0;
        }
        float packetLoss = 0.0f;
        float packetReorder = 0.0f;
        if (!ReadGlobalFloat(utils, k_ESteamNetworkingConfig_FakePacketLoss_Send, packetLoss) ||
            !ReadGlobalFloat(utils, k_ESteamNetworkingConfig_FakePacketReorder_Send, packetReorder)) {
            return 0;
        }
        *out_packet_loss_percent = packetLoss;
        *out_packet_reorder_percent = packetReorder;
        return 1;
    }
}
