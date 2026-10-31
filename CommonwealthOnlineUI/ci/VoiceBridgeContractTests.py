import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"
PLUGIN = ROOT / "CO-BETA" / "plugin"

UI_HEADER = re.sub(r"/\*.*?\*/", "", (UI / "include" / "CommonwealthOnlineUI_API.h").read_text(encoding="utf-8"), flags=re.S)
PLUGIN_HEADER = re.sub(r"/\*.*?\*/", "", (PLUGIN / "include" / "CommonwealthOnlineUI_API.h").read_text(encoding="utf-8"), flags=re.S)
PLUGIN_API = (UI / "src" / "PluginAPI.cpp").read_text(encoding="utf-8")
BRIDGE = (UI / "src" / "F4TVoiceBridge.cpp").read_text(encoding="utf-8")
STATE = (UI / "src" / "F4TVoiceBridgeState.cpp").read_text(encoding="utf-8")
MENU = (UI / "src" / "F4TPrismaUI_Menu.cpp").read_text(encoding="utf-8")
MAIN_MENU = (UI / "src" / "F4TPrismaUI_MainMenu.cpp").read_text(encoding="utf-8")
FACTION = (UI / "src" / "F4TFactionView.cpp").read_text(encoding="utf-8")
LIFECYCLE = (UI / "src" / "F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")
UI_CLIENT = (PLUGIN / "src" / "F4TUIClient.cpp").read_text(encoding="utf-8")
VOICE_UI = (PLUGIN / "src" / "F4TVoiceUi.cpp").read_text(encoding="utf-8")

V9_BLOCK = re.compile(r"^\tusing VoiceActionFn\b.*?^\tstruct IVCommonwealthOnlineUI9\n\t\{.*?^\t\};\n", re.S | re.M)
V9_MAP = re.compile(r"^\ttemplate <>\n\tstruct InterfaceVersionMap<IVCommonwealthOnlineUI9>\n\t\{.*?^\t\};\n", re.S | re.M)
EXPECTED_OPS = ["setSetting", "beginKeyCapture", "mutePlayer", "setPlayerVolume"]
ATTACH_CALL = re.compile(r"AttachView\((?:F4T::VoiceBridge::)?Surface::(\w+)")
ACTION_GUARD = r'if \(strnlen\(a_json, kActionMaxBytes \+ 1\) > kActionMaxBytes\) \{\s*LogWarn\("[^"]*"\);\s*return;\s*\}'
EVENT_GUARD = r'if \(strnlen\(a_json, kEventMaxBytes \+ 1\) > kEventMaxBytes\) \{\s*LogWarn\("[^"]*"\);\s*return;\s*\}'
ACTION_GATE = r'if \(!AllowedActionOp\(a_json\)\) \{\s*LogWarn\("[^"]*"\);\s*return;\s*\}'
NULL_EVENT_GUARD = r'if \(!event\) \{\s*LogWarn\("[^"]*"\);\s*return;\s*\}'
NO_CALLBACK_GUARD = r'if \(!g_callbacks\.SendAction\) \{\s*LogWarn\("[^"]*"\);\s*return;\s*\}'
HUD_WANTED_CALL = r"HudWanted\(\s*g_cache,\s*F4T::PrismaUI::IsMainMenuVisible\(\),\s*F4T::PrismaUI::IsBrowserVisible\(\),\s*g_sessionLockoutActive\)"
NULL_UI_GUARD = r'if \(!ui\) \{\s*REX::WARN\([^;]*\);\s*return;\s*\}'
TRAMPOLINE_TRY = r"try \{\s*if \(const auto handler = g_actionHandler\.load\(\)\) \{\s*handler\(a_actionJson\);\s*\}\s*\} catch \(\.\.\.\) \{"

failures = []


def check(label, condition):
    print(f"PASS {label}" if condition else f"FAIL {label}")
    if not condition:
        failures.append(label)


def first_match(pattern, text):
    found = pattern.search(text)
    return found.group(0) if found else None


def body_of(pattern, text):
    found = re.search(pattern, text, re.M)
    if not found:
        return ""
    end = text.find("\n\t}\n", found.end())
    return text[found.end():end] if end != -1 else ""


def live(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("//"))


def live_count(text, line):
    return len(re.findall(r"^[ \t]*" + re.escape(line) + r"[ \t]*$", live(text), re.M))


def between(text, start, end):
    found = text.find(start)
    if found == -1:
        return ""
    stop = text.find(end, found + len(start))
    return text[found:stop] if stop != -1 else ""


