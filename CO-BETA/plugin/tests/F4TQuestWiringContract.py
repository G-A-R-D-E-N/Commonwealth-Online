from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"
INBOUND = PLUGIN / "src/F4TNetworkingInbound.cpp"
NETWORKING = PLUGIN / "src/F4TNetworking.cpp"
SYNC = PLUGIN / "src/F4TQuestSync.cpp"
INBOUND_IMPL = PLUGIN / "src/F4TQuestInbound.cpp"
APPLY = PLUGIN / "src/F4TQuestApply.cpp"
LOGIC = PLUGIN / "src/F4TQuestLogic.cpp"
APPLY_TESTS = PLUGIN / "tests/F4TQuestApplyTests.cpp"
PAPYRUS_CPP = PLUGIN / "src/F4TQuestPapyrus.cpp"
PAPYRUS_H = PLUGIN / "include/F4TQuestPapyrus.h"
MAIN = PLUGIN / "src/main.cpp"
SESSION = PLUGIN / "src/F4TSessionLaunch.cpp"
SETUP_FLOW = PLUGIN / "src/F4TSessionCharacterSetupFlow.cpp"
QUEST_SCRIPT = ROOT / "CO-BETA/creation-kit/scripts/source/COQuestSyncScript.psc"
NATIVE_SCRIPT = ROOT / "CO-BETA/creation-kit/scripts/source/CommonwealthOnlineQuest.psc"
STAGE_MOD = ROOT / "CO-BETA/stage-mod.bat"
PACKAGE = ROOT / "CO-BETA/ci/package-release.sh"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(path):
    if not path.exists():
        failures.append(f"missing {path.relative_to(ROOT)}")
        return ""
    return path.read_text(encoding="utf-8")


def body_after(source, anchor):
    start = source.find(anchor)
    if start < 0:
        return None
    brace = source.index("{", start + len(anchor))
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    return None


inbound = read(INBOUND)
networking = read(NETWORKING)
sync = read(SYNC)
inbound_impl = read(INBOUND_IMPL)
apply_src = read(APPLY)
logic_src = read(LOGIC)
apply_tests = read(APPLY_TESTS)
papyrus_cpp = read(PAPYRUS_CPP)
main_src = read(MAIN)
session = read(SESSION)
setup_flow = read(SETUP_FLOW)
quest_script = read(QUEST_SCRIPT)
native_script = read(NATIVE_SCRIPT)
stage_mod = read(STAGE_MOD)
package = read(PACKAGE)

for include in ('#include "F4TQuestInbound.h"', '#include "F4TQuestSync.h"'):
    check(include in inbound, f"F4TNetworkingInbound.cpp must include {include}")

handler = body_after(inbound, "void HandlePacketLine(const std::string& a_line)")
check(handler is not None, "F4TNetworkingInbound.cpp must still define HandlePacketLine")
handler = handler or ""
dispatch = handler.find("F4T::QuestInbound::HandlePacket(*packetType, packet)")
welcome_branch = handler.find('if (*packetType == "welcome")')
check(dispatch >= 0, "HandlePacketLine must dispatch every packet through F4T::QuestInbound::HandlePacket")
check(
    dispatch >= 0 and welcome_branch >= 0 and dispatch < welcome_branch,
    "the quest dispatch must run before the welcome branch so a quest packet never reaches HandleUnknownPacket",
)
check(
    "if (F4T::QuestInbound::HandlePacket(*packetType, packet)) {" in handler,
    "the quest dispatch must consume the packet and return, like the faction and chat dispatches",
)

welcome = body_after(inbound, "void HandleWelcomePacket(const Json& a_packet)")
check(welcome is not None, "F4TNetworkingInbound.cpp must still define HandleWelcomePacket")
welcome = welcome or ""
player_guard = welcome.find("ReadPlayerId(a_packet)")
note_welcome = welcome.find("F4T::QuestSync::NoteWelcome(a_packet);")
check(note_welcome >= 0, "HandleWelcomePacket must call F4T::QuestSync::NoteWelcome(a_packet);")
check(
    0 <= player_guard < note_welcome,
    "the quest welcome hook must sit after the playerId guard, next to the object sync welcome",
)

connect = body_after(networking, "bool ConnectToServerInternal(")
disconnect = body_after(networking, "void DisconnectFromLocalServer()")
check(
    connect is not None and "F4T::QuestSync::ClearSession();" in connect,
    "ConnectToServerInternal must clear the quest capability before the socket comes up, so a stale welcome cannot report first",
)
check(
    disconnect is not None and "F4T::QuestSync::ClearSession();" in disconnect,
    "DisconnectFromLocalServer must clear the quest capability next to the other subsystem resets",
)

