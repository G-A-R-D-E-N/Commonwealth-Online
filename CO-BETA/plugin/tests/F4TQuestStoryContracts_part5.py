#!/usr/bin/env python3
"""F4TQuestStoryContracts_part5: consolidated contract sections.

Consolidated from 2 single-contract scripts so the group runs one process
instead of 2. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TQuestStoryContracts_part5.py
"""
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[3]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok


def _section_010_f4tquestwiringcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TQuestWiringContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TQuestWiringContract.py")
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
        "g_pendingReports.push_back(a_report);" in send_report and "FlushPendingReport();" in send_report,
        "SendReport must queue into g_pendingReports and call FlushPendingReport, so the report waits for the gates",
    )

    flush_report = body_after(sync, "void FlushPendingReport()")
    check(flush_report is not None, "F4TQuestSync.cpp must still define FlushPendingReport")
    flush_report = flush_report or ""
    check(
        "QuestInbound::IsActive()" in flush_report and "IsConnectedToServer()" in flush_report,
        "SendReport must refuse to send unless the server advertised quest-sync-v1 on this connection",
    )

    poll_local = body_after(sync, "void PollLocalProgress(std::uint64_t a_nowMilliseconds)")
    check(poll_local is not None and "FlushPendingReport();" in poll_local, "PollLocalProgress must call FlushPendingReport so a held report retries")

    sync_h = read(PLUGIN / "include/F4TQuestSync.h")
    check(
        "using SendFn = bool (*)(std::string_view a_json);" in sync_h,
        "SendFn must return whether the packet went out, so the log can name a failed send",
    )
    add_capabilities = body_after(sync_h, "inline void AddSessionCapabilities(F4T::ClientCompatibility::Metadata& a_metadata)")
    check(
        add_capabilities is not None and "a_metadata.capabilities.emplace_back(kFireteamCapability);" in add_capabilities,
        "AddSessionCapabilities must advertise quest-fireteam-v1, or the server never sets fireteamSupported",
    )
    check(
        'kFireteamCapability = "quest-fireteam-v1"' in sync_h
        and 'kFireteamCapability = "quest-fireteam-v1"' in read(ROOT / "co-core/include/F4TQuestProtocol.h"),
        "F4TQuestSync.h must name quest-fireteam-v1 exactly as co-core's F4TQuestProtocol.h does",
    )
    report_send = flush_report.find("!g_send(packet.dump())")
    report_failure = flush_report.find('"could not send the report for quest={} stage={} completed={}"')
    reported = flush_report.find('"reported quest={} stage={} completed={}"')
    check(
        0 <= report_send < reported,
        "SendReport must only log 'reported' when the send returned true; a failed send must not claim it was reported",
    )
    check(
        0 <= report_failure < reported,
        "a failed report send must log its own line instead of 'reported quest='",
    )
    erase_report = flush_report.find("g_pendingReports.erase(g_pendingReports.begin());")
    check(
        reported < erase_report,
        "a report leaves the queue only after its 'reported' log, so a failed send keeps it queued",
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

    check(
        0 <= request_scope.find("if (a_scope == QuestLogic::Scope::Fireteam && !ViewFireteamSupported()) {") < scope_send,
        "RequestScope must refuse a fireteam scope locally before sending when the server did not advertise fireteam support",
    )

    reset = body_after(sync, "void Reset()")
    check(reset is not None, "F4TQuestSync.cpp must still define Reset")
    reset = reset or ""
    check(
        "QuestInbound" not in reset,
        "Reset runs after the welcome burst and must not touch the inbound queue or the capability",
    )
    check(
        "g_pendingReports.clear();" in reset,
        "Reset must drop g_pendingReports so a report held for the old session is never sent in the next one",
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
    story_check = remote_apply.find("IsStoryReached(")
    check(
        busy_check < story_check < issue,
        "a completion waits for the story to reach its quest: IsStoryReached sits between the busy check and CompleteQuest",
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
        for banned in ("setstage", "completequest", "ExecuteCommand", "kCompleted", "objectives", "inDialog", "GetCurrentScene", "DialogueMenu", "PlayerBusy", "startquest"):
            check(banned not in source, f"{label} must not bring back {banned}")
    # constraint: only this module reads quest objectives, and it must stay read-only.
    ASSIST_ORIGIN = PLUGIN / "src/F4TQuestAssistCapture.cpp"
    assist_origin = read(ASSIST_ORIGIN)
    check(
        "objectives" in assist_origin,
        "F4TQuestAssistCapture.cpp must be the file that reads quest objectives; if that moved, move this check with it",
    )
    for banned in (
        "setstage",
        "completequest",
        "executecommand",
        "kcompleted",
        "setcurrentstageid",
        "setobjectivecompleted",
        "setobjectivedisplayed",
        "setobjectivefailed",
    ):
        check(banned not in assist_origin.lower(), f"F4TQuestAssistCapture.cpp must stay read-only, found {banned}")

    check("TODO-verify" not in sync, "the completequest TODO-verify line is retired; Papyrus Quest.CompleteQuest covers it")
    check("kPollInterval = 500ms" in sync, "the local progress poll stays at 500 ms")
    check("(quest->data.flags & kQuestEnabledFlag)" in sync, "flags bit 0 stays a direct read")
    check("currentStage > 0" in sync and "reported->running" in sync, "started is the script IsRunning report and a current stage above 0")
    check("QuestAssistCapture::Process(nowMilliseconds);" in sync, "assist origination runs on the quest tick, after the credit flush")
    check(
        "QuestAssistCapture::SetOriginating(ViewFireteamSupported() && F4T::Networking::IsConnectedToServer());" in sync,
        "assist origination is gated on the fireteam support flag and the live connection",
    )
    check(
        all("COMMONWEALTH_ONLINE_QUEST_ASSIST" not in source for source in (sync, read(PLUGIN / "src/F4TQuestAssistOrigin.cpp"), assist_origin)),
        "assist origination has no environment variable gate",
    )

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
    check(
        "!a_applied && command.attempts + 1 < kMaximumApplyAttempts" in papyrus_cpp
        and "constexpr int kMaximumApplyAttempts = 3;" in papyrus_cpp,
        "a failed apply must be re-queued up to kMaximumApplyAttempts for every command kind",
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
        "void TestUnstartedQuestAppliesHeldStage()" in apply_tests
        and "TestUnstartedQuestAppliesHeldStage();" in apply_tests,
        "F4TQuestApplyTests must pin that a quest which has not started receives its held stage",
    )
    check(
        "void TestDependentStageWaitsForItsPrerequisite()" in apply_tests
        and "TestDependentStageWaitsForItsPrerequisite();" in apply_tests
        and "void TestDependentCompletionWaitsForItsPrerequisite()" in apply_tests
        and "TestDependentCompletionWaitsForItsPrerequisite();" in apply_tests,
        "F4TQuestApplyTests must pin that a dependent stage and completion wait for their story prerequisite",
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
    start_call = quest_script.find("target.Start()")
    start_guard = quest_script.find("If !target.IsRunning() && !target.IsCompleted()")
    stage_branch = quest_script.find("If stage < 0")
    stage_else = quest_script.find("Else", stage_branch)
    check(
        0 <= pop < start_guard < start_call < stage_branch < stage_else < stage_call,
        "a stage or completion for a quest that is not running starts it after the pop and before the stage or complete call",
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
        _merge_exit(1)

    print("F4TQuestWiringContract: all checks passed.")

def _section_011_f4tsleepcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TSleepContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSleepContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        if not path.exists():
            failures.append(f"missing {path.relative_to(ROOT).as_posix()}")
            return ""
        return path.read_text(encoding="utf-8")


    for path in (
        PLUGIN / "include/F4TSleepProxy.h",
        PLUGIN / "src/F4TSleepProxy.cpp",
        PLUGIN / "include/F4TSleepBedHook.h",
        PLUGIN / "src/F4TSleepBedHook.cpp",
        PLUGIN / "include/F4THookThreadTable.h",
    ):
        check(path.exists(), f"missing {path.relative_to(ROOT).as_posix()}")

    identity = read(PLUGIN / "src/F4TClientIdentity.cpp")
    outbound = read(PLUGIN / "src/F4TNetworkingOutbound.cpp")
    hook = read(PLUGIN / "src/F4TSleepBedHook.cpp")
    thread_table = read(PLUGIN / "include/F4THookThreadTable.h")
    seat = read(PLUGIN / "src/F4TFurnitureSeatSync.cpp")
    proxy = read(PLUGIN / "src/F4TSleepProxy.cpp")
    runtime = read(PLUGIN / "src/F4TReplicationRuntime.cpp")
    world_state = read(PLUGIN / "src/F4TWorldStateSync.cpp")

    check("F4T::SleepBedIdentity::kCapabilityName" in identity, "client must advertise sleep-bed-v1")
    check("bool SendSleepUsePacket(const nlohmann::json& a_packet)" in outbound, "networking must define SendSleepUsePacket")
    check("0x48, 0x89, 0x5C, 0x24, 0x08" in hook, "sleep bed hook must carry the 26-byte prologue")
    check("SleepBed" in thread_table, "hook thread table must name SleepBed")
    check("PutActorInChairBedQuick" not in seat, "seat sync must not seat the proxy with PutActorInChairBedQuick")
    check("PutActorInChairBedQuick" not in proxy, "sleep proxy must not skip the bed entry animation")
    check("SetLinkedRef" in proxy and "EvaluatePackage" in proxy, "sleep proxy must request the linked-bed package")
    check("occupiedFurnitureMarkerID" in proxy, "sleep proxy must verify the occupied marker")
    check("SleepProxy::Tick" in runtime, "runtime must tick the sleep proxy")
    check("SleepController::Tick" in runtime, "runtime must tick the sleep controller")
    check("SleepClock::Tick" in runtime, "runtime must tick the sleep clock")
    check("SleepClock" in world_state, "world state sync must reference the sleep clock")

    controller = read(PLUGIN / "src/F4TSleepController.cpp")
    check("SleepClock::Begin" in controller and "SleepClock::End" in controller, "controller must bracket sleep with the shared clock")
    check("MakeSleepEnter" in controller and "MakeSleepExit" in controller, "controller must send sleep enter and exit")
    check("AdvanceSleep" in controller and "NativeStop" in controller and "NativeInterrupt" in controller, "controller must drive and abort the native sleep")
    check("SleepController::OnDisconnect" in runtime, "runtime must stop the sleep on a session change")

    for path in sorted((PLUGIN / "src").glob("*.cpp")) + sorted((PLUGIN / "include").glob("*.h")):
        check("WellRested" not in path.read_text(encoding="utf-8"),
              f"{path.relative_to(ROOT).as_posix()} must not cast or name WellRested")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TSleepContract: all checks passed.")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TQuestWiringContract.py", _section_010_f4tquestwiringcontract),
    ("CO-BETA/plugin/tests/F4TSleepContract.py", _section_011_f4tsleepcontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TQuestStoryContracts_part5: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
