from pathlib import Path
import runpy
from test_networking_session_shutdown import (
    test_server_session_end_invalidates_pending_join_on_the_game_thread,
    test_connection_ownership_token_is_reset_when_the_owned_connection_is_torn_down,
)


ROOT = Path(__file__).resolve().parents[2]
NETWORK_SOURCE = ROOT / "CO-BETA" / "plugin" / "src" / "F4TNetworkingGns.cpp"
NETWORK_IMPL_SOURCE = ROOT / "CO-BETA" / "plugin" / "src" / "F4TNetworking.cpp"
SESSION_SOURCES = [
    ROOT / "CO-BETA" / "plugin" / "src" / ("F4TSessionLaunch%s.cpp" % part)
    for part in ("Saves", "State", "World", "Onboarding", "")
]
SAVE_FLOW_SOURCE = (ROOT / "CO-BETA" / "plugin" / "src" / "F4TSessionSaveFlow.cpp").read_text(encoding="utf-8")
WORLD_STATE_SOURCE = (ROOT / "CO-BETA" / "plugin" / "src" / "F4TSessionWorldState.cpp").read_text(encoding="utf-8")
SAVE_LINEAGE_SOURCE = ROOT / "CO-BETA" / "plugin" / "src" / "F4TSaveLineageSession.cpp"
SAVE_LINEAGE_HEADER = ROOT / "CO-BETA" / "plugin" / "include" / "F4TSaveLineageSession.h"
UI_SOURCE = ROOT / "CO-BETA" / "plugin" / "src" / "F4TUIClient.cpp"
UI_HEADER = ROOT / "CO-BETA" / "plugin" / "include" / "F4TUIClient.h"
XMAKE_SOURCE = ROOT / "CO-BETA" / "plugin" / "xmake.lua"
GNS_COMPILE_HEADER = ROOT / "CO-BETA" / "plugin" / "include" / "F4TNetworkingGnsCompile.h"
GNS_INTERNAL_HEADER = ROOT / "CO-BETA" / "plugin" / "include" / "F4TNetworkingGnsInternal.h"

def read_session_source() -> str:
    return "".join(path.read_text(encoding="utf-8") for path in SESSION_SOURCES)


def test_setup_cell_never_becomes_server_movement_baseline():
    source = NETWORK_SOURCE.read_text(encoding="utf-8")

    assert '#include "F4TSoloCell.h"' in source
    assert '#include "F4TSessionLaunch.h"' in source
    assert "return cell && !F4T::SoloCell::IsSoloCell(*cell);" in source

    wrapper_start = source.index("\n\tbool SendTransformPacket(")
    wrapper_end = source.index("\n\tbool ConnectAndValidateServer(", wrapper_start)
    wrapper = source[wrapper_start:wrapper_end]

    assert "F4T::SessionLaunch::CanPublishGameplayTransform()" in wrapper
    assert "HasBlockingGameMenu()" in wrapper
    assert "HasGameplayReadyParentCell(*player)" in wrapper
    assert "return false;" in wrapper
    assert "F4T::LocalPlayerTransformSend::Send(" in wrapper
    assert "first gameplay transform after suppression" in wrapper


def test_position_corrections_are_blocked_during_onboarding_loading_and_stale_scope():
    source = NETWORK_SOURCE.read_text(encoding="utf-8")

    gate_start = source.index("bool CanApplyPositionCorrection(")
    gate_end = source.index("\n\tbool ShouldLogCorrectionGate", gate_start)
    gate = source[gate_start:gate_end]

    assert "F4T::SessionLaunch::CanPublishGameplayTransform()" in gate
    assert "HasBlockingGameMenu()" in gate
    assert "HasGameplayReadyParentCell(a_player)" in gate

    menu_gate_start = source.index("bool HasBlockingGameMenu()")
    menu_gate_end = source.index("\n\tbool CanApplyPositionCorrection", menu_gate_start)
    menu_gate = source[menu_gate_start:menu_gate_end]
    assert "GetMenuOpen<RE::LoadingMenu>()" in menu_gate
    assert "GetMenuOpen<RE::MainMenu>()" in menu_gate

    correction_start = source.index("void QueuePositionCorrection(")
    correction_end = source.index("\n\tbool ConsumePositionCorrection", correction_start)
    correction = source[correction_start:correction_end]

    assert "CanApplyPositionCorrection(*player)" in correction
    assert "PlayerMatchesCorrectionScope" in correction
    assert "ignored stale position correction" in correction
    assert "player->SetPosition" in correction


