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
send_report = body_after(sync, "void SendReport(const QuestLogic::Report& a_report)")
check(
    send_report is not None and "TutorialComplete()" in (send_report or ""),
    "SendReport must refuse to send while the tutorial is unfinished, so no report leaves early",
)
request_scope = body_after(sync, "void RequestScope(std::string_view a_quest, QuestLogic::Scope a_scope)")
check(
    request_scope is not None and "TutorialComplete()" in (request_scope or ""),
    "RequestScope must refuse to send while the tutorial is unfinished",
)
check(
    send_report is not None and "!g_questReadySent" in (send_report or ""),
    "SendReport must also wait for the sent questReady, or the server drops the report after Observe consumed it",
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

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    print(
        "F4TTutorialQuestGateContract: the tutorial quest gate is incomplete. Reports and scope "
        "requests must wait for the MQ102 pod exit on the client and for questReady on the server; "
        "see docs/code-notes/CO-BETA-plugin.md."
    )
    sys.exit(1)

print("F4TTutorialQuestGateContract: all checks passed.")
