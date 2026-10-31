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
    assert len(UI_CLIENT.splitlines()) < 750


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    for test in tests:
        test()
    print(f"{Path(__file__).stem}: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