def test_stage_100_enters_post_chargen_gameplay_wait_instead_of_completing_join():
    source = read_session_source()

    assert "WaitingForGameplayWorldReady" in source
    assert "WaitingForProfileBind" in source

    stage_start = source.index("if (g_charGenPhase == CharGenPhase::WaitingForPodExit)")
    stage_end = source.index("if (g_charGenPhase == CharGenPhase::WaitingForGameplayWorldReady)", stage_start)
    stage = source[stage_start:stage_end]
    assert "F4T::SessionCharacterSetupFlow::IsComplete()" in stage
    assert "CharGenPhase::WaitingForGameplayWorldReady" in stage
    assert "QueueProfileSave" not in stage
    assert "CompletePendingJoin(true" not in stage


def test_gameplay_ready_gate_requires_loaded_stable_non_solo_world_before_save_and_transform():
    source = read_session_source()

    ready_start = WORLD_STATE_SOURCE.index("bool IsGameplayWorldReady(")
    ready_end = WORLD_STATE_SOURCE.index("\n\tRE::MainMenu* GetMainMenu", ready_start)
    ready = WORLD_STATE_SOURCE[ready_start:ready_end]
    assert "IsWorldReadyForCharGen(a_player)" in ready
    assert "!F4T::SoloCell::IsSoloCell(*cell)" in ready

    char_gen_ready_start = WORLD_STATE_SOURCE.index("bool IsWorldReadyForCharGen(")
    char_gen_ready_end = WORLD_STATE_SOURCE.index("\n\tbool IsGameplayWorldReady", char_gen_ready_start)
    char_gen_ready = WORLD_STATE_SOURCE[char_gen_ready_start:char_gen_ready_end]
    assert "IsLoadingScreenActive()" in char_gen_ready
    assert "IsMainMenuOpen()" in char_gen_ready
    assert "IsPlaceholderSpawnPosition(a_player)" in char_gen_ready
    assert "GetParentCell()" in char_gen_ready

    phase_start = source.index("if (g_charGenPhase == CharGenPhase::WaitingForGameplayWorldReady)")
    phase_end = source.index("\n\t\tif (g_charGenPhase == CharGenPhase::WaitingForProfileBind)", phase_start)
    phase = source[phase_start:phase_end]
    assert "++g_worldReadyPolls < kGameplayReadyStablePolls" in phase
    assert "g_gameplayReadyReached = true" in phase
    assert "SetPhase(CharGenPhase::WaitingForProfileBind" in phase
    assert "QueueProfileSave()" in phase

    publish_start = source.index("bool CanPublishGameplayTransform()")
    publish_end = source.index("\n\tbool CanApplyServerCorrection()", publish_start)
    publish = source[publish_start:publish_end]
    assert "CharGenPhase::WaitingForProfileBind" in publish
    assert "g_gameplayReadyReached" in publish


def test_onboarding_save_is_not_persisted_as_gameplay_profile_and_bad_profile_reenters_onboarding():
    source = read_session_source()

    assert "bool IsFirstJoinSetupSave(" in WORLD_STATE_SOURCE
    assert "tokens[index - 2] == kFirstJoinStartCellEditorId" in WORLD_STATE_SOURCE

    launch_start = source.index("LaunchOutcome LaunchValidatedSession(")
    launch_end = source.index("\n\tbool CancelPendingJoin", launch_start)
    launch = source[launch_start:launch_end]
    assert "saveFile && IsFirstJoinSetupSave(*saveFile)" in launch
    assert "treating onboarding as incomplete" in launch
    assert "StartFirstTimeJoin(profileKey" in launch

    bind = SESSION_SOURCES[0].read_text(encoding="utf-8").split("void TryCompletePendingProfileBind(", 1)[1]
    assert "g_charGenPhase != CharGenPhase::WaitingForProfileBind" in bind
    assert "if (!a_savedName ||" in bind
    assert "if (!ServerProfileStore::BindSaveFile(boundServerKey, *a_savedName))" in bind
    assert "FailFirstTimeJoin(\"Gameplay save completed, but the server profile could not be persisted.\")" in bind
    assert "CompletePendingJoin(true" in bind


def test_first_time_ui_join_stays_pending_until_session_launch_terminal_callback():
    source = UI_SOURCE.read_text(encoding="utf-8")

    launch_start = source.index("auto deferredCompletion =")
    launch_end = source.index("\n\t\tvoid Callback_Disconnect()", launch_start)
    launch = source[launch_start:launch_end]

    assert "LaunchOutcome::Pending" in launch
    assert "if (pending)" in launch
    assert "return;" in launch
    assert "g_joinInProgress.store(false)" in launch
    assert "a_onComplete(a_success" in launch


