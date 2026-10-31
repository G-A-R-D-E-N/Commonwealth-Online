from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "CommonwealthOnlineUI/src/F4TPrismaUI_Browser.cpp"
CLANG = shutil.which("clang++") or "C:/Program Files/LLVM/bin/clang++.exe"


def main():
    source = SOURCE.read_text(encoding="utf-8")
    start = source.find("\tstd::atomic<PrismaView> g_browserControllerView")
    end = source.find("\tvoid InitializeBrowserPointerMode()", start)
    assert start >= 0 and end > start, "browser V12 action ownership is missing"
    body = source[start:end]
    harness = "\n".join(("", "#include <atomic>", "#include <cassert>", "#include <chrono>", "#include <cstdint>", "#include <iterator>", "#include <string_view>", "")) + r'''using PrismaView = std::uint64_t;
namespace PRISMA_UI_API {
enum class ControllerActionBridgeState { Missing, Pending, Ready, Failed };
enum class CursorPolicy { Default, Hidden };
bool cursorRestored = false;
bool SetViewCursorPolicy(PrismaView, CursorPolicy policy) { cursorRestored = policy == CursorPolicy::Default; return true; }
struct IVPrismaUI12 {
    int calls = 0;
    int clearCalls = 0;
    int rejectAt = 0;
    bool ownsEscape = false;
    ControllerActionBridgeState state = ControllerActionBridgeState::Pending;
    bool IsValid(PrismaView view) { return view != 0; }
    bool BindControllerAction(PrismaView, const char*, const char*) { return ++calls != rejectAt; }
    void ClearControllerActions(PrismaView) { ++clearCalls; }
    void SetViewOwnsEscape(PrismaView, bool owns) { ownsEscape = owns; }
    ControllerActionBridgeState GetControllerActionBridgeState(PrismaView) { return state; }
};
}
PRISMA_UI_API::IVPrismaUI12 api;
namespace F4T::PrismaCapabilities { auto* GetV12() { return &api; } }
namespace F4T::PrismaViewState {
int cursorPolicyResets = 0;
void ResetViewCursorPolicyCache() { ++cursorPolicyResets; }
}
PrismaView activeView = 7;
PrismaView GetActiveBrowserView() { return activeView; }
void LogWarning(std::string_view) {}
'''
    checks = r'''
int main() {
    api.rejectAt = 2;
    EnsureBrowserControllerActions();
    assert(api.calls == 10);
    assert(api.clearCalls == 1);
    assert(!g_browserControllerActionsBound);
    api.rejectAt = 0;
    g_controllerRebindAfter = {};
    EnsureBrowserControllerActions();
    assert(api.calls == 20);
    assert(g_browserControllerActionsBound);
    assert(api.ownsEscape);
    assert(!AreBrowserControllerActionsReady());
    EnsureBrowserControllerActions();
    assert(api.calls == 20);
    api.state = PRISMA_UI_API::ControllerActionBridgeState::Ready;
    EnsureBrowserControllerActions();
    assert(g_controllerRebindAttempts == 0);
    assert(AreBrowserControllerActionsReady());
    activeView = 8;
    assert(!AreBrowserControllerActionsReady());
    activeView = 7;
    api.state = PRISMA_UI_API::ControllerActionBridgeState::Failed;
    EnsureBrowserControllerActions();
    assert(!g_browserControllerActionsBound);
    assert(api.clearCalls == 2);
    g_controllerRebindAttempts = 8;
    g_controllerRebindAfter = {};
    EnsureBrowserControllerActions();
    assert(api.calls == 20);
    ClearBrowserControllerActions();
    assert(!api.ownsEscape);
    assert(PRISMA_UI_API::cursorRestored);
    assert(F4T::PrismaViewState::cursorPolicyResets == 1);
    assert(g_browserControllerView == 0);
    activeView = 8;
    api.state = PRISMA_UI_API::ControllerActionBridgeState::Pending;
    EnsureBrowserControllerActions();
    assert(api.calls == 30);
    assert(g_browserControllerView == 8);
}
'''
    with tempfile.TemporaryDirectory(prefix="co-controller-") as temp:
        temp = Path(temp)
        cpp = temp / "bindings.cpp"
        exe = temp / "bindings.exe"
        cpp.write_text(harness + body + checks, encoding="utf-8")
        subprocess.run([
            CLANG, "-std=c++23", "-UNDEBUG",
            str(cpp), "-o", str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)
    print("Browser V12 binding behavior passed")


if __name__ == "__main__":
    main()
