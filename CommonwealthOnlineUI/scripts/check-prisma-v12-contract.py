import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
repo_root = root.parent
api = (root / "include" / "PrismaUI_F4_API.h").read_text(encoding="utf-8")
capabilities = (root / "src" / "F4TPrismaCapabilities.cpp").read_text(encoding="utf-8")
prisma_ui = "\n".join(
    path.read_text(encoding="utf-8")
    for path in [root / "src" / "F4TPrismaUI.cpp", *sorted(root.glob("src/F4TPrismaUI_*.cpp"))]
)
assert '#include "F4TPrismaUI_' not in (root / "src" / "F4TPrismaUI.cpp").read_text(encoding="utf-8")
view_state = (root / "src" / "F4TPrismaViewState.cpp").read_text(encoding="utf-8")
for panel in ("g_mainMenuState", "g_pauseMenuState", "g_standaloneBrowserState"):
    focus_args = re.findall(rf"{panel}\.SetFocusArgs\((true|false), (true|false)\)", prisma_ui)
    assert focus_args and all(disable == "false" for _, disable in focus_args), f"{panel} must retain Prisma's modal focus menu"
alerts = (root / "src" / "F4TAlertSystem.cpp").read_text(encoding="utf-8")
bridge = (root / "src" / "F4TServerBrowserBridge.cpp").read_text(encoding="utf-8")
dispatch = (root / "src" / "F4TServerBrowserDispatch.cpp").read_text(encoding="utf-8")
bridge_facade = (root / "src" / "F4TServerBrowserBridgeFacade.cpp").read_text(encoding="utf-8")
main_source = (root / "src" / "main.cpp").read_text(encoding="utf-8")
menu_input = (root / "src" / "F4TMenuInput.cpp").read_text(encoding="utf-8")
http_get = (root / "src" / "F4THttpGet.cpp").read_text(encoding="utf-8")
server_data = (root / "src" / "F4TServerBrowserData.cpp").read_text(encoding="utf-8")
browser = (root / "ui" / "views" / "CommonwealthOnline" / "browser" / "browser.js").read_text(encoding="utf-8")
shim = root / "include" / "F4TPrismaV10.h"

required_api = (
    "V9 = 136, V10 = 137, V11 = 138, V12 = 139",
    "FocusOverlay",
    "SetInputRegions",
    "DispatchToGameThread",
    "BindGameThreadUIEvent",
    "BindControllerAction",
    "PrismaUI_F4_GetCapabilities",
)

for token in required_api:
    assert token in api, f"missing Prisma V12 contract token: {token}"

assert not shim.exists(), "obsolete F4TPrismaV10 ABI shim must not coexist with the V12 SDK header"

v12_request = "RequestPluginAPI<PRISMA_UI_API::IVPrismaUI12>()"
v11_request = "RequestPluginAPI<PRISMA_UI_API::IVPrismaUI11>()"
v10_request = "RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>()"
assert v12_request in capabilities
assert v11_request in capabilities
assert v10_request in capabilities
assert capabilities.index(v12_request) < capabilities.index(v11_request) < capabilities.index(v10_request)
assert "HasPrismaCapability(PRISMA_UI_API::PrismaCapability::InputRegions)" in capabilities