def test_disconnect_cancels_pending_first_time_completion_and_prevents_late_success():
    source = UI_SOURCE.read_text(encoding="utf-8")

    disconnect_start = source.index("void Callback_Disconnect()")
    disconnect_end = source.index("\n\t\tbool Callback_ConnectLocal", disconnect_start)
    disconnect = source[disconnect_start:disconnect_end]
    assert "F4T::SessionLaunch::CancelPendingJoin" in disconnect
    assert "g_joinGeneration.fetch_add(1)" in disconnect

    session = read_session_source()
    cancel_start = session.index("bool CancelPendingJoin(")
    cancel_end = session.index("\n\tbool CanPublishGameplayTransform", cancel_start)
    cancel = session[cancel_start:cancel_end]
    assert "CompletePendingJoin(false" in cancel


def test_server_corrections_resume_at_gameplay_ready_before_profile_bind_completes():
    network = NETWORK_SOURCE.read_text(encoding="utf-8")
    session = read_session_source()

    gate_start = network.index("bool CanApplyPositionCorrection(")
    gate_end = network.index("\n\tbool ShouldLogCorrectionGate", gate_start)
    gate = network[gate_start:gate_end]
    assert "F4T::SessionLaunch::CanPublishGameplayTransform()" in gate

    publish_start = session.index("bool CanPublishGameplayTransform()")
    publish_end = session.index("\n\tbool CanApplyServerCorrection()", publish_start)
    publish = session[publish_start:publish_end]
    assert "CharGenPhase::WaitingForProfileBind" in publish
    assert "g_gameplayReadyReached" in publish


def test_networking_implementation_and_gns_wrapper_are_independent_translation_units():
    wrapper = NETWORK_SOURCE.read_text(encoding="utf-8")
    xmake = XMAKE_SOURCE.read_text(encoding="utf-8")
    compile_header = GNS_COMPILE_HEADER.read_text(encoding="utf-8")
    internal_header = GNS_INTERNAL_HEADER.read_text(encoding="utf-8")

    assert '#include "F4TNetworking.cpp"' not in wrapper
    runpy.run_path(str(ROOT / "scripts/verify-production-transport.py"))["verify"](ROOT)
    assert '#define F4T_GNS_WINSOCK_REDIRECT 1' in compile_header
    assert '#include "F4TGnsWinsockShim.h"' in compile_header
    assert "NetworkingGnsInternal::SocketForValidation" in compile_header
    assert "NetworkingGnsInternal::RecvWithPositionCorrection" in compile_header
    assert "ConnectAndValidateServerLegacy" in internal_header
    assert "SendTransformPacketLegacy" not in internal_header

    orphan_api = ROOT / "CO-BETA" / "plugin" / "src" / "F4TNetworkingApi.cpp"
    assert not orphan_api.exists(), (
        "F4TNetworkingApi.cpp must not exist as a translation unit; its only body duplicated "
        "the 3-arg ConnectAndValidateServer forwarder in F4TNetworkingGns.cpp and caused a "
        "duplicate-symbol link error in the AE/NG/OG F4SE plugin DLL builds."
    )

    import re
    pattern = re.compile(
        r"bool\s+ConnectAndValidateServer\s*\(\s*const\s+char\s*\*\s*a_host\s*,\s*"
        r"std::uint16_t\s+a_port\s*,\s*std::string\s*&\s*a_error\s*\)\s*\{",
    )
    matches = pattern.findall(wrapper)
    assert len(matches) == 1, (
        "Exactly one translation unit must define the 3-arg ConnectAndValidateServer "
        "forwarder; F4TNetworkingApi.cpp was deleted and F4TNetworkingGns.cpp must own "
        "the sole surviving definition."
    )

    legacy_impl = ROOT / "CO-BETA" / "plugin" / "src" / "F4TNetworking.cpp"
    legacy_source = legacy_impl.read_text(encoding="utf-8")
    assert pattern.search(legacy_source) is not None, (
        "F4TNetworking.cpp must continue to define the 3-arg legacy implementation; "
        "the legacy TU is compiled with -DConnectAndValidateServer=ConnectAndValidateServerLegacy "
        "so the textual source-level name is rewritten at compile time."
    )

    legacy_only_sources = [
        ROOT / "CO-BETA" / "plugin" / "src" / "F4TNetworking.cpp",
        ROOT / "CO-BETA" / "plugin" / "src" / "F4TNetworkingOutbound.cpp",
    ]
    xmake_name = XMAKE_SOURCE.name
    for source_path in legacy_only_sources:
        text = source_path.read_text(encoding="utf-8")
        source_name = source_path.name
        assert f'add_files("src/{source_name}", {{' in xmake, (
            f"{xmake_name} must compile {source_name} as its own translation unit "
            "so the legacy-rename define can be applied per TU; otherwise the un-renamed "
            "3-arg symbol collides with the surviving F4TNetworkingGns.cpp definition."
        )
        assert (
            '"ConnectAndValidateServer=ConnectAndValidateServerLegacy"' in xmake
        ), f"{xmake_name} must apply the legacy-rename define to keep {source_name} "
        "from exporting the un-renamed 3-arg ConnectAndValidateServer symbol."
        assert (
            f'forceincludes = "F4TNetworkingGnsCompile.h"' in xmake
        ), f"{xmake_name} must force-include F4TNetworkingGnsCompile.h into {source_name} "
        "so the legacy TU gets the same GNS Winsock shim redirection as the wrapper."

    plugin_sources_dir = ROOT / "CO-BETA" / "plugin" / "src"
    for path in sorted(plugin_sources_dir.glob("*.cpp")):
        if path in legacy_only_sources:
            continue
        text = path.read_text(encoding="utf-8")
        if path.name == "F4TNetworkingGns.cpp":
            continue
        assert pattern.search(text) is None, (
            f"{path.name} must not define an un-renamed 3-arg ConnectAndValidateServer; "
            "the only TU permitted to export that symbol at link time is F4TNetworkingGns.cpp "
            "(or the legacy TU whose name is rewritten to ConnectAndValidateServerLegacy)."
        )


