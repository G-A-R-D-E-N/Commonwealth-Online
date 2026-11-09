import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def make_harness():
    source = (Path(__file__).resolve().parents[1] / "src/F4TMainMenuHooks.cpp").read_text(encoding="utf-8")
    start = source.index("\t\tRE::UI_MESSAGE_RESULTS MainMenuProcessMessageHook(")
    end = source.index("\n\t\tvoid MainMenuOnButtonEventHook", start)
    install_start = source.index("\tbool InstallMainMenuInputHooks()")
    install_end = source.index("\n\tbool AreMainMenuInputHooksInstalled()", install_start)
    includes = "".join(f"\x23include <{name}>\n" for name in ("cassert", "cstdint", "initializer_list", "string_view"))
    return includes + r"""
namespace RE {
enum class UI_MESSAGE_TYPE { kUpdate, kShow, kReshow, kHide, kForceHide, kScaleformEvent,
    kUserEvent, kInventoryUpdate, kUserProfileChange, kMUStatusChange, kResumeCaching,
    kUpdateController, kChatterEvent };
enum class UI_MESSAGE_RESULTS { kHandled, kPassOn, kIgnore };
struct IMenu {};
struct MainMenu { static constexpr int VTABLE /* E:/RE-work/co-main-menu-input-20261103.md */ [] = {0, 1}; };
struct MessageType {
    UI_MESSAGE_TYPE value;
    UI_MESSAGE_TYPE operator*() const { return value; }
};
struct UIMessage { MessageType type; };
}
bool visible = false;
namespace F4T::PrismaUI { bool IsMainMenuVisible() { return visible; } }
int calls = 0;
RE::IMenu* expectedMenu;
RE::UIMessage* expectedMessage;
RE::UI_MESSAGE_RESULTS Original(RE::IMenu* menu, RE::UIMessage& message) {
    assert(menu == expectedMenu && &message == expectedMessage);
    ++calls;
    return RE::UI_MESSAGE_RESULTS::kIgnore;
}
using MainMenuProcessMessageFn = RE::UI_MESSAGE_RESULTS (*)(RE::IMenu*, RE::UIMessage&);
std::uintptr_t g_originalMainMenuProcessMessage = reinterpret_cast<std::uintptr_t>(&Original);
""" + source[start:end] + r"""
namespace REL { template<class T> struct Relocation { explicit Relocation(int) {} }; }
namespace REX { template<class T, class U> T UNRESTRICTED_CAST(U value) { return reinterpret_cast<T>(value); } }
bool g_mainMenuInputHooksInstalled = false;
std::uintptr_t g_originalMainMenuOnThumbstickEvent = 0, g_originalMainMenuOnCursorMoveEvent = 0;
std::uintptr_t g_originalMainMenuOnMouseMoveEvent = 0, g_originalMainMenuOnButtonEvent = 0;
constexpr int kBSInputOnThumbstickVtableIndex = 4, kBSInputOnCursorMoveVtableIndex = 5;
constexpr int kBSInputOnMouseMoveVtableIndex = 6, kBSInputOnButtonVtableIndex = 8;
void MainMenuOnThumbstickEventHook() {}
void MainMenuOnCursorMoveEventHook() {}
void MainMenuOnMouseMoveEventHook() {}
void MainMenuOnButtonEventHook() {}
void LogWarning(const char*) {}
void LogInfo(const char*) {}
bool buttonHookFails = false, messageHookFails = false;
int messageHookAttempts = 0, hookAttempts = 0;
namespace VtableHookGuard {
std::uintptr_t TryWriteVfunc(REL::Relocation<std::uintptr_t>&, int, std::uintptr_t, std::string_view name) {
    ++hookAttempts;
    if (name == "MainMenu button" && buttonHookFails) return 0;
    if (name == "MainMenu ProcessMessage") {
        ++messageHookAttempts;
        if (messageHookFails) return 0;
    }
    return reinterpret_cast<std::uintptr_t>(&Original);
}
}
""" + source[install_start:install_end] + r"""
int main() {
    RE::IMenu menu;
    expectedMenu = &menu;
    for (bool open : {false, true, false, true, false}) {
        visible = open;
        for (int type = 0; type <= static_cast<int>(RE::UI_MESSAGE_TYPE::kChatterEvent); ++type) {
            auto event = static_cast<RE::UI_MESSAGE_TYPE>(type);
            RE::UIMessage message{{event}};
            expectedMessage = &message;
            calls = 0;
            const auto result = MainMenuProcessMessageHook(&menu, message);
            const bool blocked = open && event == RE::UI_MESSAGE_TYPE::kScaleformEvent;
            assert(calls == (blocked ? 0 : 1));
            assert(result == (blocked ? RE::UI_MESSAGE_RESULTS::kHandled : RE::UI_MESSAGE_RESULTS::kIgnore));
        }
    }
    buttonHookFails = true;
    assert(!InstallMainMenuInputHooks());
    assert(!g_mainMenuInputHooksInstalled && messageHookAttempts == 0);
    buttonHookFails = false;
    messageHookFails = true;
    assert(!InstallMainMenuInputHooks());
    assert(!g_mainMenuInputHooksInstalled && messageHookAttempts == 1);
    messageHookFails = false;
    assert(InstallMainMenuInputHooks());
    assert(g_mainMenuInputHooksInstalled && messageHookAttempts == 2);
    const int installedAttempts = hookAttempts;
    assert(InstallMainMenuInputHooks());
    assert(hookAttempts == installedAttempts);
}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check-harness", action="store_true")
    args = parser.parse_args()
    harness = make_harness()
    if args.check_harness:
        print("MainMenu message harness extracted successfully; native test not run")
        return
    if sys.platform != "win32":
        raise SystemExit("Game client tests can only run on a Windows host")
    with tempfile.TemporaryDirectory(prefix="co-menu-message-") as temporary:
        directory = Path(temporary)
        (directory / "message.cpp").write_text(harness, encoding="utf-8")
        (directory / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(MainMenuMessageTest LANGUAGES CXX)\n'
            'add_executable(message-test message.cpp)\n'
            'target_compile_features(message-test PRIVATE cxx_std_20)\n'
            'target_compile_options(message-test PRIVATE /UNDEBUG)\n', encoding="utf-8")
        subprocess.run(["cmake", "-S", str(directory), "-B", str(directory / "build"), "-A", "x64"], check=True)
        subprocess.run(["cmake", "--build", str(directory / "build"), "--config", "Release"], check=True)
        subprocess.run([str(directory / "build/Release/message-test.exe")], check=True)
    print("MainMenu message routing and close/reopen restoration PASS")


if __name__ == "__main__":
    main()
