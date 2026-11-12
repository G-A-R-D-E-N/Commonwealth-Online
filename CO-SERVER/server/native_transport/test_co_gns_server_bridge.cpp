#include "co_gns_server_bridge.h"
#include "test_udp_port.h"

#include "../../../CO-BETA/plugin/gns-bridge/co_gns_auth_protocol.h"

#include <steam/steamnetworkingsockets.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#endif

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

    void CheckRfc8032Vector()
    {
        co_gns::auth::Seed seed{};
        co_gns::auth::PublicKey expectedPublicKey{};
        co_gns::auth::Signature expectedSignature{};
        assert(co_gns::auth::FromHex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", seed.data(), seed.size()));
        assert(co_gns::auth::FromHex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", expectedPublicKey.data(), expectedPublicKey.size()));
        assert(co_gns::auth::FromHex(
            "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
            expectedSignature.data(),
            expectedSignature.size()));
        const std::array<std::uint8_t, 1> message{ 0x72 };

        co_gns::auth::PublicKey publicKey{};
        assert(co_gns::auth::DerivePublicKey(seed, publicKey));
        assert(publicKey == expectedPublicKey);
        co_gns::auth::Signature signature{};
        assert(co_gns::auth::Ed25519Sign(seed, message.data(), message.size(), signature));
        assert(signature == expectedSignature);
        assert(co_gns::auth::Ed25519Verify(publicKey, message.data(), message.size(), signature));
        signature[0] = static_cast<std::uint8_t>(signature[0] ^ 1U);
        assert(!co_gns::auth::Ed25519Verify(publicKey, message.data(), message.size(), signature));
    }

    int AdmitEveryPeer(void*, std::uint32_t, std::uint16_t, char*, std::size_t)
    {
        return 1;
    }

    int CountAdmittedPeer(void* user, std::uint32_t, std::uint16_t, char*, std::size_t)
    {
        ++*static_cast<int*>(user);
        return 1;
    }

    void CheckAdmittedPeerRejectedFrameDisconnectsOnce()
    {
        co_gns_server_handle server = nullptr;
        std::array<char, 256> error{};
        const auto port = FindAvailableUdpPort();
        assert(co_gns_server_create("127.0.0.1", port, &server, error.data(), error.size()) == 1);
        assert(co_gns_server_set_admission_callback(server, &AdmitEveryPeer, nullptr) == 1);

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
        assert(WaitUntil(server, []() { return g_clientConnected; }));

        const std::string notHello = R"({"type":"playerState","characterName":"Nomad"})";
        assert(networking->SendMessageToConnection(
            g_clientConnection,
            notHello.data(),
            static_cast<std::uint32_t>(notHello.size()),
            k_nSteamNetworkingSend_ReliableNoNagle,
            nullptr) == k_EResultOK);

        int disconnects = 0;
        bool connected = false;
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (const auto event = PollBridge(server)) {
                if (event->event.type == CO_GNS_EVENT_DISCONNECTED) {
                    ++disconnects;
                }
                if (event->event.type == CO_GNS_EVENT_CONNECTED) {
                    connected = true;
                }
            }
            std::this_thread::sleep_for(2ms);
        }
        assert(disconnects == 1);
        assert(!connected);
        assert(co_gns_server_connection_count(server) == 0);

        co_gns_server_destroy(server);
    }

    void CheckAdmittedPeerHandshakeTimeoutDisconnectsOnce()
    {
        co_gns_server_handle server = nullptr;
        std::array<char, 256> error{};
        const auto port = FindAvailableUdpPort();
        assert(co_gns_server_create("127.0.0.1", port, &server, error.data(), error.size()) == 1);
        int reservedPending = 0;
        assert(co_gns_server_set_admission_callback(server, &CountAdmittedPeer, &reservedPending) == 1);

        auto* networking = SteamNetworkingSockets();
        assert(networking != nullptr);
        SteamNetworkingIPAddr serverAddress{};
        serverAddress.Clear();
        serverAddress.SetIPv4(0x7f000001U, port);
        SteamNetworkingConfigValue_t option{};
        option.SetPtr(
            k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
            reinterpret_cast<void*>(+ClientConnectionStatusChanged));
        g_clientConnected = false;
        g_clientConnection = networking->ConnectByIPAddress(serverAddress, 1, &option);
        assert(g_clientConnection != k_HSteamNetConnection_Invalid);
        assert(WaitUntil(server, []() { return g_clientConnected; }));
        assert(reservedPending == 1);

        int disconnects = 0;
        bool connected = false;
        const auto deadline = std::chrono::steady_clock::now() + co_gns::auth::kAuthTimeout + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (const auto event = PollBridge(server)) {
                if (event->event.type == CO_GNS_EVENT_DISCONNECTED) {
                    ++disconnects;
                    reservedPending -= 1;
                }
                if (event->event.type == CO_GNS_EVENT_CONNECTED) {
                    connected = true;
                }
            }
            std::this_thread::sleep_for(2ms);
        }
        assert(disconnects == 1);
        assert(!connected);
        assert(reservedPending == 0);
        assert(co_gns_server_connection_count(server) == 0);
        assert(WaitUntil(server, []() { return g_clientConnection == k_HSteamNetConnection_Invalid; }));

        co_gns_server_destroy(server);
    }