clear_session = body_after(inbound_impl, "void ClearSession()")
check(clear_session is not None, "F4TQuestInbound.cpp must define ClearSession")
clear_session = clear_session or ""
check(
    "g_inbound.clear();" in clear_session,
    "ClearSession must drop the packets of the ended session instead of leaving them for the next one",
)
check(
    "g_active.store(false" in clear_session,
    "ClearSession must turn the capability off so no report is sent before the next welcome advertises it",
)

tick = body_after(sync, "void Tick()")
check(tick is not None, "F4TQuestSync.cpp must still define Tick")
tick = tick or ""
active_gate = tick.find("QuestInbound::IsActive()")
drain = tick.find("QuestInbound::Drain(g_tracker)")
report = tick.find("PollLocalProgress(")
check(
    0 <= active_gate < drain,
    "Tick must check QuestInbound::IsActive() before draining, so an unadvertised server never reaches the tracker",
)
check(
    0 <= active_gate < report,
    "Tick must check QuestInbound::IsActive() before the local quest poll, so an unadvertised server receives no report",
)

send_report = body_after(sync, "void SendReport(const QuestLogic::Report& a_report)")
check(send_report is not None, "F4TQuestSync.cpp must still define SendReport")
send_report = send_report or ""
check(
    "QuestInbound::IsActive()" in send_report and "IsConnectedToServer()" in send_report,
    "SendReport must refuse to send unless the server advertised quest-sync-v1 on this connection",
)

sync_h = read(PLUGIN / "include/F4TQuestSync.h")
check(
    "using SendFn = bool (*)(std::string_view a_json);" in sync_h,
    "SendFn must return whether the packet went out, so the log can name a failed send",
)
report_send = send_report.find("!g_send(packet.dump())")
report_failure = send_report.find('"could not send the report for quest={} stage={} completed={}"')
reported = send_report.find('"reported quest={} stage={} completed={}"')
check(
    0 <= report_send < reported,
    "SendReport must only log 'reported' when the send returned true; a failed send must not claim it was reported",
)
check(
    0 <= report_failure < reported,
    "a failed report send must log its own line instead of 'reported quest='",
)

request_scope = body_after(sync, "void RequestScope(std::string_view a_quest, QuestLogic::Scope a_scope)")
check(request_scope is not None, "F4TQuestSync.cpp must still define RequestScope")
request_scope = request_scope or ""
scope_send = request_scope.find("!g_send(packet.dump())")
scope_failure = request_scope.find('"could not request the scope quest={} scope={}"')
scope_requested = request_scope.find('"requested scope quest={} scope={}"')
check(
    0 <= scope_send < scope_requested,
    "RequestScope must only log 'requested scope' when the send returned true; a failed send must not claim it was requested",
)
check(
    0 <= scope_failure < scope_requested,
    "a failed scope send must log its own line instead of 'requested scope'",
)

reset = body_after(sync, "void Reset()")
check(reset is not None, "F4TQuestSync.cpp must still define Reset")
reset = reset or ""
check(
    "QuestInbound" not in reset,
    "Reset runs after the welcome burst and must not touch the inbound queue or the capability",
)

note_welcome_impl = body_after(sync, "void NoteWelcome(const nlohmann::json& a_packet)")
check(note_welcome_impl is not None, "F4TQuestSync.cpp must still define NoteWelcome")
note_welcome_impl = note_welcome_impl or ""
check(
    "QuestInbound::NoteConnection(advertised)" in note_welcome_impl,
    "NoteWelcome must hand the advertised capability to QuestInbound::NoteConnection",
)
check(
    "kCapability" in note_welcome_impl,
    "NoteWelcome must compare the welcome capabilities against F4TQuestSync.h:kCapability",
)

clear_session_wrapper = body_after(sync, "void ClearSession()")
check(clear_session_wrapper is not None, "F4TQuestSync.cpp must define ClearSession")
check(
    clear_session_wrapper is not None and "QuestInbound::ClearSession()" in clear_session_wrapper,
    "QuestSync::ClearSession must forward to QuestInbound::ClearSession",
)

