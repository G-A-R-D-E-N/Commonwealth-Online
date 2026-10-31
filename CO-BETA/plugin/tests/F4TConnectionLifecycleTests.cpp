#include "F4TConnectionPolicy.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using namespace std::chrono_literals;
constexpr int INVALID_SOCKET = -1;
constexpr int kLocalServerPort = 7777;
constexpr auto kLocalServerHost = "127.0.0.1";
struct WSADATA {};
int MAKEWORD(int, int) { return 0; }
int g_socket = INVALID_SOCKET;
std::mutex g_socketMutex;
std::mutex g_diagnosticMutex;
std::mutex g_receiveThreadMutex;
std::recursive_mutex g_connectionOperationMutex;
std::atomic_uint64_t g_connectionGeneration{0};
bool g_winsockInitialized = false;
auto g_lastConnectAttempt = std::chrono::steady_clock::time_point{};
auto g_lastKeepAliveSendTime = std::chrono::steady_clock::time_point{};
std::atomic_bool g_receiveThreadRunning{false};
thread_local bool g_isReceiveThread = false;
std::atomic_bool joinEntered{false};
std::atomic_bool terminalMode{false};
std::atomic_bool receiverEntered{false};
std::atomic_bool receiverCleaned{false};
std::atomic_bool requirePriorDrain{false};
std::atomic_bool blockResolve{false};
std::atomic_bool resolveEntered{false};
std::atomic_bool releaseResolve{false};
std::atomic_bool holdFirstCleanup{false};
std::atomic_bool cleanupEntered{false};
std::atomic_bool releaseCleanup{false};
std::atomic_bool cleanupInUse{false};
std::atomic_int cleanupCalls{0};
std::atomic_int startupCalls{0};
std::atomic_int winsockReferences{0};
std::atomic_bool welcomeRead{false};
std::atomic_bool assignedPlayer{false};
std::atomic_bool remotePlayersCleared{false};
std::atomic_bool worldResetRequested{false};
std::atomic_int inventorySyncResetCalls{0};
std::atomic_int objectSyncResetCalls{0};
std::atomic_int settlementAccessResetCalls{0};