assert "#include <mutex>" in bridge
assert "g_currentServerMutex" in bridge
assert "std::scoped_lock lock(g_currentServerMutex)" in bridge
assert (bridge + dispatch).count("CompleteDirectoryRefresh(") == 3
assert (bridge + dispatch).count("CompleteLocalScan(") == 3
assert "g_directoryRefreshRunning.store(false)" in bridge
assert "g_localScanRunning.store(false)" in bridge
assert "F4T::PrismaCapabilities::GetV11()" in bridge
assert "std::variant<DirectoryRefreshCompletion, LocalScanCompletion>" in dispatch
assert "std::unordered_map<std::uintptr_t, std::unique_ptr<DispatchCompletion>>" in dispatch
assert "reinterpret_cast<std::uintptr_t>(a_userdata)" in dispatch
assert "RegisterCompletion(" in dispatch and "DirectoryRefreshCompletion{" in dispatch
assert "LocalScanCompletion{" in dispatch
assert dispatch.count("DispatchToGameThread(") == 2
assert "reinterpret_cast<void*>(token)" in dispatch
assert dispatch.count("g_dispatches.erase(token)") == 2
assert "g_dispatches.clear()" in dispatch
assert "g_dispatchStopped = true" in dispatch
assert "std::uint64_t g_dispatchGeneration" in dispatch
assert "++g_dispatchGeneration" in dispatch
assert "std::jthread g_directoryWorker" in dispatch
assert "std::jthread g_localWorker" in dispatch
assert "request_stop()" in dispatch
assert "OwnDirectoryWorker" in dispatch and "OwnLocalWorker" in dispatch
assert "BeginDispatch(std::atomic_bool& a_running)" in dispatch
assert "FinishDispatch(std::atomic_bool& a_running, std::uint64_t a_generation)" in dispatch
assert "CaptureDispatchGeneration" not in dispatch
assert bridge.count("BeginDispatch(g_") == 2
assert bridge.count("FinishDispatch(g_") == 6
directory_cancel = bridge.index("if (query.cancelled)")
assert "FinishDispatch(g_directoryRefreshRunning, generation);" in bridge[directory_cancel:directory_cancel + 200]
assert "IsDispatchGenerationActive" in dispatch
assert "a_generation == g_dispatchGeneration" in dispatch
assert dispatch.count("IsDispatchGenerationActive(generation)") == 2
assert dispatch.count("IsDispatchGenerationActive(completion.generation)") == 2
assert "generation = *generation" in bridge
assert "new DirectoryRefreshCompletion" not in (bridge + dispatch)
assert "new LocalScanCompletion" not in (bridge + dispatch)
assert "delete completion" not in (bridge + dispatch)
assert "if (!taskInterface && !prismaV11)" in bridge
assert "not started because no game-thread dispatcher is available" in bridge
directory_worker = bridge.index("std::jthread([taskInterface, prismaV11, generation = *generation, currentServerSnapshot](std::stop_token")
local_worker = bridge.index("std::jthread([taskInterface, prismaV11, generation = *generation](std::stop_token")
assert "CompleteDirectoryRefresh(" not in bridge[directory_worker:]
assert "CompleteLocalScan(" not in bridge[local_worker:]
assert ".detach()" not in bridge
assert "stop_requested()" in bridge
assert "case F4SE::MessagingInterface::kPreLoadGame:" in main_source
assert "ServerBrowserBridge::Shutdown()" in main_source
assert "void ServerBrowserBridge::Reactivate(PRISMA_UI_API::IVPrismaUI4* a_api, PrismaView a_view)" in bridge_facade
assert "ServerBrowserBridge::Reactivate(g_api, g_mainMenuState.GetHandle())" in prisma_ui
assert 'if (typeof window.coAction !== "function" && typeof window.CO_ENABLE_MOCKS === "boolean" && window.CO_ENABLE_MOCKS)' in browser
assert "SetInputRegions" in alerts
assert "HasInputRegions" in alerts
assert "FocusOverlay" in view_state
assert "g_cefWarm" not in prisma_ui
assert "g_firstMainMenuAdvanceMs" not in prisma_ui
assert "kMaxResponseBytes" in http_get
assert "exceeds 512 KiB" in http_get
assert "kMaxDirectoryServers" in server_data
assert "kMaxServerDescriptionLength" in server_data
assert "ValidateServerEntryJson" in server_data
assert "serversNode->size() > kMaxDirectoryServers" in server_data
assert "ParseDirectoryServers(parsed, error)" in bridge
assert "LoadLibraryA" not in menu_input
assert "LoadLibraryExW" in menu_input
assert "LOAD_LIBRARY_SEARCH_SYSTEM32" in menu_input
assert 'sendAction("retryLastJoin", {})' in browser
assert 'action == "retryLastJoin"' in bridge_facade
assert "COUI::Session::RetryLastJoin" in bridge + bridge_facade
assert '"type", "retryAvailable"' in bridge + bridge_facade
assert "COUI::Session::HasLastJoin()" in bridge + bridge_facade
legacy_files = (
    root / "src" / "F4TPrismaUI.cpp",
    root / "src" / "F4TAlertSystem.cpp",
    root / "include" / "F4TAlertSystem.h",
    root / "src" / "F4TPrismaCapabilities.cpp",
    root / "build.bat",
    root / "deploy.bat",
    root / "stage-mod.bat",
    root / "scripts" / "stage-package.ps1",
    repo_root / "CO-BETA" / "stage-mod.bat",
    repo_root / "CO-BETA" / "ci" / "test-stage-mod-release.ps1",
    root / "README.md",
)
stale_cef = re.compile(r"(?<![A-Za-z])[Cc][Ee][Ff]|(?<=[a-z])C[Ee][Ff]")
for path in legacy_files:
    text = path.read_text(encoding="utf-8")
    assert not stale_cef.search(text), f"stale CEF reference: {path.relative_to(repo_root)}"

def completion_is_current(captured_generation: int, active_generation: int, stopped: bool) -> bool:
    return not stopped and captured_generation == active_generation


queued_generation = 1
active_generation = queued_generation + 1
assert not completion_is_current(queued_generation, active_generation, True)
assert not completion_is_current(queued_generation, active_generation, False)
assert completion_is_current(active_generation, active_generation, False)

def begin_dispatch(stopped: bool, running: bool, generation: int) -> int | None:
    return None if stopped or running else generation


def finish_dispatch(running: bool, captured_generation: int, active_generation: int, stopped: bool) -> bool:
    return False if not stopped and captured_generation == active_generation else running


old_generation = begin_dispatch(False, False, 1)
assert old_generation == 1
new_generation = 2
new_running = True
assert finish_dispatch(new_running, old_generation, new_generation, False)
assert not finish_dispatch(new_running, new_generation, new_generation, False)

print("Prisma V12 consumer contract checks passed")

assert "if (m_desiredRole == a_role)" in view_state
settings_bridge = (root / "src" / "F4TSettingsBridge.cpp").read_text(encoding="utf-8")
assert "PrismaView                    g_view" not in settings_bridge
assert "InteropCall(a_view," in settings_bridge
assert "tasks->AddTask([action = a_action, a_view]" in settings_bridge
for menu in ("mainMenu", "pauseMenu"):
    assert prisma_ui.count(f"SettingsBridge::HandleAction(action, g_{menu}State.GetHandle())") == 2
assert "kInjectMultiplayerRow" not in (root / "src" / "F4TMainMenuInjectInternal.h").read_text(encoding="utf-8")
char_select = (root / "src" / "F4TCharacterSelectView.cpp").read_text(encoding="utf-8")
cursor_src = (root / "src" / "F4TPrismaUI_Cursor.cpp").read_text(encoding="utf-8")
assert "ViewRole::kPanel" in char_select and "ViewRole::kWidget" not in char_select
assert "F4T::CharacterSelectView::IsCurrentlyVisible()" in cursor_src
