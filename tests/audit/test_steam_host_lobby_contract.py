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
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]
    for test in tests:
        test()
    print(f"{Path(__file__).stem}: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
