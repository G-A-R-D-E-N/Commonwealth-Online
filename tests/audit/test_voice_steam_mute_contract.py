from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "CO-SERVER" / "server-cpp"
SRC = SERVER / "src"
MAX_INC_LINES = 750


def _read(path):
    return path.read_text(encoding="utf-8")


def _slice(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def test_session_and_peer_carry_verified_steam_id():
    session = _read(SRC / "ServerSessionTypes.h")
    voice_header = _read(SRC / "ServerVoice.h")
    assert "std::string steamId;" in session
    assert "std::string_view steamId{};" in voice_header


def test_runtime_hello_binds_steam_id_on_the_same_line_count():
    runtime = _read(SRC / "ServerRuntime.inc")
    assert "protocol.BindSessionProfileVerified(event.connection_id, decision.profileId, decision.isAdmin, decision.steamId);" in runtime
    assert len(runtime.splitlines()) <= MAX_INC_LINES, "ServerRuntime.inc must not grow past the 750-line cap"


def test_web_api_verified_steam_id_binds_the_protocol_session():
    maintenance = _read(SRC / "ServerMaintenance.inc")
    assert "protocol.BindSessionSteamId(verdict.connectionId, verdict.steamId);" in maintenance


def test_voice_peers_carry_steam_id_and_admin_mute_keys_on_it():
    host = _read(SRC / "ServerVoiceHost.cpp")
    assert "&session.voice, session.profileId, session.steamId });" in host
    mute = _slice(host, "std::optional<VoiceAdminOutcome> VoiceMuteByAdmin(", "AdminActionResult VoiceAdminAction(")
    assert "steamId = found->second.steamId;" in mute
    assert "const std::string_view muteKey = steamId.empty() ? std::string_view{ profileId } : std::string_view{ steamId };" in mute
    assert "SetServerMute(VoicePeers(a_sessions), muteKey, profileId, a_muted, a_seconds, a_monotonicSeconds)" in mute


def test_relay_checks_both_steam_and_profile_keys():
    voice = _read(SRC / "ServerVoice.cpp")
    assert "IsServerMuted(sender->steamId, sender->profileId, a_monotonicSeconds)" in voice
    is_muted = _slice(voice, "bool VoiceService::IsServerMuted(", "std::vector<OutboundFrame> VoiceService::Poll(")
    assert "IsMuteActive(a_steamId, a_monotonicSeconds) || IsMuteActive(a_profileId, a_monotonicSeconds)" in is_muted


def test_set_server_mute_notifies_by_either_key():
    voice = _read(SRC / "ServerVoice.cpp")
    set_mute = _slice(voice, "std::vector<OutboundFrame> VoiceService::SetServerMute(", "bool VoiceService::IsServerMuted(")
    assert "if (a_muteKey.empty()) {" in set_mute
    assert "(entry.steamId != a_muteKey && entry.profileId != a_muteKey)" in set_mute


def main():
    test_session_and_peer_carry_verified_steam_id()
    test_runtime_hello_binds_steam_id_on_the_same_line_count()
    test_web_api_verified_steam_id_binds_the_protocol_session()
    test_voice_peers_carry_steam_id_and_admin_mute_keys_on_it()
    test_relay_checks_both_steam_and_profile_keys()
    test_set_server_mute_notifies_by_either_key()
    print("Voice steam mute contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
