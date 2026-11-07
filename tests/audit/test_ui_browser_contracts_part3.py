#!/usr/bin/env python3
"""test_ui_browser_contracts_part3: consolidated contract sections.

Consolidated from 4 single-contract scripts so the group runs one process
instead of 4. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_ui_browser_contracts_part3.py
"""
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




def _section_014_test_resource_view_bridge_contract():
    """Contract moved verbatim from tests/audit/test_resource_view_bridge_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_resource_view_bridge_contract.py")
    import json
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    MAX_LINES = 1000

    CORE_HEADER = "co-core/include/F4TStateBagProtocol.h"
    CORE_SOURCE = "co-core/src/protocol/F4TStateBagProtocol.cpp"
    SERVER_BRIDGE_HEADER = "CO-SERVER/server-cpp/src/ServerResourceBridge.h"
    SERVER_BRIDGE_SOURCE = "CO-SERVER/server-cpp/src/ServerResourceBridge.cpp"
    SERVER_IMPL_INC = "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc"
    SERVER_API_INC = "CO-SERVER/server-cpp/src/ServerProtocolApi.inc"
    UI_API_HEADER = "CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h"
    PLUGIN_API_HEADER = "CO-BETA/plugin/include/CommonwealthOnlineUI_API.h"
    UI_PLUGIN_API = "CommonwealthOnlineUI/src/PluginAPI.cpp"
    UI_HOST_HEADER = "CommonwealthOnlineUI/src/F4TResourceViewHost.h"
    UI_HOST_SOURCE = "CommonwealthOnlineUI/src/F4TResourceViewHost.cpp"
    UI_BRIDGE_JS = "CommonwealthOnlineUI/ui/views/co-resources/co-bridge.js"
    PLUGIN_CLIENT_IDENTITY = "CO-BETA/plugin/src/F4TClientIdentity.cpp"
    PLUGIN_SYNC_HEADER = "CO-BETA/plugin/src/F4TStateBagSync.h"
    PLUGIN_SYNC_SOURCE = "CO-BETA/plugin/src/F4TStateBagSync.cpp"
    PLUGIN_INBOUND = "CO-BETA/plugin/src/F4TNetworkingInbound.cpp"
    PLUGIN_NETWORKING = "CO-BETA/plugin/src/F4TNetworking.cpp"
    PLUGIN_MAIN = "CO-BETA/plugin/src/main.cpp"
    PLUGIN_LAYOUT_TESTS = "CO-BETA/plugin/tests/F4TUIApiLayoutTests.cpp"
    EXAMPLE_DIR = "CO-SERVER/server-cpp/resources-example/state-board"
    EXAMPLE_MANIFEST = EXAMPLE_DIR + "/co-resource.json"
    EXAMPLE_SERVER = EXAMPLE_DIR + "/index.js"
    TYPINGS = "scripting/co.d.ts"

    S5B_FILES = [
        CORE_HEADER,
        CORE_SOURCE,
        SERVER_BRIDGE_HEADER,
        SERVER_BRIDGE_SOURCE,
        SERVER_IMPL_INC,
        SERVER_API_INC,
        UI_API_HEADER,
        PLUGIN_API_HEADER,
        UI_PLUGIN_API,
        UI_HOST_HEADER,
        UI_HOST_SOURCE,
        UI_BRIDGE_JS,
        PLUGIN_CLIENT_IDENTITY,
        PLUGIN_SYNC_HEADER,
        PLUGIN_SYNC_SOURCE,
        PLUGIN_INBOUND,
        PLUGIN_NETWORKING,
        PLUGIN_MAIN,
        PLUGIN_LAYOUT_TESTS,
    ]


    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8").replace("\r\n", "\n")


    def block(text, marker):
        start = text.index(marker)
        end = text.index("};", start)
        return text[start:end + 2]


    def test_capability_is_advertised():
        assert '"state-bag-v1"' in read(PLUGIN_CLIENT_IDENTITY)


    def test_v16_block_matches_in_both_api_copies():
        ui = read(UI_API_HEADER)
        plugin = read(PLUGIN_API_HEADER)
        marker = "struct IVCommonwealthOnlineUI16"
        assert block(ui, marker) == block(plugin, marker)
        assert "using ResourceMessageFn" in ui
        assert ui.count("using ResourceMessageFn") == plugin.count("using ResourceMessageFn") == 1


    def test_name_and_path_validation_is_shared_and_used():
        core = read(CORE_SOURCE)
        for function in ("IsValidResourceName", "IsValidViewPath", "IsValidResourceData"):
            assert f"bool {function}(" in core
        assert "IsValidResourceName(" in read(SERVER_BRIDGE_SOURCE)
        assert "IsValidViewPath(" in read(SERVER_BRIDGE_SOURCE)
        assert "IsValidResourceData(" in read(SERVER_BRIDGE_SOURCE)
        host = read(UI_HOST_SOURCE)
        assert "IsValidResourceName(" in host
        assert "IsValidViewPath(" in host


    def test_resource_message_is_dispatched_by_server():
        assert 'type == "resourceMessage"' in read(SERVER_IMPL_INC)
        assert "resourceMessageHandler" in read(SERVER_IMPL_INC)


    def test_plugin_forwards_resource_packets_and_resets():
        sync = read(PLUGIN_SYNC_SOURCE)
        assert "kResourceViewPacketType" in sync
        assert "kResourceMessagePacketType" in sync
        assert "TryHandlePacket(*packetType, packet)" in read(PLUGIN_INBOUND)
        assert read(PLUGIN_NETWORKING).count("F4T::StateBagSync::Reset();") == 2
        assert "F4T::StateBagSync::Attach();" in read(PLUGIN_MAIN)


    def test_no_inc_files_in_s5b_diff():
        assert not [path for path in S5B_FILES if path.endswith(".inc") and path not in (SERVER_IMPL_INC, SERVER_API_INC)]


    def test_bridge_js_is_small_and_defines_listener_sugar():
        script = read(UI_BRIDGE_JS)
        assert script.count("\n") < 40
        assert "coState" in script
        assert "coResourceSend" in script


    def test_state_reaches_views_as_one_delta_per_changed_bag():
        host = read(UI_HOST_SOURCE)
        assert "kStateEvent" in host and "kBagEvent" in host
        assert "PushFullState" in host and "g_bags" in host
        assert "g_bindings" in host, "a view must be identified by a stable per-resource binding"
        assert "IsLive" not in host, "a raw ResourceView pointer must not decide whether a view is live"
        bridge = read(UI_BRIDGE_JS)
        assert "coBag" in bridge and "coState" in bridge, "the bridge must merge a bag delta into its state"


    def test_touched_files_stay_under_line_limit():
        for relative in S5B_FILES + [SERVER_BRIDGE_HEADER, PLUGIN_SYNC_HEADER, PLUGIN_SYNC_SOURCE, UI_HOST_HEADER, UI_HOST_SOURCE]:
            lines = read(relative).count("\n")
            assert lines < MAX_LINES, (relative, lines)


    def test_no_explanatory_comments_in_new_sources():
        for relative in (SERVER_BRIDGE_SOURCE, UI_HOST_SOURCE, PLUGIN_SYNC_SOURCE):
            assert not re.search(r"^\s*//", read(relative), re.MULTILINE), relative


    def test_example_view_is_shipped_and_wired():
        manifest = json.loads(read(EXAMPLE_MANIFEST))
        assert manifest["name"] == "state-board"
        assert manifest["views"] == ["board.html", "board.js", "board.css"]
        for view in manifest["views"]:
            assert (ROOT / EXAMPLE_DIR / view).is_file(), view
        server = read(EXAMPLE_SERVER)
        assert 'CO.views.open(playerId, "board.html")' in server
        assert "CO.views.onMessage(" in server
        assert 'CO.state.set("global"' in server
        view = read(EXAMPLE_DIR + "/board.js")
        assert "coResource.subscribe(" in view
        assert "coResource.send(" in view
        assert "window.coResource = {" in view
        page = read(EXAMPLE_DIR + "/board.html")
        assert "Content-Security-Policy" in page
        assert "default-src 'self'; connect-src 'none'" in page
        assert "../co-bridge.js" not in page


    def test_typings_describe_opening_views():
        typings = read(TYPINGS)
        assert "open(playerId: number, view: string): CoResult;" in typings
        assert "close(playerId: number): CoResult;" in typings

    _merge_ns = dict(locals())
    if True:
        for name, function in list(_merge_ns.items()):
            if name.startswith("test_") and callable(function):
                function()
        print("Resource view bridge contract passed")

def _section_015_test_server_double_click_pause_contract():
    """Contract moved verbatim from tests/audit/test_server_double_click_pause_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_server_double_click_pause_contract.py")
    from pathlib import Path
    import re

    ROOT = Path(__file__).resolve().parents[2]

    REQUIRED_CONDITIONS = (
        "GetConsoleProcessList(attachedProcesses, 2) == 1",
        "GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &consoleMode)",
        "GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR",
    )


    def main() -> int:
        failures: list[str] = []
        source = (ROOT / "CO-SERVER/server-cpp/src/ServerMain.cpp").read_text(encoding="utf-8")
        guard = re.search(r"if \(([^{]*?)\) \{\s*std::cout << \"Press Enter to close", source)
        if guard is None:
            failures.append("DOUBLE-CLICK-PAUSE: the Press Enter prompt must be guarded by its own if block")
        else:
            for condition in REQUIRED_CONDITIONS:
                if condition not in guard.group(1):
                    failures.append(f"DOUBLE-CLICK-PAUSE: the prompt guard must include: {condition}")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("Server double-click pause contract passed")
        return 0


    if True:
        _merge_exit(main())