handle_packet = body_after(inbound_impl, "bool HandlePacket(std::string_view a_type, const nlohmann::json& a_packet)")
check(handle_packet is not None, "F4TQuestInbound.cpp must still define HandlePacket")
handle_packet = handle_packet or ""
check(
    handle_packet.count("return false;") == 1,
    "HandlePacket must return false only for a type it does not recognise, so other packet types keep the untouched chain",
)
check(
    "if (!IsActive()) {" in handle_packet,
    "HandlePacket must consume a quest packet without queueing it while the capability is absent",
)
check(
    "QuestWire::kNotReadyType" in handle_packet,
    "HandlePacket must recognise the questNotReady frame so the rejection never reaches HandleUnknownPacket",
)
drain = body_after(inbound_impl, "void Drain(QuestLogic::Tracker& a_tracker)") or ""
check(
    "Item::Kind::Notice" in drain and "F4T::QuestNotices::Push(" in drain and "ShowHud(" in drain,
    "Drain must show the questNotReady rejection as a notice and a HUD message on the game thread",
)
show_hud = body_after(sync, "void ShowQuestHud(") or ""
check(
    "SetHudFunction(&ShowQuestHud)" in sync and "RE::SendHUDMessage::ShowHUDMessage(" in show_hud,
    "F4TQuestSync.cpp must register the real HUD callback, so the inbound target needs no CommonLib",
)

for wrapper in ("void ApplyRemoteCompletions(", "void ApplyHeldStages("):
    wrapper_body = body_after(sync, wrapper)
    check(wrapper_body is not None, f"F4TQuestSync.cpp must define {wrapper}")
    wrapper_body = wrapper_body or ""
    check(
        "MakeApplyHost(a_loadOrder, false" in wrapper_body and "PlayerBusy" not in wrapper_body,
        "quest commands are queued while the player is free; COQuestSyncScript owns the menu and scene wait",
    )

remote_apply = body_after(apply_src, "void ApplyRemoteCompletions(")
check(remote_apply is not None, "F4TQuestApply.cpp must define ApplyRemoteCompletions")
remote_apply = remote_apply or ""
take = remote_apply.find("TakeRemoteCompletions()")
busy_check = remote_apply.find("quest->busy")
issue = remote_apply.find("CompleteQuest(")
restore = remote_apply.find("RestoreRemoteCompletions(")
check(
    0 <= take < busy_check < issue and busy_check < restore,
    "a busy completion must stay queued: take, then skip CompleteQuest while quest->busy, then RestoreRemoteCompletions",
)
complete_fn = body_after(apply_src, "void CompleteQuest(")
check(
    complete_fn is not None and "enqueue(" in (complete_fn or "") and "issue(" not in apply_src,
    "a free completion must be queued for the script",
)

held_apply = body_after(apply_src, "void ApplyHeldStages(")
check(held_apply is not None and "enqueue(" in (held_apply or ""), "a free stage must be queued for the script")
check(
    held_apply is not None and (held_apply or "").find("enqueue(") < (held_apply or "").find("entry->completed"),
    "a stage command is queued before the completion that rides on it",
)

for label, source in (
    ("F4TQuestSync.cpp", sync),
    ("F4TQuestApply.cpp", apply_src),
    ("F4TQuestPapyrus.cpp", papyrus_cpp),
    ("F4TQuestApplyTests.cpp", apply_tests),
):
    for banned in ("setstage", "completequest", "ExecuteCommand", "kCompleted", "objectives", "inDialog", "GetCurrentScene", "DialogueMenu", "PlayerBusy"):
        check(banned not in source, f"{label} must not bring back {banned}")

check("TODO-verify" not in sync, "the completequest TODO-verify line is retired; Papyrus Quest.CompleteQuest covers it")
check("kPollInterval = 500ms" in sync, "the local progress poll stays at 500 ms")
check("(quest->data.flags & kQuestEnabledFlag)" in sync, "flags bit 0 stays a direct read")
check("currentStage > 0" in sync and "reported->running" in sync, "started is the script IsRunning report and a current stage above 0")

restore_body = body_after(logic_src, "void Tracker::RestoreRemoteCompletions(")
check(
    restore_body is not None and "remoteCompletions_" in (restore_body or ""),
    "RestoreRemoteCompletions must put the deferred completions back on the queue",
)

check(
    "void TestBusyCompletionStaysQueuedUntilThePlayerIsFree()" in apply_tests
    and "TestBusyCompletionStaysQueuedUntilThePlayerIsFree();" in apply_tests
    and "commands.empty()" in apply_tests
    and "stage == -1" in apply_tests,
    "F4TQuestApplyTests must keep a busy completion queued and enqueue each complete once, in order",
)

for native in (
    "HasCommand",
    "NextCommandQuest",
    "NextCommandStage",
    "PopCommand",
    "ReportApplied",
    "QueryCount",
    "QueryQuest",
    "ReportState",
):
    check(
        f'BindNativeMethod("CommonwealthOnlineQuest", "{native}"' in papyrus_cpp,
        f"CommonwealthOnlineQuest.{native} must be bound",
    )