def precedes(region, first, second):
    a = region.find(first)
    b = region.find(second)
    return a != -1 and b != -1 and a < b


def task_body(text, function):
    start = text.find(f"void {function}(PrismaView a_view)")
    return between(text[start:], "AddTask([a_view]() {", "\n\t\t});") if start != -1 else ""


ui_block = first_match(V9_BLOCK, UI_HEADER)
plugin_block = first_match(V9_BLOCK, PLUGIN_HEADER)
ui_map = first_match(V9_MAP, UI_HEADER)
plugin_map = first_match(V9_MAP, PLUGIN_HEADER)

check("(a1) V9 = 9 enumerator in both InterfaceVersion enums",
      re.search(r"\n\t\tV8 = 8,\n\t\tV9 = 9,\n", UI_HEADER) is not None
      and re.search(r"\n\t\tV8 = 8,\n\t\tV9 = 9,\n", PLUGIN_HEADER) is not None)
check("(a2) VoiceActionFn through IVCommonwealthOnlineUI9 identical in both headers",
      ui_block is not None and ui_block == plugin_block)
check("(a3) InterfaceVersionMap<IVCommonwealthOnlineUI9> identical in both headers",
      ui_map is not None and ui_map == plugin_map and "InterfaceVersion::V9;" in ui_map)

check("(b1) PluginAPI defines g_interfaceV9 as SetVoiceCallbacks then PushVoiceEvent",
      re.search(r"IVCommonwealthOnlineUI9 g_interfaceV9\{\s*Api_SetVoiceCallbacks,\s*Api_PushVoiceEvent\s*\};", live(PLUGIN_API))
      is not None)
check("(b2) VisitInterface routes InterfaceVersion::V9 to g_interfaceV9",
      re.search(r"case InterfaceVersion::V9:\s*return a_visit\(g_interfaceV9\);", live(PLUGIN_API)) is not None)

check("(c1) F4TVoiceBridge.cpp binds coVoiceAction",
      live_count(BRIDGE, 'g_api->BindUIEvent(a_view, "coVoiceAction", OnAction);') >= 1)
check("(c2) Main menu and pause menu attach their views to the voice bridge",
      live_count(MAIN_MENU, "F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::MainMenu, a_view);") >= 1
      and live_count(MENU, "F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::PauseMenu, a_view);") >= 1)
check("(c3) Faction window attaches its view as Surface::Social",
      live_count(FACTION, "F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::Social, a_view);") >= 1)
check("(c4) Lifecycle calls VoiceBridge::Initialize and VoiceBridge::Tick",
      live_count(LIFECYCLE, "F4T::VoiceBridge::Initialize();") >= 1
      and live_count(LIFECYCLE, "F4T::VoiceBridge::Tick();") >= 1)
action_body = between(live(BRIDGE), "void OnAction(const char* a_json)", "void OnHudDomReady(PrismaView a_view)")
event_body = between(live(BRIDGE), "void PushEvent(const char* a_json)", "void AttachView(Surface a_surface, PrismaView a_view)")
check("(c5) OnAction and PushEvent warn and return on oversize before the game-thread copy",
      precedes(action_body, "if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {", "RunOnGameThread(")
      and precedes(event_body, "if (strnlen(a_json, kEventMaxBytes + 1) > kEventMaxBytes) {", "RunOnGameThread(")
      and re.search(ACTION_GUARD, action_body) is not None
      and re.search(EVENT_GUARD, event_body) is not None)
check("(c5b) OnAction and PushEvent copy the payload into the game-thread task",
      live_count(action_body, "RunOnGameThread([payload = std::string(a_json)]() {") == 1
      and live_count(action_body, "HandleAction(payload);") == 1
      and live_count(event_body, "RunOnGameThread([payload = std::string(a_json)]() {") == 1
      and live_count(event_body, "HandleEvent(payload);") == 1)
check("(c6) UI PushEvent null-checks its input before the size check",
      live_count(event_body, "if (!a_json) {") == 1
      and precedes(event_body, "if (!a_json) {", "if (strnlen(a_json, kEventMaxBytes + 1) > kEventMaxBytes) {"))
check("(c7) UI OnAction null-checks its input before the size check",
      live_count(action_body, "if (!a_json) {") == 1
      and precedes(action_body, "if (!a_json) {", "if (strnlen(a_json, kActionMaxBytes + 1) > kActionMaxBytes) {"))
