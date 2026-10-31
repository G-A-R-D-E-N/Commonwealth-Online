from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

POLICY_HEADER = (ROOT / "co-core" / "include" / "F4TConnectionPolicy.h").read_text(encoding="utf-8")
POLICY_SOURCE = (ROOT / "co-core" / "src" / "session" / "F4TConnectionPolicy.cpp").read_text(encoding="utf-8")
MAINTENANCE = (ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerMaintenance.inc").read_text(encoding="utf-8")
RUNTIME = (ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerRuntime.inc").read_text(encoding="utf-8")
BRIDGE_HEADER = (ROOT / "CO-SERVER" / "server" / "native_transport" / "co_gns_server_bridge.h").read_text(encoding="utf-8")
NETWORKING = (ROOT / "CO-BETA" / "plugin" / "src" / "F4TNetworking.cpp").read_text(encoding="utf-8")
SHIM = (ROOT / "CO-BETA" / "plugin" / "src" / "F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")
REJECTION_ALERT = (ROOT / "CO-BETA" / "plugin" / "src" / "F4THandshakeRejectionAlert.cpp").read_text(encoding="utf-8")
SESSION_ENDED = (ROOT / "CO-BETA" / "plugin" / "src" / "F4TSessionEndedScreen.cpp").read_text(encoding="utf-8")


def test_the_sixty_second_idle_kick_is_gone():
    assert "DefaultIdleTimeoutSeconds" not in POLICY_HEADER, (
        "a 60 s idle kick drops a player who is loading or simply reading the screen"
    )
    assert "IdleExpired" not in POLICY_HEADER
    assert "IdleExpired" not in POLICY_SOURCE
    assert "IdleExpired" not in MAINTENANCE


def test_the_only_idle_kick_is_the_fifteen_minute_afk_kick():
    assert "DefaultAfkKickSeconds = 900.0" in POLICY_HEADER
    assert "AfkExpired(" in MAINTENANCE
    assert "state.lastActivityAtUnixSeconds" in MAINTENANCE


def test_the_afk_kick_tells_the_player_before_the_socket_closes():
    kick = MAINTENANCE.index('"afk_kick"')
    ended = MAINTENANCE.index("disconnectTracked(connectionId, reason)")
    assert kick < ended, (
        "the sessionEnded frame must be queued on the reliable stream before the connection "
        "is closed, so the player is told why instead of seeing a generic connection lost"
    )
    assert "You were kicked for being AFK." in MAINTENANCE


def test_the_afk_kick_applies_on_every_server_without_a_setting():
    assert "DefaultAfkKickSeconds = 900.0" in POLICY_HEADER
    settings = (ROOT / "co-core" / "include" / "F4TServerConfiguration.h").read_text(encoding="utf-8")
    assert "afkKick" not in settings
    assert "idleTimeout" not in settings


def test_activity_comes_from_player_packets_not_client_timers():
    activity = (ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerActivity.h").read_text(encoding="utf-8")
    for timer_type in ('"keepAlive"', '"pluginList"', '"worldState"', '"npcState"'):
        assert timer_type in activity, timer_type
    assert 'a_type == "playerState"' in activity
    assert "actionEvents" in activity
    assert "MovementChanged" in activity
    assert "ProtocolHost::Impl::TakePlayerActivity" in RUNTIME or "TakePlayerActivity" in RUNTIME


def test_an_exempt_keepalive_restarts_the_clock():
    assert 'packet["afkExempt"] = true;' in NETWORKING, (
        "a client that is joining, loading or in the character creator must say so on keepAlive"
    )
    assert "SessionAfkExempt(connectionId)" in MAINTENANCE
    assert 'session.afkExempt = packet->value("afkExempt", false);' in (
        ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerProtocolImpl.inc"
    ).read_text(encoding="utf-8"), "a client that omits afkExempt must be treated as not exempt"


def test_loading_and_the_character_creator_are_exempt():
    assert "F4T::SessionWorldState::IsLoadingScreenActive()" in NETWORKING
    assert 'F4T::SessionWorldState::IsMenuOpen("LooksMenu")' in NETWORKING, (
        "the character creator must keep the clock stopped, and LooksMenu has no MENU_NAME so it "
        "has to go through the name-based menu check the repo already uses for SPECIALMenu"
    )


def test_the_afk_message_replaces_the_generic_connection_lost_text():
    assert '"afk_kick"' in REJECTION_ALERT
    assert "Kicked for being AFK" in REJECTION_ALERT
    assert "Kicked for being AFK" in SESSION_ENDED
    assert "afk_kick" in SESSION_ENDED


def test_every_disconnect_is_logged_once_from_the_server_side():
    assert "LogDisconnect(" in RUNTIME
    support = (ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerRuntimeSupport.cpp").read_text(encoding="utf-8")
    assert "void LogDisconnect(" in support
    for field in ("sessionSeconds=", "secondsSinceLastPacket=", "ended by ", "profile=", "remote="):
        assert field in support, field


def test_refusals_are_logged():
    assert "LogRefusal(" in RUNTIME
    support = (ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerRuntimeSupport.cpp").read_text(encoding="utf-8")
    assert "connect_rate_limit" in support
    assert "pending_limit" in support


def test_a_periodic_status_line_uses_the_bridge_status_call():
    assert "co_gns_server_connection_status" in BRIDGE_HEADER
    assert "LogConnectionStatus(server, connectionId, state)" in MAINTENANCE
    assert "DefaultStatusReportSeconds = 60.0" in POLICY_HEADER


def test_the_main_loop_reports_long_iterations():
    assert "LogLoopIteration();" in RUNTIME
    assert "DefaultMainLoopStallSeconds = 0.25" in POLICY_HEADER


def test_internet_players_get_longer_connect_and_handshake_limits():
    assert "DefaultInternetConnectSeconds = 10.0" in POLICY_HEADER
    assert "DefaultInternetWelcomeWaitSeconds = 10.0" in POLICY_HEADER
    assert "LimitsForHost(state->host).connectSeconds" in SHIM
    assert "LimitsForHost(host).welcomeWaitSeconds" in NETWORKING
    assert "DefaultLoopbackConnectSeconds = 3.0" in POLICY_HEADER
    assert "DefaultWelcomeWaitSeconds = 3.0" in POLICY_HEADER


def main() -> int:
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"afk kick contract: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
