#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include "F4TReceivePolicy.h"

using namespace std::chrono_literals;
constexpr int INVALID_SOCKET = -1, WSAEWOULDBLOCK = 1;
constexpr auto kReceiveIdleSleep = 1ms;
constexpr std::size_t kMaximumReceiveBufferSize = 8;
int g_socket = 1;
std::mutex g_socketMutex, g_receiveThreadMutex;
std::atomic_bool g_receiveThreadRunning{false};
std::atomic_bool g_receiveLoopActive{false};
std::thread g_receiveThread;
thread_local bool g_isReceiveThread = false;
F4T::ReceivePolicy::Config g_inboundPolicyConfig{.maximumFrameBytes = 8};
F4T::ReceivePolicy::State g_inboundPolicyState;
std::string mode, reason;
std::atomic_int notifications{0}, receives{0};
namespace F4T::DebugLogging { template<class... T> void Log(T&&...) {} }
void LogWarningWithLocalPlayerPrefix(const char*) {}
double SteadySeconds() { return 0; }
void CloseSocket() { std::scoped_lock lock(g_socketMutex); g_socket = INVALID_SOCKET; }
void ClosePendingSocket() {}
int GetSocket() { std::scoped_lock lock(g_socketMutex); return g_socket; }
namespace F4T::UIClient {
void NotifyTransportDisconnected(const char* text = nullptr) {
    assert(GetSocket() == INVALID_SOCKET);
    reason = text ? text : "generic";
    ++notifications;
}
}
void StopReceiveThread();
void HandlePacketLine(const std::string&) {
    assert(mode == "terminal");
    StopReceiveThread();
    F4T::UIClient::NotifyTransportDisconnected("terminal");
}
int recv(int, char* bytes, int, int) {
    ++receives;
    if (mode == "eof") return 0;
    if (mode == "error" || mode == "stop") return -1;
    const std::string payload = mode == "boundary" ? "123456789\n" : mode == "overflow" ? "123456789" : "ok\n";
    std::memcpy(bytes, payload.data(), payload.size());
    return static_cast<int>(payload.size());
}
int WSAGetLastError() { return mode == "stop" ? WSAEWOULDBLOCK : 2; }
#include "receive-source.inc"
int main() {
    for (const auto* scenario : {"invalid", "eof", "error", "boundary", "overflow", "terminal", "stop"}) {
        mode = scenario; reason.clear(); notifications.store(0); receives.store(0);
        g_socket = mode == "invalid" ? INVALID_SOCKET : 1;
        g_inboundPolicyState = F4T::ReceivePolicy::Initialize(0);
        StartReceiveThread();
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (mode == "stop" ? receives.load() == 0 : g_receiveThreadRunning.load()) {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        StopReceiveThread();
        assert(notifications.load() == (mode == "stop" ? 0 : 1));
        if (mode == "boundary") assert(reason == "Server violated the receive boundary.");
        if (mode == "overflow") assert(reason == "Server sent an oversized unterminated frame.");
        if (mode == "terminal") assert(reason == "terminal");
        if (mode == "invalid" || mode == "eof" || mode == "error") assert(reason == "generic");
        assert(!g_receiveThread.joinable() && !g_receiveThreadRunning.load());
    }
}
