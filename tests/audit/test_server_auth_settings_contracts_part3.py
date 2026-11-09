#!/usr/bin/env python3
"""test_server_auth_settings_contracts_part3: consolidated contract sections.

Consolidated from 4 single-contract scripts so the group runs one process
instead of 4. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_server_auth_settings_contracts_part3.py
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




def _section_008_test_server_enforcement_contract():
    """Contract moved verbatim from tests/audit/test_server_enforcement_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_server_enforcement_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    ENFORCEMENT_HEADER = "CO-SERVER/server-cpp/include/ServerEnforcement.h"
    VIOLATION_HEADER = "CO-SERVER/server-cpp/include/ServerViolation.h"
    ENFORCEMENT_SOURCE = "CO-SERVER/server-cpp/src/ServerEnforcement.cpp"
    RUNTIME = "CO-SERVER/server-cpp/src/ServerRuntime.inc"
    RUNTIME_MESSAGE = "CO-SERVER/server-cpp/src/ServerRuntimeMessage.inc"
    RUNTIME_SUPPORT ="CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp"
    XMAKE = "CO-SERVER/server-cpp/xmake.lua"
    CONFIGURATION = "co-core/src/config/F4TServerConfiguration.cpp"
    SETTINGS = "CO-Launcher/launcher-gui/src/settings.rs"
    SETTINGS_UI = "CO-Launcher/launcher-gui/ui/server-settings.js"
    PROTOCOL_HANDLERS = "CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp"
    BOOTSTRAP = "CO-SERVER/server-cpp/src/ServerBootstrap.cpp"
    SERVER_SOURCE = "CO-SERVER/server-cpp/src"


    def test_enforcement_types_are_declared():
        header = read(ENFORCEMENT_HEADER)
        violation = read(VIOLATION_HEADER)
        assert "enum class Violation : std::uint8_t" in violation
        assert "struct PendingViolation" in violation
        assert "enum class Action : std::uint8_t" in header
        assert "class ServerEnforcement" in header
        assert "void ReportViolations(const std::vector<PendingViolation>& a_violations);" in header


    def test_enforcement_takes_its_appeal_link_from_config():
        source = read(ENFORCEMENT_SOURCE)
        assert "https://" not in source
        assert "http://" not in source
        assert 'WithAppealLink("Kicked: " + label, _appealLink)' in source
        assert 'WithAppealLink("Banned: " + BanDescription(label, effectiveExpiresAt, now), _appealLink)' in source


    def test_address_bans_are_capped_and_strikes_follow_the_steam_id():
        source = read(ENFORCEMENT_SOURCE)
        assert "consequence.action == Action::PermaBan && subject.steamId.empty()" in source
        assert "!subject.steamId.empty() ? subject.steamId" in source
        assert "const bool kickOnly = subject.steamId.empty() && (a_violation == Violation::TeleportDistance || a_violation == Violation::PvpDamage);" in source
        assert "!kickOnly && (!subject.steamId.empty() || !subject.address.empty())" in source


    def test_runtime_reports_each_wired_violation():
        runtime = read(RUNTIME).replace('#include "ServerRuntimeMessage.inc"', read(RUNTIME_MESSAGE))
        assert "CommonwealthOnline::Server::ServerEnforcement enforcement{" in runtime
        assert 'protocol.QueueViolation(event.connection_id, CommonwealthOnline::Server::Violation::MalformedPayload, "Malformed inbound payload");' in runtime
        assert "protocol.QueueViolation(event.connection_id, rate.byteBudgetExceeded ? CommonwealthOnline::Server::Violation::InboundBudget : CommonwealthOnline::Server::Violation::RateLimited, detail);" in runtime
        assert "protocol.QueueViolation(event.connection_id, CommonwealthOnline::Server::Violation::IdentityMismatch, decision.reason);" in runtime
        assert runtime.count("enforcement.ReportViolations(protocol.TakeViolations());") == 2
        assert "if (rate.verdict == F4T::PacketRateLimit::Verdict::Disconnect && rate.newViolationWindow) {" in runtime


    def test_appeal_link_reaches_every_session_end_and_admission():
        runtime = read(RUNTIME).replace('#include "ServerRuntimeMessage.inc"', read(RUNTIME_MESSAGE))
        runtime = runtime.replace('#include "ServerRuntimeAdminHooks.inc"', read("CO-SERVER/server-cpp/src/ServerRuntimeAdminHooks.inc"))
        assert ".appealLink = options.appealLink" in runtime
        for call in (
            "WithAppealLink(reason, options.appealLink)",
            "WithAppealLink(a_reason, options.appealLink)",
            "WithAppealLink(ban->reason, options.appealLink)",
            "WithAppealLink(decision.reason, options.appealLink)",
        ):
            assert call in runtime, call
        assert "CommonwealthOnline::Server::WithAppealLink(plain, a_context.appealLink, a_reasonSize - 1)" in read(RUNTIME_SUPPORT)
        assert 'CommonwealthOnline::Server::WithAppealLink("Banned.", a_context.appealLink, a_reasonSize - 1)' in read(RUNTIME_SUPPORT)


    def test_config_parses_and_validates_the_appeal_link():
        config = read(CONFIGURATION)
        assert 'options.appealLink = ReadOptionalString(Find(a_root, "appeal_link", "appealLink"), "appeal_link").value_or(std::string{});' in config
        assert "if (!IsAllowedAppealLink(a_options.appealLink)) {" in config


    def test_build_registers_the_enforcement_sources_and_test():
        xmake = read(XMAKE)
        assert 'add_files("src/ServerEnforcement.cpp")' in xmake
        assert 'add_headerfiles("include/ServerEnforcement.h")' in xmake
        assert 'add_headerfiles("include/ServerViolation.h")' in xmake
        assert 'server_test("CommonwealthOnlineServerEnforcementTests", "tests/ServerEnforcementTests.cpp", "CommonwealthOnlineServerProtocol")' in xmake


    def test_launcher_saves_the_appeal_link_under_both_keys():
        settings = read(SETTINGS)
        assert 'const APPEAL_LINK_KEYS: &[&str] = &["appeal_link", "appealLink"];' in settings
        assert 'appeal_link: text(APPEAL_LINK_KEYS, ""),' in settings
        assert "set(APPEAL_LINK_KEYS, json!(settings.appeal_link));" in settings
        assert "{ k: 'appealLink', t: 'text', label: 'Appeal link', maxLength: 200," in read(SETTINGS_UI)


    def test_detection_sites_queue_violations_and_only_the_drain_reports_them():
        handlers = read(PROTOCOL_HANDLERS)
        assert handlers.count("violations.push_back(CommonwealthOnline::Server::PendingViolation{") == 2
        assert "teleportRejections.Observe(a_monotonicSeconds)" in handlers
        assert "pvpOverCapClaims.Observe(a_monotonicSeconds)" in handlers
        for path in sorted((ROOT / SERVER_SOURCE).iterdir()):
            if path.name != "ServerEnforcement.cpp" and path.suffix in (".cpp", ".inc", ".h"):
                assert "ReportViolation(" not in path.read_text(encoding="utf-8"), path.name


    def test_unknown_connections_are_ignored_and_known_ones_are_marked():
        assert "if (!subject.connected) {" in read(ENFORCEMENT_SOURCE)
        assert "subject.connected = true;" in read(RUNTIME).replace('#include "ServerRuntimeMessage.inc"', read(RUNTIME_MESSAGE))


    def test_bans_quote_the_effective_entry_and_long_appeal_links_are_logged():
        source = read(ENFORCEMENT_SOURCE)
        assert "effectiveExpiresAt = _bans->BanSteamId(subject.steamId, reason, now, expiresAt).expiresAt;" in source
        assert "effectiveExpiresAt = _bans->Ban(subject.address, reason, now, expiresAt).expiresAt;" in source
        assert "appeal_link is longer than 111 characters" in read(BOOTSTRAP)


    def test_violation_bursts_reset_after_each_strike_and_ban_minutes_have_a_floor():
        source = read(ENFORCEMENT_SOURCE)
        assert "if (_count >= kViolationWindowHits) {" in source
        assert "(std::max)(std::llround((a_expiresAt - a_nowSeconds) / 60.0), 1LL)" in source


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"server enforcement contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_009_test_settings_apply_routes_contract():
    """Contract moved verbatim from tests/audit/test_settings_apply_routes_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_settings_apply_routes_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    REGISTRY = ROOT / "CommonwealthOnlineUI/src/F4TSettingsRegistry.cpp"


    def definition(text: str, setting_id: str) -> str:
        start = text.index(f'.id = "{setting_id}",')
        return text[start : text.index("});", start)]


    def test_sensitivity_settings_persist_and_apply_on_restart() -> None:
        text = REGISTRY.read_text(encoding="utf-8")
        for setting_id in ("controls.mouseSensitivity", "controls.gamepadSensitivity"):
            block = definition(text, setting_id)
            assert ".applyMethod = ApplyMethod::RequiresRestart," in block, setting_id
            assert ".applyTiming = ApplyTiming::RequiresRestart," in block, setting_id
            assert ".requiresRestart = true," in block, setting_id


    def test_active_effect_widget_is_marked_as_applying_on_restart() -> None:
        block = definition(REGISTRY.read_text(encoding="utf-8"), "interface.activeEffectWidget")
        assert ".applyTiming = ApplyTiming::RequiresRestart," in block
        assert ".requiresRestart = true," in block


    _merge_ns = dict(locals())
    if True:
        for name, function in list(_merge_ns.items()):
            if name.startswith("test_") and callable(function):
                function()
        print("Settings apply routes contract passed")

def _section_010_test_state_bag_contract():
    """Contract moved verbatim from tests/audit/test_state_bag_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_state_bag_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    CORE_HEADER = "co-core/include/F4TStateBagProtocol.h"
    CORE_SOURCE = "co-core/src/protocol/F4TStateBagProtocol.cpp"
    CORE_TEST = "co-core/tests/F4TStateBagProtocolTests.cpp"
    CORE_XMAKE = "co-core/xmake.lua"
    CORE_XMAKE_TESTS = "co-core/xmake-tests.lua"
    SERVER_HEADER = "CO-SERVER/server-cpp/src/ServerStateBag.h"
    SERVER_SOURCE = "CO-SERVER/server-cpp/src/ServerStateBag.cpp"
    SERVER_TEST = "CO-SERVER/server-cpp/tests/ServerStateBagTests.cpp"
    SERVER_XMAKE = "CO-SERVER/server-cpp/xmake.lua"
    WELCOME = "CO-SERVER/server-cpp/src/ServerWelcome.cpp"
    HANDLERS = "CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp"
    PROTOCOL_IMPL_INC = "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc"
    PROTOCOL_IMPL_H = "CO-SERVER/server-cpp/src/ServerProtocolImpl.h"
    PROTOCOL_API_INC = "CO-SERVER/server-cpp/src/ServerProtocolApi.inc"
    PROTOCOL_H = "CO-SERVER/server-cpp/include/ServerProtocol.h"

    NEW_SOURCES = [CORE_HEADER, CORE_SOURCE, CORE_TEST, SERVER_HEADER, SERVER_SOURCE, SERVER_TEST]
    TOUCHED = [
        CORE_XMAKE,
        CORE_XMAKE_TESTS,
        SERVER_XMAKE,
        WELCOME,
        HANDLERS,
        PROTOCOL_IMPL_INC,
        PROTOCOL_IMPL_H,
        PROTOCOL_API_INC,
        PROTOCOL_H,
    ]
    EXPECTED_LIMITS = {
        "kMaximumKeyChars": 64,
        "kMaximumValueBytes": 4096,
        "kMaximumKeysPerBag": 256,
        "kMaximumBagBytes": 65536,
    }
    PINNED_INC_FILES = {
        "CO-SERVER/server-cpp/src/ServerDirectoryRuntime.inc",
        "CO-SERVER/server-cpp/src/ServerDropProtocol.inc",
        "CO-SERVER/server-cpp/src/ServerFrameDelivery.inc",
        "CO-SERVER/server-cpp/src/ServerInventoryDispatch.inc",
        "CO-SERVER/server-cpp/src/ServerLiveSettingsLink.inc",
        "CO-SERVER/server-cpp/src/ServerLoadOrder.inc",
        "CO-SERVER/server-cpp/src/ServerMaintenance.inc",
        "CO-SERVER/server-cpp/src/ServerNpcDamageImpl.inc",
        "CO-SERVER/server-cpp/src/ServerNpcHitImpl.inc",
        "CO-SERVER/server-cpp/src/ServerNpcLootImpl.inc",
        "CO-SERVER/server-cpp/src/ServerObjectState.inc",
        "CO-SERVER/server-cpp/src/ServerOwnedInventory.inc",
        "CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc",
        "CO-SERVER/server-cpp/src/ServerProtocolApi.inc",
        "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc",
        "CO-SERVER/server-cpp/src/ServerRuntime.inc",
        "CO-SERVER/server-cpp/src/ServerRuntimeAdminHooks.inc",
        "CO-SERVER/server-cpp/src/ServerRuntimeMessage.inc",
        "CO-SERVER/server-cpp/src/ServerSessionEnded.inc",
        "CO-SERVER/server-cpp/src/ServerSnapshotDelivery.inc",
        "CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc",
        "CO-SERVER/server-cpp/src/ServerWorldPolicy.inc",
    }


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    def test_capability_is_advertised_and_pinned():
        assert re.search(r'inline constexpr std::string_view kCapability = "state-bag-v1";', read(CORE_HEADER))
        assert re.search(r'^\t\t\t"voice-v1",\n\t\t\t"state-bag-v1",\n', read(WELCOME), re.MULTILINE)


    def test_wire_type_and_limits_are_pinned():
        header = read(CORE_HEADER)
        assert 'inline constexpr std::string_view kPacketType = "stateBag";' in header
        limits = dict(re.findall(r"inline constexpr std::size_t (\w+) = (\d+);", header))
        assert {name: int(value) for name, value in limits.items() if name in EXPECTED_LIMITS} == EXPECTED_LIMITS


    def test_no_new_inc_files():
        found = set()
        for base in ("CO-SERVER", "co-core"):
            for path in (ROOT / base).rglob("*.inc"):
                found.add(path.relative_to(ROOT).as_posix())
        assert found == PINNED_INC_FILES


    def test_touched_and_new_files_stay_under_1000_lines():
        for name in NEW_SOURCES + TOUCHED:
            assert len(read(name).splitlines()) < 1000, name


    def test_new_sources_have_no_explanatory_comments():
        for name in (CORE_HEADER, CORE_SOURCE, SERVER_HEADER, SERVER_SOURCE):
            assert "//" not in read(name), name


    def test_snapshot_and_clear_are_hooked():
        handlers = read(HANDLERS)
        assert "AppendFrames(frames, StateBagSnapshotFrames(a_connectionId, a_session, stateBag));" in handlers
        assert handlers.index("ExistingWelcomeStateFrames(a_connectionId, a_session, a_unixTimeSeconds)") < handlers.index(
            "StateBagSnapshotFrames(a_connectionId, a_session, stateBag)"
        )
        assert "AppendFrames(frames, StateBagDisconnectFrames(stateBag, sessions, playerId));" in read(PROTOCOL_IMPL_INC)
        source = read(SERVER_SOURCE)
        assert "const auto packet = StateBagStore::ChangePacket(a_bag, a_key, a_value);" in source
        assert "ServerEventKind::StateBagChange" in source
        assert "StateBagBroadcastFrames(sessions, a_bag, packet)" in source


    def test_public_api_is_forwarded_and_declared():
        assert "std::vector<OutboundFrame> SetStateBag(" in read(PROTOCOL_H)
        assert "return _impl->SetStateBag(a_bag, a_key, a_value, a_failure);" in read(PROTOCOL_API_INC)
        assert "StateBagStore stateBag;" in read(PROTOCOL_IMPL_H)


    def test_client_cannot_write_state_bag():
        assert '"stateBag"' not in read(PROTOCOL_IMPL_INC)
        assert '"stateBag"' not in read(HANDLERS)


    def test_sources_are_registered_in_xmake():
        assert '"src/ServerStateBag.cpp"' in read(SERVER_XMAKE)
        assert '"tests/ServerStateBagTests.cpp"' in read(SERVER_XMAKE)
        assert '"src/protocol/F4TStateBagProtocol.cpp"' in read(CORE_XMAKE)
        assert '"include/F4TStateBagProtocol.h"' in read(CORE_XMAKE)
        assert '"F4TStateBagProtocolTests.cpp"' in read(CORE_XMAKE_TESTS)


    _merge_ns = dict(locals())
    if True:
        for name, function in list(_merge_ns.items()):
            if name.startswith("test_") and callable(function):
                function()
        print("State bag contract passed")

