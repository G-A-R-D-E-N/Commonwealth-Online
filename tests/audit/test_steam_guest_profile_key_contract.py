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
    assert "ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, cachedFingerprint, a_joinId);" in NETWORKING_GNS


def test_join_worker_passes_join_id_and_rejection_uses_profile_key():
    assert "host.c_str(), port, connectionError, &connectionFailure, serverPassword, id);" in UI_CLIENT
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


if __name__ == "__main__":
    raise SystemExit(main())