def _section_017_test_voice_steam_mute_contract():
    """Contract moved verbatim from tests/audit/test_voice_steam_mute_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_voice_steam_mute_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    SERVER = ROOT / "CO-SERVER" / "server-cpp"
    SRC = SERVER / "src"
    MAX_INC_LINES = 1000


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
        runtime = _read(SRC / "ServerRuntime.inc").replace('#include "ServerRuntimeMessage.inc"', _read(SRC / "ServerRuntimeMessage.inc"))
        assert "protocol.BindSessionProfileVerified(event.connection_id, decision.profileId, hostSelf || decision.isAdmin || decision.secretVerified, decision.steamId);" in runtime
        assert len(runtime.splitlines()) <= MAX_INC_LINES, "ServerRuntime.inc must not grow past the 1000-line cap"


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


    if True:
        _merge_exit(main())

def _section_018_test_alttab_focus_deferral_contract():
    """Contract moved verbatim from tests/test_alttab_focus_deferral_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/test_alttab_focus_deferral_contract.py")
    from pathlib import Path

    UI = Path(__file__).resolve().parents[1] / "CommonwealthOnlineUI/src"
    SRC = (UI / "F4TPrismaViewState.cpp").read_text(encoding="utf-8")
    APPLY = SRC[SRC.index("Controller::ApplyState"):SRC.index("Snapshot Controller::GetSnapshot")]


    def test_focus_is_gated_on_foreground():
        assert "IsGameForeground()" in APPLY
        assert "needsFocus = wantsFocus && gameForeground" in APPLY


    def test_deferred_focus_is_remembered_for_foreground_return():
        assert "if (wantsFocus && !gameForeground) g_lastFocusedView = this;" in APPLY


    def test_focus_calls_only_inside_needs_focus_branch():
        for call in ("->Focus(", "->FocusOverlay("):
            assert APPLY.count(call) == 1
        assert APPLY.index("if (needsFocus)") < APPLY.index("->Focus(")


    def test_foreground_return_restores_focus():
        life = (UI / "F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")
        assert life.count("RestoreFocusAndSyncEscape()") >= 3
        assert "if (restored) SyncPauseMenuEscapeOwnership();" in life


    _merge_ns = dict(locals())
    if True:
        for name, fn in sorted(_merge_ns.items()):
            if name.startswith("test_") and callable(fn):
                fn()
        print("alt-tab focus deferral contract: PASS")

_SECTIONS = [
    ("tests/audit/test_resource_view_bridge_contract.py", _section_014_test_resource_view_bridge_contract),
    ("tests/audit/test_server_double_click_pause_contract.py", _section_015_test_server_double_click_pause_contract),
    ("tests/audit/test_voice_steam_mute_contract.py", _section_017_test_voice_steam_mute_contract),
    ("tests/test_alttab_focus_deferral_contract.py", _section_018_test_alttab_focus_deferral_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_ui_browser_contracts_part3: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
