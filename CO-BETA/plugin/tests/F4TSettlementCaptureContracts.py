#!/usr/bin/env python3
"""F4TSettlementCaptureContracts: consolidated contract sections.

Consolidated from 1 single-contract script so the group runs one process
instead of 1. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TNetworkServerContracts.py
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





def _section_014_f4tsettlementcapturecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TSettlementCaptureContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSettlementCaptureContracts.py")
    import re
    import subprocess
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA" / "plugin"
    CLIENT_CPP = PLUGIN / "src" / "F4TSettlementCaptureClient.cpp"
    CLIENT_H = PLUGIN / "include" / "F4TSettlementCaptureClient.h"
    POLICY_H = PLUGIN / "include" / "F4TSettlementCapturePolicy.h"
    POLICY_TEST = PLUGIN / "tests" / "F4TSettlementCapturePolicyTests.cpp"
    ACCESS_CPP = PLUGIN / "src" / "F4TSettlementAccessClient.cpp"
    ACCESS_H = PLUGIN / "include" / "F4TSettlementAccessClient.h"
    API_PLUGIN = PLUGIN / "include" / "CommonwealthOnlineUI_API.h"
    API_UI = ROOT / "CommonwealthOnlineUI" / "include" / "CommonwealthOnlineUI_API.h"
    PLUGIN_API_CPP = ROOT / "CommonwealthOnlineUI" / "src" / "PluginAPI.cpp"
    LAYOUT_TEST = ROOT / "tests" / "audit" / "test_ui_api_layout_contract.py"
    CO_CORE_XMAKE = ROOT / "co-core" / "xmake.lua"
    CO_CORE_HEADER = ROOT / "co-core" / "include" / "F4TSettlementCapture.h"
    CO_CORE_SOURCE = ROOT / "co-core" / "src" / "world" / "F4TSettlementCapture.cpp"
    THIS_FILE = Path(__file__).resolve()

    STATE_CPP = PLUGIN / "src" / "F4TSettlementCaptureState.cpp"
    INTERNAL_H = PLUGIN / "src" / "F4TSettlementCaptureInternal.h"
    CAPTURE_FILES = [CLIENT_CPP, STATE_CPP, CLIENT_H, POLICY_H, POLICY_TEST]
    CAPTURE_SOURCES = sorted(
        list((PLUGIN / "src").glob("F4TSettlementCapture*.cpp"))
        + list((PLUGIN / "include").glob("F4TSettlementCapture*.h"))
        + list((PLUGIN / "tests").glob("F4TSettlementCapture*.cpp"))
    )
    HOOKS = {
        PLUGIN / "src" / "F4TNetworkingInbound.cpp": ["F4T::SettlementCaptureClient::TryHandlePacket(*packetType, packet)"],
        PLUGIN / "src" / "F4TUIClient.cpp": [
            "F4T::SettlementCaptureClient::WithCaptureState(",
            "F4T::SettlementCaptureClient::HandleAction(a_actionJson)",
            "F4T::SettlementCaptureClient::Reset();",
        ],
        PLUGIN / "src" / "F4TReplicationRuntimePublish.cpp": ["F4T::SettlementCaptureClient::NoteLocalPosition("],
        PLUGIN / "src" / "F4TNetworking.cpp": ["F4T::SettlementCaptureClient::Reset();"],
    }
    EDITED = CAPTURE_FILES + [ACCESS_CPP, ACCESS_H, API_PLUGIN, API_UI, PLUGIN_API_CPP, LAYOUT_TEST, CO_CORE_XMAKE, CO_CORE_HEADER, CO_CORE_SOURCE, THIS_FILE] + list(HOOKS)
    FORBIDDEN_NAMES = [
        "settlementCaptureList",
        "settlementCaptureCancel",
        "settlementCaptureStart",
        "settlementCaptureResult",
        "nearWorkbenchRefId",
        "workbenchRefId",
    ]
    MAXIMUM_LINES = 1000


    def read_bytes(path: Path) -> bytes:
        return path.read_bytes()


    def read_capture_client() -> str:
        return "\n".join(read(path) for path in (CLIENT_CPP, STATE_CPP, INTERNAL_H))


    def read(path: Path) -> str:
        return read_bytes(path).decode("utf-8").replace("\r\n", "\n")


    def test_client_speaks_the_real_wire():
        text = read_capture_client()
        for needle in [
            '"settlementCaptureState"',
            '"settlementCaptureReject"',
            '"settlementCapture"',
            '"start"',
            '"continue"',
            '"cancel"',
            "F4T::SettlementCapture::DecodeState(",
        ]:
            assert needle in text, f"client is missing {needle}"


    def test_old_contract_names_are_gone():
        for path in CAPTURE_FILES:
            text = read(path)
            for name in FORBIDDEN_NAMES:
                assert name not in text, f"{path.name} still uses {name}"


    def test_capture_sources_do_not_use_windows_macro_names():
        assert CAPTURE_SOURCES, "no capture sources found"
        for path in CAPTURE_SOURCES:
            hits = [line.strip() for line in read(path).split("\n") if re.search(r"\b(near|far)\b", line)]
            assert not hits, f"{path.name} uses a Windows macro name as an identifier: {hits}"


    def test_heartbeat_stays_inside_the_server_timeout():
        match = re.search(r"kHeartbeatInterval\{ (\d+) \}", read_capture_client())
        assert match, "heartbeat interval is missing"
        assert 0 < int(match.group(1)) < 10, "continue must be sent more often than the 10 second server timeout"


    def test_policy_uses_the_wire_radius_and_no_client_hold_limit():
        text = read(POLICY_H)
        assert "kDefaultCaptureRadius = 800.0F" in text
        assert "kHoldLimit" not in text
        assert "CountOwnedBy" not in text
        assert "cooldown" not in text.lower()


    def test_hooks_are_registered():
        for path, needles in HOOKS.items():
            text = read(path)
            for needle in needles:
                assert needle in text, f"{path.name} does not call {needle}"
        assert read(PLUGIN / "src" / "F4TNetworking.cpp").count("F4T::SettlementCaptureClient::Reset();") >= 2


    def test_access_owner_snapshot_is_declared_and_defined():
        assert "OwnersByBuildZone();" in read(ACCESS_H)
        assert "std::map<std::uint64_t, std::string> OwnersByBuildZone()" in read(ACCESS_CPP)


    def test_hud_api_slot_is_identical_and_wired():
        assert read_bytes(API_PLUGIN) == read_bytes(API_UI), "the two CommonwealthOnlineUI_API.h copies differ"
        text = read(API_PLUGIN)
        assert "V15 = 15" in text
        assert text.count("struct IVCommonwealthOnlineUI15\n") == 1
        assert "void (*PushCaptureHud)(const char* a_stateJson){ nullptr };" in text
        assert "static constexpr std::uint32_t    layout = 0x8B3F2E0F;" in text
        ui = read(PLUGIN_API_CPP)
        assert "void Api_PushCaptureHud(const char* a_stateJson)" in ui
        assert "F4T::CaptureHud::PushStateJson(a_stateJson);" in ui
        assert "IVCommonwealthOnlineUI15 g_interfaceV15{ Api_PushCaptureHud };" in ui
        assert "case InterfaceVersion::V15:\n\t\t\treturn a_visit(g_interfaceV15);" in ui
        assert '"V15": (0x8B3F2E0F, ' in read(LAYOUT_TEST)


    def test_co_core_capture_types_are_registered():
        assert CO_CORE_HEADER.exists() and CO_CORE_SOURCE.exists()
        xmake = read(CO_CORE_XMAKE)
        assert 'add_files(path.join(co_core_dir, "src/world/F4TSettlementCapture.cpp"))' in xmake
        assert 'add_headerfiles(path.join(co_core_dir, "include/F4TSettlementCapture.h"))' in xmake


    def test_capture_sources_have_no_comments():
        for path in CAPTURE_FILES:
            for line in read(path).split("\n"):
                stripped = line.strip()
                assert not stripped.startswith("//") and "/*" not in stripped, f"{path.name} has a comment: {stripped}"


    def test_edited_files_are_crlf_and_within_the_line_limit():
        for path in EDITED:
            data = read_bytes(path)
            assert data.count(b"\n") == data.count(b"\r\n"), f"{path.name} has bare LF line endings"
            lines = data.count(b"\r\n")
            assert lines <= MAXIMUM_LINES, f"{path.name} has {lines} lines"


    def test_active_captures_reach_the_map():
        text = read_capture_client()
        for needle in [
            'a_type == "settlementCaptureActive"',
            "F4T::SettlementCapture::DecodeActiveZones(a_packet)",
            "g_activeZones.clear();",
            "nlohmann::json MapCaptures()",
        ]:
            assert needle in text, needle


    def test_zone_list_is_decoded_and_stored_apart_from_attempts():
        text = read_capture_client()
        for needle in [
            'a_type == "settlementCaptureZones"',
            "HandleZonesPacket(a_packet);",
            "F4T::SettlementCapture::DecodeZones(a_packet)",
            "std::map<std::uint64_t, F4T::SettlementCapturePolicy::Anchor> g_zoneAnchors;",
            "kMaximumZoneAnchors = 1024",
            "kMaximumStoredSettlements = 64",
        ]:
            assert needle in text, f"client is missing {needle}"


    def test_idle_zone_anchors_are_findable_and_cleared_on_reset():
        text = read_capture_client()
        anchors_start = text.index("std::vector<Policy::Anchor> AnchorsLocked()")
        build_start = text.index("Json BuildState()")
        assert "g_zoneAnchors" in text[anchors_start:build_start], "AnchorsLocked must include the idle zone anchors"
        assert "owners.emplace(entry.first, std::string{});" in text[build_start:], "BuildState must give every zone anchor an owner entry"
        assert "g_zoneAnchors.clear();" in text, "Reset must clear the idle zone anchors"


    def test_capture_rows_are_budgeted_and_prioritised():
        text = read_capture_client()
        for needle in [
            "kCaptureOutputBudgetBytes = 26 * 1024",
            "CapturePriority(buildZoneId, ownerProfileId, record, self, nearest, g_localBuildZoneId)",
            "std::stable_sort(ranked.begin(), ranked.end(),",
            'capture["truncated"] = truncated;',
            "truncated = true;",
        ]:
            assert needle in text, f"client is missing {needle}"


    def test_cancel_follows_the_local_attempt_flag():
        assert "localCapturing" in read_capture_client()
        ownership = ROOT / "CommonwealthOnlineUI" / "ui" / "views" / "CommonwealthOnline" / "faction" / "faction.ownership.js"
        assert "return capture.localCapturing === true;" in read(ownership)


    def test_store_evicts_finished_records_and_decode_bounds_values():
        client = read_capture_client()
        for needle in [
            'g_captures.erase(evictable);',
            'a_entry.first != g_localBuildZoneId',
        ]:
            assert needle in client, f'client is missing {needle}'
        core = read(CO_CORE_SOURCE)
        for needle in [
            '(std::min)(pr, kMaximumCaptureRadius)',
            'std::fabs(px) <= kMaximumCoordinate',
            '(std::min)(bench.radius, kMaximumCaptureRadius)',
            'std::isfinite(hold->get<double>())',
        ]:
            assert needle in core, f'co-core decode is missing {needle}'


    def test_plugin_contract_runs_in_ci():
        workflow = None
        for folder in ('.github/public-ci/workflows', '.github/workflows'):
            if (ROOT / folder / 'co-beta-ci.yml').is_file():
                workflow = (ROOT / folder / 'co-beta-ci.yml').read_text(encoding='utf-8')
                break
        assert workflow is not None, 'co-beta-ci.yml not found in either CI layout'
        assert 'scripts/run-python-tests.py' in workflow and '--group plugin' in workflow, 'co-beta-ci does not run the plugin contract group'


    def test_stuck_captures_are_cancelled_and_reported():
        client = read_capture_client()
        for needle in [
            "Policy::CancelOnReject(g_captures, *buildZoneId, status);",
            "Policy::CancelStale(g_captures, now, kStaleCaptureInterval);",
            "Policy::IsStale(g_localAttemptStartedAt, now, kStaleCaptureInterval)",
            "if (IsRetryableRejectStatus(status)) {",
            '"timedOut") {',
            "constexpr std::chrono::seconds kStaleCaptureInterval{ 15 };",
            "std::chrono::steady_clock::now(),\n\t\t\t\t};",
            'g_lastResult = Result{ "timed_out", now };',
        ]:
            assert needle in client, f"client is missing {needle}"
        hud = read(ROOT / "CommonwealthOnlineUI" / "ui" / "views" / "CommonwealthOnline" / "capture-hud" / "capture-hud.js")
        ownership = read(ROOT / "CommonwealthOnlineUI" / "ui" / "views" / "CommonwealthOnline" / "faction" / "faction.ownership.js")
        assert 'timed_out: "Capture timed out. Press H to try again.",' in hud, "the capture HUD has no timed-out message"
        assert 'timed_out: "Capture timed out. Press H to try again.",' in ownership, "the ownership list has no timed-out message"


    def budget_of(text, name):
        return int(re.search(name + r" = (\d+) \* 1024", text).group(1)) * 1024


    def test_state_buffer_budgets_fit_the_ui_buffer():
        access = budget_of(read(PLUGIN / "src" / "F4TSettlementAccessClient.cpp"), "kStateOutputBudgetBytes")
        capture = budget_of(read_capture_client(), "kCaptureOutputBudgetBytes")
        slots = budget_of(read(PLUGIN / "src" / "F4TCharacterSlotsClient.cpp"), "kStateOutputBudgetBytes")
        ui_buffer = budget_of(read(ROOT / "CommonwealthOnlineUI" / "src" / "F4TFactionView.cpp"), "kStateBufferSize")
        friends = budget_of(read(PLUGIN / "src" / "F4TFriends.cpp"), "kFriendsOutputBudgetBytes")
        margin = 2 * 1024
        assert access <= capture, "build access must not exceed the capture budget"
        assert capture + slots + margin <= ui_buffer, "capture cumulative total plus slots and margin must fit the UI buffer"
        assert access + slots + margin <= ui_buffer, "build access plus slots and margin must fit the UI buffer"
        assert capture + slots + friends + margin <= ui_buffer, "the 200-friend list must fit the Social window buffer with the capture budgets"

    def test_package_script_keeps_an_existing_zones_file():
        script = ROOT / 'CO-SERVER' / 'server-cpp' / 'scripts' / 'package-native-release.sh'
        text = read(script)
        assert text.index('KEPT_ZONES="$(mktemp)"') < text.index('rm -rf "$OUT_DIR"') < text.index('mv "$KEPT_ZONES"')
        subprocess.run(['bash', '-n', script.as_posix()], check=True)

    def test_capture_constants_are_indented_and_min_max_parenthesised():
        text = read_capture_client()
        assert '		constexpr std::size_t kCaptureOutputBudgetBytes = 26 * 1024;' in text
        assert '(std::min)((std::max)(0.0, a_record.holdSeconds' in text

    def test_capture_hud_native_visibility_follows_the_nearby_row():
        text = read(ROOT / "CommonwealthOnlineUI" / "src" / "F4TCaptureHud.cpp")
        assert 'state.find("nearBuildZoneId")' in text
        assert 'nearZone->is_number_unsigned()' in text
        assert 'rows->is_array()' in text
        assert '*id == *nearZone' in text
        assert 'g_hudState.SetDesiredVisible(nearSettlement || selfProtected);' in text
        assert 'g_hudState.ApplyState("HudState");' in text
        assert 'SetDesiredVisible(true)' not in text


    def test_capture_hud_repaints_at_protection_expiry_and_on_access_state():
        client = read_capture_client()
        access = read(ACCESS_CPP)
        assert "g_hudRefreshAt = ProtectionExpiry(state);" in client
        assert "QueueGameTask([]() { PushCaptureHud(); });" in client
        assert "F4T::SettlementCaptureClient::RefreshHud();" in access

    def test_workbench_protection_uses_the_welcome_server_clock():
        raid = read(PLUGIN / "src" / "F4TSettlementRaidClient.cpp")
        header = read(PLUGIN / "include" / "F4TSettlementRaidClient.h")
        inbound = read(PLUGIN / "src" / "F4TNetworkingInbound.cpp")
        client = read_capture_client()
        assert "void NoteServerTime(const nlohmann::json& a_welcome);" in header
        assert "std::uint64_t ServerUnixNow();" in header
        assert "F4T::SettlementRaidClient::NoteServerTime(a_packet);" in inbound
        assert "double g_serverClockOffsetSeconds{};" in raid
        assert "ReadDouble(a_welcome, \"serverTime\")" in raid
        assert "const auto now = F4T::SettlementRaidClient::ServerUnixNow();" in client
        assert "system_clock::now()" not in client, "the workbench protection clock must not read the local clock directly"

    def main():
        checks = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for check in checks:
            check()
        print(f"settlement capture plugin contract passed: {len(checks)} checks")


    _merge_ns = dict(locals())
    if True:
        main()


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TSettlementCaptureContract.py", _section_014_f4tsettlementcapturecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TNetworkServerContracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
