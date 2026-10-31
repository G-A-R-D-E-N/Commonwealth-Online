#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "F4TConnectFailure.h"
#include "F4TSteamJoinGate.h"
#include "api-types.inc"

using namespace std::chrono_literals;
std::mutex fixtureMutex;
std::condition_variable fixtureCondition;
bool connectEntered = false;
bool releaseConnect = false;
bool connectSucceeds = true;
std::atomic_bool liveConnection{false};
std::atomic_int disconnectCalls{0};
std::deque<std::function<void()>> queuedTasks;
int launchCalls = 0;
int completionCalls = 0;
bool completionSuccess = false;
std::vector<std::string> cancelOrder;
bool pendingLaunchCancelled = false;
bool deferredLaunchCancelled = false;

namespace F4SE {
struct TaskInterface {
    void AddTask(std::function<void()> task) const {
        std::lock_guard lock(fixtureMutex);
        queuedTasks.push_back(std::move(task));
        fixtureCondition.notify_all();
    }
};
TaskInterface taskInterface;
const TaskInterface* GetTaskInterface() { return &taskInterface; }
}
namespace F4T::DebugLogging {
template<class... Args> void Log(Args&&...) {}
std::string SanitizeLogText(const std::string& value, std::size_t = 0) { return value; }
}
namespace F4T::Networking {
bool ConnectAndValidateServer(const char*, std::uint16_t, std::string& error,
    std::optional<ConnectFailure>*, const std::optional<std::string>&, const std::string&) {
    std::unique_lock lock(fixtureMutex);
    connectEntered = true;
    fixtureCondition.notify_all();
    assert(fixtureCondition.wait_for(lock, 2s, [] { return releaseConnect; }));
    liveConnection.store(connectSucceeds);
    if (!connectSucceeds) error = "Handshake failed.";
    return connectSucceeds;
}
void DisconnectFromLocalServer() { ++disconnectCalls; liveConnection.store(false); }
bool IsConnectedToServer() { return liveConnection.load(); }
bool ConnectToLocalServer(std::string&) { liveConnection.store(true); return true; }
}
namespace F4T::ServerProfileStore {
std::string ProfileServerKey(const std::string&, std::uint16_t, const std::string&) { return "fixture"; }
}
namespace F4T::SteamSession {
void CancelJoins() { cancelOrder.push_back("steam-cancel"); }
}

namespace F4T::SessionLaunch {
bool CancelPendingJoin(std::string_view) {
    cancelOrder.push_back("pending-launch-cancel");
    return pendingLaunchCancelled;
}
LaunchOutcome LaunchValidatedSession(const JoinRequest&, std::string&, JoinCompletion) {
    ++launchCalls;
    return LaunchOutcome::Complete;
}
}
void ShowHandshakeRejectionNotification(const char*, const char*, const char*, const char*) {}
namespace F4T::TextEntryMode { void Reset() {} }
namespace F4T::SessionSaveGuard {
bool sessionExitPending = false;
std::function<void()> deferredSessionExit{};
bool IsSessionExitPending() { return sessionExitPending; }
void RunOrDeferSessionExit(std::string_view, std::function<void()> a_proceed) {
    if (sessionExitPending) return;
    sessionExitPending = true;
    deferredSessionExit = std::move(a_proceed);
}
void SettleSessionExit() {
    auto proceed = std::move(deferredSessionExit);
    deferredSessionExit = nullptr;
    sessionExitPending = false;
    proceed();
}
}
namespace F4T::CharacterSlotsClient { bool CancelDeferredLaunch(std::string_view = {}) { return deferredLaunchCancelled; } }
namespace F4T::ModpackAutoFixClient {
void NoteEndpoint(const std::string&, const std::string&, std::uint16_t, const std::string&,
    const std::string&, const std::optional<std::string>&) {}
}
namespace F4T::UIClient {
void PushConnectionStatus(const char* = nullptr) {}
#include "ui-source.inc"
}

void VerifySteamJoinGate() {
    using F4T::SteamJoinGate::Gate;
    using F4T::SteamJoinGate::Has;
    using F4T::SteamJoinGate::Operation;

    Gate gate;
    const auto firstEpoch = gate.Epoch();
    assert(gate.IsCurrent(firstEpoch));
    assert(gate.Pending() == Operation::None);
    assert(!Has(Operation::None, Operation::Join));

    const auto joinEpoch = gate.Begin(Operation::Join);
    const auto hostEpoch = gate.Begin(Operation::HostStart);
    assert(joinEpoch == firstEpoch && hostEpoch == firstEpoch);
    assert(Has(gate.Pending(), Operation::Join));
    assert(Has(gate.Pending(), Operation::HostStart));

    assert(gate.Finish(Operation::Join, joinEpoch));
    assert(!Has(gate.Pending(), Operation::Join));
    assert(Has(gate.Pending(), Operation::HostStart));

    const auto cancelled = gate.Cancel();
    assert(cancelled == Operation::HostStart);
    assert(!gate.IsCurrent(firstEpoch));
    assert(gate.Pending() == Operation::None);
    assert(gate.Cancel() == Operation::None);

    const auto secondEpoch = gate.Epoch();
    assert(gate.IsCurrent(secondEpoch));
    const auto freshEpoch = gate.Begin(Operation::HostStart);
    assert(freshEpoch == secondEpoch);
    assert(!gate.Finish(Operation::HostStart, firstEpoch));
    assert(Has(gate.Pending(), Operation::HostStart));
    assert(gate.Finish(Operation::HostStart, freshEpoch));
    assert(gate.Pending() == Operation::None);

    const auto lateEpoch = gate.Begin(Operation::Join);
    assert(gate.Finish(Operation::Join, lateEpoch));
    assert(gate.Pending() == Operation::None);
}

