#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono_literals;
std::atomic<bool> joinEntered{false};
std::atomic<bool> packetEntered{false};
std::atomic<bool> selfStopped{false};
std::atomic<bool> terminalMode{true};
void Wait(const std::atomic<bool>& signal) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!signal.load()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
class CheckedThread {
    std::thread thread;
    std::atomic<bool> inUse{false};
    struct Access {
        CheckedThread& owner;
        explicit Access(CheckedThread& value) : owner(value) { assert(!owner.inUse.exchange(true)); }
        ~Access() { owner.inUse.store(false); }
    };
public:
    bool joinable() { Access access(*this); return thread.joinable(); }
    std::thread::id get_id() { Access access(*this); return thread.get_id(); }
    void join() { Access access(*this); joinEntered.store(true); thread.join(); }
    CheckedThread& operator=(std::thread value) { Access access(*this); thread = std::move(value); return *this; }
};
constexpr int INVALID_SOCKET = -1;
constexpr int WSAEWOULDBLOCK = 1;
constexpr auto kReceiveIdleSleep = 1ms;
int g_socket = 1;
std::mutex g_socketMutex;
std::mutex g_receiveThreadMutex;
thread_local bool g_isReceiveThread = false;
std::atomic<bool> g_receiveThreadRunning{false};
std::atomic<bool> g_receiveLoopActive{false};
CheckedThread g_receiveThread;
namespace F4T::DebugLogging { template<class... T> void Log(T&&...) {} }
namespace F4T::UIClient { void NotifyTransportDisconnected() {} }
void LogWarningWithLocalPlayerPrefix(const char*) {}
void CloseSocket() { std::scoped_lock lock(g_socketMutex); g_socket = INVALID_SOCKET; }
void ClosePendingSocket() {}
int GetSocket() { std::scoped_lock lock(g_socketMutex); return g_socket; }
int recv(int, char*, int, int) { return terminalMode.load() ? 1 : -1; }
int WSAGetLastError() { return WSAEWOULDBLOCK; }
void StopReceiveThread();
void ProcessReceiveBuffer(std::string&) {
    packetEntered.store(true);
    Wait(joinEntered);
    StopReceiveThread();
    selfStopped.store(true);
}
#include "receive-thread-source.inc"
int main() {
    StartReceiveThread();
    Wait(packetEntered);
    StopReceiveThread();
    assert(selfStopped && !g_receiveThread.joinable() && !g_receiveThreadRunning);
    terminalMode.store(false);
    for (int i = 0; i < 50; ++i) {
        { std::scoped_lock lock(g_socketMutex); g_socket = 1; }
        StartReceiveThread();
        std::thread stopper(StopReceiveThread);
        StopReceiveThread();
        stopper.join();
        assert(!g_receiveThread.joinable() && !g_receiveThreadRunning && g_socket == INVALID_SOCKET);
    }
}
