#include <atomic>
#include <cassert>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace PRISMA_UI_API {
enum class RuntimeState { Unavailable, Waiting, Starting, Ready, Failed };
enum class ViewRole { kPanel };
RuntimeState state = RuntimeState::Waiting;
RuntimeState GetRuntimeState() { return state; }
}
namespace RE {
struct MainMenu {};
struct UI {
    bool open = false;
    static UI* GetSingleton() { static UI ui; return &ui; }
    template<class T> bool GetMenuOpen() const { return open; }
};
}
namespace F4SE {
struct Tasks {
    mutable std::vector<std::function<void()>> permanent;
    void AddTaskPermanent(std::function<void()> fn) const { permanent.push_back(fn); }
};
const Tasks* GetTaskInterface() { static Tasks tasks; return &tasks; }
struct Version { unsigned short major() const { return 0; } unsigned short minor() const { return 7; } };
Version GetF4SEVersion() { return {}; }
}
namespace F4T::F4SECapabilities {
template <class T, class V> void SchedulePermanentTask(const T* a_tasks, const V&, std::function<void()> a_task) { a_tasks->AddTaskPermanent(std::move(a_task)); }
}
namespace F4T::PrismaCapabilities {
int api;
int* Acquire() { return &api; }
}
namespace F4T::PrismaUI::Internal {
bool g_prismaReady = false;
std::atomic<bool> g_mainMenuBridgeReady{ false };
int* g_api = nullptr;
int created = 0, bound = 0, reconciled = 0;
struct MenuState {
    bool valid = false, dom = false, visible = false;
    bool IsDomReady() const { return dom; }
    bool IsHandleValid() const { return valid; }
    int GetHandle() const { return valid ? 1 : 0; }
    void SetApi(int*) {}
    void SetDesiredRole(PRISMA_UI_API::ViewRole) {}
} g_mainMenuState;
void LogInfo(std::string_view) {}
void LogWarning(std::string_view) {}
void BindMainMenuDomHandlers(int) { if (g_mainMenuBridgeReady.exchange(true)) return; ++bound; }
void ReconcileMainMenuOverlayState(std::string_view) { ++reconciled; }
void InstallMainMenuCursorReleaseHook() {}
void CreateMainMenuView() {
    if (g_mainMenuState.valid) return;
    ++created;
    g_mainMenuState.valid = true;
}
}

#include "F4TPrismaUI_Startup.cpp"

int main() {
    using namespace F4T::PrismaUI::Internal;
    using PRISMA_UI_API::RuntimeState;
    F4T::PrismaUI::PrepareMenuRuntime();
    F4T::PrismaUI::PrepareMenuRuntime();
    const auto* tasks = F4SE::GetTaskInterface();
    assert(tasks->permanent.size() == 1);
    auto tick = tasks->permanent.front();
    RE::UI::GetSingleton()->open = true;
    for (auto state : {RuntimeState::Waiting, RuntimeState::Starting, RuntimeState::Failed, RuntimeState::Unavailable}) {
        PRISMA_UI_API::state = state;
        tick();
        assert(created == 0);
    }
    PRISMA_UI_API::state = RuntimeState::Ready;
    RE::UI::GetSingleton()->open = false;
    tick(); assert(created == 0);
    RE::UI::GetSingleton()->open = true;
    tick(); tick();
    assert(created == 1 && !g_mainMenuState.visible);
    g_mainMenuState.dom = true;
    TryFinalizeMainMenu(); assert(bound == 0 && reconciled == 0);
    g_menuServicesReady = true;
    TryFinalizeMainMenu(); TryFinalizeMainMenu();
    assert(bound == 1 && reconciled == 2);
    g_mainMenuBridgeReady = false; bound = reconciled = 0;
    g_mainMenuState.dom = false;
    TryFinalizeMainMenu(); assert(bound == 0 && reconciled == 0);
    g_mainMenuState.dom = true;
    TryFinalizeMainMenu(); TryFinalizeMainMenu(); assert(bound == 1);
    g_mainMenuState.valid = false;
    TryFinalizeMainMenu(); assert(bound == 1);
    PRISMA_UI_API::state = RuntimeState::Unavailable;
    g_prismaReady = false; assert(!IsMenuRuntimeReady());
    g_prismaReady = true; assert(IsMenuRuntimeReady());
    for (auto state : {RuntimeState::Waiting, RuntimeState::Starting, RuntimeState::Failed}) {
        PRISMA_UI_API::state = state;
        assert(!IsMenuRuntimeReady());
    }
    PRISMA_UI_API::state = RuntimeState::Ready;
    assert(IsMenuRuntimeReady());
    PRISMA_UI_API::state = RuntimeState::Failed;
    g_prismaReady = false; assert(!IsMenuRuntimeReady());
    g_prismaReady = true; assert(IsMenuRuntimeReady());
    assert(UsesLegacyMenuReadiness(RuntimeState::Failed) && UsesLegacyMenuReadiness(RuntimeState::Unavailable));
    assert(!UsesLegacyMenuReadiness(RuntimeState::Waiting) && !UsesLegacyMenuReadiness(RuntimeState::Ready));
}