void Wait(const std::atomic_bool& signal) {
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (!signal.load()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
class JoiningThread {
    std::thread thread;
public:
    bool joinable() { return thread.joinable(); }
    void join() { joinEntered.store(true); thread.join(); }
    JoiningThread& operator=(std::thread value) { thread = std::move(value); return *this; }
} g_receiveThread;
int GetSocket() { std::scoped_lock lock(g_socketMutex); return g_socket; }
void CloseSocket() { std::scoped_lock lock(g_socketMutex); g_socket = INVALID_SOCKET; }
void ClosePendingSocket() {}
bool IsConnectedToServer() { return GetSocket() != INVALID_SOCKET; }
int WSAStartup(int, WSADATA*) { ++startupCalls; ++winsockReferences; return 0; }
void WSACleanup() { assert(winsockReferences.fetch_sub(1) == 1); }
void LogWarningWithLocalPlayerPrefix(const std::string&) {}
void LogStatusWithLocalPlayerPrefix(const std::string&) {}
template<class... T> std::string FixtureFormat(T&&...) { return "fixture"; }
namespace F4T::DebugLogging { template<class... T> void Log(T&&...) {} }
namespace F4T::RemotePlayerState {
void ClearAssignedPlayerId() { assignedPlayer.store(false); }
void ClearRemotePlayers() { remotePlayersCleared.store(true); }
std::optional<int> GetAssignedPlayerId() {
    welcomeRead.store(true);
    return assignedPlayer.load() ? std::optional<int>{1} : std::nullopt;
}
}
namespace F4T::Factions { void Reset() {} }
namespace F4T::Chat { void Reset() {} }
namespace F4T::WorldStateSync { void RequestReset() { worldResetRequested.store(true); } }
namespace F4T::InventorySync { void Reset() { ++inventorySyncResetCalls; } }
namespace F4T::ObjectSync { void Reset() { ++objectSyncResetCalls; } }
namespace F4T::ConsoleGuard { void Reset() {} }
namespace F4T::SettlementAccessClient { void Reset() { ++settlementAccessResetCalls; } }
namespace F4T::SettlementCaptureClient { void Reset() {} }
namespace F4T::CharacterSlotsClient { void Reset() {} }
namespace F4T::QuestSync { void ClearSession() {} }
namespace F4T::LoadOrderSync { void Reset() {} }
namespace F4T::ReplicationRuntime { void NoteConnectionEstablished() {} }
void ResetPacketDiagnostics() {}
void ResetInboundBudget() {
    if (requirePriorDrain.load() && !g_isReceiveThread) assert(receiverCleaned.load());
    assert(!cleanupInUse.exchange(true));
    ++cleanupCalls;
    cleanupEntered.store(true);
    if (holdFirstCleanup.exchange(false)) Wait(releaseCleanup);
    cleanupInUse.store(false);
}
bool NormalizeConnectEndpoint(std::string&, std::uint16_t&) { return true; }
bool ShouldAttemptReconnect() { return true; }
bool ResolveAndConnectIPv4(const std::string&, std::uint16_t, std::string&) {
    resolveEntered.store(true);
    if (blockResolve.load()) Wait(releaseResolve);
    std::scoped_lock lock(g_socketMutex);
    assert(g_winsockInitialized && winsockReferences.load() == 1);
    assert(!assignedPlayer.load());
    assert(remotePlayersCleared.load() && worldResetRequested.load() && inventorySyncResetCalls.load() > 0 && objectSyncResetCalls.load() > 0 && settlementAccessResetCalls.load() > 0);
    g_socket = 1;
    return true;
}
void DisconnectFromLocalServer();
void ReceiveLoop() {
    g_isReceiveThread = true;
    const bool terminal = terminalMode.load();
    receiverEntered.store(true);
    if (terminal) {
        if (requirePriorDrain.load()) Wait(joinEntered);
        DisconnectFromLocalServer();
        receiverCleaned.store(true);
        terminalMode.store(false);
    } else {
        while (g_receiveThreadRunning.load()) std::this_thread::yield();
    }
    g_receiveThreadRunning.store(false);
}
#include "transport-source.inc"

void ResetFixture() {
    assert(!g_receiveThread.joinable());
    assert(!g_winsockInitialized && winsockReferences.load() == 0);
    joinEntered.store(false);
    terminalMode.store(false);
    receiverEntered.store(false);
    receiverCleaned.store(false);
    requirePriorDrain.store(false);
    blockResolve.store(false);
    resolveEntered.store(false);
    releaseResolve.store(false);
    holdFirstCleanup.store(false);
    cleanupEntered.store(false);
    releaseCleanup.store(false);
    cleanupCalls.store(0);
    startupCalls.store(0);
    welcomeRead.store(false);
    assignedPlayer.store(false);
    remotePlayersCleared.store(false);
    worldResetRequested.store(false);
    inventorySyncResetCalls.store(0);
}
int main() {
    ResetFixture();
    holdFirstCleanup.store(true);
    std::thread first(DisconnectFromLocalServer);
    Wait(cleanupEntered);
    std::atomic_bool secondEntered{false};
    std::thread second([&] { secondEntered.store(true); DisconnectFromLocalServer(); });
    Wait(secondEntered);
    std::this_thread::sleep_for(100ms);
    assert(cleanupCalls.load() == 1);
    releaseCleanup.store(true);
    first.join();
    second.join();
    assert(cleanupCalls.load() == 2);
    assert(inventorySyncResetCalls.load() >= 2);
    assert(settlementAccessResetCalls.load() >= 2);

    ResetFixture();
    blockResolve.store(true);
    std::thread connector([] { std::string error; assert(ConnectToServerInternal("fixture", 7777, error)); });
    Wait(resolveEntered);
    secondEntered.store(false);
    std::thread disconnector([&] { secondEntered.store(true); DisconnectFromLocalServer(); });
    Wait(secondEntered);
    std::this_thread::sleep_for(100ms);
    assert(cleanupCalls.load() == 0 && winsockReferences.load() == 1);
    releaseResolve.store(true);
    connector.join();
    disconnector.join();
    assert(GetSocket() == INVALID_SOCKET && !g_receiveThread.joinable());

    ResetFixture();
    assert(EnsureWinsockInitialized());
    terminalMode.store(true);
    requirePriorDrain.store(true);
    StartReceiveThread();
    Wait(receiverEntered);
    std::string error;
    assert(ConnectToServerInternal("fixture", 7777, error));
    assert(receiverCleaned.load() && startupCalls.load() == 2);
    DisconnectFromLocalServer();

    ResetFixture();
    terminalMode.store(true);
    assert(!ConnectAndValidateServer("fixture", 7777, error));
    Wait(receiverCleaned);
    DisconnectFromLocalServer();
    assert(GetSocket() == INVALID_SOCKET && !g_receiveThread.joinable());
    assert(!g_winsockInitialized && winsockReferences.load() == 0);

    ResetFixture();
    std::atomic_bool validationFinished{false};
    std::thread waiter([&] {
        std::string waitError;
        assert(!ConnectAndValidateServer("fixture", 7777, waitError));
        validationFinished.store(true);
    });
    Wait(welcomeRead);
    const auto cancelStart = std::chrono::steady_clock::now();
    DisconnectFromLocalServer();
    assert(std::chrono::steady_clock::now() - cancelStart < 1s);
    Wait(validationFinished);
    waiter.join();

    ResetFixture();
    validationFinished.store(false);
    std::thread staleWaiter([&] {
        std::string waitError;
        assert(!ConnectAndValidateServer("fixture", 7777, waitError));
        validationFinished.store(true);
    });
    Wait(welcomeRead);
    {
        const std::scoped_lock lock(g_connectionOperationMutex);
        CloseSocket();
        assert(ConnectToServerInternal("replacement", 7777, error));
        assignedPlayer.store(true);
    }
    Wait(validationFinished);
    staleWaiter.join();
    assert(IsConnectedToServer() && g_receiveThread.joinable() && winsockReferences.load() == 1);
    DisconnectFromLocalServer();

    ResetFixture();
    assignedPlayer.store(true);
    std::string staleIdentityError;
    assert(ConnectToServerInternal("stale-identity", 7777, staleIdentityError));
    assert(!assignedPlayer.load());
    DisconnectFromLocalServer();

    ResetFixture();
    assert(!ConnectAndValidateServer("fixture", 7777, error));
    assert(GetSocket() == INVALID_SOCKET && !g_receiveThread.joinable());
    assert(!g_winsockInitialized && winsockReferences.load() == 0);
}