def test_outbound_packets_are_sent_without_newline_framing():
    outbound = (ROOT / "CO-BETA" / "plugin" / "src" / "F4TNetworkingOutbound.cpp").read_text(encoding="utf-8")
    network = NETWORK_IMPL_SOURCE.read_text(encoding="utf-8")

    assert "push_back('\\n')" not in outbound, "outbound packets must not append a newline"
    assert '+ "\\n"' not in outbound, "outbound packets must not append a newline"
    assert '}\\n",' not in outbound, "worldState formats must not end with a newline"
    assert '{\\"type\\":\\"keepAlive\\"}";' in network, "keepAlive must be sent without a newline"


def test_pending_profile_bind_requires_a_live_server_connection():
    source = read_session_source()

    bind = SESSION_SOURCES[0].read_text(encoding="utf-8").split("void TryCompletePendingProfileBind(", 1)[1]

    assert "!F4T::Networking::IsConnectedToServer()" in bind
    assert "BindSaveFile" in bind
    assert "CompletePendingJoin(true" in bind

    guard_index = bind.index("!F4T::Networking::IsConnectedToServer()")
    assert guard_index < bind.index("ServerProfileStore::BindSaveFile"), (
        "the connectivity gate must precede the profile bind so a disconnected session "
        "can never persist the first gameplay save."
    )
    assert guard_index < bind.index("CompletePendingJoin(true"), (
        "the connectivity gate must precede join success so a disconnected session "
        "can never complete the first-time join successfully."
    )

    bind_paths = source.index("void OnF4SEMessage")
    message_path = source[bind_paths:]
    assert "TryCompletePendingProfileBind(onboardingSave);" in message_path, (
        "the post-save message path must still route through the gated bind completion "
        "so it cannot bypass the connectivity check."
    )

    cancel_start = source.index("bool CancelPendingJoin(")
    cancel_end = source.index("\n\tbool CanPublishGameplayTransform", cancel_start)
    cancel = source[cancel_start:cancel_end]
    assert "CompletePendingJoin(false" in cancel



def test_stale_handshake_attempt_cannot_disconnect_a_newer_connection():
    source = UI_SOURCE.read_text(encoding="utf-8")

    assert "std::atomic_uint64_t g_connectionOwnerToken{ 0 };" in source
    assert "std::atomic_uint64_t g_connectionTokenCounter{ 0 };" in source
    assert "const auto attemptToken = g_connectionTokenCounter.fetch_add(1) + 1;" in source
    assert "g_connectionOwnerToken.store(attemptToken);" in source

    stale_start = source.index("if (g_joinGeneration.load() != joinGeneration) {")
    stale_end = source.index("\n\t\t\t\t\tif (!connected) {", stale_start)
    stale = source[stale_start:stale_end]
    assert "if (connected && g_connectionOwnerToken.load() == attemptToken) {" in stale, (
        "a stale attempt must only disconnect the connection it still explicitly owns; "
        "otherwise it tears down a newer connection established by Callback_ConnectLocal."
    )

    deferred_start = source.index("auto deferredCompletion =")
    deferred_end = source.index("\n\t\t\t\t\tconst auto outcome", deferred_start)
    deferred = source[deferred_start:deferred_end]
    assert "if (!a_success && g_connectionOwnerToken.load() == attemptToken && !F4T::SessionSaveGuard::IsSessionExitPending()) {" in deferred, (
        "the deferred failure path must also be ownership-guarded so a late failure cannot "
        "disconnect a newer connection."
    )

    connect_local_start = source.index("bool Callback_ConnectLocal(")
    connect_local_end = source.index("\n\t\tbool Callback_IsConnected", connect_local_start)
    connect_local = source[connect_local_start:connect_local_end]
    assert "g_connectionOwnerToken.store(g_connectionTokenCounter.fetch_add(1) + 1);" in connect_local, (
        "an explicit local connect must claim connection ownership with a newer token so a "
        "lingering stale join attempt cannot disconnect it."
    )