check("(c8) HUD html path is exactly CommonwealthOnline/voice-hud/index.html",
      live_count(live(BRIDGE), 'constexpr auto kHudHtmlPath = "CommonwealthOnline/voice-hud/index.html";') == 1)
check("(f1) Main menu and pause menu attach inside their DomReady AddTask bodies",
      live_count(task_body(MAIN_MENU, "OnMainMenuDomReady"),
                 "F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::MainMenu, a_view);") == 1
      and live_count(task_body(MENU, "OnPauseMenuDomReady"),
                     "F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::PauseMenu, a_view);") == 1)
check("(f1b) MainMenu attach runs after TryFinalizeMainMenu inside its DomReady task body",
      precedes(task_body(MAIN_MENU, "OnMainMenuDomReady"), "TryFinalizeMainMenu();",
               "F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::MainMenu, a_view);"))
check("(f2) Menu files have exactly two AttachView calls, both inside the DomReady task bodies",
      live(MENU).count("F4T::VoiceBridge::AttachView(") + live(MAIN_MENU).count("F4T::VoiceBridge::AttachView(") == 2
      and live(task_body(MAIN_MENU, "OnMainMenuDomReady")).count("F4T::VoiceBridge::AttachView(") == 1
      and live(task_body(MENU, "OnPauseMenuDomReady")).count("F4T::VoiceBridge::AttachView(") == 1)
check("(f3) Faction Social attach sits in FinishDomReady, which only runs through RunOnGameThread",
      live_count(between(live(FACTION), "void FinishDomReady(PrismaView a_view)", "void OnDomReady(PrismaView a_view)"),
                 "F4T::VoiceBridge::AttachView(F4T::VoiceBridge::Surface::Social, a_view);") == 1
      and "RunOnGameThread([a_view]() {\n\t\t\t\tFinishDomReady(a_view);\n\t\t\t});" in live(FACTION)
      and live_count(FACTION, "FinishDomReady(a_view);") == 1)
check("(f3b) Faction RunOnGameThread queues through AddTask",
      live_count(between(FACTION, "void RunOnGameThread(", "LogWarn("),
                 "taskInterface->AddTask(std::move(a_task));") == 1)

attach_surfaces = []
for path in sorted(p for p in (UI / "src").rglob("*") if p.suffix in (".cpp", ".h")):
    attach_surfaces += ATTACH_CALL.findall(live(path.read_text(encoding="utf-8")))
check("(f4) exactly four AttachView calls in CommonwealthOnlineUI/src, one per surface",
      sorted(attach_surfaces) == ["Hud", "MainMenu", "PauseMenu", "Social"])
hud_body = between(live(BRIDGE), "void OnHudDomReady(PrismaView a_view)", "\n\t\t}\n")
hud_task = between(hud_body, "RunOnGameThread([a_view]() {", "\n\t\t\t});")
check("(f5) Hud attach sits inside the OnHudDomReady RunOnGameThread body",
      live_count(hud_task, "AttachView(Surface::Hud, a_view);") == 1)

handle_action = between(live(BRIDGE), "void HandleAction(const std::string& a_json)", "void OnAction(const char* a_json)")
handle_event = between(live(BRIDGE), "void HandleEvent(const std::string& a_json)", "void HandleAction(const std::string& a_json)")
attach_body = between(live(BRIDGE), "void AttachView(Surface a_surface, PrismaView a_view)", "void Tick()")
set_callbacks_body = between(live(BRIDGE), "void SetCallbacks(", "void PushEvent(")
trampoline_body = between(live(VOICE_UI), "void Callback_SendVoiceAction(const char* a_actionJson)", "const COMMONWEALTH_ONLINE_UI_API::VoiceCallbacks kVoiceCallbacks")
action_gate = re.search(ACTION_GATE, handle_action)
check("(g1) HandleAction drops disallowed ops before g_callbacks.SendAction",
      action_gate is not None and handle_action.find("g_callbacks.SendAction(") > action_gate.start())
check("(g2) AttachView stores the view before replaying cached events",
      live_count(attach_body, "g_views[static_cast<std::size_t>(a_surface)] = a_view;") == 1
      and precedes(attach_body, "g_views[static_cast<std::size_t>(a_surface)] = a_view;", "g_cache.ReplayScripts()"))
check("(g3) HandleEvent caches each valid event before it reaches the surfaces",
      live_count(handle_event, "g_cache.Store(*event);") == 1
      and precedes(handle_event, "g_cache.Store(*event);", "InvokeSurfaces(EventScript(*event));"))
