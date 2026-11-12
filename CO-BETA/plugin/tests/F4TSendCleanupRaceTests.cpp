#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
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
std::atomic<bool> g_observeSendLock{false};
std::atomic<bool> g_sendLockAttempted{false};

class SendMutex
{
public:
    void lock()
    {
        if (g_observeSendLock.load()) g_sendLockAttempted.store(true);
        inner.lock();
    }

    bool try_lock()
    {
        if (g_observeSendLock.load()) g_sendLockAttempted.store(true);
        return inner.try_lock();
    }

    void unlock() { inner.unlock(); }

private:
    std::mutex inner;
};

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
SendMutex g_sendMutex;
std::atomic<bool> g_receiveLoopActive{false};
thread_local bool g_isReceiveThread = false;

std::atomic<SOCKET> g_shutdownHandle{INVALID_SOCKET};
std::atomic<SOCKET> g_closeHandle{INVALID_SOCKET};
std::atomic<std::uint64_t> g_sessionGeneration{1};
std::atomic<int> g_sendCalls{0};
std::atomic<bool> g_sendSucceeds{false};
std::atomic<bool> g_blockSend{false};
std::atomic<bool> g_insideSend{false};
std::atomic<bool> g_releaseSend{false};
std::atomic<bool> g_lossSaveHold{false};
namespace F4T::Networking { bool IsLossSaveHeld() { return g_lossSaveHold.load(); } }

namespace F4T::ReplicationRuntime
{
    std::uint64_t CurrentGeneration() { return g_sessionGeneration.load(); }
    void NoteConnectionEstablished() { ++g_sessionGeneration; }
}

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

int send(SOCKET, const char*, int a_size, int)
{
    ++g_sendCalls;
    if (g_blockSend) {
        g_insideSend.store(true);
        while (!g_releaseSend.load()) {
            std::this_thread::yield();
        }
    }
    return g_sendSucceeds ? a_size : SOCKET_ERROR;
}

bool WaitForSocketWritable(SOCKET, std::chrono::milliseconds)
{
    return false;
}

#include "socket-lifecycle.inc"

namespace F4T::Networking::Detail
{
bool SendPacketBytes(std::string_view, std::optional<std::uint64_t>);
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

    void TestSessionBoundSend()
    {
        ResetObservations();
        {
            const std::scoped_lock lock(g_sendMutex, g_socketMutex);
            g_sessionGeneration.store(2);
            g_socket = 400;
        }
        g_sendCalls.store(0);
        g_sendSucceeds.store(true);
        assert(!F4T::Networking::Detail::SendPacketBytes("frame", 1));
        assert(!F4T::Networking::Detail::SendPacketBytes("frame", 0));
        assert(g_sendCalls.load() == 0);
        assert(F4T::Networking::Detail::SendPacketBytes("frame", 2));
        assert(g_sendCalls.load() == 1);
        g_sendSucceeds.store(false);
    }

    void TestReconnectWaitsForSessionBoundSend()
    {
        g_sessionGeneration.store(3);
        InstallSocket(500);
        g_sendSucceeds.store(true);
        g_insideSend.store(false);
        g_releaseSend.store(false);
        g_blockSend.store(true);
        std::atomic<bool> installed{false};
        std::thread sender([] { assert(F4T::Networking::Detail::SendPacketBytes("frame", 3)); });
        while (!g_insideSend.load()) {
            std::this_thread::yield();
        }
        g_sendLockAttempted.store(false);
        g_observeSendLock.store(true);
        std::thread reconnect([&] {
            const std::scoped_lock lock(g_sendMutex, g_socketMutex);
            g_sessionGeneration.store(4);
            g_socket = 600;
            installed.store(true);
        });
        while (!g_sendLockAttempted.load()) {
            std::this_thread::yield();
        }
        assert(!installed.load());
        g_observeSendLock.store(false);
        g_releaseSend.store(true);
        sender.join();
        reconnect.join();
        assert(installed.load());
        assert(!F4T::Networking::Detail::SendPacketBytes("frame", 3));
        g_blockSend.store(false);
        g_sendSucceeds.store(false);
    }

    void TestLoadResetWaitsForSessionBoundSend()
    {
        g_sessionGeneration.store(5);
        InstallSocket(700);
        g_sendSucceeds.store(true);
        g_insideSend.store(false);
        g_releaseSend.store(false);
        g_blockSend.store(true);
        std::atomic<bool> reset{false};
        std::thread sender([] { assert(F4T::Networking::Detail::SendPacketBytes("frame", 5)); });
        while (!g_insideSend.load()) {
            std::this_thread::yield();
        }
        g_sendLockAttempted.store(false);
        g_observeSendLock.store(true);
        std::thread loadReset([&] {
            F4T::Networking::Detail::InvalidateReplicationSession();
            reset.store(true);
        });
        while (!g_sendLockAttempted.load()) {
            std::this_thread::yield();
        }
        assert(!reset.load());
        g_observeSendLock.store(false);
        g_releaseSend.store(true);
        sender.join();
        loadReset.join();
        assert(reset.load());
        assert(!F4T::Networking::Detail::SendPacketBytes("frame", 5));
        g_blockSend.store(false);
        g_sendSucceeds.store(false);
    }
}

int main()
{
    TestFailedSendClosesItsOwnSocket();
    TestReconnectBeforeCleanupSelectsAHandleKeepsTheNewSocket();
    TestSessionBoundSend();
    TestReconnectWaitsForSessionBoundSend();
    TestLoadResetWaitsForSessionBoundSend();
    return 0;
}