def test_join_completion_fires_at_most_once_and_late_completion_is_dropped():
    session = read_session_source()

    complete_start = session.index("void CompletePendingJoin(")
    complete_end = session.index("\n\tvoid FailFirstTimeJoin", complete_start)
    complete = session[complete_start:complete_end]
    assert "std::move(g_pendingJoinCompletion)" in complete, (
        "the pending completion must be moved out before firing so duplicate terminal "
        "callbacks cannot both report a result."
    )
    assert "g_pendingJoinCompletion = {};" in complete, (
        "the pending completion must be cleared after firing so a second completion "
        "path finds nothing to invoke."
    )

    cancel_start = session.index("bool CancelPendingJoin(")
    cancel_end = session.index("\n\tbool CanPublishGameplayTransform", cancel_start)
    cancel = session[cancel_start:cancel_end]
    assert "if (!IsFirstJoinInProgress() && !g_pendingProfileBind) {" in cancel, (
        "CancelPendingJoin must be a no-op when nothing is pending so it cannot emit a "
        "spurious completion after the join already ended."
    )

    source = UI_SOURCE.read_text(encoding="utf-8")
    deferred_start = source.index("auto deferredCompletion =")
    deferred = source[deferred_start:]
    assert "if (g_joinGeneration.load() != joinGeneration) {" in deferred, (
        "a superseded generation must drop its completion instead of reporting success."
    )


def test_connect_local_is_rejected_while_a_join_is_pending():
    source = UI_SOURCE.read_text(encoding="utf-8")

    connect_local_start = source.index("bool Callback_ConnectLocal(")
    connect_local_end = source.index("\n\t\tbool Callback_IsConnected", connect_local_start)
    connect_local = source[connect_local_start:connect_local_end]

    assert "if (g_joinInProgress.load()) {" in connect_local, (
        "Callback_ConnectLocal must refuse an explicit local connect while a join "
        "(handshake, first-time onboarding, or a pending profile bind) is still active. "
        "IsConnectedToServer only proves a socket exists, not which session it belongs "
        "to, so without this guard a dead join A's socket can be silently replaced by a "
        "fresh local connection B while A (e.g. still WaitingForProfileBind) believes it "
        "owns the live connection, letting a later save event bind/complete A against B."
    )

    guard_index = connect_local.index("if (g_joinInProgress.load()) {")
    guard_end_index = connect_local.index("}", guard_index) + 1
    guard_block = connect_local[guard_index:guard_end_index]
    assert "return false;" in guard_block
    assert "g_joinInProgress.store" not in guard_block, (
        "the guard must only observe join state, never mutate it, so it cannot "
        "interfere with an unrelated in-flight join's own state machine."
    )

    connect_index = connect_local.index("F4T::Networking::ConnectToLocalServer(")
    assert guard_end_index <= connect_index, (
        "the join-in-progress guard must run before the socket is ever established, so "
        "a stale join A can never be shadowed by a fresh connection B while A is pending."
    )


def test_connect_local_still_claims_ownership_when_no_join_is_pending():
    source = UI_SOURCE.read_text(encoding="utf-8")

    connect_local_start = source.index("bool Callback_ConnectLocal(")
    connect_local_end = source.index("\n\t\tbool Callback_IsConnected", connect_local_start)
    connect_local = source[connect_local_start:connect_local_end]

    assert "g_connectionOwnerToken.store(g_connectionTokenCounter.fetch_add(1) + 1);" in connect_local, (
        "a local connect with no join in progress must still claim a fresh ownership "
        "token exactly as before, so normal local-connect and reconnect flows (including "
        "an explicit Disconnect followed by ConnectLocal) are unchanged by the new guard."
    )
    assert connect_local.index("F4T::Networking::ConnectToLocalServer(") < connect_local.index(
        "g_connectionOwnerToken.store(g_connectionTokenCounter.fetch_add(1) + 1);"
    )