check("(g4) SetCallbacks copies the callbacks on the game thread",
      live_count(set_callbacks_body, "g_callbacks = callbacks;") == 1
      and precedes(set_callbacks_body, "RunOnGameThread(", "g_callbacks = callbacks;"))
check("(g5) plugin voice trampoline catches every exception and warns",
      live_count(trampoline_body, "} catch (...) {") == 1
      and precedes(trampoline_body, "} catch (...) {", "REX::WARN("))

on_game_data_ready = body_of(r"^\tvoid OnGameDataReady\(\)\n\t\{", live(UI_CLIENT))
check("(d1) F4TUIClient.cpp calls F4T::VoiceUi::Register() inside OnGameDataReady",
      live_count(on_game_data_ready, "F4T::VoiceUi::Register();") >= 1)

ops_body = re.search(r"kAllowedActionOps\{(.*?)\};", live(STATE), re.S)
ops = re.findall(r'"([^"]+)"', ops_body.group(1)) if ops_body else []
check("(e1) F4TVoiceBridgeState.cpp op whitelist is exactly the four plan ops",
      ops == EXPECTED_OPS)

run_on_game_thread = between(live(BRIDGE), "void RunOnGameThread(", "void InvokeSurfaces(")
invoke_surfaces = between(live(BRIDGE), "void InvokeSurfaces(", "void HandleEvent(")
init_body = between(live(BRIDGE), "void Initialize()", "void SetCallbacks(")
tick_body = between(live(BRIDGE), "void Tick()", "\n\t}\n")
api_set_callbacks = between(live(PLUGIN_API), "void Api_SetVoiceCallbacks(", "\n\t}\n")
api_push_event = between(live(PLUGIN_API), "void Api_PushVoiceEvent(", "\n\t}\n")
register_body = between(live(VOICE_UI), "void Register()", "bool PushEvent(")
push_body = between(live(VOICE_UI), "bool PushEvent(const char* a_eventJson)", "\n\t}\n")
set_handler_body = between(live(VOICE_UI), "void SetActionHandler(ActionHandler a_handler)", "void Register()")

check("(h1) Tick passes the main menu, browser and lockout inputs to HudWanted",
      live_count(tick_body, "F4T::PrismaUI::IsMainMenuVisible(),") == 1
      and live_count(tick_body, "F4T::PrismaUI::IsBrowserVisible(),") == 1
      and live_count(tick_body, "g_sessionLockoutActive);") == 1)
check("(h2) Tick creates the HUD only when wanted, then sets its order",
      live_count(tick_body, "if (wanted && !g_hudState.IsHandleValid()) {") == 1
      and live_count(tick_body, "if (g_hudState.EnsureCreated(kHudHtmlPath, OnHudDomReady, g_prismaReady) != 0) {") == 1
      and live_count(tick_body, "g_api->SetOrder(g_hudState.GetHandle(), kHudViewOrder);") == 1)
check("(h3) Tick shows and hides the HUD through ApplyState",
      live_count(tick_body, 'g_hudState.ApplyState(wanted ? "HudShow" : "HudHide");') == 1)
check("(h7) Tick passes the cache, then the main menu, browser and lockout inputs, to HudWanted once",
      len(re.findall(HUD_WANTED_CALL, tick_body)) == 1)
check("(h8) Tick sets the desired visibility before ApplyState shows or hides the HUD",
      live_count(tick_body, "g_hudState.SetDesiredVisible(wanted);") == 1
      and precedes(tick_body, "g_hudState.SetDesiredVisible(wanted);", 'g_hudState.ApplyState(wanted ? "HudShow" : "HudHide");'))
check("(h4) HUD view order is kHudViewOrder 520",
      live_count(live(BRIDGE), "constexpr int kHudViewOrder = 520;") == 1)
check("(h5) OnHudDomReady keeps the DomReady guard, role assignment and DomReady state",
      live_count(hud_body, "if (!g_hudState.MarkDomReady(a_view) || !g_api) {") == 1
      and live_count(hud_body, "g_hudState.AssignRoleIfNeeded();") == 1
      and live_count(hud_body, 'g_hudState.ApplyState("DomReady");') == 1)
check("(h6) Initialize keeps the HUD from taking focus",
      live_count(init_body, "g_hudState.SetFocusArgs(false, true);") == 1)
check("(h9) Initialize hands the HUD controller the API",
      live_count(init_body, "g_hudState.SetApi(g_api);") == 1)