#ifndef _WIN32
    void CheckPrivateSeedFileModeIsEnforced()
    {
        const auto identityPath = std::filesystem::temp_directory_path() /
            ("co_gns_identity_mode_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".key");
        const auto publicPath = identityPath.string() + ".pub";
        assert(setenv("CO_GNS_SERVER_IDENTITY_FILE", identityPath.c_str(), 1) == 0);

        struct stat status{};
        std::string error;
        co_gns::auth::ServerIdentity created;
        assert(created.LoadOrCreate(error));
        assert(::stat(identityPath.c_str(), &status) == 0);
        assert((status.st_mode & 0777) == 0600);
        const auto createdKey = created.PublicKeyValue();

        assert(::chmod(identityPath.c_str(), 0644) == 0);
        co_gns::auth::ServerIdentity loaded;
        assert(loaded.LoadOrCreate(error));
        assert(::stat(identityPath.c_str(), &status) == 0);
        assert((status.st_mode & 0777) == 0600);
        assert(loaded.PublicKeyValue() == createdKey);

        assert(unsetenv("CO_GNS_SERVER_IDENTITY_FILE") == 0);
        std::error_code removeError;
        std::filesystem::remove(identityPath, removeError);
        std::filesystem::remove(publicPath, removeError);
    }
#endif
}

int main()
{
    CheckRfc8032Vector();
    assert(co_gns_maximum_message_bytes() == 256uz * 1024uz);
    assert(co_gns_set_maximum_message_bytes(512uz * 1024uz) == 0);
    assert(co_gns_maximum_message_bytes() == 256uz * 1024uz);
    assert(co_gns_set_maximum_message_bytes(64uz * 1024uz) == 1);
    assert(co_gns_maximum_message_bytes() == 64uz * 1024uz);
    assert(co_gns_set_maximum_message_bytes(256uz * 1024uz) == 1);
    assert(co_gns_maximum_message_bytes() == 256uz * 1024uz);
#ifndef _WIN32
    CheckPrivateSeedFileModeIsEnforced();
#endif
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
    assert(WaitUntil(server, []() { return g_clientConnected; }));

    const auto authNonce = co_gns::auth::RandomNonce();
    const auto authHello = co_gns::auth::BuildHello(authNonce);
    assert(networking->SendMessageToConnection(
        g_clientConnection,
        authHello.data(),
        static_cast<std::uint32_t>(authHello.size()),
        k_nSteamNetworkingSend_ReliableNoNagle,
        nullptr) == k_EResultOK);

    const auto connectedEvent = WaitForEvent(server, CO_GNS_EVENT_CONNECTED);
    assert(connectedEvent.has_value());
    const auto authReply = WaitForClientMessage(server, networking);
    assert(authReply.has_value());
    co_gns::auth::PublicKey serverPublicKey{};
    co_gns::auth::Signature serverSignature{};
    assert(co_gns::auth::ParseReply(
        reinterpret_cast<const std::uint8_t*>(authReply->payload.data()),
        authReply->payload.size(),
        serverPublicKey,
        serverSignature));
    assert(co_gns::auth::VerifyNonce(serverPublicKey, authNonce, serverSignature));

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

    const std::string tooLarge(co_gns_maximum_message_bytes() + 1, 'x');
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
    CheckAdmittedPeerRejectedFrameDisconnectsOnce();
    CheckAdmittedPeerHandshakeTimeoutDisconnectsOnce();
    return 0;
}