def test_join_continuation_revalidates_a_live_connection_before_launching():
    source = UI_SOURCE.read_text(encoding="utf-8")

    continuation_start = source.index("if (g_joinGeneration.load() != joinGeneration) {")
    continuation_end = source.index("const auto outcome = F4T::SessionLaunch::LaunchValidatedSession(", continuation_start)
    continuation = source[continuation_start:continuation_end]

    generation_index = continuation.index("if (g_joinGeneration.load() != joinGeneration) {")
    connected_index = continuation.index("if (!connected) {")
    live_index = continuation.index("if (!F4T::Networking::IsConnectedToServer()) {")
    assert generation_index < connected_index < live_index, (
        "the join continuation must order: generation check, then the captured-connected "
        "check, then a live-connection revalidation. ConnectAndValidateServer "
        "returns true after welcome/playerId while ReceiveLoop is already running, so the captured "
        "connected bool can be stale by the time this game-thread continuation executes."
    )

    live_guard_end = continuation.index("return;", live_index)
    live_guard = continuation[live_index:live_guard_end]
    assert "g_connectionOwnerToken.compare_exchange_strong(ownerToken, 0);" in live_guard
    worker = source[source.index("auto worker = [host,"):continuation_start]
    assert worker.index("if (connected) {") < worker.index("g_connectionOwnerToken.store(attemptToken);") < worker.index("taskInterface->AddTask(")
    assert "g_joinInProgress.store(false);" in live_guard, (
        "a join whose connection died before the continuation ran must clear g_joinInProgress "
        "so a later join (or an explicit local connect) is not blocked by a stuck flag."
    )
    assert "a_onComplete(false" in live_guard, (
        "the dead-connection path must fail the join exactly once through the completion "
        "callback so the UI reports failure instead of success."
    )
    assert "g_connectionOwnerToken.store(attemptToken);" not in live_guard, (
        "the dead-connection path must not adopt ownership, since there is no live "
        "connection to own."
    )
    assert "LaunchValidatedSession" not in live_guard, (
        "the dead-connection path must not call LaunchValidatedSession; an existing profile "
        "would otherwise return Complete without rechecking connectivity and the UI would "
        "report join success after the connection already died."
    )

    # Prove the race is real: the receive thread starts before the handshake returns and
    # can disconnect the socket before the queued continuation runs.
    network = NETWORK_IMPL_SOURCE.read_text(encoding="utf-8")
    connect_start = network.index("bool ConnectToServerInternal(")
    connect_end = network.index("namespace F4T::Networking", connect_start)
    connect_internal = network[connect_start:connect_end]
    assert "StartReceiveThread();" in connect_internal, (
        "ConnectToServerInternal must start the receive thread before returning; that thread "
        "is what can consume a sessionEnded packet and close the socket before the join "
        "continuation runs."
    )
    assert "F4T::Networking::IsConnectedToServer()" in source


def test_synchronous_complete_launch_revalidates_the_live_session_before_success():
    source = UI_SOURCE.read_text(encoding="utf-8")

    launch_index = source.index("const auto outcome = F4T::SessionLaunch::LaunchValidatedSession(")
    boundary_index = source.index(
        "if (outcome == F4T::SessionLaunch::LaunchOutcome::Complete) {", launch_index
    )
    success_index = source.index('a_onComplete(ok, ok ? "" : launchError.c_str(), a_userData);', launch_index)

    assert launch_index < boundary_index < success_index, (
        "an existing-profile launch can return LaunchOutcome::Complete synchronously, so a "
        "final success boundary must sit after LaunchValidatedSession and before the success "
        "callback. The Race 5 pre-launch check cannot cover a socket that dies while "
        "LaunchValidatedSession itself runs."
    )

    boundary_end = source.index("if (!ok) {", boundary_index)
    boundary = source[boundary_index:boundary_end]

    assert "g_joinGeneration.load() != joinGeneration" in boundary, (
        "the final success boundary must revalidate the join generation so a superseded "
        "attempt cannot report success after LaunchValidatedSession returns."
    )
    assert "!F4T::Networking::IsConnectedToServer()" in boundary, (
        "the final success boundary must revalidate IsConnectedToServer() so a socket that "
        "died during LaunchValidatedSession cannot still be reported as a successful join."
    )
    assert "g_connectionOwnerToken.load() == attemptToken" in boundary, (
        "the boundary must only tear the connection down when this attempt still owns it, "
        "preserving the Race 3/4 ownership model."
    )

    fail_index = boundary.index("a_onComplete(")
    fail_call_end = boundary.index("return;", fail_index)
    fail_call = boundary[fail_index:fail_call_end]
    assert "false" in fail_call, (
        "the dead-connection/superseded path must report failure, never success."
    )
    assert "g_joinInProgress.store(false);" in boundary[:fail_index], (
        "the boundary must clear g_joinInProgress before failing so the join state is "
        "resolved rather than stranded."
    )
    assert "g_connectionOwnerToken.store(0);" in boundary[:fail_index], (
        "the boundary must clear ownership when it tears its own connection down."
    )
    assert boundary_index + boundary.index("return;") < success_index, (
        "the boundary must return before falling through to the success callback."
    )
    pending_index = source.index("if (pending) {", launch_index)
    assert pending_index < boundary_index, (
        "the pending path must still return before the final success boundary, so "
        "LaunchOutcome::Pending behavior is unchanged."
    )

    session = read_session_source()
    complete_return = session.index("return LaunchOutcome::Complete;")
    load_started_guard = session.index("if (!loadStarted) {")
    assert load_started_guard < complete_return, (
        "the synchronous Complete path proven here is the existing-profile load branch, "
        "which returns Complete without any connectivity recheck."
    )
    assert "IsConnectedToServer" not in session[load_started_guard:complete_return], (
        "LaunchValidatedSession itself must not be assumed to recheck connectivity; the "
        "final boundary in the join continuation is what closes this race."
    )


