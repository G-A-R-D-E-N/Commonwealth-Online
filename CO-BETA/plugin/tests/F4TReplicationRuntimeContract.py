from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

BASELINE_TASK_ORDER = [
    "F4T::Networking::UpdateConnectionMaintenance",
    "F4T::RemotePlayerState::ApplyPendingCombatHits",
    "SendPendingCombatHits",
    "PollLocalPlayerTransform",
    "F4T::WorldStateSync::PollHostCapture",
    "F4T::WorldStateSync::ApplyPendingState",
    "F4T::NpcSync::Update",
    "F4T::ProxyActorController::Update",
    "F4T::SessionLaunch::Update",
]

SESSION_OWNED_PUBLICATION_STATE = [
    "hasLastTransform",
    "lastSentTransform",
    "lastDebugSampleTransform",
    "lastKnownLocation",
    "lastSentMovementState",
    "lastSentEquipmentState",
    "lastSentAppearanceState",
    "lastSentPowerArmorState",
    "lastSendTime",
    "lastLogTime",
    "lastDebugSampleTime",
    "debugMovingHoldUntil",
    "networkMovingHoldUntil",
    "soloCellTransformSuppressed",
]

PROCESS_OWNED_CAPTURE_STATE = [
    "lastMeleeAttackState",
    "lastRangedFireState",
    "nextActionSequence",
    "lastUnsentEquipmentLog",
    "lastLoggedEquippedItems",
]

PROCESS_DIAGNOSTIC_STATICS = {
    "playerUnavailableWarningLogged",
    "s_lastSendDiag",
}

CALL_PATTERN = re.compile(r"((?:F4T::)?(?:\w+::)*\w+)\s*\(")
NOT_PHASES = {"if", "for", "while", "switch", "return", "sizeof"}

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(relative):
    path = PLUGIN / relative
    if not path.exists():
        failures.append(f"missing {relative}")
        return ""
    return path.read_text(encoding="utf-8")


def body_after(text, anchor):
    start = text.find(anchor)
    if start < 0:
        return None
    brace = text.index("{", start + len(anchor))
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:index]
    return None


def phase_calls(body):
    calls = []
    for line in body.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        match = CALL_PATTERN.match(stripped)
        if match and match.group(1) not in NOT_PHASES:
            calls.append(match.group(1))
    return calls


main = read("src/main.cpp")
publish = read("src/F4TReplicationRuntimePublish.cpp")
runtime_h = read("include/F4TReplicationRuntime.h")
runtime_cpp = read("src/F4TReplicationRuntime.cpp")
capture_h = read("include/F4TRangedCombatCapture.h")
capture_cpp = read("src/F4TRangedCombatCapture.cpp")
networking = read("src/F4TNetworking.cpp")

permanent = body_after(main, "F4T::F4SECapabilities::AddPermanentTask([]()")
check(permanent is not None, "main.cpp must register a permanent task")
if permanent is not None:
    check(
        phase_calls(permanent) == ["F4T::ReplicationRuntime::Tick"],
        f"permanent task must delegate only to F4T::ReplicationRuntime::Tick, found {phase_calls(permanent)}",
    )

tick = body_after(runtime_cpp, "void Tick()")
check(tick is not None, "F4TReplicationRuntime.cpp must define Tick()")
if tick is not None:
    calls = [call for call in phase_calls(tick) if call in BASELINE_TASK_ORDER]
    check(calls == BASELINE_TASK_ORDER, f"Tick phase order changed: {calls}")
    sync = tick.find("g_runtime.SyncSession(")
    first_phase = tick.find("F4T::Networking::UpdateConnectionMaintenance(")
    check(0 <= sync < first_phase, "Tick must sync the session before the first phase")
    npc = tick.find("F4T::NpcSync::Update(")
    check(npc > 0 and "F4T_ENABLE_NPC_SYNC" not in tick, "NpcSync::Update must run unconditionally in Tick")
    check("F4T::Networking::IsConnectedToServer()" in tick, "Tick must observe the connection")
    check("F4T::RemotePlayerState::GetAssignedPlayerId().has_value()" in tick, "Tick must observe the assigned player id")
    check("g_connectionEpoch.load(" in tick, "Tick must observe the connection epoch")
    check("PollLocalPlayerTransform(g_runtime.Session(), g_runtime.Capture())" in tick, "poll must receive runtime-owned state")
    check("SendPendingCombatHits(g_runtime)" in tick, "hit send must receive the runtime boundary")

poll = body_after(publish, "void PollLocalPlayerTransform(")
check(poll is not None, "F4TReplicationRuntimePublish.cpp must define PollLocalPlayerTransform")
if poll is not None:
    statics = set(re.findall(r"\bstatic\s+[^;=]*?\b(\w+)\s*(?:=|\{|;)", poll))
    check(statics <= PROCESS_DIAGNOSTIC_STATICS, f"PollLocalPlayerTransform keeps hidden state statics: {sorted(statics - PROCESS_DIAGNOSTIC_STATICS)}")
    for name in SESSION_OWNED_PUBLICATION_STATE:
        check(f"auto& {name} = a_session.localPlayer.{name};" in poll, f"{name} must bind to session-owned state")
    for name in PROCESS_OWNED_CAPTURE_STATE:
        check(f"auto& {name} = a_capture.{name};" in poll, f"{name} must bind to runtime-owned capture state")

publication = body_after(runtime_h, "struct LocalPlayerPublicationState")
capture_state = body_after(runtime_h, "struct LocalPlayerCaptureState")
check(publication is not None, "runtime header must declare LocalPlayerPublicationState")
check(capture_state is not None, "runtime header must declare LocalPlayerCaptureState")
if publication is not None:
    for name in SESSION_OWNED_PUBLICATION_STATE:
        check(re.search(rf"\b{name}\b", publication) is not None, f"LocalPlayerPublicationState missing {name}")
