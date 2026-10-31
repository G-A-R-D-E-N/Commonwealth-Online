#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>

using SOCKET = std::uintptr_t;
constexpr SOCKET INVALID_SOCKET = static_cast<SOCKET>(~static_cast<SOCKET>(0));
constexpr int SOCKET_ERROR = -1;
constexpr int WSAEWOULDBLOCK = 10035;
constexpr int SD_BOTH = 2;
constexpr auto kSocketWriteWait = std::chrono::milliseconds(250);

std::atomic<bool> g_signalNextUnlock{false};
std::atomic<bool> g_mutexReleased{false};
std::atomic<bool> g_allowCleanup{false};

class ObservedMutex
{
public:
    void lock()
    {
        inner.lock();
    }

    bool try_lock()
    {
        return inner.try_lock();
    }

    void unlock()
    {
        inner.unlock();
        if (g_signalNextUnlock.exchange(false)) {
            g_mutexReleased.store(true);
            while (!g_allowCleanup.load()) {
                std::this_thread::yield();
            }
        }
    }

private:
    std::mutex inner;
};

SOCKET g_socket = INVALID_SOCKET;
SOCKET g_socketPendingClose = INVALID_SOCKET;
ObservedMutex g_socketMutex;
std::mutex g_sendMutex;
std::atomic<bool> g_receiveLoopActive{false};
thread_local bool g_isReceiveThread = false;

std::atomic<SOCKET> g_shutdownHandle{INVALID_SOCKET};
std::atomic<SOCKET> g_closeHandle{INVALID_SOCKET};

void shutdown(SOCKET a_socket, int)
{
    g_shutdownHandle.store(a_socket);
}

int closesocket(SOCKET a_socket)
{
    g_closeHandle.store(a_socket);
    return 0;
}

int WSAGetLastError()
{
    return 0;
}

int send(SOCKET, const char*, int, int)
{
    return SOCKET_ERROR;
}

bool WaitForSocketWritable(SOCKET, std::chrono::milliseconds)
{
    return false;
}

#include "socket-lifecycle.inc"

namespace F4T::Networking::Detail
{
#include "send-bytes.inc"
}

namespace
{
    void InstallSocket(SOCKET a_socket)
    {
        const std::scoped_lock lock(g_socketMutex);
        g_socket = a_socket;
    }

    SOCKET CurrentSocket()
    {
        const std::scoped_lock lock(g_socketMutex);
        return g_socket;
    }

    void ResetObservations()
    {
        g_shutdownHandle.store(INVALID_SOCKET);
        g_closeHandle.store(INVALID_SOCKET);
        g_signalNextUnlock.store(false);
        g_mutexReleased.store(false);
        g_allowCleanup.store(false);
    }

    void TestFailedSendClosesItsOwnSocket()
    {
        ResetObservations();
        InstallSocket(100);
        assert(!F4T::Networking::Detail::SendPacketBytes("frame"));
        assert(CurrentSocket() == INVALID_SOCKET);
        assert(g_shutdownHandle.load() == 100);
        assert(g_closeHandle.load() == 100);
    }

    void TestReconnectBeforeCleanupSelectsAHandleKeepsTheNewSocket()
    {
        ResetObservations();
        InstallSocket(200);
        g_signalNextUnlock.store(true);

        std::thread failingSender([] {
            assert(!F4T::Networking::Detail::SendPacketBytes("frame"));
        });
        while (!g_mutexReleased.load()) {
            std::this_thread::yield();
        }

        std::thread reconnector([] {
            CloseSocket();
            InstallSocket(300);
        });
        g_allowCleanup.store(true);
        failingSender.join();
        reconnector.join();

        assert(g_shutdownHandle.load() == 200);
        assert(g_closeHandle.load() == 200);
        assert(CurrentSocket() == 300);
    }
}

int main()
{
    TestFailedSendClosesItsOwnSocket();
    TestReconnectBeforeCleanupSelectsAHandleKeepsTheNewSocket();
    return 0;
}