def test_pending_profile_bind_revalidates_liveness_immediately_before_binding():
    source = read_session_source()

    bind = SESSION_SOURCES[0].read_text(encoding="utf-8").split("void TryCompletePendingProfileBind(", 1)[1]

    assert bind.count("!F4T::Networking::IsConnectedToServer()") == 2, (
        "the pending bind completion must recheck connectivity at entry (Race 1) and again "
        "immediately before BindSaveFile, because the receive thread can close the socket "
        "during save discovery after the entry check has already passed."
    )

    entry_index = bind.index("!F4T::Networking::IsConnectedToServer()")
    pre_bind_index = bind.rindex("!F4T::Networking::IsConnectedToServer()")
    bind_index = bind.index("ServerProfileStore::BindSaveFile")
    success_index = bind.index("CompletePendingJoin(true")

    assert entry_index < pre_bind_index < bind_index < success_index, (
        "the pre-bind liveness recheck must sit after the entry check and before "
        "BindSaveFile and CompletePendingJoin(true)."
    )

    guard_end = bind.index("return;", pre_bind_index)
    assert pre_bind_index < guard_end < bind_index, (
        "the pre-bind liveness guard must return rather than fall through to binding, so a "
        "dead connection can neither persist the first gameplay save nor complete success."
    )

    guard = bind[pre_bind_index:guard_end]
    assert "g_pendingProfileBind = false" not in guard, (
        "the guard must leave the pending state for the existing disconnect handling to "
        "resolve, matching Race 1, instead of silently dropping it."
    )
    assert "CompletePendingJoin" not in guard


def test_pending_commit_boundary_is_the_single_success_authority():
    source = read_session_source()

    bind = SESSION_SOURCES[0].read_text(encoding="utf-8").split("void TryCompletePendingProfileBind(", 1)[1]

    boundary_index = bind.rindex("!F4T::Networking::IsConnectedToServer()")
    bind_index = bind.index("ServerProfileStore::BindSaveFile")
    active_index = bind.index("g_activeProfileServerKey = boundServerKey;")
    clear_index = bind.index("g_pendingProfileBind = false;")
    complete_index = bind.index("CompletePendingJoin(true")

    assert boundary_index < bind_index < active_index < clear_index < complete_index, (
        "the pre-BindSaveFile liveness check must be the single Pending success commit "
        "boundary: nothing is bound, published, or cleared until it passes, and "
        "CompletePendingJoin(true) is the last step after the profile is committed."
    )

    assert bind.count("CompletePendingJoin(true") == 1, (
        "the committed Pending success must be reported from exactly one place."
    )

    complete_start = source.index("void CompletePendingJoin(")
    complete_end = source.index("\n\tvoid FailFirstTimeJoin", complete_start)
    complete = source[complete_start:complete_end]
    phase_index = complete.index("SetPhase(CharGenPhase::Complete")
    working_index = complete.index("ClearFirstJoinWorkingState(a_reason);")
    invoke_index = complete.index("completion(a_success, a_reason);")
    assert phase_index < working_index < invoke_index, (
        "CompletePendingJoin must commit the terminal phase and clear working state before "
        "invoking the deferred completion, so past the boundary the success is already "
        "authoritative and a later disconnect cannot retroactively undo it."
    )


def test_deferred_pending_callback_cannot_downgrade_a_committed_success():
    source = UI_SOURCE.read_text(encoding="utf-8")

    deferred_start = source.index("auto deferredCompletion =")
    deferred_end = source.index(
        "const auto outcome = F4T::SessionLaunch::LaunchValidatedSession(", deferred_start
    )
    deferred = source[deferred_start:deferred_end]

    assert "IsConnectedToServer" not in deferred, (
        "the deferred completion must not recheck or rewrite connectivity. CompletePendingJoin(true) "
        "is only reached after TryCompletePendingProfileBind's pre-bind liveness boundary, so "
        "re-checking here would let a disconnect after that boundary turn a profile-bound, "
        "CharGenPhase::Complete join into a reported failure."
    )
    assert "a_success = false" not in deferred, (
        "the deferred completion must never downgrade an already-committed success; a "
        "post-boundary disconnect is a normal post-join disconnect, not a retroactive join failure."
    )

    generation_index = deferred.index("if (g_joinGeneration.load() != joinGeneration) {")
    ownership_index = deferred.index("if (!a_success && g_connectionOwnerToken.load() == attemptToken")
    clear_index = deferred.index("g_joinInProgress.store(false);")
    callback_index = deferred.index("a_onComplete(a_success")
    assert generation_index < ownership_index < clear_index < callback_index, (
        "the generation drop and the ownership-guarded failure teardown must be preserved, "
        "and the callback must report exactly the success value it was given."
    )
    assert "g_connectionOwnerToken.store(0);" in deferred


