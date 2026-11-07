#!/usr/bin/env python3
"""test_steam_join_contracts: consolidated contract sections.

Consolidated from 6 single-contract scripts so the group runs one process
instead of 6. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_steam_join_contracts.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


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




def _section_001_test_steam_guest_profile_key_contract():
    """Contract moved verbatim from tests/audit/test_steam_guest_profile_key_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_steam_guest_profile_key_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    PLUGIN = ROOT / "CO-BETA" / "plugin"
    STEAM_SESSION = (PLUGIN / "src" / "F4TSteamSession.cpp").read_text(encoding="utf-8")
    SESSION_LAUNCH = (PLUGIN / "src" / "F4TSessionLaunch.cpp").read_text(encoding="utf-8")
    SERVER_KEY = (PLUGIN / "src" / "F4TServerKey.cpp").read_text(encoding="utf-8")
    PROFILE_STORE = (PLUGIN / "src" / "F4TServerProfileStore.cpp").read_text(encoding="utf-8")
    CLIENT_IDENTITY = (PLUGIN / "src" / "F4TClientIdentity.cpp").read_text(encoding="utf-8")
    NETWORKING_GNS = (PLUGIN / "src" / "F4TNetworkingGns.cpp").read_text(encoding="utf-8")
    UI_CLIENT = (PLUGIN / "src" / "F4TUIClient.cpp").read_text(encoding="utf-8")


    def _slice(source, start, end):
        begin = source.index(start)
        return source[begin:source.index(end, begin)]


    def test_steam_guest_join_is_keyed_by_host_steam_id():
        on_join = _slice(STEAM_SESSION, "void OnJoin(const char* a_friendCode", "void OnRichPresenceJoin")

        assert "const auto host = F4T::Steam::FindHost(friendCode);" in on_join
        assert 'BeginLocalJoin("steam-" + std::to_string(host.steamId), "Steam friend", connected.loopbackPort' in on_join, (
            "the guest join id must come from the host SteamID so the profile key survives the ephemeral loopback port."
        )
        assert "steam-guest" not in STEAM_SESSION


    def test_steam_host_path_keeps_its_id_and_fixed_port():
        assert "constexpr std::uint16_t kLocalServerPort = 7777;" in STEAM_SESSION
        assert 'BeginLocalJoin("steam-host", name, kLocalServerPort, password, &OnHostJoinComplete, epoch, joinError)' in STEAM_SESSION
        assert 'request.host = "127.0.0.1";' in STEAM_SESSION


    def test_steam_guest_id_helper_accepts_only_nonzero_steam_ids():
        assert "bool IsSteamGuestJoinId(std::string_view a_joinId)" in SERVER_KEY
        assert 'constexpr std::string_view kSteamGuestJoinIdPrefix{ "steam-" };' in SERVER_KEY
        assert "steamId != 0" in SERVER_KEY


    def test_profile_endpoint_maps_steam_guest_to_id_and_port_7777():
        endpoint = _slice(PROFILE_STORE, "std::pair<std::string, std::uint16_t> ProfileEndpoint(", "std::string ProfileServerKey(")

        assert "constexpr std::uint16_t kSteamGuestProfilePort = 7777;" in endpoint
        assert 'ServerProfileKey::NormalizeHost(a_host) == "127.0.0.1" && ServerProfileKey::IsSteamGuestJoinId(a_joinId)' in endpoint
        assert "return { ServerProfileKey::SteamGuestProfileHost(a_joinId), kSteamGuestProfilePort };" in endpoint
        assert "return { a_joinId, kSteamGuestProfilePort };" not in endpoint, (
            "a guest keyed by a bare steam-<id> host would collide with a real server served at that host name on :7777"
        )
        assert "return { a_host, a_port };" in endpoint


    def test_steam_guest_profile_host_is_not_a_hostname():
        assert "std::string SteamGuestProfileHost(std::string_view a_joinId)" in SERVER_KEY
        assert 'constexpr std::string_view kSteamGuestHostPrefix{ "steam:" };' in SERVER_KEY, (
            "the Steam guest profile host must be separated by a character no host name can contain, "
            "otherwise a server listed as host steam-<id> on :7777 shares the guest's profile, resume "
            "credential and save attestation"
        )
        assert "std::string{ kSteamGuestHostPrefix } + std::string{ a_joinId };" not in SERVER_KEY, (
            "the join id already carries the steam- prefix, so concatenating it whole yields "
            "steam:steam-<id> instead of steam:<id>"
        )
        assert "std::string{ kSteamGuestHostPrefix } + std::string{ steamId }" in SERVER_KEY
        assert "std::string_view kSteamGuestJoinIdPrefix{ \"steam-\" };" in SERVER_KEY
        assert "a_joinId.substr(kSteamGuestJoinIdPrefix.size())" in _slice(
            SERVER_KEY, "std::string SteamGuestProfileHost(", "bool IsSteamGuestJoinId("
        ), "the guest host must strip the join id's steam- prefix to key the guest as steam:<digits>"


    def test_steam_join_refuses_an_empty_host_id():
        on_join = _slice(STEAM_SESSION, "void OnJoin(const char* a_friendCode", "void OnRichPresenceJoin")

        assert "if (host.steamId == 0) {" in on_join, (
            "a zero host SteamID is not a Steam guest join id, so the profile key would silently fall back "
            "to the ephemeral loopback port and mint a new profile on every join"
        )
        assert on_join.index("if (host.steamId == 0) {") < on_join.index('BeginLocalJoin("steam-"'), (
            "the refusal must happen before the guest join starts"
        )


    def test_launch_derives_profile_key_from_join_id():
        launch = _slice(SESSION_LAUNCH, "LaunchOutcome LaunchValidatedSession(", "bool CancelPendingJoin(")

        assert "ServerProfileStore::ProfileEndpoint(host, port, a_request.id)" in launch
        assert "ServerProfileStore::MakeServerKey(profileHost, profilePort)" in launch
        assert "ServerProfileStore::ResolveServerKey(profileHost, profilePort)" in launch
        assert "ServerProfileStore::MakeServerKey(host, port)" not in launch
        assert "ServerProfileStore::ResolveServerKey(host, port)" not in launch


    def test_connection_identity_uses_the_same_profile_key():
        identity = _slice(CLIENT_IDENTITY, "std::optional<ConnectionIdentity> PrepareConnectionIdentity(", "void HoldConnectionIdentity")

        assert "ServerProfileStore::ProfileServerKey(a_host, a_port, a_joinId)" in identity, (
            "the resume credential, profile id and save attestation are keyed by the connection identity, "
            "so they must use the same profile key as the launch or a guest never finds its own profile."
        )
        assert "ServerProfileStore::ResolveServerKey(a_host" not in identity
        assert "ServerProfileStore::MakeServerKey(a_host" not in identity
        assert "PrepareConnectionIdentity(host, port, a_identityError, a_serverPassword, a_joinId)" in NETWORKING_GNS
        assert "ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, fingerprint, a_joinId, true, cancelled, a_backgroundValidation, a_generationOut);" in NETWORKING_GNS


    def test_join_worker_passes_join_id_and_rejection_uses_profile_key():
        assert "host.c_str(), port, connectionError, &connectionFailure, serverPassword, id, true, &connectionGeneration);" in UI_CLIENT
        assert "const auto serverKey = F4T::ServerProfileStore::ProfileServerKey(host, port, id);" in UI_CLIENT
        assert "F4T::ServerProfileStore::ResolveServerKey(host, port)" not in UI_CLIENT


    def main() -> int:
        test_steam_guest_join_is_keyed_by_host_steam_id()
        test_steam_host_path_keeps_its_id_and_fixed_port()
        test_steam_guest_id_helper_accepts_only_nonzero_steam_ids()
        test_steam_guest_profile_host_is_not_a_hostname()
        test_steam_join_refuses_an_empty_host_id()
        test_profile_endpoint_maps_steam_guest_to_id_and_port_7777()
        test_launch_derives_profile_key_from_join_id()
        test_connection_identity_uses_the_same_profile_key()
        test_join_worker_passes_join_id_and_rejection_uses_profile_key()
        print("Steam guest profile key contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_002_test_steam_host_lobby_contract():
    """Contract moved verbatim from tests/audit/test_steam_host_lobby_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_steam_host_lobby_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    PLUGIN = ROOT / "CO-BETA" / "plugin"
    STEAM_SESSION = (PLUGIN / "src" / "F4TSteamSession.cpp").read_text(encoding="utf-8")
    STEAM_SERVICE = (PLUGIN / "src" / "F4TSteamTunnelService.cpp").read_text(encoding="utf-8")


    def _slice(source, start, end):
        begin = source.index(start)
        return source[begin:source.index(end, begin)]


    def test_host_start_does_not_auto_join_the_local_server():
        on_host_start = _slice(STEAM_SESSION, "void OnHostStart(const char* a_name", "void OnHostEnter()")

        assert "BeginLocalJoin" not in on_host_start, "Start Hosting must leave the host in the lobby, not join the local server."
        assert "g_hostJoinName = name;" in on_host_start
        assert on_host_start.index("g_hostJoinName = name;") < on_host_start.index('"steamHostStarted"')


    def test_host_enter_joins_the_local_server_without_stopping_hosting():
        on_host_enter = _slice(STEAM_SESSION, "void OnHostEnter()", "void OnLobbyPeers(")

        assert 'BeginLocalJoin("steam-host", name, kLocalServerPort, password, &OnHostJoinComplete, epoch, joinError)' in on_host_enter
        assert "F4T::Steam::HostRunning()" in on_host_enter
        assert "StopHost" not in on_host_enter, "a failed Enter world must keep the lobby and friend code valid."


    def test_host_enter_is_the_last_steam_callback():
        callbacks = _slice(STEAM_SESSION, "static const COMMONWEALTH_ONLINE_UI_API::SteamCallbacks kCallbacks{", "};")

        assert callbacks.rstrip().endswith("OnHostEnter")


    def test_service_pump_reports_lobby_peers_only_on_change():
        pump = _slice(STEAM_SERVICE, "void PumpLoop()", "void StartPumpLocked(")
        refresh = _slice(STEAM_SERVICE, "bool RefreshLobbyLocked(", "std::vector<LobbyPeer> ResolveLobbyPeers(")

        assert "RefreshLobbyLocked(service, steamIds)" in pump
        assert "sink = service.lobbyPeersSink;" in pump
        assert pump.index("sink(peers);") > pump.index("RefreshLobbyLocked(service, steamIds)")
        assert "generation = service.lobbyGeneration.load(" in pump
        assert "if (generation == service.lobbyGeneration.load(" in pump
        assert "a_service.host->PeerSteamIds()" in refresh
        assert "steamIds == a_service.lobbyPeerIds" in refresh
        assert "friendName" not in refresh, "the Steam friend-name call must not run under Service::mutex."


    def test_service_pump_resolves_peer_names_outside_the_service_mutex():
        resolver = _slice(STEAM_SERVICE, "std::vector<LobbyPeer> ResolveLobbyPeers(", "void PumpLoop()")
        pump = _slice(STEAM_SERVICE, "void PumpLoop()", "void StartPumpLocked(")

        assert "a_friends.friendName(steamId)" in resolver
        assert "FriendCode(steamId)" in resolver
        assert pump.index("ResolveLobbyPeers(friends, steamIds)") > pump.index("RefreshLobbyLocked(service, steamIds)")


    def test_session_pushes_steam_lobby_peers_from_the_pump_sink():
        on_lobby_peers = _slice(STEAM_SESSION, "void OnLobbyPeers(", "void OnHostStop()")
        stop_host = _slice(STEAM_SERVICE, "void StopHost()", "bool HostRunning()")

        assert "F4T::Steam::SetLobbyPeersSink(&OnLobbyPeers);" in STEAM_SESSION
        assert '{ "type", "steamLobbyPeers" }' in on_lobby_peers
        assert '{ "peers", peers }' in on_lobby_peers
        assert "lobbyPeerIds.clear();" in stop_host
        assert "std::lock_guard delivery{ service.lobbyDeliveryMutex };" in stop_host
        assert stop_host.index("service.lobbyGeneration.fetch_add(1") < stop_host.index("if (lobbySink) lobbySink({});")


    def main():
        tests = [v for k, v in sorted(_merge_ns.items()) if k.startswith("test_") and callable(v)]
        for test in tests:
            test()
        print(f"{Path(__file__).stem}: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_003_test_steam_join_gaps_contract():
    """Contract moved verbatim from tests/audit/test_steam_join_gaps_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_steam_join_gaps_contract.py")
    import re
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    PLUGIN = ROOT / "CO-BETA" / "plugin"
    UI = ROOT / "CommonwealthOnlineUI"


    def _read(path):
        return path.read_text(encoding="utf-8").replace("\r\n", "\n")


    SESSION = _read(PLUGIN / "src" / "F4TSteamSession.cpp")
    SESSION_HEADER = _read(PLUGIN / "include" / "F4TSteamSession.h")
    UI_CLIENT = _read(PLUGIN / "src" / "F4TUIClient.cpp")
    ALERT = _read(PLUGIN / "src" / "F4THandshakeRejectionAlert.cpp")
    ACTIONS_HEADER = _read(UI / "include" / "F4TSteamActions.h")
    ACTIONS_SOURCE = _read(UI / "src" / "F4TSteamActions.cpp")
    API_HEADER = _read(UI / "include" / "CommonwealthOnlineUI_API.h")
    BROWSER_INTERNAL = _read(UI / "include" / "F4TServerBrowserBridgeInternal.h")


    def _body(source, signature):
        start = source.index(signature)
        open_brace = source.index("{", start)
        depth = 0
        for index in range(open_brace, len(source)):
            depth += {"{": 1, "}": -1}.get(source[index], 0)
            if depth == 0:
                return source[open_brace:index + 1]
        raise AssertionError(f"unterminated body for {signature}")


    def _in_order(text, *needles):
        last = -1
        for needle in needles:
            position = text.find(needle, last + 1)
            assert position > last, f"{needle!r} is missing or out of order"
            last = position


    def test_launch_args_are_parsed_once_and_queued_in_attach():
        attach = _body(SESSION, "void Attach()")

        assert SESSION.count("ParseLaunchArgs(") == 1
        _in_order(
            attach,
            "F4T::Steam::Initialize();",
            "F4T::Steam::ParseLaunchArgs(GetCommandLineA());",
            "g_hasLaunchJoin.store(launch.connect.has_value() || launch.lobbyId.has_value());",
            "F4T::Steam::QueueLaunchJoin(launch);",
        )


    def test_launch_join_fires_only_once_the_main_menu_is_open():
        ready = _body(UI_CLIENT, "void OnGameDataReady()")
        attach = _body(SESSION, "void Attach()")
        fire = _body(SESSION, "void FireLaunchJoin()")

        assert "FireLaunchJoin" not in attach, "the launch join must not fire before the UI can show a refusal."
        _in_order(
            fire,
            "!g_hasLaunchJoin.load()",
            "g_launchJoinWatching.exchange(true)",
            "std::thread([]() {",
            "F4SE::GetTaskInterface()",
            "AddTask([]() {",
            "F4T::SessionWorldState::IsMainMenuOpen()",
            "g_launchJoinFired.store(true);",
            "(void)F4T::Steam::FireLaunchJoin();",
            ".detach();",
        )
        assert '#include "F4TSessionWorldState.h"' in SESSION
        assert len(re.findall(r"F4T::Steam::FireLaunchJoin\(\)", SESSION)) == 1
        _in_order(
            ready,
            "F4T::SteamSession::Attach();",
            "g_ui->OnGameDataReady();",
            "F4T::SteamSession::FireLaunchJoin();",
        )
        assert "void FireLaunchJoin();" in SESSION_HEADER
        for source in PLUGIN.joinpath("src").glob("*.cpp"):
            if source.name in ("F4TSteamSession.cpp", "F4TUIClient.cpp"):
                continue
            assert "SteamSession::FireLaunchJoin" not in _read(source), f"{source.name} must not fire the launch join."


    def test_join_failure_handler_pushes_the_refusal_text_to_the_ui():
        attach = _body(SESSION, "void Attach()")
        refused = _body(SESSION, "void OnJoinRefused(")

        assert "F4T::Steam::SetJoinFailureHandler(&OnJoinRefused);" in attach
        assert 'PushFailed("steamJoinFailed", a_message' in refused


    def test_steam_status_is_pushed_after_attach_and_every_retry():
        status = _body(SESSION, "void PushStatus()")
        attach = _body(SESSION, "void Attach()")

        for field in ('{ "type", "steamStatus" }', '{ "available", status.available }', '{ "reason", status.reason }', '{ "matchmaking", status.matchmaking }'):
            assert field in status
        _in_order(attach, "g_ui->SetSteamCallbacks(&kCallbacks);", "PushStatus();")
        for name in ("void OnHostStart(", "void OnJoin("):
            body = _body(SESSION, name)
            _in_order(body, "F4T::Steam::RetryIfUnavailable()", "PushStatus();")


    def test_ui_replays_the_last_steam_status_when_the_browser_opens():
        push_event = _body(ACTIONS_SOURCE, "void PushEvent(")
        push_capabilities = _body(ACTIONS_SOURCE, "void PushCapabilities()")
        emit = _body(ACTIONS_SOURCE, "void EmitCapabilities()")

        _in_order(push_event, 'type == "steamStatus"', "g_lastStatus = event;", "DispatchEvent(event);")
        _in_order(push_capabilities, "EmitCapabilities();", "DispatchEvent(g_lastStatus)")
        assert '{ "steamP2p", g_callbacks.HostStart != nullptr }' in emit and "g_lastStatus" not in emit


    def test_friend_hosts_are_listed_only_from_a_worker_thread():
        list_hosts = _body(SESSION, "void OnListHosts()")
        push_hosts = _body(SESSION, "void PushFriendHosts(")

        assert len(re.findall(r"ListHostingFriends\(", SESSION)) == 1
        _in_order(
            list_hosts,
            "g_listHostsRunning.exchange(true)",
            "std::thread([]() {",
            "F4T::Steam::Initialize();",
            "F4T::Steam::RetryIfUnavailable()",
            "PushStatus();",
            "F4T::Steam::Status().available",
            "ListHostingFriends()",
            "PushFriendHosts(friends);",
            "PushFriendHosts(F4T::Steam::HostingFriends{});",
            ".detach();",
        )
        assert "g_listHostsRunning.store(false);" in list_hosts
        for field in ("friendCode", "name", "host", "players", "maxPlayers", "build", "compatible", "reason"):
            assert f'{{ "{field}",' in push_hosts
        assert '{ "type", "steamFriendHosts" }' in push_hosts
        assert "std::to_string(host.friendCode)" in push_hosts
        _in_order(push_hosts, "hosts.push_back(", ".dump(", "> kMaxFriendHostsBytes", "hosts.erase(hosts.size() - 1);", "break;")
        assert re.search(r"kMaxFriendHostsBytes = 28 \* 1024;", SESSION)
        for source in PLUGIN.joinpath("src").glob("*.cpp"):
            if source.name in ("F4TSteamSession.cpp", "F4TSteamTunnelService.cpp"):
                continue
            assert "ListHostingFriends" not in _read(source), f"{source.name} must not list hosts on its own thread."


    def test_list_hosts_action_is_routed_through_the_pinned_join_callback():
        route = _body(ACTIONS_HEADER, "inline bool RouteSteamAction(")
        session_token = re.search(r'kListHostsToken\{ "(.*?)" \}', SESSION)
        ui_token = re.search(r'kListHostsJoinToken = "(.*?)";', ACTIONS_HEADER)
        callbacks = re.search(r"struct SteamCallbacks\n\t\{\n(.*?)\n\t\};", API_HEADER, re.S).group(1)

        assert '"steamListHosts"' in _body(ACTIONS_HEADER, "inline bool IsSteamAction(")
        assert 'a_callbacks.Join(kListHostsJoinToken, "");' in route
        assert route.index('action == "steamListHosts"') < route.index("ParseJoin(a_action, request, error)")
        assert session_token and ui_token and session_token.group(1) == ui_token.group(1)
        assert session_token.group(1).startswith(chr(92) + "x01")
        assert callbacks.count("(*") == 5, "SteamCallbacks is ABI-pinned; the list request must not add a function pointer."
        on_join = _body(SESSION, "void OnJoin(")
        _in_order(on_join, "kListHostsToken == a_friendCode", "OnListHosts();", "RunAsync(")


    def test_second_invite_is_ignored_while_joining_or_connected():
        guard = _body(SESSION, "void OnRichPresenceJoin(")

        _in_order(
            guard,
            "g_gate.Pending() != SteamJoinGate::Operation::None",
            "join request ignored: a Steam join or host start is already in progress",
            "F4T::Networking::IsConnectedToServer()",
            "join request ignored: already connected to a server",
            "OnJoin(a_connect",
        )
        assert '#include "F4TNetworking.h"' in SESSION


    def test_guest_presence_is_set_after_the_link_connects_and_cleared_on_every_teardown():
        on_join = _body(SESSION, "void OnJoin(")

        _in_order(
            on_join,
            "WaitForGuestConnected(",
            "F4T::Steam::SetGuestPresence(host.name, F4T::Steam::FriendCode(host.steamId));",
            "BeginLocalJoin(",
            "F4T::Steam::ClearGuestPresence();",
        )
        for name in ("void OnGuestJoinComplete(", "void StopTunnels()", "void CancelJoins()"):
            assert "F4T::Steam::ClearGuestPresence();" in _body(SESSION, name), f"{name} must clear guest presence."
        assert SESSION.count("SetGuestPresence(") == 1


    def test_version_and_runtime_rejections_have_friendly_text():
        presentation = _body(ALERT, "Presentation BuildPresentation(")

        for code, title in (("client_version_mismatch", "Client Version Mismatch"), ("runtime_mismatch", "Fallout 4 Version Mismatch")):
            branch = presentation[presentation.index(f'a_code == "{code}"'):]
            branch = branch[:branch.index("} else if")]
            assert f'result.title = "{title}";' in branch
            assert "This server requires a different" in branch
            assert "Host is running" not in branch


    def test_every_steam_join_checks_the_host_build_before_connecting():
        join = _body(SESSION, "void OnJoin(")

        _in_order(
            join,
            "RunAsync(",
            "F4T::Steam::FindHost(friendCode)",
            "F4T::Steam::CheckHostBuild(host.steamId)",
            "F4T::Steam::ConnectGuest(host.steamId)",
        )
        run_async = join.index("RunAsync(")
        assert join.index("PushStatus();") > run_async, "the status push must run on the join worker."
        assert join.index("SetGuestPresence(") > run_async, "guest presence is set from the join worker."
        assert 'PushFailed("steamJoinFailed", *buildRefusal);' in join
        assert SESSION.count("CheckHostBuild(") == 1


    def test_join_failures_pushed_while_the_browser_is_hidden_are_replayed():
        push = _body(ACTIONS_SOURCE, "void PushEvent(")
        capabilities = _body(ACTIONS_SOURCE, "void PushCapabilities()")
        action = _body(ACTIONS_SOURCE, "bool HandleAction(")

        assert "bool CanDispatchToBrowser();" in BROWSER_INTERNAL
        _in_order(push, 'type == "steamJoinFailed"', "CanDispatchToBrowser()", "g_pendingJoinFailure = event;", "return;", "DispatchEvent(event);")
        _in_order(
            capabilities,
            "g_lastStatus",
            "g_pendingJoinFailure.is_object()",
            "kPendingJoinFailureMaxAge",
            "CanDispatchToBrowser()",
            "DispatchEvent(g_pendingJoinFailure);",
            "g_pendingJoinFailure = nlohmann::json();",
        )
        assert "g_pendingJoinFailureAt = std::chrono::steady_clock::now();" in push
        assert 'action == "steamJoin" || action == "steamHostStart"' in action
        assert "g_pendingJoinFailure = nlohmann::json();" in action


    def test_ui_client_stays_under_the_line_cap():
        assert len(UI_CLIENT.splitlines()) < 1000


    def main():
        tests = [v for k, v in sorted(_merge_ns.items()) if k.startswith("test_") and callable(v)]
        for test in tests:
            test()
        print(f"{Path(__file__).stem}: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_004_test_steam_pause_menu_invite_contract():
    """Contract moved verbatim from tests/audit/test_steam_pause_menu_invite_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_steam_pause_menu_invite_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"
    PAUSE = UI / "ui" / "views" / "CommonwealthOnline" / "pause-menu"


    def _read(path):
        return path.read_text(encoding="utf-8").replace("\r\n", "\n")


    HTML = _read(PAUSE / "index.html")
    SCRIPT = _read(PAUSE / "script.js")
    STYLES = _read(PAUSE / "styles.css")
    ACTIONS = _read(UI / "src" / "F4TSteamActions.cpp")
    ACTIONS_HEADER = _read(UI / "include" / "F4TSteamActions.h")
    MENU = _read(UI / "src" / "F4TPrismaUI_Menu.cpp")
    SESSION = _read(ROOT / "CO-BETA" / "plugin" / "src" / "F4TSteamSession.cpp")


    def _body(source, signature):
        start = source.index(signature)
        open_brace = source.index("{", start)
        depth = 0
        for index in range(open_brace, len(source)):
            depth += {"{": 1, "}": -1}.get(source[index], 0)
            if depth == 0:
                return source[open_brace:index + 1]
        raise AssertionError(f"unterminated body for {signature}")


    def test_pause_menu_has_a_hidden_invite_item_and_notice():
        assert 'data-action="invite" hidden' in HTML
        assert "INVITE FRIENDS" in HTML
        assert 'id="inviteNotice"' in HTML
        assert ".invite-notice[hidden]" in STYLES


    def test_invite_item_sends_the_steam_invite_action():
        assert 'action === "invite"' in SCRIPT
        assert 'sendCoAction({ action: "steamInvite" })' in SCRIPT
        assert "window.coSetInviteAvailable = function (on)" in SCRIPT
        assert "window.coShowInviteNotice = function (text)" in SCRIPT


    def test_menu_routes_the_invite_action_to_the_steam_bridge():
        assert 'actionName == "steamInvite"' in MENU
        assert "F4T::Steam::InviteFromPauseMenu();" in MENU
        assert '#include "F4TSteamActions.h"' in MENU
        assert "F4T::Steam::SyncPauseMenuInvite();" in _body(MENU, "void ReconcilePauseMenuAfterDomReady()")
        assert "void InviteFromPauseMenu();" in ACTIONS_HEADER and "void SyncPauseMenuInvite();" in ACTIONS_HEADER


    def test_hosting_state_shows_and_hides_the_item():
        push = _body(ACTIONS, "void PushEvent(")
        assert 'type == "steamHostStarted" || type == "steamHostStopped"' in push
        assert 'g_hosting = type == "steamHostStarted";' in push
        assert "SyncPauseMenuInvite();" in push
        sync = _body(ACTIONS, "void SyncPauseMenuInvite()")
        assert "coSetInviteAvailable" in sync and "g_hosting" in sync


    def test_invite_and_join_refusals_reach_the_pause_menu_notice():
        push = _body(ACTIONS, "void PushEvent(")
        assert 'type == "steamInviteFailed" || type == "steamJoinFailed"' in push
        assert "ShowPauseInviteNotice(" in push
        route = _body(ACTIONS, "void InviteFromPauseMenu()")
        assert '{ "action", "steamInvite" }' in route and "&PauseInviteSink" in route


    def test_ignored_invites_tell_the_player_why():
        guard = _body(SESSION, "void OnRichPresenceJoin(")
        assert guard.count("F4T::Steam::ReportJoinFailure(") == 2
        assert guard.index("mine == a_connect") < guard.index("g_gate.Pending()")
        assert 'window.coShowInviteNotice("");' in SCRIPT


    def test_invites_wait_until_the_host_has_left_vault_111():
        join_flow = _read(ROOT / "CO-BETA" / "plugin" / "src" / "F4TJoinFlow.cpp")
        flow_header = _read(ROOT / "CO-BETA" / "plugin" / "include" / "F4TJoinFlow.h")
        assert "bool HasLeftVault();" in flow_header
        assert "Mq102Stage() >= kMq102ControlsRestoredStage" in _body(join_flow, "bool HasLeftVault()")
        assert "kMq102ControlsRestoredStage = 10;" in join_flow
        invite = _body(SESSION, "void OnInvite()")
        gate = invite.index("F4T::JoinFlow::HasLeftVault()")
        assert gate < invite.index("OpenFriendInvite()")
        assert 'PushFailed("steamInviteFailed"' in invite[gate:invite.index("OpenFriendInvite()")]
        assert SESSION.count("OpenFriendInvite()") == 1
        assert '#include "F4TJoinFlow.h"' in SESSION


    def test_overlay_invites_are_held_until_the_host_has_left_vault_111():
        src = ROOT / "CO-BETA" / "plugin" / "src"
        hold = _read(src / "F4TSteamJoinHold.cpp")
        header = _read(ROOT / "CO-BETA" / "plugin" / "include" / "F4TSteamJoinHold.h")
        launch = _read(src / "F4TSessionLaunch.cpp")
        inbound = _read(src / "F4TNetworkingInbound.cpp")
        assert "void SetJoinHeld(bool a_held);" in header
        assert "void HoldJoinForNewHost();" in header
        assert "g_pending = false;" in _body(hold, "void HoldJoinForNewHost()")
        assert "void AnnounceHostWhenOpen(std::string_view a_name, std::uint64_t a_friendCode);" in header
        assert "F4T::Steam::SetJoinHeld(!F4T::JoinFlow::HasLeftVault());" in _body(launch, "void Update()")
        announce = _body(hold, "void AnnounceHostWhenOpen(")
        assert announce.index("if (g_held)") < announce.index("AnnounceHosting(a_name)")
        assert announce.index("return;") < announce.index("AnnounceHosting(a_name)")
        release = _body(hold, "void SetJoinHeld(bool a_held)")
        assert "HostRunning()" in release and "AnnounceHosting(name)" in release and "RememberInviteCode(code)" in release
        assert "F4T::Steam::AnnounceHosting(" not in inbound and "AnnounceHostWhenOpen(*name, 0)" in inbound
        assert "F4T::Steam::AnnounceHosting(" not in SESSION and "RememberInviteCode(friendCode)" not in SESSION
        assert "F4T::Steam::AnnounceHostWhenOpen(name, friendCode);" in SESSION
        assert SESSION.index("F4T::Steam::HoldJoinForNewHost();") < SESSION.index("F4T::Steam::StartHost(kLocalServerPort")
        assert '#include "F4TSteamJoinHold.h"' in SESSION


    def main():
        tests = [v for k, v in sorted(_merge_ns.items()) if k.startswith("test_") and callable(v)]
        for test in tests:
            test()
        print(f"{Path(__file__).stem}: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_005_test_join_refusal_reason_contract():
    """Contract moved verbatim from tests/audit/test_join_refusal_reason_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_join_refusal_reason_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    GNS = ROOT / "CO-BETA/plugin/src/F4TNetworkingGns.cpp"
    UICLIENT = ROOT / "CO-BETA/plugin/src/F4TUIClient.cpp"
    ALERT = ROOT / "CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp"


    def test_server_refusal_survives_the_transport_disconnect_generation_bump():
        gns = GNS.read_text(encoding="utf-8")
        start = gns.index("if (failure->rejection) {")
        block = gns[start:gns.index("a_error = FormatValidationFailure(*failure);", start)]
        assert "const std::function<bool()> userCancelled = [&] {" in block
        assert "(!a_joinFlow && a_cancelRequested && a_cancelRequested())" in block
        assert "IsVoluntaryExitRequested()" in block
        assert block.count("userCancelled()") == 2
        assert "MakeSessionRejectionFailure(rejection, serverKey, userCancelled, maxDuration)" in block


    def test_join_worker_shows_the_refusal_instead_of_connection_cancelled():
        ui = UICLIENT.read_text(encoding="utf-8").replace(chr(13) + chr(10), chr(10))
        refused = ui.index("const bool serverRefused = !connected && connectionFailure")
        gated = ui.index("if (!serverRefused) {", refused)
        cancelled = ui.index(
            "if (g_joinGeneration.load() != joinGeneration) { finishJoin(false, connected ? \"Connection cancelled.\" : connectionError, true); return; }",
            gated,
        )
        assert refused < gated < cancelled
        assert "ConnectFailureKind::SessionRejected && !g_voluntaryExitRequested.load()" in ui[refused:gated]
        assert ui.index("ShowHandshakeRejectionNotification(", cancelled) > cancelled


    def test_modpack_and_ban_cards_use_plain_english_and_keep_the_detail():
        alert = ALERT.read_text(encoding="utf-8")
        assert "Your mods do not match this server. Contact the server admin to find out which mods you need." in alert
        assert "You are banned from this server. Contact the server admin if you think this is a mistake." in alert
        assert "Your online profile is banned from this server. Contact the server admin if you think this is a mistake." in alert
        assert alert.count("Contact the server admin if you think this is a mistake.") == 2
        assert r'} else if (!reason.empty() && code == "modpack_mismatch") {' in alert
        assert r'std::format("\nWhat is different:\n{}", reason);' in alert
        assert r'} else if (!reason.empty()) {' in alert
        assert r'std::format("\nReason: {}", reason);' in alert
        assert 'code != "modpack_mismatch" && code != "banned"' not in alert
        assert '"modpack", "Get Modpack", nullptr' in alert


    def test_modpack_detail_drops_the_server_preamble_and_gets_the_larger_cap():
        alert = ALERT.read_text(encoding="utf-8")
        assert "constexpr std::size_t kMaximumRejectionReasonBytes = 256;" in alert
        assert "constexpr std::size_t kMaximumModpackReasonBytes = 1024;" in alert
        assert "Your modpack does not match this server. The server manifest is in requiredModpack." in alert
        assert "a_detail.find(F4T::Identity::kModpackPageMarker)" in alert
        assert "if (first == std::string::npos) {" in alert
        assert r'constexpr std::string_view kWhitespace = " \n\r\t";' in alert
        assert 'constexpr std::string_view kMismatchFallback = "Your modpack does not match this server.";' in alert
        assert "return text == kMismatchFallback ? std::string{} : text;" in alert
        assert "find_first_not_of(kWhitespace, preamble + kServerPreamble.size())" in alert
        assert alert.index("a_detail.find(F4T::Identity::kModpackPageMarker)") < alert.index("a_detail.find(kServerPreamble)")
        assert "\n\t\tconstexpr std::size_t kMaximumModpackReasonBytes = 1024;" in alert
        strip = alert.index("reason = StripModpackDetailNoise(reason);")
        truncate = alert.index(
            "reason = F4T::Utf8::TruncateToBytes(reason, code == \"modpack_mismatch\" ? kMaximumModpackReasonBytes : kMaximumRejectionReasonBytes);"
        )
        assert strip < truncate
        assert alert.index("else if (!reason.empty() && code == \"modpack_mismatch\") {") > truncate
        failure_probe = (ROOT / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
        assert "constexpr std::size_t kMaximumFailureReasonBytes = 256;" in failure_probe


    def test_transport_loss_during_a_handshake_does_not_cancel_the_next_join():
        ui = UICLIENT.read_text(encoding="utf-8").replace(chr(13) + chr(10), chr(10))
        start = ui.index("void NotifyTransportDisconnected(const char* a_reason)")
        body = ui[start:ui.index("\n\t}\n", start)]
        assert "const bool bumpGeneration = g_connectionOwnerToken.load() != 0 || !g_joinWorkerRunning.load(); g_connectionOwnerToken.store(0);" in body
        assert "taskInterface->AddTask([reasonCopy, bumpGeneration]() {" in body
        assert "if (bumpGeneration) { g_joinGeneration.fetch_add(1); }" in body


    if True:
        test_server_refusal_survives_the_transport_disconnect_generation_bump()
        test_join_worker_shows_the_refusal_instead_of_connection_cancelled()
        test_modpack_and_ban_cards_use_plain_english_and_keep_the_detail()
        test_modpack_detail_drops_the_server_preamble_and_gets_the_larger_cap()
        test_transport_loss_during_a_handshake_does_not_cancel_the_next_join()
        print("join refusal reason contract passed")

def _section_006_test_join_worker_ownership():
    """Contract moved verbatim from tests/audit/test_join_worker_ownership.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_join_worker_ownership.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    UI_SOURCE = ROOT / "CO-BETA" / "plugin" / "src" / "F4TUIClient.cpp"

    def test_join_worker_is_owned_and_never_detached():
        source = UI_SOURCE.read_text(encoding="utf-8")

        assert "g_joinWorker->detach()" not in source, (
            "the join worker must stay owned by g_joinWorker so a new join can refuse while it runs "
            "and reap it once finished; a detached thread has no owner and can outlive the join state "
            "it reports into."
        )

        join_start = source.index("bool Callback_JoinSession(")
        join_end = source.index("\n\t\tvoid Callback_Disconnect()", join_start)
        join = source[join_start:join_end]

        assert "if (g_joinWorkerRunning.load() || g_joinInProgress.exchange(true)) {" in join, (
            "a join must be refused while the previous worker still runs, even after a cancel cleared "
            "g_joinInProgress, so two connect attempts never overlap."
        )
        assert join.index("g_joinWorker->join();") < join.index("g_joinWorker = new std::thread("), (
            "the finished previous worker must be joined before the owned pointer is replaced."
        )
        assert join.index("taskInterface->AddTask([") < join.index("g_joinWorkerRunning.store(false);"), (
            "the worker must clear its running flag only after it has posted its completion task."
        )
        assert "dropped stale join completion" in source, (
            "a stale deferred completion must be logged when it is dropped."
        )


    def test_stale_attempt_disconnects_only_its_own_connection():
        source = UI_SOURCE.read_text(encoding="utf-8")

        worker_start = source.index("auto worker = [host,")
        stale_start = source.index("if (g_joinGeneration.load() != joinGeneration) {")
        worker = source[worker_start:stale_start]
        assert worker.index("if (connected) {") < worker.index("g_connectionOwnerToken.store(attemptToken);") < worker.index("taskInterface->AddTask("), (
            "the worker must publish the owner token right after a successful connect and before it queues "
            "the completion, so a superseded completion finds its own connection already owned."
        )

        stale_end = source.index("\n\t\t\t\t\tif (!connected) {", stale_start)
        stale = source[stale_start:stale_end]
        assert 'finishJoin(false, connected ? "Connection cancelled." : connectionError, true);' in stale
        cleanup = source[source.index("auto finishJoin ="):stale_start]
        assert "DisconnectFromLocalServerIfGeneration(ownedGeneration" in cleanup
        assert "g_connectionOwnerToken.load() == attemptToken && !F4T::SessionSaveGuard::IsSessionExitPending()" in cleanup
        assert "g_connectionOwnerToken.compare_exchange_strong(owner, 0);" in cleanup



    def test_join_worker_creation_failure_releases_join_state():
        source = UI_SOURCE.read_text(encoding="utf-8")

        join_start = source.index("bool Callback_JoinSession(")
        join_end = source.index("\n\t\tvoid Callback_Disconnect()", join_start)
        join = source[join_start:join_end]

        assert "} catch (const std::exception& a_error) {" in join, (
            "a failed std::thread construction must be caught so the join reports an error instead of "
            "leaving g_joinInProgress and g_joinWorkerRunning set forever."
        )
        catch_start = join.index("} catch (const std::exception& a_error) {")
        catch_block = join[catch_start:join.index("return false;", catch_start)]
        assert join.index("try {") < join.index("g_joinWorker = new std::thread(") < catch_start
        assert join.index("delete g_joinWorker;") < join.index("g_joinWorker = nullptr;") < join.index("g_joinWorker = new std::thread("), (
            "a reaped worker must be cleared before the new thread is created, or a failed creation leaves a dangling pointer."
        )
        assert "g_joinWorkerRunning.store(false);" in catch_block
        assert "g_joinInProgress.store(false);" in catch_block
        assert "CopyError(a_errorBuf, a_errorBufSize," in catch_block
        assert "delete" not in catch_block, (
            "a failed construction leaves g_joinWorker null, so the catch must not delete anything."
        )


    _TEST_PREFIX = "test_"


    def main() -> int:
        failures: list[str] = []
        tests = [
            (name, value)
            for name, value in sorted(_merge_ns.items())
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

        print(f"join worker ownership contracts passed ({len(tests)} checks)")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())


_SECTIONS = [
    ("tests/audit/test_steam_guest_profile_key_contract.py", _section_001_test_steam_guest_profile_key_contract),
    ("tests/audit/test_steam_host_lobby_contract.py", _section_002_test_steam_host_lobby_contract),
    ("tests/audit/test_steam_join_gaps_contract.py", _section_003_test_steam_join_gaps_contract),
    ("tests/audit/test_steam_pause_menu_invite_contract.py", _section_004_test_steam_pause_menu_invite_contract),
    ("tests/audit/test_join_refusal_reason_contract.py", _section_005_test_join_refusal_reason_contract),
    ("tests/audit/test_join_worker_ownership.py", _section_006_test_join_worker_ownership),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_steam_join_contracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