check(
    "QuestPapyrus::RegisterPapyrus" in main_src,
    "main.cpp must register the quest Papyrus natives with the F4SE papyrus interface",
)
check("Function HasCommand()" in native_script and "Function ReportState(" in native_script, "CommonwealthOnlineQuest.psc must declare the natives")
check(
    "Function ReportState(Quest akQuest, Bool abCompleted, Bool abRunning)" in native_script,
    "ReportState must take the quest, not a query-list index, so a replaced query list cannot shift a report",
)
check(
    "ReportState(target, target.IsCompleted(), target.IsRunning())" in quest_script,
    "COQuestSyncScript must report completed and running on the quest object it read",
)
check("ReportState(index," not in quest_script, "the script must not report a query-list index")
check(
    "g_reports[a_quest->GetFormID()]" in papyrus_cpp,
    "ReportState must store the report under the quest form id",
)

check("kQuestQueryBatch = 32" in sync, "the per-tick quest query batch must stay bounded")
check(
    "g_queryCursor" in sync and "g_candidates" in sync and "g_publishedBatch" in sync,
    "the tick must keep a round-robin query cursor and the batch the script was asked about",
)
check(
    "(std::min)(kQuestQueryBatch, total)" in sync,
    "a tick must publish at most one batch, so a full quest sweep spreads across ticks",
)

check(
    "WarnIfQuestScriptSilent();" in tick,
    "the in-world tick must run the silent-script watchdog",
)
check(
    "CommonwealthOnline.esp is missing its quest script" in sync
    and "RE::SendHUDMessage::ShowHUDMessage(kQuestScriptSilentNotice.data(), nullptr, true, true)" in sync,
    "a silent quest script must log and show the one HUD notice that names the missing quest script",
)
check(
    "kScriptSilenceMilliseconds = 10'000" in sync and "g_scriptSilenceWarned" in sync,
    "the silent-script warning must wait ~10 s and must not repeat every tick",
)

check(
    "F4T::SessionCharacterSetupFlow::EnsureRunning()" in session,
    "a returning character's save load must ensure CO_CharacterSetupQuest is running",
)
check(
    "void EnsureRunning()" in setup_flow
    and '"startquest CO_CharacterSetupQuest"' in setup_flow
    and "QuestFlag::kEnabled" in setup_flow,
    "EnsureRunning must start CO_CharacterSetupQuest only while its running bit is clear",
)
check(
    '"cqf CO_CharacterSetupQuest Prime"' in setup_flow,
    "a running CO_CharacterSetupQuest must be primed on load so an inert script starts its timer",
)

check(
    "void TestRunningQuestAtStageZeroIsHeld()" in apply_tests
    and "TestRunningQuestAtStageZeroIsHeld();" in apply_tests,
    "F4TQuestApplyTests must pin that a running quest at stage 0 does not count as started",
)

menu = quest_script.find("Utility.IsInMenuMode()")
scene = quest_script.find("IsInScene()")
pop = quest_script.find("PopCommand()")
stage_call = quest_script.find("SetCurrentStageID(")
complete_call = quest_script.find("CompleteQuest()")
check(quest_script.find("StartTimer(0.5") >= 0, "COQuestSyncScript must arm a 0.5 s timer")
check("OnPlayerLoadGame" in quest_script and "OnQuestInit()" in quest_script, "the timer starts on quest init and on player load")
check(
    0 <= menu < pop and 0 <= scene < pop and pop < stage_call and pop < complete_call,
    "a menu or a scene must leave commands queued; the script pops one command and then applies stage or complete",
)

for name in ("COQuestSyncScript", "CommonwealthOnlineQuest"):
    check(f'"!BASE_NAME!"=="{name}"' in stage_mod, f"stage-mod.bat must stage {name}")
    check(f"Scripts\\{name}.pex" in stage_mod, f"stage-mod.bat must require {name}.pex")
    check(f"Scripts\\Source\\User\\{name}.psc" in stage_mod, f"stage-mod.bat must require {name}.psc")
    check(f"Scripts/{name}.pex" in package, f"package-release.sh must require {name}.pex")
    check(f"Scripts/Source/User/{name}.psc" in package, f"package-release.sh must require {name}.psc")

def _balanced_params(text, open_index):
    depth = 0
    for index in range(open_index, len(text)):
        char = text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[open_index + 1:index], index
    return None, None


