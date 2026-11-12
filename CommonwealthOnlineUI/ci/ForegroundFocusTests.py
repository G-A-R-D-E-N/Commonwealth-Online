from pathlib import Path
import subprocess
import sys
import tempfile

if sys.platform != "win32":
    raise SystemExit("Game client tests can only run on a Windows host")

source = (Path(__file__).resolve().parents[1] / "src/F4TPrismaViewState.cpp").read_text(encoding="utf-8")
start = source.index("\tbool RestoreLastFocusedView() noexcept")
end = source.index("\n\tstatic bool s_haveAppliedCursorPolicy", start)
implementation = source[start:end]
MENU = (Path(__file__).resolve().parents[1] / "src/F4TPrismaUI_Menu.cpp").read_text(encoding="utf-8")
BROWSER = (Path(__file__).resolve().parents[1] / "src/F4TPrismaUI_Browser.cpp").read_text(encoding="utf-8")
INTERNAL = (Path(__file__).resolve().parents[1] / "src/F4TPrismaUIInternal.h").read_text(encoding="utf-8")
# Invariant: the vanilla pause menu must stay live until the CO view has focus, and the CO view must not blur an open Pip-Boy.
assert "SetVanillaPauseMenuSuppressed(true);" in MENU and MENU.count("SetVanillaPauseMenuSuppressed(true);") == 1
assert "!g_pauseMenuState.IsCurrentlyFocused()" in MENU and "SuppressVanillaPauseIfFocused();" in MENU
assert "if (F4T::PrismaUI::IsPipboyOpen())" in MENU and "g_pauseShowDeferredForPipboy" in MENU
assert 'ApplyPauseMenuState("PauseFocusRetry")' in MENU and "g_lastFocusedView" not in MENU
assert "TickPauseMenuFocusGuard();" in BROWSER and "VanillaMenuUnfocusedTooLong(unfocused, unfocusedSince" in BROWSER
assert "kVanillaMenuUnfocusedLimit{ 2 }" in (Path(__file__).resolve().parents[1] / "include/F4TMenuWatchdog.h").read_text(encoding="utf-8")
harness = chr(35) + r"""include <cassert>
namespace F4T::PrismaCapabilities {
    bool active = false;
    bool HasAnyActiveFocus() { return active; }
}
namespace F4T::PrismaViewState {
    struct Controller {
        bool desiredVisible = true, desiredFocused = true, visible = true, valid = true, focused = false;
        int attempts = 0;
        bool IsDesiredVisible() const { return desiredVisible; }
        bool IsDesiredFocused() const { return desiredFocused; }
        bool IsCurrentlyVisible() const { return visible; }
        bool IsHandleValid() const { return valid; }
        bool IsCurrentlyFocused() const { return focused; }
        void ApplyState(const char*) { ++attempts; focused = true; }
    };
    Controller* g_lastFocusedView = nullptr;
""" + implementation + r"""
}
int main() {
    using namespace F4T::PrismaViewState;
    Controller view;
    assert(!RestoreLastFocusedView());
    g_lastFocusedView = &view;
    assert(RestoreLastFocusedView() && view.attempts == 1);
    assert(RestoreLastFocusedView() && view.attempts == 1);
    view.focused = false;
    assert(RestoreLastFocusedView() && view.attempts == 2);
    view.focused = false;
    F4T::PrismaCapabilities::active = true;
    assert(!RestoreLastFocusedView() && view.attempts == 2);
    F4T::PrismaCapabilities::active = false;
    view.visible = false;
    assert(!RestoreLastFocusedView() && view.attempts == 2);
    view.visible = true; view.valid = false;
    assert(!RestoreLastFocusedView() && view.attempts == 2);
    view.valid = true; view.desiredFocused = false;
    assert(!RestoreLastFocusedView() && view.attempts == 2);
    view.desiredFocused = true; view.desiredVisible = false;
    assert(!RestoreLastFocusedView() && view.attempts == 2);
}
"""
with tempfile.TemporaryDirectory(prefix="co-foreground-focus-") as temp:
    root = Path(temp)
    (root / "focus.cpp").write_text(harness, encoding="utf-8")
    (root / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.16)\nproject(focus LANGUAGES CXX)\n"
        "add_executable(focus focus.cpp)\ntarget_compile_features(focus PRIVATE cxx_std_17)\n"
        "target_compile_options(focus PRIVATE /UNDEBUG)\n", encoding="utf-8")
    subprocess.run(["cmake", "-S", str(root), "-B", str(root / "build"), "-A", "x64"], check=True)
    subprocess.run(["cmake", "--build", str(root / "build"), "--config", "Release"], check=True)
    subprocess.run([str(root / "build/Release/focus.exe")], check=True)
print("foreground focus recovery: PASS")
