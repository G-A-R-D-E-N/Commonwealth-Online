#!/usr/bin/env python3
"""F4TNetworkServerContracts_part2: consolidated contract sections.

Consolidated from 5 single-contract scripts so the group runs one process
instead of 5. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TNetworkServerContracts_part2.py
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


def _section_010_f4treplicationruntimecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TReplicationRuntimeContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TReplicationRuntimeContract.py")
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
        "F4T::AdminItems::ApplyPendingAdminItems",
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
        "nextActionSequence",
        "lastUnsentEquipmentLog",
        "lastLoggedEquippedItems",
    ]

    PROCESS_DIAGNOSTIC_STATICS = {
        "playerUnavailableWarningLogged",
        "s_lastSendDiag",
        "visibilityPauseUnsupportedLogged",
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
    world_check = body_after(runtime_cpp, "bool IsInWorld()")
    check(world_check is not None and all(value in world_check for value in
          ("GetMenuOpen<RE::LoadingMenu>()", "GetMenuOpen<RE::MainMenu>()", "player->GetParentCell()")),
          "the general world check must reject loading, main menu and missing parent cell")
    survival_world = body_after(runtime_cpp, "bool IsSurvivalWorldReady()")
    world_state = read("src/F4TSessionWorldState.cpp")
    gameplay_world = body_after(world_state, "bool IsGameplayWorldReady(")
    chargen_world = body_after(world_state, "bool IsWorldReadyForCharGen(")
    check(survival_world is not None and "SessionWorldState::IsGameplayWorldReady(*player)" in survival_world and
          gameplay_world is not None and "IsWorldReadyForCharGen(a_player)" in gameplay_world and
          "SoloCell::IsSoloCell(*cell)" in gameplay_world and chargen_world is not None and
          all(value in chargen_world for value in ("IsLoadingScreenActive()", "IsMainMenuOpen()",
              "IsPlaceholderSpawnPosition(a_player)", "a_player.GetParentCell()")),
          "the Survival gate must reject loading, menus, placeholder positions, missing cells and solo cells")

    permanent = body_after(main, "F4T::F4SECapabilities::AddPermanentTask([]()")
    check(permanent is not None, "main.cpp must register a permanent task")
    if permanent is not None:
        check(
            phase_calls(permanent) == ["F4T::MapKey::Install", "F4T::PipboyUnpause::Install", "F4T::ReplicationRuntime::Tick", "F4T::SleepBedHook::Tick"],
            f"permanent task must delegate only to F4T::MapKey::Install, F4T::PipboyUnpause::Install, F4T::ReplicationRuntime::Tick and F4T::SleepBedHook::Tick, found {phase_calls(permanent)}",
        )

    tick = body_after(runtime_cpp, "void Tick()")
    check(tick is not None, "F4TReplicationRuntime.cpp must define Tick()")
    if tick is not None:
        calls = [call for call in phase_calls(tick) if call in BASELINE_TASK_ORDER]
        check(calls == BASELINE_TASK_ORDER, f"Tick phase order changed: {calls}")
        check("const bool loaded = IsSurvivalWorldReady();" in tick and
              "SurvivalEnforcer::Tick(connection, g_runtime.CurrentGeneration(), loaded," in tick,
              "the primary Survival gate must use gameplay world readiness")
        gated = body_after(tick, "F4T::SurvivalEnforcer::Tick(")
        check(gated is not None and "F4T::WorldStateSync::PollHostCapture();" in gated
              and "F4T::WorldStateSync::ApplyPendingState();" in gated and "SendPendingCombatHits(g_runtime);" in gated,
              "world-state capture and application must wait for Survival readiness")
        check("if (!F4T::Networking::IsConnectedToServer()) F4T::WorldStateSync::ApplyPendingState();" in tick,
              "disconnected ticks must still process world-state reset")
        sync = tick.find("g_runtime.SyncSession(")
        first_phase = tick.find("F4T::Networking::UpdateConnectionMaintenance(")
        check(0 <= sync < first_phase, "Tick must sync the session before the first phase")
        npc = tick.find("F4T::NpcSync::Update(")
        check(npc > 0 and "F4T_ENABLE_NPC_SYNC" not in tick, "NpcSync::Update must run unconditionally in Tick")
        npc_health = body_after(tick, "const auto runGameplay = ")
        check("F4T::NpcSync::Update(runGameplay)" in tick and npc_health is not None and "TryRunForConnectionGeneration(connection" in npc_health and
              "SurvivalEnforcer::Tick(connection, g_runtime.CurrentGeneration(), IsSurvivalWorldReady(), a_work)" in npc_health,
              "NPC health work must run under the connection and Survival leases")
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
    queue_hit = body_after(capture_cpp, "void QueuePendingHit(")
    check(
        queue_hit is not None and queue_hit.count("F4T::ReplicationRuntime::CurrentGeneration()") == 1
        and queue_hit.count("a_hit.sessionGeneration = generation;") == 1,
        "queued outbound hits must be stamped with the current session generation exactly once",
    )
    check("candidate.sessionGeneration" in capture_cpp, "drained hits must keep their session generation")
    queue_hit = body_after(capture_cpp, "void QueuePendingHit(")
    check(queue_hit is not None and all(0 <= queue_hit.find(item) < queue_hit.find("lock(s_hitQueueLock)") for item in (
              "GetConnectionGeneration()", "CurrentGeneration()", "g_clientState.ReadyRevision(connection, generation)",
              "!revision", "IsConnectedToServer()"))
          and "a_hit.connectionGeneration = connection;" in queue_hit and "a_hit.policyRevision = *revision;" in queue_hit,
          "queued hits must capture connection and policy before locking the queue")
    drain_hits = body_after(capture_cpp, "std::vector<CombatHit> GetAndClearPendingCombatHits()")
    check(drain_hits is not None and re.search(r"hits\.push_back\(CombatHit\{[^}]*candidate\.connectionGeneration\s*,\s*candidate\.policyRevision", drain_hits, re.S) is not None,
          "drained hits must retain their connection and policy revision")

    send = body_after(publish, "void SendPendingCombatHits(")
    check(send is not None, "F4TReplicationRuntimePublish.cpp must define SendPendingCombatHits")
    if send is not None:
        admit = send.find("a_runtime.Admits(hit.sessionGeneration)")
        packet = send.find("F4T::Networking::SendCombatHitPacket(")
        npc_packet = send.find("F4T::Networking::SendNpcHitPacket(")
        check(0 <= admit < npc_packet < packet, "stale-session hits must be rejected before either hit packet")
        check(0 <= send.find("g_clientState.Revision(hit.connectionGeneration) != hit.policyRevision") < npc_packet,
              "stale-policy or stale-connection hits must not be sent")
        damage_packet = send.find("F4T::Networking::SendNpcDamagePacket(")
        check(0 <= send.find("a_runtime.Admits(pending.sessionGeneration)") < damage_packet and
              0 <= send.find("g_clientState.Revision(pending.connectionGeneration) != pending.policyRevision") < damage_packet,
              "stale NPC damage must be rejected before sending")

    connect = body_after(networking, "bool ConnectToServerInternal(")
    check(connect is not None, "F4TNetworking.cpp must define ConnectToServerInternal")
    if connect is not None:
        resolved = connect.find("if (!ResolveAndConnectIPv4(host, port, a_error))")
        receive = connect.find("StartReceiveThread();")
        check(0 <= resolved < receive, "connection must be published before its receive thread starts")
    resolve = body_after(networking, "bool ResolveAndConnectIPv4(")
    check(resolve is not None, "F4TNetworking.cpp must define ResolveAndConnectIPv4")
    if resolve is not None:
        lock = resolve.rfind("const std::scoped_lock lock(g_sendMutex, g_socketMutex);")
        noted = resolve.find("F4T::ReplicationRuntime::NoteConnectionEstablished();", lock)
        published = resolve.find("g_socket = connectedSocket;", noted)
        check(0 <= lock < noted < published, "connection epoch and socket must be published under the send lock")
    reset = body_after(networking, "void InvalidateReplicationSession(")
    check(reset is not None, "load reset must serialize session invalidation with sends")
    if reset is not None:
        lock = reset.find("const std::scoped_lock sendLock(g_sendMutex);")
        noted = reset.find("F4T::ReplicationRuntime::NoteConnectionEstablished();")
        check(0 <= lock < noted, "load reset must advance the epoch under the send lock")
    check(networking.count("NoteConnectionEstablished") == 2, "only connection and load reset may advance the epoch")
    proxy_save = read("src/F4TProxySaveSafety.cpp")
    check(
        "F4T::Networking::Detail::InvalidateReplicationSession();" in proxy_save
        and "F4T::ReplicationRuntime::NoteConnectionEstablished();" not in proxy_save,
        "post-load session invalidation must use the send-locked path",
    )

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
        lambda_body = body_after(post, "AddTask([loadSucceeded, survivalLoad]()")
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

    check(
        '"type":"visibilityPause","paused":true' in publish and
        '"type":"visibilityPause","paused":false' in publish and
        "GetAssignedPlayerId()" in publish,
        "solo-cell suppression must send pause and resume controls after the session is ready",
    )
    inbound = read("src/F4TNetworkingInbound.cpp")
    check('*packetType == "peerHidden"' in inbound and "HandlePeerHiddenPacket(packet);" in inbound, "peerHidden packets must dispatch to HandlePeerHiddenPacket")
    check("void HandlePeerHiddenPacket(" in inbound and "RemoveRemotePlayer(*playerId)" in inbound, "HandlePeerHiddenPacket must remove the remote player so its proxy is held")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TReplicationRuntimeContract: all checks passed.")

def _section_011_f4tserverlivecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TServerLiveContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TServerLiveContract.py")
    from pathlib import Path
    import re
    import sys

    PLUGIN = Path(__file__).resolve().parents[1]
    REPO = PLUGIN.parents[1]
    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        return re.sub(r"//[^\n]*", "", (PLUGIN / path).read_text(encoding="utf-8"))


    def read_root(path):
        return (REPO / path).read_text(encoding="utf-8")


    source = read("src/F4TServerLive.cpp")
    header = read("include/F4TServerLive.h")
    session = read("src/F4TSteamSession.cpp")
    api = read("include/CommonwealthOnlineUI_API.h")
    host_link = read_root("co-core/include/F4TSteamHostLink.h")
    server_link = read_root("CO-SERVER/server-cpp/src/SteamIdentityLink.cpp")
    runtime = read_root("CO-SERVER/server-cpp/src/ServerRuntime.inc")
    live_settings = read_root("CO-SERVER/server-cpp/src/ServerLiveSettings.cpp")
    ui_bridge = read_root("CommonwealthOnlineUI/src/F4TServerSettingsBridge.cpp")
    ui_settings = read_root("co-core/src/config/F4TServerSettingsFile.cpp")
    ui_live = read_root("CommonwealthOnlineUI/src/F4TUiServerLive.cpp")
    ui_api = read_root("CommonwealthOnlineUI/src/PluginAPI.cpp")
    ui_panel = read_root("CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/server-settings.js")
    inbound = read_root("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")

    check('kApplySettingsCommand = "apply_settings"' in host_link and
          'kServerConfigCommand = "server_config"' in host_link,
          "the live settings and running config command names must live in the shared co-core header")
    check("kApplySettingsCommand" in source and "kApplySettingsCommand" in server_link,
          "the plugin and the server must use the shared live settings command name")
    check("kServerConfigCommand" in source and "kServerConfigCommand" in server_link,
          "the plugin and the server must use the shared running config command name")

    check("SteamHostLink::Directory()" in source and "SteamHostLink::ReadEndpoint" in source,
          "the live path must read the published Steam host link endpoint")
    check("SteamHostLink::Send" in source,
          "the live path must send through the existing host link client")
    check("admin-token" not in source and "adminPort" not in source and "USERPROFILE" not in source,
          "the live path must not read the admin token or any server key path")
    check("kHostLinkGamePort = 7777" in source,
          "the live path must address the same game port the Steam host link is published for")

    check("RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI12>" in source,
          "the plugin must request the V12 interface that carries the live callbacks")
    check("SetServerLiveCallbacks(&kCallbacks)" in source and
          "ServerLiveCallbacks kCallbacks{ ApplyServerSettings }" in source,
          "the live callback table must carry the whole-settings applier")
    check("F4T::ServerLive::Attach();" in session,
          "the Steam session attach path must install the live callbacks")
    check('#include "F4TServerLive.h"' in session, "the Steam session must include the live header")
    check("Attach()" in header, "the live header must declare Attach")

    check("V12 = 12" in api, "the UI API must append interface version 12")
    check(re.search(r"struct IVCommonwealthOnlineUI12\s*\{\s*void \(\*SetServerLiveCallbacks\)", api) is not None,
          "V12 must only add SetServerLiveCallbacks; earlier versions must keep their members")
    check("bool (*ApplyServerSettings)(const char* a_requestJson, char* a_resultBuffer, std::size_t a_resultBufferSize)" in api,
          "the live callback table must expose one whole-settings applier")
    check("0x7A4B5C0C" in api, "the V12 layout tag must be unique to this interface")

    check("Api_SetServerLiveCallbacks" in ui_api and "g_interfaceV12{ Api_SetServerLiveCallbacks }" in ui_api,
          "the UI must implement V12 with the live callback registration")
    check("case InterfaceVersion::V12:" in ui_api, "the UI must serve V12 through VisitInterface")
    check("F4T::UiServerLive::SetCallbacks" in ui_api and "F4T::UiServerLive::ApplySettings" in ui_bridge,
          "the UI bridge must apply the saved settings through the registered live callbacks")
    check("g_registered" in ui_live and "callbacks->ApplyServerSettings(" in ui_live,
          "the UI live module must only call a registered settings applier")
    check("QueryRunningConfig" in ui_live and "QueryRunningConfig" in ui_bridge,
          "the UI must be able to read the running server's own config path")

    check("LiveSettingsApplier" in ui_settings and "RevertLiveSettings(a_liveSettings" in ui_settings,
          "the save sequence owns the live failure and the put-back through the whole-settings applier")
    check("a_runningDocument" in ui_settings and "previousDocument" in ui_settings,
          "the put-back baseline is passed in from the running server, with the file as the fallback")
    refusal_path = ui_settings[ui_settings.index("case LiveApplyResult::Refused"):ui_settings.index("case LiveApplyResult::Unavailable")]
    check("RevertLiveSettings(a_liveSettings, baseline" in refusal_path and "a_runningDocument" in refusal_path,
          "a refusal after a partial apply must put the running server's own settings back through the same applier")
    check("const auto& baseline" in refusal_path,
          "the put-back must prefer the running document over the file it may have drifted from")
    check("had already applied" in refusal_path,
          "a refusal that could not be reverted must name the fields the server had already applied")
    server_admin = read_root("CO-SERVER/server-cpp/src/AdminControlServer.cpp")
    check('response["data"] = std::move(a_data);' in server_admin and "&a_request, result.data" in server_link,
          "the server failure reply must carry the applied fields to the plugin")
    check('did not answer the settings link in time' in source and '{ "ok", false }, { "error", error }' in source,
          "a link failure must carry its own error text so it is a refusal, not an unavailable server")
    check("a_result = parsed;" in ui_live,
          "the UI live call must keep the refusal reply so the save sequence can see applied fields")
    check('a_result.find("data")' in ui_settings,
          "the save sequence must read the applied fields out of the server reply data")
    check("liveApplied" in ui_bridge and "liveResult" in ui_bridge,
          "the save reply must report what the running server applied")

    live_link = read_root("CO-SERVER/server-cpp/src/ServerLiveSettingsLink.inc")
    check("ServerLiveRelay" in runtime and "serverLiveRelay.Submit(" in live_link and
          "serverLiveRelay.Drain(applyLiveSettings);" in runtime,
          "the host link must post the settings change to the main loop instead of applying it on the link thread")
    check('"document"' in live_link and "ReadEditable" in live_link,
          "the config reply must carry the running document a refused change is put back to")
    check((REPO / "CO-SERVER/server-cpp/include/ServerLiveRelay.h").is_file(),
          "the main-loop live settings relay must exist")
    check(runtime.index("serverLiveRelay.FailPending(") < runtime.index("admin.Stop();"),
          "a waiting settings change must be failed before the admin server stops")
    check("steamIdentityLink.SetApplySettingsHandler(" in live_link and "steamIdentityLink.SetServerConfigProvider(" in live_link and
          'ServerLiveSettingsLink.inc' in runtime,
          "the runtime must install the live settings handler and its own config path")
    check("SetApplySettingsHandler" in server_link and "Live server settings are unavailable." in server_link,
          "the host link must answer the live settings command and say when no handler is installed")
    check("server_config" not in server_link.replace("kServerConfigCommand", ""),
          "the host link must not hard-code the running config command name twice")

    check("SetChatEnabled" in live_settings and "SetFactionsEnabled" in live_settings and
          "SetMaximumPlayers" in live_settings and "SetSavingEnabled" in live_settings,
          "the server must apply chat, factions, capacity and persistence live")
    check("ServerInfoPacket()" in live_settings and "SetObjectStateEnabled" in live_settings,
          "the server must re-send capabilities after a live settings change")
    check("ServerInfoPacket()" in live_settings and "broadcastPacket" in live_settings and
          "if (state.active) liveSettingsDelivery.Queue(id, a_packet);" in runtime and
          "liveSettingsDelivery.Flush(" in runtime and "return SendReliableJson(server, a_connectionId, a_packet);" in runtime and
          "liveSettingsDelivery.Erase(a_connectionId);" in runtime,
          "the server must broadcast capabilities after applying settings")
    check("ApplyAdminWorldMode" in runtime and "setWorldMode = applyWorldMode" in runtime,
          "the admin world mode command must share the live world mode path")
    check("SetObjectStateEnabled" in read_root("CO-SERVER/server-cpp/src/ServerWorldPolicy.inc"),
          "object state must be switchable after a session has activated")
    check("SetSavingEnabled" in read_root("CO-SERVER/server-cpp/src/PersistenceGate.cpp"),
          "persistence must be switchable at runtime")

    check("LiveApply::Refused" in ui_bridge and "LiveApply::Unavailable" in ui_bridge,
          "the bridge must separate a refusal from a server that could not be asked")
    check("liveApplied" in ui_panel and "Applied to the running server" in ui_panel,
          "the panel must tell the host what the running server applied")
    check("Restart the server to apply it" not in ui_panel,
          "the panel must not tell the host to restart for a saved setting")
    check("applies on its next start" not in ui_panel,
          "the panel must not describe saved settings as next-start settings")

    check('"capabilities"' in inbound and "F4T::Chat::NoteWelcome" in inbound and
          "F4T::Factions::NoteWelcome" in inbound and "F4T::ObjectSync::NoteCapabilities" in inbound,
          "the plugin must react to the server's capabilities change")
    check("NoteCapabilities" in read("include/F4TObjectSync.h"),
          "the object sync capability update must be a separate entry point")

    world_policy = read_root("CO-SERVER/server-cpp/src/ServerWorldPolicy.inc")
    tunnel_service = read("src/F4TSteamTunnelService.cpp")
    tunnel_core = read_root("co-core/src/session/F4TSteamTunnel.cpp") + read_root("co-core/src/session/F4TSteamTunnelHost.cpp")
    tunnel_header = read_root("co-core/include/F4TSteamTunnel.h")
    check('"maxPlayers"' in world_policy and "SetAdvertisedMaximumPlayers" in live_settings,
          "a live max players change must be advertised on the capability packet")
    check("SetHostMemberLimit" in inbound and "SetHostMemberLimit" in tunnel_service,
          "the plugin must route the live max players change into the Steam tunnel host")
    check("SetMaximumConnections" in tunnel_service and "SetMaximumConnections" in tunnel_core,
          "the Steam tunnel host must accept a live member limit")
    check("ClampHostMaximumConnections" in tunnel_header and "ClampHostMaximumConnections" in tunnel_core,
          "the live member limit must clamp to the members already connected")
    start_host = tunnel_service.split("Transport::TunnelError StartHost(", 1)[1].split("void StopHost()", 1)[0]
    check("ClampHostMaximumConnections(static_cast<std::size_t>(a_maxPlayers) - 1, 0)" in start_host and "maximumConnections," in start_host
          and "a_maxPlayers - 1," not in start_host,
          "StartHost must clamp the requested guest count to the Steam tunnel connection ceiling")
    check("kMaxPlayers = 17;" in read_root("CommonwealthOnlineUI/include/F4TSteamActions.h"),
          "the Steam host start action must cap max players at the tunnel ceiling plus the host")

    launcher = read("src/F4TLocalServerLauncher.cpp")
    check((PLUGIN / "src/F4TLocalServerLauncher.cpp").exists() and (PLUGIN / "include/F4TLocalServerLauncher.h").exists(),
          "the local server launcher must stay in the plugin")
    check('#include "F4TLocalServerLauncher.h"' in session, "the Steam session must include the local server launcher")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("server live settings contract ok")

def _section_012_f4tserverpasswordstorecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TServerPasswordStoreContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TServerPasswordStoreContract.py")
    from pathlib import Path


    root = Path(__file__).resolve().parents[3]
    source = (root / "CO-BETA/plugin/src/F4TServerProfileStore.cpp").read_text(encoding="utf-8")
    shim = (root / "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")
    ui = (root / "CO-BETA/plugin/src/F4TUIClient.cpp").read_text(encoding="utf-8")

    assert "serverPassword" not in source, "the server profile store must never read or persist the server password"
    assert ".serverPassword = identity->serverPassword" in shim, "the Winsock shim must copy serverPassword into the ClientIdentity it hands to co-core"
    assert "host.c_str(), port, connectionError, &connectionFailure, serverPassword, id, true, &connectionGeneration)" in ui, "the UI join worker must pass the join password into ConnectAndValidateServer"

def _section_013_f4tserverprofilestoresourcecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TServerProfileStoreSourceContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TServerProfileStoreSourceContract.py")
    from pathlib import Path


    root = Path(__file__).resolve().parents[3]
    source = (root / "CO-BETA/plugin/src/F4TServerProfileStore.cpp").read_text(encoding="utf-8")
    start = source.index("bool CommitFileToDisk")
    end = source.index("\n\tvoid EnsureLoaded", start)
    commit = source[start:end]

    assert "_O_WRONLY" in commit
    assert "_O_BINARY" in commit
    assert "_O_RDONLY" not in commit

def _section_015_f4tsteamjoinfailurecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TSteamJoinFailureContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSteamJoinFailureContract.py")
    from pathlib import Path
    import re

    root = Path(__file__).resolve().parents[3]
    src = root / "CO-BETA/plugin/src"
    session = (src / "F4TSteamSession.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
    launcher = (src / "F4TLocalServerLauncher.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
    ui_client = (src / "F4TUIClient.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
    server_main = (root / "CO-SERVER/server-cpp/src/ServerMain.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
    gate = (root / "CO-BETA/plugin/include/F4TSteamJoinGate.h").read_text(encoding="utf-8").replace("\r\n", "\n")


    def body(text, signature):
        start = text.index(signature)
        open_brace = text.index("{", start)
        depth = 0
        for index in range(open_brace, len(text)):
            depth += {"{": 1, "}": -1}.get(text[index], 0)
            if depth == 0:
                return text[open_brace:index + 1]
        raise AssertionError(f"unterminated body for {signature}")


    assert "std::uint64_t Begin(Operation a_operation)" in gate, \
        "Begin must return the epoch the operation registered under"
    assert "bool Finish(Operation a_operation, std::uint64_t a_epoch)" in gate, \
        "Finish must take the registration epoch so a stale worker cannot clear a newer operation"
    assert "if (epoch != a_epoch)" in gate, "Finish must only clear the operation while its epoch is current"
    assert "Operation Cancel()" in gate and "std::scoped_lock" in gate, \
        "the epoch and pending set are shared by the game thread and detached workers and need one lock"

    for name, failure, operation in (
            ("OnGuestJoinComplete", "steamJoinFailed", "Join"),
            ("OnHostJoinComplete", "steamHostFailed", "HostStart")):
        handler = body(session, f"void {name}(")
        assert "Running()" not in handler, f"{name} must not drop a failed join because the tunnel already stopped"
        assert f"FinishOperation(SteamJoinGate::Operation::{operation}, epoch)" in handler, \
            f"{name} must claim the completion with the epoch that started it"
        assert f'PushFailed("{failure}"' in handler, f"{name} must report the failure to the UI"

    run_async = body(session, "void RunAsync(")
    assert "g_gate.Begin(a_operation)" in run_async, "the async Steam operations must register with the join gate"
    assert "std::function<void(std::uint64_t)>" in session, \
        "the worker must run under the epoch it registered with, not a separately read epoch"
    assert "if (FinishOperation(operation, epoch))" in run_async, \
        "a superseded worker must drop its failure because the cancel already reported it"

    assert len(re.findall(r"BeginLocalJoin\([^;]*\bepoch, joinError\)", session)) == 2, \
        "both Steam joins must pass their epoch to BeginLocalJoin"
    assert "reinterpret_cast<void*>(static_cast<std::uintptr_t>(a_epoch))" in body(session, "bool BeginLocalJoin("), \
        "BeginLocalJoin must hand the epoch to the completion callback"

    join_worker = body(session, "void OnJoin(")
    assert "if (FinishOperation(SteamJoinGate::Operation::Join, epoch))" in join_worker, \
        "the join worker must check the epoch before it pushes a failure"
    assert "if (!EpochCurrent(epoch))" in join_worker, "the join worker must stop its tunnel when it was superseded"
    host_worker = body(session, "void OnHostStart(")
    assert "if (FinishOperation(SteamJoinGate::Operation::HostStart, epoch))" in host_worker, \
        "the host worker must check the epoch before it pushes a failure"
    assert "if (!EpochCurrent(epoch))" in host_worker, "the host worker must stop its tunnel when it was superseded"

    disconnect_now = body(ui_client, "void DisconnectNow()\n")
    assert disconnect_now.index("F4T::SteamSession::CancelJoins();") < disconnect_now.index("CancelPendingJoin("), \
        "a cancel must bump the Steam epoch before CancelPendingJoin can complete the join"

    cancel_joins = body(session, "void CancelJoins()")
    assert "g_gate.Cancel()" in cancel_joins, "CancelJoins must supersede pending Steam join completions"
    assert 'PushFailed("steamJoinFailed", "The join was cancelled.")' in cancel_joins, \
        "a cancelled Steam join must reach the UI instead of leaving the panel on connecting"
    assert 'PushFailed("steamHostFailed", "Hosting was cancelled.")' in cancel_joins, \
        "a cancelled Steam host start must reach the UI instead of leaving the panel on starting"
    assert "F4T::Steam::StopHost()" in cancel_joins and "F4T::Steam::StopGuest()" in cancel_joins, \
        "CancelJoins must drop the tunnels it cancelled"
    assert "Has(cancelled, SteamJoinGate::Operation::Join)" in cancel_joins and \
        "Has(cancelled, SteamJoinGate::Operation::HostStart)" in cancel_joins, \
        "CancelJoins must report only the operations that were actually in flight"

    assert "g_gate.Cancel()" in body(session, "void StopTunnels()"), \
        "StopTunnels must supersede pending Steam joins before stopping the tunnels"
    assert "error_handler_t::replace" in body(session, "void Push("), \
        "Steam events carry server output and must not throw on invalid UTF-8"

    ensure = body(launcher, "static std::string LaunchServer(")
    assert 'L"server-console.log"' in ensure, "the launcher must capture the server output to server-console.log"
    assert "PROC_THREAD_ATTRIBUTE_HANDLE_LIST" in ensure, "only the log handle may be inherited by the server"
    assert "LastLogLine(consoleLog)" in ensure, "an early server exit must report the server's last output line"
    assert "RotateConsoleLog(consoleLog)" in ensure, "a retry must rotate the previous server output instead of overwriting it"
    rotate = body(launcher, "void RotateConsoleLog(")
    assert 'L"server-console.prev.log"' in rotate and "MOVEFILE_REPLACE_EXISTING" in rotate, \
        "the rotated server output must land in server-console.prev.log"
    assert "SetHandleInformation(logHandle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) != FALSE" in ensure, \
        "the launch must fall back to the console path when the log handle cannot be made inheritable"
    assert "SetHandleInformation(logHandle, HANDLE_FLAG_INHERIT, 0)" in ensure, \
        "the log handle must stop being inheritable after the launch"
    assert "attributeListReady" in ensure and "DeleteProcThreadAttributeList" in ensure, \
        "the process attribute list must be deleted whenever it was initialized"
    assert "sizeof(STARTUPINFOW)" in ensure, \
        "the launch without captured output must use the plain STARTUPINFO size"
    assert "inheritLog ? LastLogLine(consoleLog)" in ensure, \
        "an early exit must not read a stale log from a previous launch"
    assert '" Full output: Data\\\\server\\\\server-console.log."' in ensure, \
        "the reported server line must be separated from the rest of the error"
    assert "MentionsFailure" not in launcher, \
        "an earlier line that mentions missing must not replace the last log line"
    assert "return SelectLastLogLine(text);" in body(launcher, "std::string LastLogLine("), \
        "LastLogLine must return the last non-empty line of the captured log"

    assert "std::setvbuf(stdout, nullptr, _IONBF, 0)" in server_main, \
        "the server must not lose its redirected output tail to stdio buffering"
    assert "std::setvbuf(stderr, nullptr, _IONBF, 0)" in server_main, \
        "the server must not lose its redirected error tail to stdio buffering"

    print("steam join failure contract: ok")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TReplicationRuntimeContract.py", _section_010_f4treplicationruntimecontract),
    ("CO-BETA/plugin/tests/F4TServerLiveContract.py", _section_011_f4tserverlivecontract),
    ("CO-BETA/plugin/tests/F4TServerPasswordStoreContract.py", _section_012_f4tserverpasswordstorecontract),
    ("CO-BETA/plugin/tests/F4TServerProfileStoreSourceContract.py", _section_013_f4tserverprofilestoresourcecontract),
    ("CO-BETA/plugin/tests/F4TSteamJoinFailureContract.py", _section_015_f4tsteamjoinfailurecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TNetworkServerContracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
