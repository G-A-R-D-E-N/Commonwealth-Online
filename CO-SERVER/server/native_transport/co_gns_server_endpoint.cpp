#include "co_gns_server_bridge_internal.h"

#include <steam/steamnetworkingsockets.h>

extern "C"
{
    int co_gns_server_remote_ipv4(
        co_gns_server_handle handle,
        uint32_t connection_id,
        uint32_t* out_ipv4_host_order,
        uint16_t* out_port)
    {
        if (handle == nullptr || connection_id == 0 || out_ipv4_host_order == nullptr || out_port == nullptr) {
            return 0;
        }

        const auto* server = static_cast<const co_gns::detail::ServerBridge*>(handle);
        const auto connection = static_cast<HSteamNetConnection>(connection_id);
        auto* networking = server->Networking();
        if (networking == nullptr || !server->OwnsConnection(connection)) {
            return 0;
        }

        SteamNetConnectionInfo_t info{};
        if (!networking->GetConnectionInfo(connection, &info)) {
            return 0;
        }
        if (!info.m_addrRemote.IsIPv4()) {
            return 0;
        }

        *out_ipv4_host_order = info.m_addrRemote.GetIPv4();
        *out_port = info.m_addrRemote.m_port;
        return 1;
    }

    int co_gns_server_remote_address(
        co_gns_server_handle handle,
        uint32_t connection_id,
        char* out_address,
        size_t out_address_size,
        uint16_t* out_port)
    {
        if (handle == nullptr || connection_id == 0 || out_address == nullptr || out_address_size == 0 || out_port == nullptr) {
            return 0;
        }

        const auto* server = static_cast<const co_gns::detail::ServerBridge*>(handle);
        const auto connection = static_cast<HSteamNetConnection>(connection_id);
        auto* networking = server->Networking();
        if (networking == nullptr || !server->OwnsConnection(connection)) {
            return 0;
        }

        SteamNetConnectionInfo_t info{};
        if (!networking->GetConnectionInfo(connection, &info)) {
            return 0;
        }
        if (!co_gns::detail::FormatAddressText(info.m_addrRemote, out_address, out_address_size)) {
            return 0;
        }
        *out_port = info.m_addrRemote.m_port;
        return 1;
    }
}
