#!/usr/bin/env python3
"""test_workshop_settlement_contracts_part3: consolidated contract sections.

Consolidated from 4 single-contract scripts so the group runs one process
instead of 4. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_workshop_settlement_contracts_part3.py
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




def _section_008_test_settlement_access_bridge_contract():
    """Contract moved verbatim from tests/audit/test_settlement_access_bridge_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_settlement_access_bridge_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")


    def read_workflow(name):
        for folder in (".github/public-ci/workflows", ".github/workflows"):
            if (ROOT / folder / name).is_file():
                return (ROOT / folder / name).read_text(encoding="utf-8")
        raise FileNotFoundError(f"workflow not found: {name}")


    client_h = read("CO-BETA/plugin/include/F4TSettlementAccessClient.h")
    client = read("CO-BETA/plugin/src/F4TSettlementAccessClient.cpp")
    inbound = read("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")
    ui_client = read("CO-BETA/plugin/src/F4TUIClient.cpp")
    faction_view = read("CommonwealthOnlineUI/src/F4TFactionView.cpp")
    factions = read("CO-BETA/plugin/src/F4TFactions.cpp")
    identity = read("CO-BETA/plugin/src/F4TClientIdentity.cpp")
    identity_h = read("CO-BETA/plugin/include/F4TClientIdentity.h")
    networking = read("CO-BETA/plugin/src/F4TNetworking.cpp")
    ui_api = read("CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h")
    plugin_api = read("CO-BETA/plugin/include/CommonwealthOnlineUI_API.h")
    workflow = read_workflow("co-audit-regressions.yml")
    faction_js = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/faction.js")
    build_js = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/faction.build.js")
    index_html = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/index.html")
    ui_workflow = read_workflow("co-ui-ci.yml")

    # Outbound packets must use the exact names and fields from packets.md.
    assert '"settlementAccessGet"' in client, "get request must use type settlementAccessGet"
    assert '"settlementAccessUpdate"' in client, "update request must use type settlementAccessUpdate"
    assert '"buildZoneId"' in client, "requests must carry buildZoneId"
    assert '"targetProfileId"' in client, "updates must carry targetProfileId"
    assert '"grant"' in client and '"revoke"' in client, "updates must map to op grant/revoke"
    assert "buildAccessGet" in client, "the bridge must accept op buildAccessGet"
    assert "buildAccessGrant" in client, "the bridge must accept op buildAccessGrant"
    assert "buildAccessRevoke" in client, "the bridge must accept op buildAccessRevoke"

    # Inbound packets must use the exact names and fields from packets.md.
    assert '"settlementAccessState"' in client, "the client must handle settlementAccessState"
    assert '"settlementAccessReject"' in client, "the client must handle settlementAccessReject"
    assert '"ownerProfileId"' in client, "stored state must keep ownerProfileId"
    assert '"allowedProfileIds"' in client, "stored state must keep allowedProfileIds"
    assert '"lastReject"' in client, "stored state must surface the last reject for the UI"
    for status in ("invalidRequest", "unknownBuildZone", "notOwner", "alreadyAllowed",
                   "notAllowed", "full", "rateLimited", "persistenceUnavailable", "tooFar"):
        assert f'"{status}"' in client, f"reject status {status} must be recognized"
    for gone in ("settlementLimit", "committed"):
        assert f'"{gone}"' not in client, f"unreachable status {gone} must not be accepted"
    assert 'op == "refresh"' in client and "RefreshStoredZones" in client, \
        "opening the view must re-request every stored zone with the existing Get"
    assert "ownerChanged" in client, "an owner-changed push must re-request that zone"
    assert "No settlements you own yet" in build_js, "the empty state must name owned settlements"
    assert "knownNames" in build_js, "names must fall back to last-known session names"
    for gone in ("settlementLimit", "committed"):
        assert gone not in build_js, "unreachable status " + gone + " must not render"

    # The receive loop must offer server packets to the new module.
    assert '#include "F4TSettlementAccessClient.h"' in inbound
    assert "F4T::SettlementAccessClient::TryHandlePacket(*packetType, packet)" in inbound
    assert inbound.index("SettlementAccessClient::TryHandlePacket") < inbound.index("HandleUnknownPacket(packet)"), \
        "settlement packets must be offered before the unknown-packet path"

    # The UI bridge must route actions, embed stored state, and reset on disconnect.
    assert "F4T::SettlementAccessClient::HandleAction(a_actionJson)" in ui_client
    assert "F4T::SettlementAccessClient::WithBuildAccess(F4T::Factions::BuildStateJson())" in ui_client
    assert "F4T::SettlementAccessClient::Reset();" in ui_client
    assert "{ \"name\", zoneNames[zone] }" in client, "every settlement row must carry its zone name, or repair rows read as unnamed"
    assert "name = F4T::SettlementCaptureClient::ZoneName(zone);" in client, "zone names must be read outside the access lock"
    assert '"buildAccessGet"' in faction_view, "the faction view must let buildAccessGet through"
    assert '"buildAccessGrant"' in faction_view, "the faction view must let buildAccessGrant through"
    assert '"buildAccessRevoke"' in faction_view, "the faction view must let buildAccessRevoke through"

    # Constraint: no new UI capability, the existing faction bridge carries build access.
    run_slot = ui_api.split("struct IVCommonwealthOnlineUI22", 1)[1].split("\n\t};", 1)[0] if "struct IVCommonwealthOnlineUI22" in ui_api else ""
    assert "V22" not in ui_api or ("GetViewHandle" in run_slot and "BuildAccess" not in run_slot), "V22 may only be the view lookup, never build access"
    map_key_slot = ui_api.split("struct IVCommonwealthOnlineUI19", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI19" in ui_api else ""
    assert "V19" not in ui_api or ("HandlePauseMenuMapKey" in map_key_slot and "BuildAccess" not in map_key_slot), "V19 may only be the map key, never build access"
    trade_slot = ui_api.split("struct IVCommonwealthOnlineUI20", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI20" in ui_api else ""
    assert "V20" not in ui_api or ("SetTradeActionCallback" in trade_slot and "BuildAccess" not in trade_slot), "V20 may only be the trade interface, never build access"
    report_slot = ui_api.split("struct IVCommonwealthOnlineUI21", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI21" in ui_api else ""
    assert "V21" not in ui_api or ("SetReportCallbacks" in report_slot and "BuildAccess" not in report_slot), "V21 may only be the report interface, never build access"
    state_bag_slot = ui_api.split("struct IVCommonwealthOnlineUI16", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI16" in ui_api else ""
    assert "V16" not in ui_api or ("PushStateBag" in state_bag_slot and "BuildAccess" not in state_bag_slot), "V16 may only be the state bag bridge, never build access"
    capture_slot = ui_api.split("struct IVCommonwealthOnlineUI15", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI15" in ui_api else ""
    assert "V15" not in ui_api or ("PushCaptureHud" in capture_slot and "BuildAccess" not in capture_slot), "V15 may only be the capture HUD slot, never build access"
    assert ui_api == plugin_api, "the UI API header copies must stay byte-identical"

    # Capped files must not grow; the new module must stay under the limit.
    def lines(text):
        return text.count("\n") + (0 if text.endswith("\n") else 1)

    assert lines(client) < 1000, "the new client module must stay under 1000 lines"
    assert lines(factions) <= 1000, "F4TFactions.cpp must not grow past 1000 lines"
    assert lines(ui_client) < 1000, "F4TUIClient.cpp must stay under 1000 lines"
    assert lines(inbound) < 1000, "F4TNetworkingInbound.cpp must stay under 1000 lines"
    assert lines(faction_view) < 1000, "F4TFactionView.cpp must stay under 1000 lines"

    # The new module must ride the existing client sources glob, not a new .inc.
    assert "src/F4TSettlementAccessClient.cpp" not in read("CO-BETA/plugin/xmake.lua"), \
        "the client glob already covers src/**.cpp; do not list the file explicitly"

    # The Get refresh must stay inside the server bucket (1 per second, burst 4) and the
    # state must stay inside the 32 KiB faction buffer.
    assert "kMaximumRefreshGets" in client and "kMinimumGetInterval" in client, \
        "the refresh must pace and cap its settlementAccessGet requests"
    assert "g_nextGetAllowed = now + kMinimumGetInterval * static_cast<std::chrono::seconds::rep>(count)" in client, \
        "a multi-zone refresh must reserve one second per request it sends"
    assert "QueueGetRequest" in client and client.count("g_nextGetAllowed") >= 4, \
        "single get paths must share one reservation window with the refresh"
    assert "kRejectDisplayInterval" in client, "a stale reject banner must expire"
    assert client.count("QueuePacket(Json{ { \"type\", \"settlementAccessGet\" }") >= 2, \
        "the refresh and the single-zone path must both re-request through settlementAccessGet"
    assert "kStateOutputBudgetBytes" in client, "build access must respect the faction state buffer budget"
    assert 'const auto self = F4T::ClientIdentity::ConnectedProfileId();' in client and 'state["selfProfileId"] = self;' in client, \
        "the plugin must report the local profile id so an owner outside a faction can manage"
    assert "ClientIdentity::ClearConnectedProfileId()" in client, "the profile id must reset on disconnect"
    assert "ConnectedProfileId" in identity_h and "LookupProfileId(a_serverKey)" in identity, \
        "the connected profile id must come from the accepted connection's profile store entry"
    assert "a_identity.profileId" in identity, \
        "the accepted ready identity carries the authoritative profile id"
    assert "ClearConnectedProfileId()" in identity.split("void ClearConnectionIdentity()")[1][:400], \
        "the identity module must clear its own cached profile id"
    assert networking.count("F4T::SettlementAccessClient::Reset();") == 2, \
        "both local teardown paths must reset the build access store"
    assert "F4T::SettlementAccessClient::Reset();" in networking.split("bool ConnectToServerInternal")[1][:3000], \
        "the connect teardown must reset the previous server's build access state"
    assert "build_access_send_offline" in client, "a packet dropped while offline must be logged"
    assert "build_access_state_dump" in client, "a failed state merge must be logged"

    assert "scripts/run-python-tests.py" in workflow and "--group audit" in workflow, \
        "CI must run this contract"

    # The Social view must send the exact bridge ops and render real server state.
    for op in ('"buildAccessGet"', '"buildAccessGrant"', '"buildAccessRevoke"'):
        assert op in faction_js, "faction.js actionFor must build " + op
        assert op in build_js, "faction.build.js must render and send " + op
    assert "coBuildAccessTarget" in build_js and "coBuildAccessTarget" in faction_js
    assert "coFactionBuild.buildSection" in faction_js, "renderState must include the Build access section"
    assert '<script src="faction.build.js"></script>' in index_html
    assert "faction.build.test.js" in ui_workflow, "CI must run the Build access panel tests"
    assert "Owner:" in build_js and "You cannot build here" in build_js, "read-only non-owned line must render"
    assert "rateLimited" in build_js, "rate-limited replies must render visible text"
    assert "selfProfileId" in build_js, "the view must prefer the plugin-reported profile id"
    assert "truncated" in build_js, "a state truncated by the buffer budget must be disclosed"
    assert "{8,128}" in build_js and "A-Za-z0-9._:@-" in build_js, \
        "the profile id field must mirror the server's ValidProfileId range and charset"
    assert "buildAccessTargets" in faction_js, "typed profile ids must stay per settlement"
    assert "resetCache" in build_js and "coFactionBuild.resetCache()" in faction_js, \
        "the name cache must be dropped when the connection state resets"
    assert "Number.isSafeInteger" in build_js, "a build zone id must be a safe JS integer before it is sent"
    for name in ("faction.js", "faction.build.js", "faction.build.test.js", "faction.harness.js"):
        path = "CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/" + name
        assert lines(read(path)) < 1000, name + " must stay under 1000 lines"

    print("test_settlement_access_bridge_contract: all checks passed.")

def _section_009_test_settlement_access_contract():
    """Contract moved verbatim from tests/audit/test_settlement_access_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_settlement_access_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")


    def read_workflow(name):
        for folder in (".github/public-ci/workflows", ".github/workflows"):
            if (ROOT / folder / name).is_file():
                return (ROOT / folder / name).read_text(encoding="utf-8")
        raise FileNotFoundError(f"workflow not found: {name}")


    service = read("CO-SERVER/server-cpp/src/ServerSettlementAccess.cpp")
    protocol = read("co-core/src/protocol/F4TSettlementAccessProtocol.cpp")
    table = read("co-core/src/world/F4TSettlementAccess.cpp")
    header = read("co-core/include/F4TSettlementAccess.h")
    workshop = read("CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc")
    dispatch = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
    setup = read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc")
    store = read("CO-SERVER/server-cpp/src/DurableStateFileStore.cpp") + read("CO-SERVER/server-cpp/src/DurableStateFileStoreCamps.cpp")
    gate = read("CO-SERVER/server-cpp/src/PersistenceGate.cpp")
    server_xmake = read("CO-SERVER/server-cpp/xmake.lua")
    core_xmake = read("co-core/xmake.lua")
    core_tests = read("co-core/xmake-tests.lua")
    workflow = read_workflow("co-audit-regressions.yml")

    assert "(void)table.Restore(a_snapshot);" in service, "a save that fails must roll the build list back"
    assert "persistence->SaveSettlementAccess(F4T::SettlementAccessProtocol::MakeStoredJson(table.Snapshot()))" in service, "every committed build-list change must be saved"
    assert "kRefillPerSecond = 1.0;" in service and "kBurst = 4.0;" in service, "settlement access requests must be rate limited"
    assert '"settlementAccessGet"' in service and '"settlementAccessUpdate"' in service, "the server must answer get and update"
    assert '"settlementAccessState"' in protocol and '"settlementAccessReject"' in protocol, "the protocol must name state and reject"
    assert "MaximumAllowedProfiles = 15;" in header and "MaximumSettlements = 1024;" in header, "the list and settlement caps must stay at 15 and 1024"
    assert "Identity::IsValidProfileId(a_profileId)" in table, "listed profile ids must pass the identity rule"
    assert "workshopAuthority = SettlementAuthority();" in workshop and "workshopAuthority{ SettlementAuthority() };" in workshop, "the workshop authority must default to the build list"
    assert "WorkshopAuthority::AllowAll" not in workshop, "allow-all must not be the default workshop authority"
    assert "if (auto frames = DispatchSettlementPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);" in dispatch, "settlement packets must reach the settlement service"
    assert "if (!protocol.EnableSettlementAccess(persistence)) {" in setup, "stored build lists must be restored at startup"
    assert '_root.value("settlementAccess", nlohmann::json::array())' in store and 'a_root["settlementAccess"] = a_settlementAccess;' in store, "build lists must persist under the settlementAccess root key"
    assert "_store.GetSettlementAccess()" in gate and "if (!SavingEnabled()) return;\n\t\t_store.SaveSettlementAccess(a_settlementAccess);" in gate, "build lists must save only when saving is enabled"
    assert 'add_files("src/ServerSettlementAccess.cpp")' in server_xmake, "the server build must compile the settlement service"
    assert 'server_test("CommonwealthOnlineServerSettlementAccessTests", "tests/ServerSettlementAccessTests.cpp", "CommonwealthOnlineServerProtocol")' in server_xmake, "the server settlement tests must be registered"
    assert 'add_files(path.join(co_core_dir, "src/world/F4TSettlementAccess.cpp"))' in core_xmake, "the core build must compile the settlement table"
    assert 'add_files(path.join(co_core_dir, "src/protocol/F4TSettlementAccessProtocol.cpp"))' in core_xmake, "the core build must compile the settlement protocol"
    assert 'core_test("CoreSettlementAccessTests", "CommonwealthOnlineCoreSettlementAccessTests"' in core_tests, "the core settlement table tests must be registered"
    assert 'core_test("CoreSettlementAccessProtocolTests", "CommonwealthOnlineCoreSettlementAccessProtocolTests"' in core_tests, "the core settlement protocol tests must be registered"
    assert "scripts/run-python-tests.py" in workflow and "--group audit" in workflow, "CI must run this contract"

    print("test_settlement_access_contract: all checks passed.")

def _section_010_test_settlement_capture_contract():
    """Contract moved verbatim from tests/audit/test_settlement_capture_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_settlement_capture_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")


    def read_workflow(name):
        for folder in (".github/public-ci/workflows", ".github/workflows"):
            if (ROOT / folder / name).is_file():
                return (ROOT / folder / name).read_text(encoding="utf-8")
        raise FileNotFoundError(f"workflow not found: {name}")


    header = read("co-core/include/F4TSettlementCapture.h")
    logic = read("co-core/src/world/F4TSettlementCapture.cpp")
    service = read("CO-SERVER/server-cpp/src/ServerSettlementCapture.cpp")
    workshop = read("CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc")
    maintenance = read("CO-SERVER/server-cpp/src/ServerMaintenance.inc")
    transactions = read("co-core/include/F4TWorkshopTransactions.h")
    zones = read("co-core/src/protocol/F4TWorkshopProtocol.cpp")
    access = read("CO-SERVER/server-cpp/src/ServerSettlementAccess.cpp")
    protocol_api = read("CO-SERVER/server-cpp/src/ServerProtocolApi.inc")
    session_types = read("CO-SERVER/server-cpp/src/ServerSessionTypes.h")
    persistence_setup = read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc")
    server_xmake = read("CO-SERVER/server-cpp/xmake.lua")
    core_xmake = read("co-core/xmake.lua")
    core_tests = read("co-core/xmake-tests.lua")
    server_access_tests = read("CO-SERVER/server-cpp/tests/ServerSettlementAccessTests.cpp")
    server_capture_tests = read("CO-SERVER/server-cpp/tests/ServerSettlementCaptureTests.cpp")
    core_capture_tests = read("co-core/tests/F4TSettlementCaptureTests.cpp")
    workflow = read_workflow("co-audit-regressions.yml")
    package_script = read("CO-SERVER/server-cpp/scripts/package-native-release.sh")
    verify_script = read("CO-SERVER/server-cpp/scripts/verify-native-package.sh")

    # Constraint: capture rules and server-side authority.
    assert "double holdSeconds{ 60.0 };" in header, "the capture hold must stay 60 seconds"
    assert "double heartbeatTimeoutSeconds{ 10.0 };" in header and "double maximumPositionAgeSeconds{ 5.0 };" in header, "heartbeat and position age limits must stay 10 and 5 seconds"
    assert "double defaultRadius{ 800.0 };" in header, "the default capture radius must stay 800 units"
    assert "!a_presence.vitalsKnown || !a_presence.alive" in logic and "if (a_presence.downed)" in logic, "unknown, dead or downed players must not capture"
    assert "age > a_policy.maximumPositionAgeSeconds" in logic, "a stale position must not capture"
    assert "Classify(a_workbench, presence, m_policy) == Standing::Eligible" in logic, "only eligible players may contest a hold"
    assert "attempt.holdStart = a_nowSeconds;" in logic, "a contested hold must restart its timer"
    assert "a_refusal(zoneId, std::string{ event.capturerProfileId })" in service, "a capture must be refused on raid rules, not only the owner"
    assert "Status::Cancelled" in service, "a refused capture must end cancelled"
    assert "Status::Captured" in logic and "Tracker::Update" in logic, "a capture must complete in the poll"
    assert "a_access.SetOwner(" in service, "a completed capture must set the owner through the settlement access service"
    assert "CaptureVitals" not in service, "the capture service must not keep a second vitals parser"
    assert "SettlementCaptureService::Peers CapturePeers(double a_monotonicSeconds) const" in workshop, "the workshop implementation must build capture presences"
    assert "const auto vitals = SessionVitals(session);" in workshop, "capture presence must reuse the server vitals parse"
    assert "peer.presence.positionAgeSeconds = a_monotonicSeconds - *session.lastMovementMonotonic;" in workshop, "capture presence must come from server-known state"
    assert "kRefillPerSecond = 2.0;" in service and "kBurst = 6.0;" in service, "capture requests must be rate limited"
    assert 'a_type == "settlementCapture"' in workshop, "settlementCapture packets must reach the capture service"
    assert "protocol.PollSettlementCapture(loopMonotonic, nowUnix)" in maintenance, "the server loop must poll captures"

    # Constraint: zone data must carry the workbench and capture radius, and the wire carries them back.
    assert "float captureRadius{};" in transactions and "bool hasWorkbench{};" in transactions, "zones must carry a workbench and capture radius"
    assert '"workbench"' in zones and '"captureRadius"' in zones, "the zone file must parse the workbench and capture radius"
    assert '"workbenchX"' in logic and '"captureRadius", a_workbench.radius' in logic, "capture state must carry the resolved workbench point and radius"
    assert "DecodeState" in header and "hasCapturePoint" in header, "capture state must decode tolerantly with a capture-point fallback"
    assert "TestHugeZoneIdIsAcceptedWithoutThrowing" in core_capture_tests, "the core packet reader must accept the maximum zone id without throwing"
    assert "Capture::WorkbenchOf(zone->second.Policy(), tracker.Rules())" in service, "capture notices must use the server-resolved workbench"

    # The settlement list must be pushed on join and every change must be broadcast.
    assert "SettlementListFrames(" in workshop and "settlementListSentZones" in workshop, "the server must push the settlement list once per zone set"
    assert "for (auto& frame : _impl->SettlementListFrames(a_connectionId)) frames.push_back(std::move(frame));" in protocol_api, "the message path must append the settlement list"
    assert "std::size_t settlementListSentZones" in session_types, "the session must remember which zone set the list covered"
    assert "kMaximumSettlementListZones" in access and "SettlementListFrames" in access, "the list push must be capped"
    assert "frames.push_back(ReliableFrame(peer.connectionId, StateJson(after, peer.profileId)));" in access, "a settlement change must be broadcast to every active session"

    # A corrupt durable payload must fail closed and must never be overwritten.
    assert "corruptStore = true;" in access and "denying every build" in access, "a corrupt stored payload must fail closed"
    assert "refusing to overwrite the unreadable stored payload" in access and "(void)table.Restore(a_snapshot);" in access, "a refused write must roll the table back and must never overwrite the payload"
    assert "if (corruptStore) {" in access[: access.index("return table.CanBuild")], "builds must be denied while the store is corrupt"
    assert "ApplyStatus::PersistenceUnavailable" in access.split("settlementAccessGet")[1][:800], "a get must answer persistenceUnavailable while the store is corrupt"
    assert "every build is denied until the stored" in persistence_setup, "the startup log must say the feature is closed, not empty"
    assert "CapturersForZone" in header and "Status::Cancelled" in service, "a removed zone must cancel the live attempt"

    # Tests must cover the ownership rule, the list budget and the corrupt store.
    assert "TestUncontestedHoldTransfersOwnership" in server_capture_tests and "TestContestedHoldResetsUntilRivalLeaves" in server_capture_tests, "the server tests must cover start, reset and completion"
    assert "TestForgedPositionFieldsAreIgnored" in server_capture_tests and "notInRange" in server_capture_tests, "forged client positions must be ignored"
    assert "TestZoneRemovalCancelsTheAttempt" in server_capture_tests, "the removed-zone cancel path must be tested"
    assert "TestHugeZoneIdIsRejectedWithoutThrowing" in server_capture_tests, "a huge build zone id must be rejected without throwing"
    assert "TestCorruptStoreRejectsCaptureStart" in server_capture_tests, "a corrupt store must refuse a capture start before the hold begins"
    assert "TestEndedContinueNotifiesCapturerAndOwner" in server_capture_tests, "an ended attempt must notify the capturer and the owner"
    assert "TestCaptureRadiusIsClampedToSixThousand" in server_capture_tests, "the server must enforce the 6000 unit capture radius clamp"
    assert "TestCaptureZonesAreSentOnJoinAndWhenTheZoneSetChanges" in server_capture_tests and "TestCaptureZoneListIsChunkedAndCapped" in server_capture_tests, "the capture zone list must be pushed, chunked and capped"
    assert "TestRateLimitAllowsSixThenRefillsTwoPerSecond" in server_capture_tests, "the capture rate limit must be tested"
    assert "TestCorruptStoredAccessFailsClosed" in server_access_tests, "the corrupt store must be tested"
    assert "TestSettlementListFitsTheClientStateBudget" in server_access_tests and "TestSettlementListIsCapped" in server_access_tests, "the settlement list size must be tested"
    assert "TestSettlementListIsSentOnceOnJoin" in server_access_tests, "the join push must be tested"
    assert "TestAllowListSurvivesRestart" in server_access_tests, "ownership persistence must be tested"

    # Build registration and CI wiring must stay in place.
    assert 'add_files("src/ServerSettlementCapture.cpp")' in server_xmake, "the server build must compile the capture service"
    assert 'server_test("CommonwealthOnlineServerSettlementCaptureTests", "tests/ServerSettlementCaptureTests.cpp", "CommonwealthOnlineServerProtocol")' in server_xmake, "the server capture tests must be registered"
    assert 'add_files(path.join(co_core_dir, "src/world/F4TSettlementCapture.cpp"))' in core_xmake, "the core build must compile the capture logic"
    assert 'core_test("CoreSettlementCaptureTests", "CommonwealthOnlineCoreSettlementCaptureTests"' in core_tests, "the core capture tests must be registered"
    assert "scripts/run-python-tests.py" in workflow and "--group audit" in workflow, "CI must run this contract"

    assert "maximumRadius{ 6000.0 }" in header, "the capture radius must be clamped to 6000 units"
    assert "(std::min)(radius, a_captureRules.maximumRadius)" in logic, "every zone radius must pass through the clamp before any client sees it"
    assert "std::isfinite(a_workbench.radius)" in logic, "a non-finite workbench must be out of range"
    assert "bool attemptEnded{};" in header and "reply.attemptEnded" in service, "an ended attempt must be told to the capturer and owner, not rejected"
    assert "Capture::Op::Start && !a_access.StoreUsable()" in service, "a start on a corrupt store must be refused before the hold"
    assert "SettlementAccessService::StoreUsable() const noexcept" in access, "the access service must expose the store health gate"
    assert "kMaximumCaptureZones = 1024" in service and "kZonesPerFrame = 256" in service, "the zone list must be capped at 1024 and framed at 256"
    assert "ZoneFrames(a_connectionId, workshopZones)" in workshop, "the join push must include the capture zone list"
    assert "Capture::MakeZones(chunk)" in service, "zone frames must be encoded by the co-core helper"
    assert "nlohmann::json MakeZones(" in logic and "std::optional<std::vector<Workbench>> DecodeZones(" in logic, "the zone list encoder and decoder must live in co-core"
    assert "MakeZones" in core_capture_tests and "DecodeZones" in core_capture_tests, "the zone list round trip must be tested"
    assert 'cp "$SERVER_CPP_DIR/workshop-zones.sample.json" "$OUT_DIR/workshop-zones.json"' in package_script, "the native package must ship the zone file"
    assert "workshop-zones.json" in verify_script, "the native package verifier must require the zone file"
    assert 'cp "$SERVER_CPP_DIR/data/workshop-pieces.json" "$OUT_DIR/workshop-pieces.json"' in package_script, "the native package must ship the piece table"
    assert 'cp "$SERVER_CPP_DIR/data/no-build-zones.json" "$OUT_DIR/no-build-zones.json"' in package_script, "the native package must ship the no-build zones"
    assert "workshop-pieces.json workshop-costs.json weapon-classes.json no-build-zones.json" in verify_script and 'cp "$SERVER_CPP_DIR/data/workshop-costs.json" "$OUT_DIR/workshop-costs.json"' in package_script and 'cp "$SERVER_CPP_DIR/data/weapon-classes.json" "$OUT_DIR/weapon-classes.json"' in package_script, "the native package verifier must require the piece table and no-build zones"
    assert (ROOT / "CO-SERVER/server-cpp/data/workshop-pieces.json").is_file() and (ROOT / "CO-SERVER/server-cpp/data/no-build-zones.json").is_file(), "the packaged data files must exist"

    print("test_settlement_capture_contract: all checks passed.")

def _section_011_test_settlement_capture_ui_contract():
    """Contract moved verbatim from tests/audit/test_settlement_capture_ui_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_settlement_capture_ui_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    FACTION = "CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/"


    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")


    def read_workflow(name):
        for folder in (".github/public-ci/workflows", ".github/workflows"):
            if (ROOT / folder / name).is_file():
                return (ROOT / folder / name).read_text(encoding="utf-8")
        raise FileNotFoundError(f"workflow not found: {name}")


    def lines(text):
        return text.count("\n") + (0 if text.endswith("\n") else 1)


    faction_view = read("CommonwealthOnlineUI/src/F4TFactionView.cpp")
    faction_js = read(FACTION + "faction.js")
    ownership_js = read(FACTION + "faction.ownership.js")
    ownership_test = read(FACTION + "faction.ownership.test.js")
    index_html = read(FACTION + "index.html")
    preview_html = read(FACTION + "preview.html")
    harness = read(FACTION + "faction.harness.js")
    ui_workflow = read_workflow("co-ui-ci.yml")
    workflow = read_workflow("co-audit-regressions.yml")

    is_known = faction_view.split("bool IsActionKnown(std::string_view a_op)")[1].split("void PushState()")[0]
    for op in ("capture", "cancelCapture", "refreshCapture", "structureRepair", "structureRebuild", "workbenchRepair", "upkeepPay"):
        assert '"' + op + '"' in is_known, "the native faction bridge must let " + op + " through IsActionKnown"

    for op in ('"capture"', '"cancelCapture"', '"refreshCapture"'):
        assert "case " + op + ":" in faction_js, "faction.js actionFor must build " + op
    assert "buildZoneId: Number(data.buildZone)" in faction_js, "capture must send buildZoneId as a number"
    assert "window.coFactionAction" in faction_js and "coFactionAction" in faction_view, \
        "capture ops must travel the shared coFactionAction bridge"

    assert "ownershipSection(s, esc)" in faction_js, "renderState must include the Ownership section"
    assert "g_promptPush" in faction_view and "coFactionSetPrompt" in faction_js, "the workbench prompt mode must reach the Social page"
    assert '<script src="faction.ownership.js"></script>' in index_html, "the Social window must load faction.ownership.js"
    assert '<script src="faction.ownership.js"></script>' in preview_html, "the preview must load faction.ownership.js"
    assert "faction.ownership.js" in harness, "the shared harness must load faction.ownership.js"
    assert "faction.ownership.test.js" in ui_workflow, "CI must run the Ownership panel tests"
    assert "faction.ownership.js" in ui_workflow, "CI must node --check the Ownership module"

    for key in ("settlementCapture", "settlements", "nearBuildZoneId", "promptCapture", "localCapturing", "buildZoneId"):
        assert key in ownership_js, "faction.ownership.js must read " + key
    for name in ("workbenchRefId", "nearWorkbenchRefId"):
        assert name not in ownership_js and name not in faction_js, \
            name + " was renamed to buildZoneId in the contract deviations"
    assert 'data-op="capture"' in ownership_js and 'data-op="cancelCapture"' in ownership_js \
        and 'data-op="refreshCapture"' in ownership_js, "the panel must render all three capture controls"
    assert "Ownership data is not available yet." in ownership_js, "a missing state must show a placeholder"
    assert "Number.isSafeInteger" in ownership_js, "a zone id must be a safe integer before it renders into an action"
    assert '"Settlement "' not in ownership_js and "row.name" in ownership_js, \
        "a zone is labelled by its name, never by its id"
    assert "coFactionOwnership" in ownership_test or "faction.ownership.js" in ownership_test, \
        "the ownership test must load the module under test"

    assert lines(faction_js) < 1000, "faction.js must stay under 1000 lines"
    assert lines(ownership_js) < 1000, "faction.ownership.js must stay under 1000 lines"
    assert lines(faction_view) < 1000, "F4TFactionView.cpp must stay under 1000 lines"

    assert "scripts/run-python-tests.py" in workflow and "--group audit" in workflow, \
        "CI must run this contract"

    print("test_settlement_capture_ui_contract: all checks passed.")

def _section_012_test_named_zone_build_gate_contract():
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")

    core_header = read("co-core/include/F4TSettlementAccess.h")
    core = read("co-core/src/world/F4TSettlementAccess.cpp")
    access = read("CO-SERVER/server-cpp/src/ServerSettlementAccess.cpp")
    access_header = read("CO-SERVER/server-cpp/src/ServerSettlementAccess.h")
    workshop = read("CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc")
    power = read("CO-SERVER/server-cpp/src/ServerWorkshopPower.h")
    repair = read("CO-SERVER/server-cpp/src/ServerWorkshopRepair.h")
    charge = read("CO-SERVER/server-cpp/src/ServerSettlementCharge.h")
    client = read("CO-BETA/plugin/src/F4TSettlementCaptureClient.cpp")
    server_tests = read("CO-SERVER/server-cpp/tests/ServerSettlementAccessTests.cpp")
    core_tests = read("co-core/tests/F4TSettlementAccessTests.cpp")

    assert "inline bool UnownedZoneOpenToBuild(bool a_namedZone) noexcept" in core_header, \
        "the unowned-named build rule must live once in co-core"
    assert "bool CanPlace(Workshop::BuildZoneId a_buildZoneId, std::string_view a_profileId, bool a_namedZone) const;" in core_header, \
        "the table must expose the placement rule"
    place = core[core.index("bool Table::CanPlace("):core.index("std::optional<Settlement> Table::Find(")]
    assert "if (m_records.contains(a_buildZoneId)) {" in place and "return CanBuild(a_buildZoneId, a_profileId);" in place and \
        "return UnownedZoneOpenToBuild(a_namedZone);" in place, \
        "an unowned named zone must refuse every non-owner placement"
    assert "bool CanPlace(F4T::Workshop::BuildZoneId a_buildZoneId, const std::string& a_profileId, bool a_namedZone) const;" in access_header, \
        "the service must expose the placement rule"
    service = access[access.index("bool SettlementAccessService::CanPlace("):]
    service = service[:service.index("\n\t}")]
    assert "if (corruptStore) {" in service and "return table.CanPlace(a_buildZoneId, a_profileId, a_namedZone);" in service, \
        "placement must fail closed on a corrupt store and defer to the table rule"
    assert "bool IsNamedWorkshopZone(F4T::Workshop::BuildZoneId a_buildZoneId) const" in workshop and \
        "!zone->second.Policy().name.empty()" in workshop, \
        "a named zone is one whose policy carries a name, the same test the capture zone list uses"
    assert "return settlementAccess.CanPlace(a_buildZoneId, a_profileId, IsNamedWorkshopZone(a_buildZoneId));" in workshop, \
        "the workshop placement authority must apply the named rule"
    assert "settlementAccess.CanPlace(wire->buildZoneId, a_session.profileId, IsNamedWorkshopZone(wire->buildZoneId))" in power, \
        "power wiring is a build action and must apply the named rule"
    assert "SettlementChargeGate(a_session, rebuild->buildZoneId, true)" in repair, \
        "rebuilding must pass the charge gate with builder access"
    assert "return owner == a_profileId || (a_allowBuilders && settlementAccess.CanPlace(a_buildZoneId, a_profileId, IsNamedWorkshopZone(a_buildZoneId)));" in charge, \
        "the builder branch of the charge gate must apply the named rule, so rebuild, repair and workbench repair all do"
    cannot = client[client.index("bool CannotBuildAtWorkbench("):]
    cannot = cannot[:cannot.index("namespace")]
    assert "return !F4T::SettlementAccess::UnownedZoneOpenToBuild(IsNamedLocked(anchor->buildZoneId));" in cannot, \
        "the client must block unowned named zones with the same co-core rule"
    assert "beingCaptured" not in cannot, "the client block must not depend on a capture in progress"
    assert "TestNamedUnownedZoneNeedsCaptureToBuild" in server_tests, "the server must test the named unowned build gate"
    assert "!table.CanPlace(9, \"profile-anyone\", true)" in core_tests, \
        "the co-core tests must pin the named unowned rule"
    print("test_named_zone_build_gate_contract: all checks passed.")


_SECTIONS = [
    ("tests/audit/test_settlement_access_bridge_contract.py", _section_008_test_settlement_access_bridge_contract),
    ("tests/audit/test_settlement_access_contract.py", _section_009_test_settlement_access_contract),
    ("tests/audit/test_settlement_capture_contract.py", _section_010_test_settlement_capture_contract),
    ("tests/audit/test_settlement_capture_ui_contract.py", _section_011_test_settlement_capture_ui_contract),
    ("tests/audit/test_named_zone_build_gate_contract.py", _section_012_test_named_zone_build_gate_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_workshop_settlement_contracts_part3: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
