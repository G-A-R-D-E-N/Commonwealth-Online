#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;
enum { kResumed, kRetry, kStop, kRejoin };
std::vector<int> g_attempts;
std::size_t g_attemptCalls = 0;
std::function<void()> g_onAttempt;
std::vector<std::string> g_joinErrors;
std::size_t g_joinCalls = 0;
bool g_voluntaryExit = false, g_joinInProgress = false;
int g_notifies = 0, g_releases = 0, g_tradeResets = 0;
std::string g_lastNotify;
namespace F4T::Secret { inline void Wipe(std::string& a_secret) { a_secret.assign(a_secret.size(), '\0'); } }
namespace F4T::DebugLogging { template<class... T> void Log(T&&...) {} }
namespace F4T::RemotePlayerState { std::optional<std::uint32_t> GetAssignedPlayerId() { return 7; } }
namespace F4T::TradeClient { void Reset() { ++g_tradeResets; } }
namespace F4T::JoinFlow { void ReleaseVaultExitControls(std::string_view, bool = false) { ++g_releases; } }
namespace F4SE {
struct TaskInterface { void AddTask(std::function<void()> a_task) const { a_task(); } };
const TaskInterface* GetTaskInterface() { static const TaskInterface task; return &task; }
}
namespace COMMONWEALTH_ONLINE_UI_API {
struct JoinRequest { const char* id{}; const char* name{}; const char* host{}; std::uint16_t port{ 7777 }; bool passworded{}; };
using JoinCompleteFn = void (*)(bool, const char*, void*);
}
namespace F4T::UIClient {
bool IsVoluntaryExitRequested() { return g_voluntaryExit; }
bool IsJoinInProgress() { return g_joinInProgress; }
void PushConnectionStatus(const char* = nullptr) {}
void NotifyTransportDisconnected(const char* a_reason = nullptr) { ++g_notifies; g_lastNotify = a_reason ? a_reason : ""; }
bool BeginJoin(const COMMONWEALTH_ONLINE_UI_API::JoinRequest*, std::optional<std::string>, COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn, void*, char* a_errorBuf, std::size_t a_errorBufSize) {
    assert(g_joinCalls < g_joinErrors.size());
    const auto& error = g_joinErrors[g_joinCalls++];
    std::snprintf(a_errorBuf, a_errorBufSize, "%s", error.c_str());
    return error.empty();
}
}
#include "reconnect-source.inc"
namespace F4T::Reconnect {
namespace {
Attempt TryResumeOnce(Target&, std::optional<std::uint32_t>, Clock::time_point) {
    assert(!g_attempts.empty());
    const auto outcome = g_attempts[std::min(g_attemptCalls, g_attempts.size() - 1)];
    ++g_attemptCalls;
    if (g_onAttempt) g_onAttempt();
    return outcome == kResumed ? Attempt::resumed : outcome == kRetry ? Attempt::retry : outcome == kStop ? Attempt::stop : Attempt::rejoin;
}
}
std::chrono::milliseconds RunForTest(std::chrono::milliseconds a_sinceLost) {
    Finish();
    Rearm();
    const auto begin = Clock::now();
    RunWindow(Target{ "host", 1, "id", std::nullopt, std::nullopt }, 7, begin - a_sinceLost);
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin);
}
}
namespace {
std::chrono::milliseconds Run(std::chrono::milliseconds a_sinceLost, std::vector<int> a_attempts, std::vector<std::string> a_joinErrors = {}, std::function<void()> a_onAttempt = {}, bool a_joinInProgress = false) {
    g_attempts = std::move(a_attempts);
    g_joinErrors = std::move(a_joinErrors);
    g_onAttempt = std::move(a_onAttempt);
    g_attemptCalls = g_joinCalls = 0;
    g_notifies = g_releases = g_tradeResets = 0;
    g_voluntaryExit = false;
    g_joinInProgress = a_joinInProgress;
    g_lastNotify.clear();
    return F4T::Reconnect::RunForTest(a_sinceLost);
}
const std::string kTransient = "Finishing the previous disconnect; try again shortly.";
}
int main() {
    auto elapsed = Run(60000ms, { kRetry });
    assert(g_attemptCalls == 0 && g_notifies == 1 && g_lastNotify == "Could not reconnect to the server." && g_releases == 1 && g_tradeResets == 1 && !F4T::Reconnect::IsActive() && elapsed < 1000ms);

    elapsed = Run(54800ms, { kRetry });
    assert(g_attemptCalls == 1 && g_notifies == 1 && elapsed < 1500ms && !F4T::Reconnect::IsActive());

    Run(0ms, { kResumed });
    assert(g_attemptCalls == 1 && g_notifies == 0 && g_releases == 0 && g_tradeResets == 0);

    Run(0ms, { kStop });
    assert(g_attemptCalls == 1 && g_notifies == 1 && g_releases == 1 && !F4T::Reconnect::IsActive());

    elapsed = Run(0ms, { kRetry }, {}, [] { F4T::Reconnect::Cancel(); });
    assert(g_attemptCalls == 1 && g_notifies == 0 && g_releases == 1 && g_tradeResets == 1 && !F4T::Reconnect::IsActive() && elapsed < 1000ms);

    Run(0ms, { kRejoin }, {}, [] { g_voluntaryExit = true; });
    assert(g_attemptCalls == 1 && g_joinCalls == 0 && g_notifies == 0 && g_releases == 1 && g_tradeResets == 1 && !F4T::Reconnect::IsActive());

    Run(0ms, { kRejoin }, { "" });
    assert(g_joinCalls == 1 && g_notifies == 0 && !F4T::Reconnect::IsActive());

    elapsed = Run(0ms, { kRejoin }, { kTransient, "" });
    assert(g_joinCalls == 2 && g_notifies == 0 && elapsed >= 2900ms && elapsed < 6000ms && !F4T::Reconnect::IsActive());

    elapsed = Run(53000ms, { kRejoin }, { kTransient });
    assert(g_joinCalls == 1 && g_notifies == 1 && elapsed < 1500ms && !F4T::Reconnect::IsActive());

    Run(0ms, { kRejoin }, { "Server is full." });
    assert(g_joinCalls == 1 && g_notifies == 1 && !F4T::Reconnect::IsActive());

    Run(0ms, { kRejoin }, { "A join is already in progress." }, {}, true);
    assert(g_joinCalls == 1 && g_notifies == 0 && g_tradeResets == 1 && !F4T::Reconnect::IsActive());
}
