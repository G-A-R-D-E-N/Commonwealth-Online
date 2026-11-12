from pathlib import Path
import sys
# RE offset source:commonlibf4-nomad/include/RE/B/BSInputEventUser.h, include/RE/M/MenuControls.h.

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"
UI = ROOT / "CommonwealthOnlineUI"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(relative):
    return (PLUGIN / relative).read_text(encoding="utf-8")


def read_ui(relative):
    return (UI / relative).read_text(encoding="utf-8")


def function_body(code, signature):
    start = code.index(signature)
    brace = code.index("{", start)
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[brace:index + 1]
    raise AssertionError(f"unterminated body for {signature}")


map_key = read("src/F4TMapKey.cpp")
map_key_header = read("include/F4TMapKey.h")
wheel = read("src/F4TInteractWheel.cpp")
wheel_header = read("include/F4TInteractWheel.h")
main = read("src/main.cpp")
api_ui = read_ui("include/CommonwealthOnlineUI_API.h")
api_plugin = read("include/CommonwealthOnlineUI_API.h")
plugin_api = read_ui("src/PluginAPI.cpp")
prisma_header = read_ui("include/F4TPrismaUI.h")
prisma_internal = read_ui("src/F4TPrismaUIInternal.h")
menu = read_ui("src/F4TPrismaUI_Menu.cpp")
pause_script = read_ui("ui/views/CommonwealthOnline/pause-menu/script.js")

check("namespace F4T::MapKey" in map_key_header and "void Install();" in map_key_header, "F4T::MapKey::Install must be declared")
check('kQuickMapUserEvent{ "QuickMap" }' in map_key, "the hook must consume only the QuickMap user event")
check("kOnButtonEventSlot{ 8 }" in map_key, "the hook must replace BSInputEventUser::OnButtonEvent at slot 8")
check("RE::MenuControls::GetSingleton()" in map_key and "controls->pipboyHandler" in map_key, "the hook must find the PipboyHandler vtable through the live MenuControls singleton")
for rva in ("0x12AB480", "0xFEFF00", "0x1077010"):
    check(rva not in map_key, f"the hook must not pin the fixed address {rva}")
check("kSlotNotExecutable" in map_key and "WriteSucceeded" in map_key, "the hook must accept a chained slot and confirm the write")
check("F4T::InteractWheel::CoWindowOpen()" in map_key, "M must reuse the CO window guard")
check("F4T::InteractWheel::VanillaMenuOpen(false)" in map_key, "M must reuse the vanilla menu guard without the pause menu")
check("QJustPressed()" in map_key, "M must open on the press edge only")
check("IVCommonwealthOnlineUI19" in map_key and "HandlePauseMenuMapKey" in map_key, "the hook must reach the overlay through IV19")
check("F4T::Networking::IsConnectedToServer()" in map_key, "M must be consumed only while connected to CO")
check("F4T::RemotePlayerState::GetAssignedPlayerId()" in map_key, "M must wait for an assigned player id")
check('#include "F4TMapKey.h"' in main and main.count("F4T::MapKey::Install();") == 1, "main must install the M-key hook once")
check("F4T::MapKey::Install();" in function_body(main, "F4T::F4SECapabilities::AddPermanentTask([]() {"), "the M-key hook must retry from the per-frame permanent task until the menus exist")
check("CoWindowOpen()" in wheel_header and "VanillaMenuOpen(bool a_includePauseMenu)" in wheel_header, "the guards must be exported")
check("bool VanillaMenuOpen(bool a_includePauseMenu)" in wheel, "VanillaMenuOpen must take the pause-menu switch")
check('"LoadingMenu"' in wheel, "M must stay closed on loading screens")
check("F4T::JoinFlow::IsActive()" in function_body(wheel, "bool CoWindowOpen()"), "M must stay closed in the join flow")
check("V19 = 19" in api_ui and "IVCommonwealthOnlineUI19" in api_ui, "the API must gain V19")
check(api_ui == api_plugin, "the two API headers must stay identical")
check("InterfaceVersionMap<IVCommonwealthOnlineUI19>" in api_ui and "0xA2D71F13" in api_ui, "V19 needs its own layout fingerprint")
check("case InterfaceVersion::V19:" in plugin_api and "g_interfaceV19" in plugin_api, "the API dispatcher must serve V19")
check("HandlePauseMenuMapKey" in prisma_header and "HandlePauseMenuMapKeyInternal" in prisma_internal, "the UI must declare the M-key entry point")
check("onPauseMenuMapKey" in menu and "g_pauseMenuMapOpenDeadline" in menu, "the UI must route M to the overlay")
check("PauseMenu::MENU_NAME" in menu and "kShow" in function_body(menu, "void HandlePauseMenuMapKeyInternal(bool a_allowed)"), "M must open the vanilla PauseMenu through the message queue")
check("window.onPauseMenuMapKey = function" in pause_script, "the pause page must accept the M key")
check("function leaveMap()" in pause_script and "mapOpenedDirectly" in pause_script, "the pause page must close a directly opened map in one step")
check("leaveMap();" in function_body(pause_script, "function handleMapKeydown(event)"), "Escape must close a directly opened map")
for menu_name in ("Console", "VATSMenu", "ExamineMenu", "MessageBoxMenu", "SleepWaitMenu", "BookMenu", "PowerArmorModMenu", "SPECIALMenu", "LevelUpMenu",
                  "CookingMenu", "RobotModMenu", "MultiActivateMenu", "ExamineConfirmMenu", "FavoritesMenu", "PipboyHolotapeMenu", "TerminalHolotapeMenu", "VertibirdMenu", "Workshop_CaravanMenu"):
    check(f'"{menu_name}"' in function_body(wheel, "bool VanillaMenuOpen(bool a_includePauseMenu)"), f"M must stay closed while {menu_name} is open")
check('case "b":\n          leaveMap();' in pause_script, "gamepad B must close a directly opened map in one step, resuming the game")
check('event.stopPropagation();\n      leaveMap();' in pause_script, "the map Back button must close a directly opened map in one step, resuming the game")
check("admin->IsAdminWindowVisible && admin->IsAdminWindowVisible()" in function_body(wheel, "bool CoWindowOpen()"), "the admin window must keep M and the wheel closed")
check("IsInteractWheelVisible" not in function_body(wheel, "bool CoWindowOpen()"), "CoWindowOpen must not read the wheel's own window, or the wheel closes itself")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TMapKeyContract: all checks passed")