def test_active_profile_binding_takes_a_resolved_name_instead_of_a_resolver():
    header = SAVE_LINEAGE_HEADER.read_text(encoding="utf-8")
    source = SAVE_LINEAGE_SOURCE.read_text(encoding="utf-8")

    for text in (header, source):
        signature_start = text.index("void TryUpdateActiveProfileBinding(")
        signature_end = text.index(")", signature_start)
        signature = text[signature_start:signature_end]
        assert "(*" not in signature, "TryUpdateActiveProfileBinding must not take a function-pointer resolver"
        assert "std::function" not in signature
        assert "a_bindableSaveFile" in signature

    assert "const auto& saveFileName = a_bindableSaveFile;" in source
    assert "a_resolveBindableSaveFile" not in source


def test_post_save_game_binds_the_captured_save_name_not_the_previous_save():
    source = read_session_source()

    resolver_start = SAVE_FLOW_SOURCE.index("std::optional<std::string> ResolveBindablePostSaveName(")
    resolver_end = SAVE_FLOW_SOURCE.index("\n\tJoinSaveResolution ResolveJoinSave(", resolver_start)
    resolver = SAVE_FLOW_SOURCE[resolver_start:resolver_end]
    assert "a_captured.empty() || SessionWorldState::IsFirstJoinSetupSave(a_captured)" in resolver
    assert "ResolveCurrentSaveFileName" not in resolver, "an empty capture must skip the bind, not read mostRecentSaveGame"
    assert "ResolveBindableCurrentSaveFileName" not in (source + SAVE_FLOW_SOURCE)
    assert "return std::nullopt;" in resolver
    assert "return std::string{ a_captured };" in resolver

    post_start = source.index("case F4SE::MessagingInterface::kPostSaveGame:")
    post_end = source.index("case F4SE::MessagingInterface::kPostLoadGame:", post_start)
    post = source[post_start:post_end]
    assert "const auto savedName = F4T::SaveLineageSession::ReadPostSaveGameName(*a_msg);" in post
    assert "ResolveBindablePostSaveName(savedName)" in post
    assert "ResolveBindableCurrentSaveFileName" not in post


def test_first_join_bind_takes_the_captured_name_not_the_queue_or_baseline():
    source = read_session_source()

    bind = SESSION_SOURCES[0].read_text(encoding="utf-8").split("void TryCompletePendingProfileBind(", 1)[1]
    assert "queuedTasks" not in bind, "the first-join bind must not read the save queue"
    assert "ResolveCurrentSaveFileName" not in bind, "the first-join bind must bind the captured name, not mostRecentSaveGame"
    assert "g_profileSaveBaseline" not in source, "the save-completed baseline is replaced by the captured name"

    assert source.count("TryCompletePendingProfileBind(") == 2, (
        "TryCompletePendingProfileBind must have one definition and one call, made from kPostSaveGame only"
    )
    post_start = source.index("case F4SE::MessagingInterface::kPostSaveGame:")
    post_end = source.index("case F4SE::MessagingInterface::kPostLoadGame:", post_start)
    post = source[post_start:post_end]
    capture_index = post.index("const auto savedName = F4T::SaveLineageSession::ReadPostSaveGameName(*a_msg);")
    first_join_index = post.index("TryCompletePendingProfileBind(onboardingSave);")
    lineage_index = post.index("F4T::SaveLineageSession::TryUpdateActiveProfileBinding(")
    assert capture_index < first_join_index < lineage_index, (
        "the first-join bind must take the name captured at kPostSaveGame and run before the lineage bind."
    )


def test_save_lineage_reads_most_recent_save_only_inside_pending_record_start():
    source = SAVE_LINEAGE_SOURCE.read_text(encoding="utf-8")

    begin_start = source.index("void BeginPendingRecord(")
    begin_end = source.index("\n\tvoid PollPendingRecord()", begin_start)
    begin = source[begin_start:begin_end]
    assert "mostRecentSaveGame" in begin
    assert source.count("mostRecentSaveGame") == begin.count("mostRecentSaveGame")


_TEST_PREFIX = "test_"


def main() -> int:
    failures: list[str] = []
    tests = [
        (name, value)
        for name, value in sorted(globals().items())
        if name.startswith(_TEST_PREFIX) and callable(value)
    ]

    for name, func in tests:
        try:
            func()
        except AssertionError as exc:
            failures.append(f"{name}: {exc}")
        except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
            failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print(f"first-join spawn safety regression contracts passed ({len(tests)} checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