check("(h10) Initialize pins the HUD controller to the widget role",
      live_count(init_body, "g_hudState.SetDesiredRole(PRISMA_UI_API::ViewRole::kWidget);") == 1)
check("(h11) Tick returns early when the HUD visibility is unchanged",
      re.search(r"if \(wanted == g_hudState\.IsDesiredVisible\(\)\) \{\s*return;\s*\}", tick_body) is not None)
check("(i1) RunOnGameThread queues its task through AddTask",
      live_count(run_on_game_thread, "taskInterface->AddTask(std::move(a_task));") == 1
      and live_count(run_on_game_thread, "a_task();") == 0)
check("(i2) InvokeSurfaces guards each delivery with IsValid and invokes it",
      live_count(invoke_surfaces, "if (view != 0 && g_api->IsValid(view)) {") == 1
      and live_count(invoke_surfaces, "g_api->Invoke(view, a_script.c_str());") == 1
      and live_count(invoke_surfaces, "for (const auto view : g_views) {") == 1)
check("(i3) AttachView replays each cached event into the attached view",
      live_count(attach_body, "g_api->Invoke(a_view, script.c_str());") == 1)
check("(i5) AttachView replays every cached script and never breaks out of the loop",
      live_count(attach_body, "for (const auto& script : g_cache.ReplayScripts()) {") == 1
      and precedes(attach_body, "for (const auto& script : g_cache.ReplayScripts()) {", "g_api->Invoke(a_view, script.c_str());")
      and "break;" not in attach_body)
check("(i4) HandleEvent and HandleAction check for nullopt and a missing callback before use",
      live_count(handle_event, "if (!event) {") == 1
      and precedes(handle_event, "if (!event) {", "g_cache.Store(*event);")
      and re.search(NULL_EVENT_GUARD, handle_event) is not None
      and live_count(handle_action, "if (!g_callbacks.SendAction) {") == 1
      and precedes(handle_action, "if (!g_callbacks.SendAction) {", "g_callbacks.SendAction(a_json.c_str());")
      and re.search(NO_CALLBACK_GUARD, handle_action) is not None)
check("(j1) PluginAPI forwards voice callbacks and events to the VoiceBridge",
      live_count(api_set_callbacks, "F4T::VoiceBridge::SetCallbacks(a_callbacks);") == 1
      and live_count(api_push_event, "F4T::VoiceBridge::PushEvent(a_eventJson);") == 1)
check("(k1) plugin Register stores the interface and passes the callbacks to it",
      live_count(register_body, "g_ui.store(ui);") == 1
      and live_count(register_body, "ui->SetVoiceCallbacks(&kVoiceCallbacks);") == 1
      and re.search(r"const COMMONWEALTH_ONLINE_UI_API::VoiceCallbacks kVoiceCallbacks\{\s*Callback_SendVoiceAction\s*\};", live(VOICE_UI)) is not None)
check("(k2) plugin PushEvent guards null, forwards to PushVoiceEvent and returns true",
      live_count(push_body, "if (!a_eventJson || !ui || !ui->PushVoiceEvent) {") == 1
      and live_count(push_body, "ui->PushVoiceEvent(a_eventJson);") == 1
      and re.search(r"ui->PushVoiceEvent\(a_eventJson\);\s*return true;", push_body) is not None)
check("(k3) plugin Register warns and returns when the UI DLL lacks V9, before SetVoiceCallbacks",
      len(re.findall(NULL_UI_GUARD, register_body)) == 1
      and precedes(register_body, "if (!ui) {", "ui->SetVoiceCallbacks(&kVoiceCallbacks);"))
check("(k6) plugin Register calls SetVoiceCallbacks only when the interface sets it",
      live_count(register_body, "if (ui->SetVoiceCallbacks) {") == 1
      and precedes(register_body, "if (ui->SetVoiceCallbacks) {", "ui->SetVoiceCallbacks(&kVoiceCallbacks);"))
check("(k4) plugin voice trampoline calls the action handler inside try and catches everything",
      len(re.findall(TRAMPOLINE_TRY, trampoline_body)) == 1)
check("(k5) plugin SetActionHandler stores the handler it is given",
      live_count(set_handler_body, "g_actionHandler.store(a_handler);") == 1)

if failures:
    print(f"voice bridge contract FAILED: {len(failures)} check(s)")
    sys.exit(1)
print("voice bridge contract passed: V9 interface, PluginAPI wiring, Prisma attach points, plugin Register, op whitelist, size guards")
