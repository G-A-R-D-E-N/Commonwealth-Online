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
main_source = (root / "src" / "F4TUiPlugin.cpp").read_text(encoding="utf-8")
menu_input = (root / "src" / "F4TMenuInput.cpp").read_text(encoding="utf-8")
http_get = (root / "src" / "F4THttpGet.cpp").read_text(encoding="utf-8")
server_data = (root / "src" / "F4TServerBrowserData.cpp").read_text(encoding="utf-8")
browser_rules = (root / "src" / "F4TServerBrowserMatchRules.cpp").read_text(encoding="utf-8")
parser_ci = (root / "xmake-parser-ci.lua").read_text(encoding="utf-8")
assert "src/F4TServerBrowserMatchRules.cpp" in parser_ci, "the parser test target must compile the match rules it links"
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

reachability = (root / "src" / "F4TServerBrowserReachability.cpp").read_text(encoding="utf-8")

assert "#include <mutex>" in bridge
assert "g_currentServerMutex" in bridge
assert "std::scoped_lock lock(g_currentServerMutex)" in bridge
assert (bridge + dispatch).count("CompleteDirectoryRefresh(") == 3
assert (bridge + dispatch).count("CompleteLocalScan(") == 3
assert "g_localScanRunning.store(false)" in bridge
assert "g_directoryRefreshRunning.store(false)" in dispatch
assert "F4T::PrismaCapabilities::GetV11()" in bridge
assert "std::variant<DirectoryRefreshCompletion, LocalScanCompletion, ReachabilityCompletion>" in dispatch
assert "std::unordered_map<std::uintptr_t, std::unique_ptr<DispatchCompletion>>" in dispatch
assert "reinterpret_cast<std::uintptr_t>(a_userdata)" in dispatch
assert "RegisterCompletion(" in dispatch and "DirectoryRefreshCompletion{" in dispatch
assert "LocalScanCompletion{" in dispatch
assert "ReachabilityCompletion{" in dispatch
assert dispatch.count("DispatchToGameThread(") == 3
assert "reinterpret_cast<void*>(token)" in dispatch
assert dispatch.count("g_dispatches.erase(token)") == 3
assert "g_dispatches.clear()" in dispatch
assert "g_dispatchStopped = true" in dispatch
assert "std::uint64_t g_dispatchGeneration" in dispatch
assert "std::uint64_t g_directoryGeneration" in dispatch
assert "++g_dispatchGeneration" in dispatch
assert "++g_directoryGeneration" in dispatch
assert "std::jthread g_directoryWorker" in dispatch
assert "std::jthread g_localWorker" in dispatch
assert "request_stop()" in dispatch
assert "OwnDirectoryWorker" in dispatch and "OwnLocalWorker" in dispatch
assert "BeginDispatch(std::atomic_bool& a_running)" in dispatch
assert "FinishDispatch(std::atomic_bool& a_running, std::uint64_t a_generation)" in dispatch
assert "std::optional<DispatchTicket> BeginDirectoryDispatch()" in dispatch
assert "void FinishDirectoryDispatch(const DispatchTicket& a_ticket)" in dispatch
assert "bool IsDirectoryGenerationActive(const DispatchTicket& a_ticket)" in dispatch
assert "a_ticket.generation == g_dispatchGeneration" in dispatch
assert "a_ticket.directoryGeneration == g_directoryGeneration" in dispatch
assert "CaptureDispatchGeneration" not in dispatch
assert bridge.count("BeginDispatch(g_") == 1
assert bridge.count("FinishDispatch(g_") == 3
assert bridge.count("BeginDirectoryDispatch(") == 1
assert bridge.count("FinishDirectoryDispatch(") == 1
assert bridge.count("CancelDirectoryWorker(") == 1
assert "if (query.cancelled)" not in bridge
assert "IsDispatchGenerationActive" in dispatch
assert "a_generation == g_dispatchGeneration" in dispatch
assert dispatch.count("IsDispatchGenerationActive(generation)") == 1
assert dispatch.count("IsDispatchGenerationActive(completion->ticket.generation)") == 1
assert "generation = *generation" in bridge
assert "ticket = *ticket" in bridge
assert "new DirectoryRefreshCompletion" not in (bridge + dispatch)
assert "new LocalScanCompletion" not in (bridge + dispatch)
assert "delete completion" not in (bridge + dispatch)
assert "if (!taskInterface && !prismaV11)" in bridge
assert "not started because no game-thread dispatcher is available" in bridge
assert "void RefreshDirectory(" in reachability
directory_cancel = reachability.index("if (query.cancelled)")
assert "FinishDirectoryDispatch(a_ticket);" in reachability[directory_cancel:directory_cancel + 200]
assert "Reachability::Prepare(servers)" in reachability
assert "Reachability::Run(prepared.targets" in reachability
assert "DispatchReachabilityCompletion(" in reachability
assert "prepared.epochs" in reachability
assert "Reachability::IsCurrentEpoch(entry.host, entry.port, epoch)" in bridge
assert "JoinFailureBadgeFor(a_reason)" in bridge
assert "CachedOrJoinFailureBadge(" in bridge
assert "Reachability::CurrentResult(" in bridge
assert "the last check did not match the join" not in (bridge + dispatch)
assert "bool JoinFailureProvesAnswer(std::string_view a_reason)" in browser_rules
assert "Reachability::NoteAnswer(a_entry.host, a_entry.port, badgeResult.milliseconds)" in bridge
assert "JoinFailureBadgeFor(a_reason)" in bridge
assert "CachedOrJoinFailureBadge(" in bridge
assert "Reachability::CurrentResult(" in bridge
assert "the last check did not match the join" not in (bridge + dispatch)
assert "NormalizeListedEntries(a_servers)" in reachability
assert "void NormalizeListedEntries(std::vector<ServerEntry>& a_servers)" in browser_rules
assert '"type", "serverReachabilityUpdated"' in dispatch
assert '{ "updates", std::move(payload) }' in dispatch
assert "SetReachability(" in dispatch
assert "OwnDirectoryWorker([taskInterface" in bridge
assert "OwnLocalWorker([taskInterface" in bridge
assert "std::jthread(" not in bridge
browser_workers = bridge + dispatch + reachability
assert ".detach()" not in browser_workers
assert "g_abandonedWorkers" in dispatch
assert "WorkerFinished(" in dispatch
assert "ReapWorkers(" in dispatch
assert "RetireWorker(" in dispatch
assert "AbandonWorkers(" in dispatch
assert "finished->store(true" in dispatch
assert "stop_requested()" in bridge + reachability
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
assert "std::string AddressKey(std::string_view a_host, int a_port)" in browser_rules
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
    root / "stage-mod.bat",
    root / "scripts" / "stage-package.ps1",
    repo_root / "CO-BETA" / "stage-mod.bat",
    repo_root / "CO-BETA" / "ci" / "test-stage-mod-release.ps1",
)
stale_cef = re.compile(r"(?<![A-Za-z])[Cc][Ee][Ff]|(?<=[a-z])C[Ee][Ff]")
for path in legacy_files:
    text = path.read_text(encoding="utf-8")
    assert not stale_cef.search(text), f"stale CEF reference: {path.relative_to(repo_root)}"

