#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <format>
#include <future>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nlohmann {
struct json {
    std::string text;
    json() = default;
    json(const char* s) : text(s) {}
    explicit json(std::string s) : text(std::move(s)) {}
    static json array() { return {}; }
    static json parse(const std::string& s) { return json{s}; }
    json value(const char*, const json& fallback) const { return fallback; }
    std::size_t size() const { return 0; }
    std::string dump() const { return text; }
};
}
using AlertResponseFn = void (*)(const char*, const char*, void*);
struct AlertRequest { const char* id{}; const char* category{}; const char* title{}; };
struct PendingResponse { AlertResponseFn callback{}; void* userData{}; };
struct PendingShow { std::string json; PendingResponse response; std::string id; };
struct View {
    bool ready{};
    bool IsHandleValid() const { return true; }
    bool IsDomReady() const { return ready; }
    int GetHandle() const { return 1; }
    void SetDesiredVisible(bool) {}
    void SetDesiredFocused(bool) {}
    void SetDesired(bool, bool) {}
    void ApplyState(const char*) {}
    void ClearHandle() {}
    void SetApi(void*) {}
} g_viewState;
std::mutex g_mutex;
std::recursive_mutex g_deliveryMutex;
std::unordered_map<std::string, PendingResponse> g_responses;
std::vector<PendingShow> g_pendingShows;
std::string g_lastShownId;
std::atomic_bool g_hasActionable{false};
bool g_runtimeReady{false};
bool g_pointerFocusActive{false};
int g_hitRegion{};
std::mutex eventsMutex;
std::vector<std::string> events;
std::atomic_bool blockShow{false}, showEntered{false}, releaseShow{false};
void Wait(const std::atomic_bool& signal) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!signal.load()) { assert(std::chrono::steady_clock::now() < limit); std::this_thread::yield(); }
}
struct Api {
    void Invoke(int, const char* script) { std::scoped_lock lock(eventsMutex); events.emplace_back(script); }
} api;
Api* g_api = &api;
std::string MakeAutoId() { return "auto"; }
nlohmann::json BuildAlertJson(const AlertRequest& request, const std::string& id) { return nlohmann::json{id + ":" + request.title}; }
std::string BuildFallbackAlertJson(const std::string& id) { return id; }
void CopyIdOut(const std::string&, char*, std::size_t) {}
void LogInfo(const std::string&) {}
void LogWarn(const std::string&) {}
std::string EscapeForLog(const char* s) { return s; }
void CreateView() {}
void ApplyInputRegion(int) {}
void InvokeInteractionMode() {}
void InvokeShow(const std::string& json) {
    if (blockShow.load()) { showEntered.store(true); Wait(releaseShow); }
    std::scoped_lock lock(eventsMutex);
    events.push_back("show:" + json);
}
#include "alert-source.inc"
void FirstResponse(const char*, const char*, void*) {}
void LatestResponse(const char*, const char*, void*) {}
int main() {
    AlertRequest old{"same", "test", "old"};
    AlertRequest latest{"same", "test", "new"};
    assert(Show(old, nullptr, nullptr, nullptr, 0));
    Dismiss("same");
    g_viewState.ready = true;
    FlushPending();
    assert(events.empty() && g_pendingShows.empty() && g_responses.empty());
    g_viewState.ready = false;
    assert(Show(old, FirstResponse, nullptr, nullptr, 0));
    assert(Show(latest, LatestResponse, nullptr, nullptr, 0));
    assert(g_pendingShows.size() == 1);
    assert(g_responses.at("same").callback == LatestResponse);
    assert(g_pendingShows[0].response.callback == LatestResponse);
    g_viewState.ready = true;
    FlushPending();
    assert(events.size() == 1 && events[0] == "show:same:new");
    assert(g_responses.at("same").callback == LatestResponse);
    events.clear();
    g_viewState.ready = false;
    assert(Show(old, nullptr, nullptr, nullptr, 0));
    g_viewState.ready = true;
    blockShow.store(true);
    auto flush = std::async(std::launch::async, [] { FlushPending(); });
    Wait(showEntered);
    auto dismiss = std::async(std::launch::async, [] { Dismiss("same"); });
    assert(dismiss.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    releaseShow.store(true);
    flush.get();
    dismiss.get();
    assert(events.size() == 2 && events[0] == "show:same:old");
    assert(events[1].find("dismissCoAlert") != std::string::npos);
    assert(g_responses.empty());
    events.clear();
    g_viewState.ready = false;
    assert(Show(old, nullptr, nullptr, nullptr, 0));
    DismissAll();
    g_viewState.ready = true;
    FlushPending();
    assert(events.empty() && g_responses.empty() && g_pendingShows.empty());
    g_viewState.ready = false;
    assert(Show(old, nullptr, nullptr, nullptr, 0));
    Shutdown();
    assert(!g_api && g_responses.empty() && g_pendingShows.empty());
}
