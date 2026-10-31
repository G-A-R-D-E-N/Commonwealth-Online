from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CLANG = shutil.which("clang++") or "C:/Program Files/LLVM/bin/clang++.exe"


def main():
    source = (ROOT / "CommonwealthOnlineUI/src/F4TMainMenuController.cpp").read_text(encoding="utf-8")
    start = source.index("\tvoid MainMenuOnThumbstickEventHook(")
    end = source.index("\n}", start)
    body = source[start:end]
    harness = "\n".join(("", "#include <cassert>", "#include <cstdint>", "")) + r'''namespace RE { struct BSInputEventUser {}; struct ThumbstickEvent {}; }
int forwarded = 0, converted = 0;
bool browserVisible = true, focused = true, foreground = true;
namespace F4T::PrismaUI {
bool IsCustomMenuInputCaptured() { return true; }
bool IsBrowserVisible() { return browserVisible; }
bool IsGameForeground() { return foreground; }
bool IsBrowserControllerFocused() { return browserVisible && focused && foreground; }
}
struct API { std::uint64_t GetFocusedView() { return focused ? 7 : 0; } } api;
namespace F4T::PrismaCapabilities { auto* GetV12() { return &api; } }
namespace F4T::MenuInput { void ForwardThumbstickEvent(const RE::ThumbstickEvent*) { ++forwarded; } }
using MainMenuOnThumbstickEventFn = void (*)(RE::BSInputEventUser*, const RE::ThumbstickEvent*);
void Convert(RE::BSInputEventUser*, const RE::ThumbstickEvent*) { ++converted; }
std::uintptr_t g_originalMainMenuOnThumbstickEvent = reinterpret_cast<std::uintptr_t>(&Convert);
'''
    checks = r'''
int main() {
    RE::BSInputEventUser user;
    RE::ThumbstickEvent stick;
    MainMenuOnThumbstickEventHook(&user, &stick);
    assert(converted == 1 && forwarded == 0);
    browserVisible = false;
    MainMenuOnThumbstickEventHook(&user, &stick);
    assert(converted == 1 && forwarded == 1);
    browserVisible = true;
    focused = false;
    MainMenuOnThumbstickEventHook(&user, &stick);
    assert(converted == 1 && forwarded == 2);
    focused = true;
    foreground = false;
    MainMenuOnThumbstickEventHook(&user, &stick);
    assert(converted == 1 && forwarded == 3);
}
'''
    menu = (ROOT / "CommonwealthOnlineUI/src/F4TMenuInput.cpp").read_text(encoding="utf-8")
    owned = "g_browserActive.load() && F4T::PrismaUI::IsBrowserControllerInputOwned()"
    focused = "g_browserActive.load() && F4T::PrismaUI::IsBrowserControllerFocused()"
    start = menu.index("bool ForwardButtonEvent(")
    assert owned in menu[start:start + 200] and "g_browserActive.load() ||" not in menu[start:start + 200], "buttons use Owned"
    start = menu.index("void PollGamepadInternal(")
    poll = menu[start:menu.index("bool ForwardButtonEvent(", start)]
    assert f"const bool browserFocused = {focused};" in poll and f"const bool browserOwned = {owned};" in poll, "poll derives Focused and Owned"
    assert "g_browserActive.load() ||" not in poll, "poll does not gate on bare browser activity"
    assert "if (browserOwned || !AnyMenuActive()" in poll, "owned poll returns before buttons and stick"
    assert poll.index("if (browserFocused) {") < poll.index("const auto* stickButton = StickAxesToButton("), "focused poll skips stick-to-dpad"
    assert "g_browserOwnedLastPoll && !browserOwned && g_browserActive.load()" in poll, "baseline resync on ownership drop"
    assert poll.index("SyncPolledGamepadBaseline();") < poll.index("g_browserOwnedLastPoll = browserOwned;") < poll.index("if (browserOwned ||"), "resync before polling resumes"
    start = menu.index("bool ForwardThumbstickEvent(")
    assert "g_browserActive" not in menu[start:start + 200]
    lifecycle = (ROOT / "CommonwealthOnlineUI/src/F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")
    start = lifecycle.index("bool IsBrowserControllerFocused()")
    assert "HasFocus(view)" in lifecycle[start:start + 300], "IsBrowserControllerFocused"
    start = lifecycle.index("bool IsBrowserControllerInputOwned()")
    assert "return IsBrowserControllerFocused() && AreBrowserControllerActionsReady();" in lifecycle[start:start + 200], "Owned = Focused && Ready"
    assert "IsBrowserControllerFocused()" in body and "IsBrowserControllerInputOwned" not in body, "hook uses Focused"
    with tempfile.TemporaryDirectory(prefix="co-controller-routing-") as temp:
        temp = Path(temp)
        cpp, exe = temp / "routing.cpp", temp / "routing.exe"
        cpp.write_text(harness + body + checks, encoding="utf-8")
        subprocess.run([CLANG, "-std=c++23", "-UNDEBUG", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
    print("Browser thumbstick routing behavior passed")


if __name__ == "__main__":
    main()