def local_completion_is_current(captured_generation: int, active_generation: int, stopped: bool) -> bool:
    return not stopped and captured_generation == active_generation


queued_generation = 1
active_generation = queued_generation + 1
assert not local_completion_is_current(queued_generation, active_generation, True)
assert not local_completion_is_current(queued_generation, active_generation, False)
assert local_completion_is_current(active_generation, active_generation, False)


def directory_completion_is_current(ticket: tuple, active_generation: int, active_directory: int, stopped: bool) -> bool:
    return not stopped and ticket == (active_generation, active_directory)


def begin_directory_dispatch(stopped: bool, active_generation: int, active_directory: int) -> tuple | None:
    return None if stopped else (active_generation, active_directory + 1)


def finish_directory_dispatch(running: bool, ticket: tuple, active_generation: int, active_directory: int, stopped: bool) -> bool:
    return False if not stopped and ticket[1] == active_directory and ticket[0] == active_generation else running


first_ticket = begin_directory_dispatch(False, 1, 1)
assert first_ticket == (1, 2)
assert begin_directory_dispatch(True, 1, 2) is None
assert directory_completion_is_current(first_ticket, 1, 2, False)
assert not directory_completion_is_current(first_ticket, 1, 1, False)
assert not directory_completion_is_current(first_ticket, 1, 2, True)
second_ticket = begin_directory_dispatch(False, 1, 2)
assert finish_directory_dispatch(True, first_ticket, 1, 3, False)
assert not finish_directory_dispatch(True, second_ticket, 1, 3, False)


def begin_local_dispatch(stopped: bool, running: bool, generation: int) -> int | None:
    return None if stopped or running else generation


def finish_local_dispatch(running: bool, captured_generation: int, active_generation: int, stopped: bool) -> bool:
    return False if not stopped and captured_generation == active_generation else running


old_generation = begin_local_dispatch(False, False, 1)
assert old_generation == 1
new_generation = 2
new_running = True
assert finish_local_dispatch(new_running, old_generation, new_generation, False)
assert not finish_local_dispatch(new_running, new_generation, new_generation, False)

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