def _section_011_test_session_flush_await_contract():
    """Contract moved verbatim from tests/audit/test_session_flush_await_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_session_flush_await_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    MAINTENANCE = "CO-SERVER/server-cpp/src/ServerMaintenance.inc"
    RUNTIME = "CO-SERVER/server-cpp/src/ServerRuntime.inc"
    AWAITED_LAMBDA_FLUSH = "flushPersistence(true);"
    AWAITED_DIRECT_FLUSH = "FlushPersistence(persistence, durableStore.get(), true);"

    maintenance = read(MAINTENANCE)
    assert "flushPersistence(false);" in maintenance, "periodic maintenance flush must stay non-blocking"
    assert AWAITED_LAMBDA_FLUSH not in maintenance, "periodic maintenance flush must not await the commit"

    runtime = read(RUNTIME)
    POSTED_DIRECT_FLUSH = "FlushPersistence(persistence, durableStore.get(), false);"
    assert runtime.count(POSTED_DIRECT_FLUSH) >= 2, "both disconnect paths must flush pending player state into the store"
    assert AWAITED_DIRECT_FLUSH not in runtime, "disconnect flushes must not stall the main loop on other players' commits"
    assert AWAITED_LAMBDA_FLUSH in runtime, "shutdown must await the commit"
    print("session flush await contract: ok")

_SECTIONS = [
    ("tests/audit/test_server_enforcement_contract.py", _section_008_test_server_enforcement_contract),
    ("tests/audit/test_settings_apply_routes_contract.py", _section_009_test_settings_apply_routes_contract),
    ("tests/audit/test_state_bag_contract.py", _section_010_test_state_bag_contract),
    ("tests/audit/test_session_flush_await_contract.py", _section_011_test_session_flush_await_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_server_auth_settings_contracts_part3: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
