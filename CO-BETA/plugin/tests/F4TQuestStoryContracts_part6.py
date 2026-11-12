#!/usr/bin/env python3
"""F4TQuestStoryContracts_part6: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TQuestStoryContracts_part6.py
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




def _section_013_f4ttutorialquestgatecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TTutorialQuestGateContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TTutorialQuestGateContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"
    SERVER_SRC = ROOT / "CO-SERVER/server-cpp/src"
    WIRE_H = PLUGIN / "include/F4TQuestWire.h"
    WIRE_CPP = PLUGIN / "src/F4TQuestWire.cpp"
    SYNC = PLUGIN / "src/F4TQuestSync.cpp"
    READY_GATE = PLUGIN / "src/F4TQuestReadyGate.cpp"
    QUESTUI = PLUGIN / "src/F4TQuestUI.cpp"
    SESSION_TYPES = SERVER_SRC / "ServerSessionTypes.h"
    SERVER_QUEST = SERVER_SRC / "ServerQuest.cpp"
    TEST_SUPPORT = SERVER_SRC.parent / "tests/ServerQuestTestSupport.h"
    SERVER_TESTS = SERVER_SRC.parent / "tests/ServerQuestPartyTests.cpp"
    QUESTS_JS = ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/quests/quests.js"
    QUESTS_TEST = ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/quests/quests.test.js"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        try:
            return path.read_text(encoding="utf-8")
        except OSError:
            failures.append(f"missing {path}")
            return ""


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


    wire_h = read(WIRE_H)
    wire_cpp = read(WIRE_CPP)
    sync = read(SYNC)
    ready_gate = read(READY_GATE)
    questui = read(QUESTUI)
    session_types = read(SESSION_TYPES)
    server_quest = read(SERVER_QUEST)
    test_support = read(TEST_SUPPORT)
    server_tests = read(SERVER_TESTS)
    quests_js = read(QUESTS_JS)
    quests_test = read(QUESTS_TEST)

    check(
        'kReadyType = "questReady"' in wire_h,
        "F4TQuestWire.h must name the questReady client signal next to the other quest types",
    )
    check(
        "Json EncodeReady();" in wire_h,
        "F4TQuestWire.h must declare EncodeReady",
    )
    check(
        "Json EncodeReady()" in wire_cpp and "kReadyType" in wire_cpp,
        "F4TQuestWire.cpp must encode the questReady signal from kReadyType",
    )

    check(
        '#include "F4TSessionCharacterSetupFlow.h"' in sync,
        "F4TQuestSync.cpp must include the character setup flow for the pod-exit signal",
    )
    check(
        "F4T::SessionCharacterSetupFlow::IsPodExitSignal()" in sync,
        "the tutorial gate must read the MQ102 pod-exit signal, so leaving the tutorial unlocks quests",
    )
    send_report = body_after(sync, "void FlushPendingReport()")
    check(
        send_report is not None and "TutorialComplete()" in (send_report or ""),
        "FlushPendingReport must refuse to send while the tutorial is unfinished, so no report leaves early",
    )
    request_scope = body_after(sync, "void RequestScope(std::string_view a_quest, QuestLogic::Scope a_scope)")
    check(
        request_scope is not None and "TutorialComplete()" in (request_scope or ""),
        "RequestScope must refuse to send while the tutorial is unfinished",
    )
    check(
        send_report is not None and "!g_questReadySent" in (send_report or ""),
        "FlushPendingReport must also wait for the sent questReady, or the server drops the report after Observe consumed it",
    )
    check(
        request_scope is not None and "!g_questReadySent" in (request_scope or ""),
        "RequestScope must also wait for the sent questReady instead of sending into the server drop",
    )
    poll = body_after(sync, "void PollLocalProgress(std::uint64_t a_nowMilliseconds)")
    check(
        poll is not None
        and "g_questReadySent.load(std::memory_order_acquire)" in (poll or "")
        and "g_tracker.Observe(observation, a_nowMilliseconds)" in (poll or ""),
        "PollLocalProgress must not observe before questReady is sent, or the baseline advance loses the stage",
    )
    check(
        poll is not None and "entry.key == kTutorialQuestKey" not in (poll or ""),
        "the tutorial quest must reach the server through the explicit report, not through a polling exception",
    )
    tick = body_after(sync, "void Tick()")
    check(
        tick is not None and "DriveTutorialGate()" in (tick or "") and "g_questReadySent.store(" in (tick or ""),
        "Tick must drive the tutorial report/ready gate once the tutorial completes",
    )
    check(
        tick is not None and "QuestInbound::TakeReadyRetry()" in (tick or "") and "NoteRefused(NowMilliseconds())" in (tick or ""),
        "Tick must retry a refused questReady with backoff instead of sending it once",
    )
    gate_body = body_after(sync, "void DriveTutorialGate()")
    check(
        gate_body is not None and "QuestReady::Drive(" in (gate_body or ""),
        "DriveTutorialGate must delegate the report/ready ordering to the tested ready gate",
    )
    drive_body = body_after(ready_gate, "DriveResult Drive(")
    check(
        drive_body is not None
        and "Gate::Action::Report" in (drive_body or "")
        and "NoteReportSent(" in (drive_body or "")
        and (drive_body or "").find("Gate::Action::Report") < (drive_body or "").find("a_sendReady()"),
        "the tutorial report must be sent before questReady, so the server can verify the exit",
    )
    check(
        "_backoffMilliseconds = kInitialBackoffMilliseconds;" not in (body_after(ready_gate, "void Gate::NoteReadySent(") or ""),
        "NoteReadySent must not reset the backoff, or a refusing server turns the retry into a one-per-second loop",
    )
    check(
        "kMaximumAttempts" in ready_gate and "Exhausted()" in ready_gate,
        "the ready gate must cap the per-session attempts instead of retrying forever",
    )
    clear_session = body_after(sync, "void ClearSession()")
    check(
        "g_questReadyResetRequested = true;" in (clear_session or ""),
        "ClearSession must request the questReady reset so a reconnect resends it from the tick thread",
    )
    check(
        "g_questReadySent.store(false" in (clear_session or "") and "ClearReadyRetry()" in (clear_session or ""),
        "ClearSession must clear the sent flag and the queued retry, or a new session starts with a stale ready",
    )

    check(
        "F4T::SessionCharacterSetupFlow::IsPodExitSignal()" in questui,
        "F4TQuestUI.cpp must gate the quest window on the pod-exit signal",
    )
    enabled = body_after(questui, "bool IsEnabled()")
    check(
        enabled is not None and "F4T::Networking::IsConnectedToServer()" in enabled
        and "F4T::QuestInbound::IsActive()" in enabled
        and "IsPodExitSignal()" in enabled,
        "IsEnabled must stay connected plus quest-sync-v1 plus pod exit, or the tab opens in the tutorial",
    )
    check(
        '":tutorial:"' in questui and '":ready:"' in questui,
        "BuildSignature must change on pod exit so the window repaints when the tutorial completes",
    )
    check(
        '{ "tutorial"' in questui,
        "the quest state JSON must carry the tutorial flag for the Quests tab message",
    )
    check(
        "questSyncLive" in (body_after(questui, "bool BuildStateJson(") or "")
        and "QuestInbound::IsActive()" in (body_after(questui, "bool BuildStateJson(") or ""),
        "the tutorial flag must be gated on the live quest-sync connection, or servers without quests show the intro message",
    )

    check(
        "bool questReady{};" in session_types,
        "ServerSessionTypes.h must keep the per-session questReady flag next to the quest buckets",
    )
    check(
        'a_type == "questReady"' in server_quest and "a_session.questReady = true" in server_quest,
        "DispatchQuestPacket must flip the session questReady flag on the client signal",
    )
    handle_report = body_after(
        server_quest,
        "std::vector<OutboundFrame> ProtocolHost::Impl::HandleQuestReport(",
    )
    check(
        handle_report is not None and "!a_session.questReady" in (handle_report or "")
        and "questReportsRejected++" in (handle_report or ""),
        "HandleQuestReport must reject reports from a session that never sent questReady",
    )
    handle_scope = body_after(
        server_quest,
        "std::vector<OutboundFrame> ProtocolHost::Impl::HandleQuestScope(",
    )
    check(
        handle_scope is not None and "!a_session.questReady" in (handle_scope or "")
        and "questScopesRejected++" in (handle_scope or ""),
        "HandleQuestScope must reject scope requests from a session that never sent questReady",
    )
    check(
        '"questReady"' in test_support,
        "the quest test join helper must send questReady so existing tests model a post-tutorial client",
    )
    check(
        "TestTutorialGateRejectsQuestWritesUntilReady" in server_tests
        and "TestTutorialGateRejectsQuestWritesUntilReady();" in server_tests,
        "ServerQuestPartyTests must pin that writes are rejected before questReady and applied after",
    )

    check(
        "Complete the intro to unlock quests" in quests_js,
        "the Quests tab must explain the tutorial lock instead of the unavailable-server message",
    )
    check(
        "tutorial: true" in quests_test and "Complete the intro to unlock quests" in quests_test,
        "quests.test.js must pin the tutorial-locked tab message",
    )

    PROTOCOL_H = ROOT / "co-core/include/F4TQuestProtocol.h"
    WORLD_POLICY = SERVER_SRC / "ServerWorldPolicy.inc"
    SYNC_H = PLUGIN / "include/F4TQuestSync.h"
    INBOUND_CPP = PLUGIN / "src/F4TQuestInbound.cpp"
    NOTICES_H = PLUGIN / "include/F4TQuestNotices.h"
    NOTICES_CPP = PLUGIN / "src/F4TQuestNotices.cpp"

    protocol_h = read(PROTOCOL_H)
    world_policy = read(WORLD_POLICY)
    sync_h = read(SYNC_H)
    inbound_cpp = read(INBOUND_CPP)
    notices_h = read(NOTICES_H)
    notices_cpp = read(NOTICES_CPP)

    check(
        'kReadyCapability = "quest-ready-v1"' in protocol_h,
        "F4TQuestProtocol.h must name the quest-ready-v1 capability",
    )
    check(
        'kNotReadyType = "questNotReady"' in protocol_h and "MakeNotReady" in protocol_h,
        "F4TQuestProtocol.h must declare the questNotReady frame builder",
    )
    check(
        'kNotReadyReason = "notReady"' in protocol_h and 'kUpdateRequiredReason = "updateRequired"' in protocol_h,
        "the protocol must name the notReady and updateRequired reason codes",
    )
    check(
        "kReadyCapability" in world_policy,
        "the welcome capabilities must advertise quest-ready-v1 next to quest-sync-v1",
    )
    check(
        'kReadyCapability = "quest-ready-v1"' in sync_h
        and "a_metadata.capabilities.emplace_back(kReadyCapability);" in sync_h,
        "the client hello must advertise quest-ready-v1 next to quest-sync-v1",
    )
    check(
        "RequiresQuestReady" in server_quest
        and "RequiresQuestReady(a_session)" in (handle_report or "")
        and "RequiresQuestReady(a_session)" in (handle_scope or ""),
        "both blocked paths must gate the questReady requirement on the quest-ready-v1 capability",
    )
    check(
        '"questNotReady"' in server_quest or "kNotReadyType" in server_quest or "MakeNotReady" in server_quest,
        "the blocked paths must answer with a questNotReady frame instead of dropping silently",
    )
    check(
        "kNotReadyReason" in server_quest and "kUpdateRequiredReason" in server_quest,
        "rejections of ready-capable clients use notReady while legacy clients get one updateRequired notice",
    )
    check(
        "bool questUpdateNoticeSent{};" in session_types and "questUpdateNoticeSent" in server_quest,
        "the updateRequired notice must fire once per session, so legacy reports are not spammed",
    )
    check(
        "DecodeNotReady" in wire_h and "DecodeNotReady" in wire_cpp and "kNotReadyType" in wire_cpp,
        "F4TQuestWire must decode the questNotReady frame from the quest-ready-v1 type",
    )
    check(
        "Item::Kind::Notice" in inbound_cpp and "DecodeNotReady" in inbound_cpp,
        "QuestInbound must queue the questNotReady frame as a notice item",
    )
    drain = body_after(inbound_cpp, "void Drain(QuestLogic::Tracker& a_tracker)")
    check(
        drain is not None and "Item::Kind::Notice" in (drain or "")
        and "F4T::QuestNotices::Push(" in (drain or "")
        and "ShowHud(" in (drain or ""),
        "Drain must push the questNotReady notice and show it as a HUD message on the game thread",
    )
    show_hud = body_after(sync, "void ShowQuestHud(")
    check(
        "SetHudFunction(&ShowQuestHud)" in sync
        and show_hud is not None
        and "RE::SendHUDMessage::ShowHUDMessage(" in (show_hud or ""),
        "the HUD call must live in F4TQuestSync.cpp behind the inbound callback, or the test target needs CommonLib",
    )
    check(
        "FormatNotReady" in notices_h and "FormatNotReady" in notices_cpp,
        "the notices module must format the questNotReady text per reason code",
    )
    check(
        "Update the mod to sync quests." in notices_cpp,
        "the client must show legacy sessions the update notice with the mod-update text",
    )
    check(
        "kTutorialQuestLocalId = 0x0001CC2A" in protocol_h
        and 'kTutorialQuestPlugin = "fallout4.esm"' in protocol_h
        and "kTutorialExitStage = 2" in protocol_h,
        "the protocol must name the MQ102 exit assumption in one constant (UNVERIFIED live)",
    )
    check(
        "NoteTutorialProgress" in server_quest and "TutorialExitSeen" in server_quest,
        "the server must record per-profile tutorial progress and check it before accepting questReady",
    )
    check(
        '"tutorialExit"' in server_quest,
        "per-profile tutorial progress must persist in the quest blob so restarts do not strand ready clients",
    )
    check(
        "stats.questReadyRejected++" in server_quest,
        "a ready claim without the exit on record must be refused and counted, not silently dropped",
    )
    check(
        "TutorialExitSeen(a_session.profileId)" in server_quest,
        "questReady must be verified against the server record for that profile, not taken on assertion",
    )
    check(
        "kTutorialQuestKey" in sync and "SendTutorialReport(" in sync,
        "the client must report the tutorial quest from its current stage before questReady so the server sees the exit",
    )
    check(
        "std::atomic_bool g_questReadySent{ false };" in sync,
        "the ready flag must be atomic so the tick thread and the network thread agree on it",
    )
    tick = body_after(sync, "void Tick()") or ""
    credits = body_after(sync, "void FlushCredits(") or ""
    check(
        0 <= credits.find("*form == F4T::QuestProtocol::kTutorialQuestLocalId") < credits.find("QuestPapyrus::EnqueueCredit("),
        "remote tutorial quest credits must be skipped before they can change an objective",
    )
    gate = tick.find("if (!TutorialComplete() || !g_questReadySent.load(std::memory_order_acquire))")
    check(
        gate >= 0 and all(tick.find(call) > gate for call in (
            "ApplyRemoteCompletions(", "ApplyHeldStages(", "FlushCredits("
        )),
        "remote quest changes and credits must wait until the local tutorial is complete and quest-ready was sent",
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(
            "F4TTutorialQuestGateContract: the tutorial quest gate is incomplete. Reports and scope "
            "requests must wait for the MQ102 pod exit on the client and for questReady on the server; "
            "see docs/code-notes/CO-BETA-plugin.md."
        )
        _merge_exit(1)

    print("F4TTutorialQuestGateContract: all checks passed.")

def _section_014_f4tvoicerostercontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TVoiceRosterContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TVoiceRosterContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def strip_comments(text):
        return re.sub(r"//[^\n]*", "", text)


    def read_code(relative):
        return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


    def function_body(code, signature):
        start = code.find(signature)
        if start < 0:
            return ""
        brace = code.find("{", start)
        if brace < 0:
            return ""
        depth = 0
        for index in range(brace, len(code)):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    return code[brace:index + 1]
        return ""


    runtime = read_code("src/F4TVoiceRuntime.cpp")
    remote = read_code("src/F4TRemotePlayerState.cpp")

    read_roster = function_body(runtime, "void ReadRoster(RuntimeState& a_state, bool a_active)")
    check(read_roster != "", "ReadRoster must take the persistent RuntimeState and the active flag")
    check("GetRemotePlayerVoiceRoster(" in read_roster,
          "ReadRoster must read the light voice roster accessor")
    check("GetRemotePlayerSnapshot" not in read_roster,
          "ReadRoster must not deep-copy every remote player through GetRemotePlayerSnapshot")
    check("GetRemotePlayerSnapshot" not in runtime,
          "the voice runtime must not call GetRemotePlayerSnapshot")

    voice_roster = function_body(remote, "void GetRemotePlayerVoiceRoster(")
    check("std::scoped_lock lock(g_stateMutex);" in voice_roster and "a_out.clear();" in voice_roster,
          "GetRemotePlayerVoiceRoster must refill the caller's vector under g_stateMutex")
    check("GetRemotePlayerSnapshot" not in voice_roster and "RemotePlayerState>" not in voice_roster,
          "GetRemotePlayerVoiceRoster must not copy whole RemotePlayerState values")
    check("std::vector<RemotePlayerState> GetRemotePlayerSnapshot()" in remote,
          "GetRemotePlayerSnapshot must remain for its other callers")

    update_roster = function_body(runtime, "void UpdateRoster(")
    check("const auto before" not in update_roster and "before = a_state.session.Players();" in update_roster,
          "UpdateRoster must reuse the persistent rowsBefore buffer instead of a fresh copy each frame")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TVoiceRosterContract: all checks passed.")

def _section_015_f4tworldpolicycontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TWorldPolicyContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TWorldPolicyContract.py")
    from pathlib import Path
    import re
    import sys

    PLUGIN = Path(__file__).resolve().parents[1]
    REPO = PLUGIN.parents[1]
    failures = []
    pending = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        return re.sub(r"//[^\n]*", "", (PLUGIN / path).read_text(encoding="utf-8"))


    def read_root(path):
        return (REPO / path).read_text(encoding="utf-8")


    def function_body(text, signature):
        start = text.index("{", text.index(signature))
        depth = 0
        for position in range(start, len(text)):
            if text[position] == "{":
                depth += 1
            elif text[position] == "}":
                depth -= 1
                if depth == 0:
                    return text[start:position + 1]
        return text[start:]


    enforcer = read("src/F4TWorldPolicyEnforcer.cpp")
    enforcer_header = read("include/F4TWorldPolicyEnforcer.h")
    client = read("src/F4TWorldPolicyClient.cpp")
    client_header = read("include/F4TWorldPolicyClient.h")
    shim = read("src/F4TGnsWinsockShim.cpp")
    sync = read("src/F4TObjectSync.cpp")
    sync_header = read("include/F4TObjectSync.h")
    inbound = read("src/F4TNetworkingInbound.cpp")
    identity = read("src/F4TClientIdentity.cpp")
    ui_client = read("src/F4TUIClient.cpp")
    plugin_sources = "".join(read(source.relative_to(PLUGIN).as_posix()) for source in (PLUGIN / "src").glob("*.cpp"))
    co_core_header = read_root("co-core/include/F4TWorldPolicy.h")
    adapter = "".join(read_root(name) for name in (
        "co-core/src/protocol/F4TGnsProtocolAdapter.cpp",
        "co-core/src/protocol/F4TGnsProtocolAdapterInternal.h",
        "co-core/src/protocol/F4TGnsProtocolAdapterImpl.h",
        "co-core/src/protocol/F4TGnsProtocolAdapterOutbound.cpp",
        "co-core/src/protocol/F4TGnsProtocolAdapterInbound.cpp",
    ))

    check(not (PLUGIN / "include/F4TWorldPolicy.h").exists(),
          "the plugin must not shadow the co-core F4TWorldPolicy.h")
    check(not (PLUGIN / "include/F4TWorldPolicyWire.h").exists(),
          "the plugin must not keep a second world policy wire parser")
    check(not (PLUGIN / "src/F4TWorldPolicyWire.cpp").exists(),
          "the plugin must not keep a second world policy wire parser")
    check("Reassembler" not in enforcer and "g_reassembler" not in enforcer,
          "the enforcer must not reassemble; the protocol adapter's co-core reassembler is the single copy")
    check("worldPolicyReassembler.Apply(packet)" in adapter,
          "the protocol adapter must reassemble worldPolicy and worldPolicyPart through the co-core reassembler")
    check("kMaximumTotalDisableEntries" in enforcer,
          "the disabled-reference tracking cap must be the server's total disable cap")
    check("kMaximumTrackedDisabledRefs = F4T::WorldPolicy::kMaximumTotalDisableEntries" in enforcer,
          "the tracking cap must equal the 131072-entry policy list limit")

    check("HandleWorldPolicyPacket" not in enforcer and "HandleWorldPolicyPacket" not in enforcer_header
          and "HandleWorldPolicyPacket" not in plugin_sources,
          "the plugin-side packet handler must be gone; no plugin source may dispatch worldPolicy packets")
    check("void AcceptPolicy(const F4T::WorldPolicy::Policy& a_policy);" in enforcer_header,
          "the enforcer must expose AcceptPolicy for the adapter's completed policy")
    check("TryHandlePacket" not in enforcer_header, "the enforcer must not claim the faction handler name")
    check("TryHandlePacket" not in enforcer, "the enforcer must not define a second TryHandlePacket")
    check(inbound.count("Factions::TryHandlePacket(") == 1, "the factions inbound dispatch must stay the only inbound factions TryHandlePacket")
    check("worldPolicy" not in inbound, "the legacy inbound dispatch must not handle worldPolicy; the adapter consumes it")

    shim_wiring = re.search(
        r"const std::scoped_lock protocolLock\(state->protocolMutex\);\s*adapted = state->protocol\.AdaptInbound\([\s\S]*?"
        r"worldPolicy = state->protocol\.GetWorldPolicy\(\);\s*\}\s*if \(worldPolicy\) \{\s*"
        r"F4T::WorldPolicyEnforcer::AcceptPolicy\(\*worldPolicy\);\s*\}",
        shim)
    check(shim_wiring is not None,
          "the shim must read the adapted policy under protocolMutex right after AdaptInbound and pass it to AcceptPolicy after the lock is released")
    check('#include "F4TWorldPolicyEnforcer.h"' in shim, "the shim must include the enforcer header")

    check("kWorldPolicyPartType" not in enforcer and "kWorldPolicyType" not in enforcer,
          "the enforcer must not claim the worldPolicy packet types any more")

    check("MarkAsDeleted" not in enforcer and "MarkAsDeleted" not in client,
          "world policy must disable references, never delete them")
    check("DeleteOnly" not in client_header,
          "the ref action set must only offer Skip and Disable")
    check(re.search(r"RefAction DecideRefAction\(RefState a_state\)", client_header) is not None,
          "the ref decision must depend on the reference state alone")
    check(re.search(r"void ReleaseDisabledRefs\(\)[\s\S]{0,200}?ReleaseRef\(refId\)", enforcer) is not None,
          "every disabled reference must be released through ReleaseRef")

    enforcer_raw = (PLUGIN / "src/F4TWorldPolicyEnforcer.cpp").read_text(encoding="utf-8")
    hide_body = function_body(enforcer, "void HideQuest(")
    stop_body = function_body(enforcer, "void StopQuest(")
    set_body = function_body(enforcer, "bool TrySetShutdownStage(")
    restore_match = re.search(r"void RestoreQuestFlags\(\)\s*\{[\s\S]*?std::erase_if\(g_suppressedQuests[^\n]*\n\s*\}", enforcer)
    restore_body = restore_match.group(0) if restore_match else ""

    check("g_stoppedQuestFlags" not in enforcer and "g_suppressedQuests" in enforcer and re.search(r"void RestoreQuestFlags\(\)", enforcer) is not None,
          "suppressed quests must be recorded per quest and restored")
    check(re.search(r"g_suppressedQuests\.try_emplace[\s\S]*?a_quest\.data\.flags = static_cast<std::uint16_t>\(a_quest\.data\.flags & ~kQuestFlagsToClear\)", hide_body) is not None,
          "a hidden quest must record its original flags before the sweep clears them")
    check(re.search(r"g_stageIssued\.insert_or_assign\(resolvedId, a_stage\);[\s\S]*?a_quest\.SetStage\(a_stage\)", set_body) is not None,
          "the shutdown stage must be recorded as issued before SetStage so SetStage is never called twice")
    check("g_stageIssued.find(a_quest.GetFormID())" in stop_body and "issued->second == a_quest.currentStage" in stop_body,
          "a quest whose shutdown stage was already set must never get a second SetStage")
    check(re.search(r"SelectShutdownStage\(StageSummaries\(a_quest\)\)", stop_body) is not None,
          "the terminal stage must come from the quest's own stage list")
    check(re.search(r"TrySetShutdownStage\(a_quest, \*stage\)", stop_body) is not None,
          "a stopped quest must land on its shutdown stage through TESQuest::SetStage")
    check(re.search(r"HideQuest\(a_quest, \"no shutdown stage\"\)", stop_body) is not None,
          "a quest without a shutdown stage must fall back to the flag hide")
    check("REX::WARN(" in hide_body and "g_hideWarned.insert(resolvedId).second" in hide_body and "TODO-verify(#669)" in hide_body,
          "the flag-only fallback must be logged at warn level once per quest with TODO-verify(#669)")
    check("TODO-verify(#669)" in enforcer_raw,
          "the unverified shutdown-stage assumption must be marked TODO-verify(#669) in the enforcer")
    check(re.search(r"DecideQuestRestore\(record\.kind", restore_body) is not None and
          "case QuestRestore::StaysEnded:" in restore_body and "std::erase_if(g_suppressedQuests" in restore_body and "stays ended in the save" in restore_body,
          "every suppressed quest must be restored, kept ended with a warning, or dropped with a log")
    check(re.search(r"\(quest->data\.flags & kQuestFlagsEnded\) != 0,", enforcer) is not None and
          "g_blockedQuestIds.contains(quest->GetFormID())" in enforcer,
          "the quest sweep must skip finished quests and stop quests the policy blocks")
    check(re.search(r"void ResolveIds\(const Policy& a_policy\)[\s\S]*?a_policy\.blockedQuests[\s\S]*?g_blockedQuestIds\.insert", enforcer) is not None,
          "blocked quest form keys must be resolved from the policy")
    check("F4T::WorldPolicyClient::WantsQuestSweep(g_applied)" in enforcer,
          "the quest sweep must run for a suppressing policy or a policy with blocked quests")
    check("g_allowQuestIds" not in enforcer and "allowQuests" not in client_header,
          "the unused quest whitelist must be gone")

    check(re.search(r"void ApplyAdoptedPolicy\(std::uint64_t a_generation\)[\s\S]{0,400}?SweepAttachedCells\(\)", enforcer) is not None,
          "an adopted policy must sweep the loaded cells immediately")
    check(re.search(r"void ApplyAdoptedPolicy\(std::uint64_t a_generation\)[\s\S]{0,400}?SweepQueuedCells\(\)", enforcer) is not None,
          "an adopted policy must drain the cell queue immediately")
    accept_body = function_body(enforcer, "void AcceptPolicy(")
    check("AddTask" not in accept_body and "GetTaskInterface" not in accept_body,
          "the accept path runs under the receive thread's socket lock and must not queue F4SE tasks")
    check(re.search(r"void Tick\(std::uint64_t a_nowMs, std::uint64_t a_generation\)[\s\S]{0,600}?ApplyAdoptedPolicy\(a_generation\)", enforcer) is not None,
          "the game-thread Tick must adopt the stored policy")
    check("F4T::ReplicationRuntime::CurrentGeneration()" in accept_body,
          "the accept path must stamp the stored policy with the generation it was received in")
    check(re.search(r"if \(g_delivered && \*g_delivered == a_policy\) \{\s*return;\s*\}", accept_body) is not None,
          "the same policy must not be re-applied on every inbound packet")
    check(re.search(r"g_delivered = a_policy;[\s\S]{0,200}?stored = g_inbound\.Store\(a_policy, generation\);", accept_body) is not None,
          "the delivered policy and the inbound store must be updated under the same lock")
    check("g_policyAdopted" in enforcer and "bool HasAdoptedPolicy() noexcept" in enforcer_header,
          "the join flow needs a policy-adopted flag to wait on")
    check(re.search(r"if \(a_generation != g_lastGeneration\)[\s\S]{0,200}?ClearSessionPolicy\(a_generation\)", enforcer) is not None,
          "a new session must clear the applied policy")
    clear_body = function_body(enforcer, "void ClearSessionPolicy(")
    check("ReleaseDisabledRefs()" in clear_body and "RestoreQuestFlags()" in clear_body,
          "leaving a session must re-enable the disabled references and restore the quest flags")
    check("F4T::PolicyNoInteract::Clear()" in clear_body,
          "leaving a session must release the no-interact block")
    check("g_suppressedQuests.clear()" in clear_body,
          "leaving a session must drop the kept ended records")
    check("g_hideWarned.erase(resolvedId);" in set_body,
          "a freshly issued shutdown stage must re-arm the hide warning")
    check("F4T::PolicyNoInteract::Apply(policy.noInteract)" in function_body(enforcer, "bool AdoptInboundPolicy("),
          "adopting a policy must apply its no-interact keys")
    release_body = function_body(enforcer, "void ReleaseForSave(")
    check("ReleaseDisabledRefs()" in release_body and "RestoreQuestFlags()" in release_body and "F4T::PolicyNoInteract::Clear()" in release_body,
          "the save release must re-enable references, restore quest flags and release the no-interact block")
    check("F4T::PolicyNoInteract::Apply(g_applied.noInteract)" in function_body(enforcer, "void ReapplyAfterSave("),
          "the save re-apply must reapply the no-interact block")
    check(re.search(r"g_inbound\.OnSessionGeneration\(a_generation\)", clear_body) is not None,
          "leaving a session must reset the inbound policy state so a stale policy is never applied to the next one")
    check(re.search(r"g_delivered\.reset\(\)", clear_body) is not None,
          "leaving a session must forget the delivered policy so the same policy is applied again on the next session")
    check("InboundPolicyState" in client_header and
          re.search(r"bool InboundPolicyState::TakeAdoptable\(std::uint64_t a_generation, Policy& a_out\)", client) is not None,
          "the inbound generation, dirty flag and adoption must live in the pure state machine")
    check(re.search(r"bool InboundPolicyState::Store\(const Policy& a_policy, std::uint64_t a_generation\)[\s\S]{0,220}?a_policy\.version <= policy_\.version", client) is not None,
          "an older policy version must not overwrite a newer one")
    check("void ReleaseForSave()" in enforcer_header and "void ReapplyAfterSave()" in enforcer_header,
          "the save flow must be able to release the enforcement and re-apply it")
    check(re.search(r"kPreSaveGame:\s*F4T::WorldPolicyEnforcer::ReleaseForSave\(\);", read("src/main.cpp")) is not None,
          "disabled references and cleared quest flags must be released before the save is written")
    check(re.search(r"kPostSaveGame:\s*F4T::WorldPolicyEnforcer::ReapplyAfterSave\(\);", read("src/main.cpp")) is not None,
          "the enforcement must be re-applied once the save is written")
    check(re.search(r"F4T::ObjectSync::RefreshTrackedLocks\(\);\s*F4T::WorldPolicyEnforcer::ReapplyAfterSave\(\);", read("src/main.cpp")) is not None,
          "a loaded save must have the enforcement re-applied to it")

    check("kMaximumDisableEntriesPerMessage" in co_core_header and
          "kMaximumTotalDisableEntries = 2 * kMaximumClassEntries" in co_core_header,
          "the wire chunk and total caps must come from the co-core header")
    check(re.search(r"inline constexpr std::string_view kCapabilityName = \"world-policy-v1\";", co_core_header) is not None,
          "the co-core capability name must be world-policy-v1")
    check("metadata.capabilities.emplace_back(std::string{ F4T::WorldPolicy::kCapabilityName });" in identity,
          "the client must claim the world-policy-v1 capability in its hello metadata")
    check(re.search(r"if \(expected\.kind == Kind::Door && !g_sharedDoors\.load\(\)\) \{\s*return;\s*\}\s*g_revisions\[\*key\] = revision;", sync) is not None,
          "a door state received while the policy keeps doors vanilla must not consume its revision")

    check("SetSharedDoors" in sync_header and re.search(r"void SetSharedDoors\(bool a_sharedDoors\)", sync) is not None,
          "object sync must expose the shared-door policy")
    check(re.search(r"const bool door = expected\.kind == Kind::Door;\s*if \(door && !g_sharedDoors\.load\(\)\) \{\s*continue;\s*\}", sync) is not None,
          "door state must not be applied while the policy keeps doors vanilla")
    check(re.search(r"const bool published = state\.kind != Kind::Door \|\| \(kEnableDoorOpenState && g_sharedDoors\.load\(\)\);", sync) is not None,
          "door state must not be published while the policy keeps doors vanilla")
    check(re.search(r"if \(tracked\.observed\.kind == Kind::Door && !g_sharedDoors\.load\(\)\) \{\s*continue;\s*\}", sync) is not None,
          "a door tracked under a shared policy must be dropped when the policy turns vanilla")
    check(re.search(r"void SyncDoorReplication\(\)\s*\{\s*const bool shared = g_applied\.doors == F4T::WorldPolicy::Doors::Shared;", enforcer) is not None,
          "the enforcer must push the applied door policy into object sync")
    check(re.search(r"if \(g_sharedDoorsPushed && shared == g_sharedDoorsValue\) \{\s*return;\s*\}", enforcer) is not None,
          "the door mode must only be pushed into object sync when it changes")
    check(re.search(r"if \(!g_disableIds\.empty\(\) \|\| !g_disabledByUs\.empty\(\)\)", enforcer) is not None,
          "the world sweep must be skipped while no reference is targeted or disabled")
    check(re.search(r"if \(g_policyAdopted\.load\(\)\) \{\s*PushModeToUi\(\);\s*\}", enforcer) is not None,
          "the world mode must not reach the UI before a real policy has been adopted")
    check("SetSharedDoors(true)" in sync or "g_sharedDoors{ true }" in sync,
          "doors must default to shared, which is the behaviour without a policy")

    check("kWorldSweepIntervalMs = 30000" in enforcer,
          "the full attached-cell sweep must be slow; the cell-loaded sink covers new cells")
    check(re.search(r"if \(a_nowMs >= g_nextWorldSweepMs\)", enforcer) is not None,
          "the slow sweep must stay interval-driven")

    ui_api = read("include/CommonwealthOnlineUI_API.h")
    check(ui_api.count("SetWorldMode") == 1 and re.search(r"struct IVCommonwealthOnlineUI5\s*\{\s*void \(\*SetWorldMode\)", ui_api) is not None,
          "SetWorldMode must appear once, as the first member of IVCommonwealthOnlineUI5; earlier interface versions must not change")
    check("g_uiWorld->SetWorldMode" in ui_client,
          "F4TUIClient::PushWorldMode must deliver the mode to the UI through SetWorldMode")
    check("COUI::WorldMode::Set(" in read_root("CommonwealthOnlineUI/src/PluginAPI.cpp"),
          "the UI side of SetWorldMode must route into COUI::WorldMode::Set")

    for item in pending:
        print(f"PENDING: {item}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("world policy contract ok")

_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TTutorialQuestGateContract.py", _section_013_f4ttutorialquestgatecontract),
    ("CO-BETA/plugin/tests/F4TVoiceRosterContract.py", _section_014_f4tvoicerostercontract),
    ("CO-BETA/plugin/tests/F4TWorldPolicyContract.py", _section_015_f4tworldpolicycontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TQuestStoryContracts_part6: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