if capture_state is not None:
    for name in PROCESS_OWNED_CAPTURE_STATE:
        check(re.search(rf"\b{name}\b", capture_state) is not None, f"LocalPlayerCaptureState missing {name}")
check("#include \"RE/" not in runtime_h, "runtime header must stay engine-independent")

check("std::uint64_t sessionGeneration{};" in capture_h, "CombatHit must carry its session generation")
check(
    capture_cpp.count("F4T::ReplicationRuntime::CurrentGeneration()") == 1,
    "queued outbound hits must be stamped with the current session generation exactly once",
)
check("candidate.sessionGeneration" in capture_cpp, "drained hits must keep their session generation")

send = body_after(publish, "void SendPendingCombatHits(")
check(send is not None, "F4TReplicationRuntimePublish.cpp must define SendPendingCombatHits")
if send is not None:
    admit = send.find("a_runtime.Admits(hit.sessionGeneration)")
    packet = send.find("F4T::Networking::SendCombatHitPacket(")
    check(0 <= admit < packet, "stale-session hits must be rejected before SendCombatHitPacket")

connect = body_after(networking, "bool ConnectToServerInternal(")
check(connect is not None, "F4TNetworking.cpp must define ConnectToServerInternal")
if connect is not None:
    resolved = connect.find("if (!ResolveAndConnectIPv4(host, port, a_error))")
    noted = connect.find("F4T::ReplicationRuntime::NoteConnectionEstablished();")
    receive = connect.find("StartReceiveThread();")
    check(0 <= resolved < noted < receive, "a new connection must advance the epoch before its receive thread starts")
check(networking.count("NoteConnectionEstablished") == 1, "only the connect path may advance the connection epoch")

identity = read("src/F4TClientIdentity.cpp")
quest_sync = read("src/F4TQuestSync.cpp")
quest_sync_h = read("include/F4TQuestSync.h")
quest_inbound_h = read("include/F4TQuestInbound.h")

check('kCapability = "quest-sync-v1"' in quest_sync_h, "the client must name the quest-sync-v1 capability")
check("a_metadata.capabilities.emplace_back(kCapability);" in quest_sync_h, "the hello must advertise quest-sync-v1")
check('kReadyCapability = "quest-ready-v1"' in quest_sync_h, "the client must name the quest-ready-v1 capability")
check("a_metadata.capabilities.emplace_back(kReadyCapability);" in quest_sync_h, "the hello must advertise quest-ready-v1")
check("F4T::QuestSync::AddSessionCapabilities(metadata);" in identity, "the packaged client metadata must carry quest-sync-v1")
check("kCoTestFeatures" not in quest_sync_h and "kCoTestFeatures" not in quest_sync, "quest sync must not be gated by the test features switch")
check("void Reset()" not in quest_inbound_h, "the inbound queue must not offer a hard clear that a session change could call")

welcome = body_after(quest_sync, "void NoteWelcome(")
check(welcome is not None, "F4TQuestSync.cpp must define NoteWelcome")
if welcome is not None:
    check("QuestInbound::NoteConnection(" in welcome, "the welcome must open the quest packet connection epoch")

reset = body_after(quest_sync, "void Reset()")
check(reset is not None, "F4TQuestSync.cpp must define Reset")
if reset is not None:
    check(
        "QuestInbound" not in reset,
        "a session generation change must keep the quest packets queued by the current connection",
    )

quest_tick = body_after(quest_sync, "void Tick()")
check(quest_tick is not None, "F4TQuestSync.cpp must define Tick")
if quest_tick is not None:
    capability = quest_tick.find("QuestInbound::IsActive()")
    drain = quest_tick.find("QuestInbound::Drain(")
    poll = quest_tick.find("PollLocalProgress(")
    check(
        0 <= capability < drain < poll,
        "the quest tick must wait for a server that advertises quest-sync-v1 before draining and polling",
    )
    check("IsConnectedToServer()" in quest_tick, "the quest tick must observe the connection")

on_message = body_after(main, "void F4SEAPI OnF4SEMessage(F4SE::MessagingInterface::Message* a_msg)")
check(on_message is not None, "main.cpp must define OnF4SEMessage")
if on_message is not None:
    post_label = "case F4SE::MessagingInterface::kPostLoadGame:"
    check(on_message.count(post_label) == 1, "kPostLoadGame must have exactly one case label")
    game_loaded = on_message[on_message.find("case F4SE::MessagingInterface::kGameLoaded:"):on_message.find(post_label)]
    check(
        "StartPlayerPositionPolling();" in game_loaded and "AddTask(" not in game_loaded,
        "kGameLoaded must still call StartPlayerPositionPolling directly",
    )
    post = on_message[on_message.find(post_label):on_message.find("default:")]
    lambda_body = body_after(post, "AddTask([loadSucceeded]()")
    check(lambda_body is not None, "kPostLoadGame must run its work through an AddTask lambda")
    if lambda_body is not None:
        polling_at = lambda_body.find("StartPlayerPositionPolling();")
        launch_at = lambda_body.find("F4T::SessionLaunch::OnF4SEMessage(&")
        check(0 <= polling_at < launch_at, "AddTask lambda must start polling before session launch")
        check("a_msg" not in lambda_body, "AddTask lambda must not capture the message pointer")
        outside = post.replace(lambda_body, "")
        else_at = outside.find("} else {")
        direct_at = outside.find("StartPlayerPositionPolling();")
        check(
            0 <= else_at < direct_at and outside.count("StartPlayerPositionPolling();") == 1,
            "StartPlayerPositionPolling may run outside the AddTask lambda only in the null task-interface fallback",
        )

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TReplicationRuntimeContract: all checks passed.")