def _normalize_params(raw):
    parts = []
    current = []
    depth = 0
    for char in raw:
        if char in "<([{":
            depth += 1
        elif char in ">)]}":
            depth = max(0, depth - 1)
        if char == "," and depth == 0:
            parts.append("".join(current))
            current = []
            continue
        current.append(char)
    if "".join(current).strip():
        parts.append("".join(current))
    cleaned = []
    for part in parts:
        chunk = []
        depth = 0
        for char in part:
            if char in "<([{":
                depth += 1
            elif char in ">)]}":
                depth = max(0, depth - 1)
            if char == "=" and depth == 0:
                break
            chunk.append(char)
        cleaned.append(" ".join("".join(chunk).split()))
    return ", ".join(cleaned)


def _signature(text, pattern):
    match = re.search(pattern, text)
    if match is None:
        return None
    params, close_index = _balanced_params(text, match.end() - 1)
    if params is None:
        return None
    rest = text[close_index + 1:]
    stops = [index for index in (rest.find("{"), rest.find(";")) if index >= 0]
    rest = rest[:min(stops)] if stops else rest[:80]
    qualifiers = tuple(name for name in ("const", "noexcept") if re.search(r"\b" + name + r"\b", rest))
    return (_normalize_params(params), qualifiers)


SERVER_SRC = ROOT / "CO-SERVER/server-cpp/src"
QUEST_H = SERVER_SRC / "ServerQuest.h"
QUEST_CPP = SERVER_SRC / "ServerQuest.cpp"
QUEST_XMAKE = ROOT / "CO-SERVER/server-cpp/xmake.lua"

check(not (SERVER_SRC / "ServerQuestImpl.inc").exists(), "ServerQuestImpl.inc must stay deleted; quest handlers live in ServerQuest.cpp")
check(not (SERVER_SRC / "ServerQuestStore.inc").exists(), "ServerQuestStore.inc must stay deleted; quest state lives in ServerQuest.h/.cpp")
quest_h = read(QUEST_H)
quest_cpp = read(QUEST_CPP)
quest_xmake = read(QUEST_XMAKE)
quest_owner = read(SERVER_SRC / "ServerOwnedInventory.inc")
for symbol in (
    "HandleQuestReport", "HandleQuestScope", "DispatchQuestPacket", "ExistingQuestFrames",
    "EnableQuestSync", "QuestSyncEnabled", "QuestMode", "QuestStatus", "IsQuestGated",
    "IsQuestSolo", "SetQuestMode", "SetQuestGate", "SetQuestSolo", "SetQuestScope",
    "SaveQuests", "SupportsQuestSync", "QuestConnectionForProfile", "CommitQuestScope",
    "QuestAudience", "QuestFactionOfPlayer", "BroadcastQuestStage", "QuestSyncFrames",
    "QuestPolicyFor", "QuestPolicyPacket", "QuestPolicyFrames",
):
    check(f"ProtocolHost::Impl::{symbol}(" in quest_cpp, f"ServerQuest.cpp must define ProtocolHost::Impl::{symbol}")
    check(f" {symbol}(" in quest_h, f"ServerQuest.h must declare {symbol}")
    definition = _signature(quest_cpp, "ProtocolHost::Impl::" + re.escape(symbol) + r"\s*\(")
    declaration = _signature(quest_h, r"\b" + re.escape(symbol) + r"\s*\(")
    check(definition is not None, f"ServerQuest.cpp:{symbol} must keep a full parameter list")
    check(definition is not None and declaration is not None and definition == declaration, f"ServerQuest.cpp:{symbol} signature {definition} must match the ServerQuest.h declaration {declaration}")
for var in ("questsEnabled{};", "questPersistence{};", "questOverrides;", "quests;"):
    check(var in quest_h, f"ServerQuest.h must keep the quest member {var}")
check('"ServerQuest.h"' in quest_owner, "ServerOwnedInventory.inc must include ServerQuest.h")
check("ServerQuestStore.inc" not in quest_owner and "ServerQuestImpl.inc" not in quest_owner, "ServerOwnedInventory.inc must not include the deleted quest .inc files")
check('add_files("src/ServerQuest.cpp")' in quest_xmake, "xmake must build src/ServerQuest.cpp")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    print(
        "F4TQuestWiringContract: the quest sync path is incomplete. The dispatch, the welcome hook "
        "and the session clear are the three lines that make F4TQuestInbound.cpp:HandlePacket and "
        "F4TQuestSync.cpp:NoteWelcome reachable; see docs/code-notes/CO-BETA-plugin.md."
    )
    sys.exit(1)

print("F4TQuestWiringContract: all checks passed.")