void Complete(bool success, const char*, void*) {
    ++completionCalls;
    completionSuccess = success;
}
void WaitFor(const std::function<bool()>& predicate) {
    std::unique_lock lock(fixtureMutex);
    assert(fixtureCondition.wait_for(lock, 2s, predicate));
}
void Publish() {
    {
        std::lock_guard lock(fixtureMutex);
        releaseConnect = true;
        fixtureCondition.notify_all();
    }
    WaitFor([] { return !queuedTasks.empty(); });
}
std::function<void()> TakeTask(bool a_newest) {
    std::lock_guard lock(fixtureMutex);
    assert(queuedTasks.size() == 1 || (a_newest && queuedTasks.size() == 2));
    auto task = std::move(a_newest ? queuedTasks.back() : queuedTasks.front());
    a_newest ? queuedTasks.pop_back() : queuedTasks.pop_front();
    return task;
}
void AssertCancelOrder() {
    assert(cancelOrder.size() == 2);
    assert(cancelOrder[0] == "steam-cancel");
    assert(cancelOrder[1] == "pending-launch-cancel");
}
void Disconnect() {
    cancelOrder.clear();
    F4T::UIClient::Callback_Disconnect();
    TakeTask(true)();
}
void RunCompletion() {
    auto task = TakeTask(false);
    assert(task);
    task();
    assert(completionCalls == 1);
    assert(!F4T::UIClient::g_joinInProgress.load());
}
void WaitForWorkerExit() {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (F4T::UIClient::g_joinWorkerRunning.load()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
void Begin(bool succeeds = true) {
    WaitForWorkerExit();
    {
        std::lock_guard lock(fixtureMutex);
        connectEntered = false;
        releaseConnect = false;
        connectSucceeds = succeeds;
    }
    liveConnection.store(false);
    disconnectCalls.store(0);
    F4T::UIClient::g_connectionOwnerToken.store(0);
    launchCalls = completionCalls = 0;
    completionSuccess = false;
    COMMONWEALTH_ONLINE_UI_API::JoinRequest request;
    request.host = "fixture";
    char error[128]{};
    assert(F4T::UIClient::Callback_JoinSession(&request, Complete, nullptr, error, sizeof(error)));
    WaitFor([] { return connectEntered; });
    assert(!F4T::UIClient::Callback_JoinSession(&request, nullptr, nullptr, error, sizeof(error)));
    assert(!F4T::UIClient::Callback_ConnectLocal(error, sizeof(error)));
}
int main() {
    VerifySteamJoinGate();

    Begin();
    Disconnect();
    AssertCancelOrder();
    Publish();
    assert(liveConnection.load());
    RunCompletion();
    assert(!liveConnection.load());
    assert(!completionSuccess && launchCalls == 0 && disconnectCalls.load() == 2);

    Begin();
    Publish();
    Disconnect();
    assert(cancelOrder.empty());
    assert(F4T::SessionSaveGuard::IsSessionExitPending() && liveConnection.load() && disconnectCalls.load() == 0);
    Disconnect();
    assert(liveConnection.load() && disconnectCalls.load() == 0);
    F4T::SessionSaveGuard::SettleSessionExit();
    AssertCancelOrder();
    RunCompletion();
    assert(!liveConnection.load());
    assert(!completionSuccess && launchCalls == 0 && disconnectCalls.load() == 1);

    Begin();
    Publish();
    F4T::UIClient::g_joinGeneration.fetch_add(1);
    F4T::UIClient::g_connectionOwnerToken.store(F4T::UIClient::g_connectionTokenCounter.load() + 1);
    RunCompletion();
    assert(liveConnection.load());
    assert(!completionSuccess && launchCalls == 0 && disconnectCalls.load() == 0);

    Begin();
    Publish();
    RunCompletion();
    assert(liveConnection.load());
    assert(completionSuccess && launchCalls == 1 && disconnectCalls.load() == 0);

    Begin();
    Publish();
    liveConnection.store(false);
    RunCompletion();
    assert(!completionSuccess && launchCalls == 0 && disconnectCalls.load() == 0);
    assert(F4T::UIClient::g_connectionOwnerToken.load() == 0);

    Begin(false);
    Publish();
    RunCompletion();
    assert(!liveConnection.load());
    assert(!completionSuccess && launchCalls == 0 && disconnectCalls.load() == 0);

    Begin();
    pendingLaunchCancelled = true;
    Disconnect();
    AssertCancelOrder();
    assert(!F4T::UIClient::g_joinInProgress.load());
    pendingLaunchCancelled = false;
    Publish();
    RunCompletion();
    assert(!completionSuccess);

    Begin();
    deferredLaunchCancelled = true;
    Disconnect();
    assert(!F4T::UIClient::g_joinInProgress.load());
    deferredLaunchCancelled = false;
    Publish();
    RunCompletion();
    assert(!completionSuccess);
}
