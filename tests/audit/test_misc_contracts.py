#!/usr/bin/env python3
"""test_misc_contracts: consolidated contract sections.

Consolidated from 8 single-contract scripts so the group runs one process
instead of 8. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_misc_contracts.py
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




def _section_001_test_afk_kick_contract():
    """Contract moved verbatim from tests/audit/test_afk_kick_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_afk_kick_contract.py")
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
        assert "AfkExpired(" in (ROOT / "CO-SERVER" / "server-cpp" / "include" / "ServerAfk.h").read_text(encoding="utf-8")
        assert "state.lastActivityAtUnixSeconds" in MAINTENANCE


    def test_the_afk_kick_tells_the_player_before_the_socket_closes():
        kick = MAINTENANCE.index('"afk_kick"')
        notify = MAINTENANCE.index("SendSessionEnded(server, a_expiring, entry.code, entry.reason, nowUnix)")
        ended = MAINTENANCE.index("disconnectTracked(a_expiring, entry.endedBy)")
        assert kick < notify < ended, (
            "the sessionEnded frame must be queued on the reliable stream before the connection "
            "is closed, so the player is told why instead of seeing a generic connection lost"
        )
        assert "EndSessionAfterNotifying(" in MAINTENANCE, (
            "the send-before-close order must come from the one helper the unit test drives"
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
        assert "SessionAfkExemption(connectionId)" in MAINTENANCE
        assert "Afk::Evaluate(" in MAINTENANCE, "the maintenance loop must use the one decision the tests cover"
        assert 'session.afkExemption = Afk::NextExemption(session.afkExemption, exempt != packet->end() && exempt->get<bool>(), a_unixTimeSeconds);' in (
            ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerProtocolImpl.inc"
        ).read_text(encoding="utf-8"), "a client that omits afkExempt must be treated as not exempt"
        assert "DefaultAfkExemptionFreshnessSeconds = 45.0" in POLICY_HEADER
        assert "ExemptionLive(" in (ROOT / "CO-SERVER" / "server-cpp" / "include" / "ServerAfk.h").read_text(encoding="utf-8"), (
            "an exemption must expire when the client stops reporting it"
        )


    def test_loading_and_the_character_creator_are_exempt():
        assert "F4T::SessionWorldState::IsLoadingScreenActive()" in NETWORKING
        assert 'F4T::SessionWorldState::IsMenuOpen("LooksMenu")' in NETWORKING, (
            "the character creator must keep the clock stopped, and LooksMenu has no MENU_NAME so it "
            "has to go through the name-based menu check the repo already uses for SPECIALMenu"
        )
        assert "AfkExemptFromClientState(" in NETWORKING
        assert "F4T::JoinFlow::IsActive()" in NETWORKING, (
            "the join flow, not \"not ready\" (which every normal session is), marks a client as exempt"
        )
        assert "JoinFlow::IsReady" not in NETWORKING


    def test_only_the_steam_host_of_its_own_session_is_exempt_from_the_kick():
        assert "SteamSourceOf(connectionId)" in RUNTIME
        assert "SteamIdSource::HostSelf" in RUNTIME
        assert "steamHost = steamHostLink.has_value()" not in RUNTIME, (
            "loopback is not proof of being the host: Steam tunnel guests arrive on loopback too"
        )


    def test_the_disconnect_line_names_the_transport_end_reason():
        assert 'LogDisconnect(runtimeConnections, event.connection_id, "peer or network", event.debug, event.reason)' in RUNTIME
        support = (ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerRuntimeSupport.cpp").read_text(encoding="utf-8")
        assert "endReason=" in support
        assert "SanitizeLogField(" in support, "a reason from the wire must not be able to forge a log line"
        window = (ROOT / "CO-SERVER" / "server-cpp" / "include" / "ServerRefusalWindow.h").read_text(encoding="utf-8")
        assert "kMaximumRefusalPeers" in window, "refusal logging must be rate limited and bounded"
        assert "SweepRefusals();" in MAINTENANCE, (
            "a peer that stops refusing must still get its suppressed count printed"
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
        assert "TimedLoopPoll(" in RUNTIME
        assert RUNTIME.count("LogLoopIterationEnd();") == 4
        assert "DefaultMainLoopStallSeconds = 0.25" in POLICY_HEADER


    def test_internet_players_get_longer_connect_and_handshake_limits():
        assert "DefaultInternetConnectSeconds = 10.0" in POLICY_HEADER
        assert "DefaultInternetWelcomeWaitSeconds = 10.0" in POLICY_HEADER
        assert "LimitsForHost(state->host).connectSeconds" in SHIM
        assert "LimitsForHost(host).welcomeWaitSeconds" in NETWORKING
        assert "DefaultLoopbackConnectSeconds = 3.0" in POLICY_HEADER
        assert "DefaultWelcomeWaitSeconds = 3.0" in POLICY_HEADER


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"afk kick contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_002_test_combat_damage_authority_contract():
    """Contract moved verbatim from tests/audit/test_combat_damage_authority_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_combat_damage_authority_contract.py")
    from pathlib import Path

    # Constraint: symbol names mirror the contracts (source:co-core/include/F4TNpcLootSync.h, CO-BETA/plugin/include/F4TRemotePlayerState.h).

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def main() -> int:
        failures: list[str] = []

        combat_checks_h = read("CO-SERVER/server-cpp/src/ServerCombatChecks.h")
        combat_checks = read("CO-SERVER/server-cpp/src/ServerCombatChecks.cpp")
        server_protocol = read("CO-SERVER/server-cpp/include/ServerProtocol.h")
        protocol_handlers = read("CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp")
        npc_hit = read("CO-SERVER/server-cpp/src/ServerNpcHitImpl.inc")
        npc_damage = read("CO-SERVER/server-cpp/src/ServerNpcDamageImpl.inc")
        remote_state = read("CO-BETA/plugin/src/F4TRemotePlayerState.cpp")

        require(
            "kMaximumCombatDamageClaim" in combat_checks_h,
            "H1: the server must own an absolute per-hit damage ceiling (kMaximumCombatDamageClaim)",
            failures,
        )
        require(
            "ClampCombatDamageClaim" in combat_checks_h and "ClampCombatDamageClaim" in combat_checks,
            "H1: the server must expose and implement ClampCombatDamageClaim",
            failures,
        )
        require(
            "std::isfinite" in combat_checks,
            "H1: ClampCombatDamageClaim must reject non-finite damage",
            failures,
        )

        combat_start = protocol_handlers.find("ProtocolHost::Impl::HandleCombatHit(")
        combat_end = protocol_handlers.find("\n\t}\n", combat_start) if combat_start >= 0 else -1
        combat_body = protocol_handlers[combat_start:combat_end] if combat_end > combat_start else ""
        require(
            combat_body != "",
            "H1: HandleCombatHit must exist in ServerProtocolHandlers.cpp",
            failures,
        )
        require(
            "ClampCombatDamageClaim" in combat_body,
            "H1: HandleCombatHit must bound the client-asserted damage before relaying it",
            failures,
        )
        require(
            '(*normalized)["damage"] = boundedDamage;' in combat_body,
            "H1: HandleCombatHit must relay the bounded damage, not the client claim",
            failures,
        )
        require(
            "stats.combatDamageClaimsClamped++" in combat_body,
            "H1: clamped combat damage claims must be counted",
            failures,
        )

        require(
            "ClampCombatDamageClaim" in npc_hit,
            "L2: npcHit must apply the same damage bound before forwarding to the NPC authority",
            failures,
        )
        require(
            "ClampCombatDamageClaim" in npc_damage,
            "L2: npcDamage must apply the same damage bound before forwarding to the victim",
            failures,
        )
        require(
            "combatDamageClaimsClamped" in server_protocol and "npcDamageClaimsClamped" in server_protocol,
            "H1/L2: ProtocolStats must expose the damage-claim clamp counters",
            failures,
        )

        require(
            "kUnknownWeaponDamageClaimCap = 300.0F" in remote_state,
            "H1: the victim-side cap for an unresolved weapon id must be 300, not 500",
            failures,
        )
        require(
            "kWeaponBaseDamageClaimMultiplier = 10.0F" in remote_state,
            "H1: the victim-side weapon damage headroom must be 10x base damage",
            failures,
        )
        require(
            "kMinimumWeaponDamageClaimCap = 300.0F" in remote_state and "kThrowableDamageClaimCap = 300.0F" in remote_state,
            "H1: the victim-side floor and the throwable fallback must both be 300",
            failures,
        )
        require(
            "kMaximumWeaponDamageClaimCap = 1000.0F" in remote_state,
            "H1: the victim-side weapon-derived cap must have an absolute ceiling",
            failures,
        )
        require(
            "kThrowableDamageClaimCap" in remote_state and "IsThrowableSlot" in remote_state,
            "H1: thrown weapons must be capped from the throwable slot, not the unarmed cap",
            failures,
        )
        require(
            "GetRemoteRightHandWeaponFormId" not in remote_state,
            "H1: the obsolete right-hand-only claim lookup must be replaced by the slot-aware lookup",
            failures,
        )
        require(
            "10000.0F" not in remote_state,
            "H1: the victim-side damage cap must no longer allow 10,000 damage per hit",
            failures,
        )

        test_source = read("CO-SERVER/server-cpp/tests/ServerCombatDamageClaimTests.cpp")
        require(
            "ClampCombatDamageClaim" in test_source and "combatDamageClaimsClamped" in test_source,
            "H1: a server regression test must cover the clamped combat damage claim path",
            failures,
        )
        xmake = read("CO-SERVER/server-cpp/xmake.lua")
        require(
            "CommonwealthOnlineServerCombatDamageClaimTests" in xmake,
            "H1: the combat damage claim test target must be registered in the server xmake build",
            failures,
        )

        if failures:
            for failure in failures:
                print(f"FAIL {failure}")
            return 1
        print("PASS combat damage authority contract")
        return 0


    if True:
        _merge_exit(main())

def _section_003_test_esp_content_contract():
    """Contract moved verbatim from tests/audit/test_esp_content_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_esp_content_contract.py")
    import struct
    import zlib
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    ESP = ROOT / "CO-BETA" / "creation-kit" / "esp" / "CommonwealthOnline.esp"

    COMPRESSED_FLAG = 0x00040000

    REQUIRED_EDITOR_IDS = {
        "COPlayerProxy",
        "COPlayerProxyFemale",
        "CO_CampKit",
        "CO_CharacterSetupQuest",
        "CO_GenderChoiceMessage",
        "CO_SexChangePending",
        "CO_WelcomeQuest",
        "CO_XMarkerHeading",
        "CO_Markers",
        "COProbeSleepPackage",
        "COProbeSleepLink",
    }

    REMOVED_ORPHAN_EDITOR_IDS = {
        "LargeRooms",
        "LrgHalls",
    }

    EXPECTED_RECORD_COUNT = 17


    def _payload(data: bytes, start: int, size: int, flags: int) -> bytes:
        body = data[start:start + size]
        if flags & COMPRESSED_FLAG and len(body) >= 4:
            body = zlib.decompress(body[4:])
        return body


    def _editor_id(payload: bytes) -> str:
        index = 0
        pending_size = None
        while index + 6 <= len(payload):
            signature = payload[index:index + 4]
            sub_size = struct.unpack_from("<H", payload, index + 4)[0]
            body_start = index + 6
            if signature == b"XXXX" and sub_size == 4:
                pending_size = struct.unpack_from("<I", payload, body_start)[0]
                index = body_start + 4
                continue
            if pending_size is not None:
                sub_size = pending_size
                pending_size = None
            if signature == b"EDID":
                return payload[body_start:body_start + sub_size].split(b"\x00")[0].decode("latin1")
            index = body_start + sub_size
        return ""


    def _records(data: bytes, start: int, end: int):
        offset = start
        while offset + 24 <= end:
            signature = data[offset:offset + 4]
            size = struct.unpack_from("<I", data, offset + 4)[0]
            if signature == b"GRUP":
                if size < 24:
                    size = 24
                yield from _records(data, offset + 24, min(offset + size, end))
                offset += size
                continue
            if offset + 24 + size > end:
                break
            flags = struct.unpack_from("<I", data, offset + 8)[0]
            yield signature.decode("latin1"), _editor_id(_payload(data, offset + 24, size, flags))
            offset += 24 + size


    def read_records() -> list[tuple[str, str]]:
        data = ESP.read_bytes()
        return [record for record in _records(data, 0, len(data)) if record[0] != "TES4"]


    def main() -> int:
        failures: list[str] = []

        if not ESP.exists() or ESP.stat().st_size == 0:
            print("FAIL: ESP-01: CommonwealthOnline.esp is missing or empty")
            return 1

        records = read_records()
        editors = {editor for _, editor in records if editor}

        for editor in sorted(REQUIRED_EDITOR_IDS):
            if editor not in editors:
                failures.append(f"ESP-02: required data form '{editor}' is missing from CommonwealthOnline.esp")

        for record in (("PACK", "COProbeSleepPackage"), ("KYWD", "COProbeSleepLink")):
            if record not in records:
                failures.append(f"ESP-05: {record[1]} must be a {record[0]} record")

        for editor in sorted(REMOVED_ORPHAN_EDITOR_IDS):
            if editor in editors:
                failures.append(f"ESP-03: unreferenced orphan layer '{editor}' must not be reintroduced")

        if len(records) != EXPECTED_RECORD_COUNT:
            failures.append(
                f"ESP-04: expected {EXPECTED_RECORD_COUNT} authored forms after orphan-layer removal, "
                f"found {len(records)}"
            )

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("CommonwealthOnline.esp data contract passed")
        return 0


    if True:
        _merge_exit(main())

def _section_004_test_faction_list_server_contract():
    """Contract moved verbatim from tests/audit/test_faction_list_server_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_faction_list_server_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    FACTIONS = "CO-SERVER/server-cpp/src/ServerFactions.cpp"
    FACTION_LIST = "CO-SERVER/server-cpp/src/ServerFactionList.cpp"
    FACTIONS_H = "CO-SERVER/server-cpp/src/ServerFactions.h"
    RULES = "CO-SERVER/server-cpp/src/ServerFactionRules.h"
    SERVER_CONFIG = "co-core/include/F4TServerConfiguration.h"
    PROTOCOL_IMPL = "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc"
    VALIDATION = "co-core/src/validation/F4TFactionValidation.cpp"
    VALIDATION_H = "co-core/include/F4TFactionValidation.h"
    VALIDATION_TESTS = "co-core/tests/F4TFactionValidationTests.cpp"
    TEST = "CO-SERVER/server-cpp/tests/ServerFactionListTests.cpp"
    XMAKE = "CO-SERVER/server-cpp/xmake.lua"
    CONTRACT_FIELDS = {"id", "name", "tag", "color", "description", "leader", "memberCount", "onlineCount", "yours"}
    TOUCHED = [FACTIONS, FACTION_LIST, FACTIONS_H, RULES, TEST, XMAKE, VALIDATION, VALIDATION_H, VALIDATION_TESTS]
    TEST_NAMES = [
        "TestDisabledProducesNoFrames",
        "TestListsContractFieldsWithoutLeakingMembers",
        "TestRequestsAreRateLimitedPerSession",
        "TestNameOrderIgnoresCase",
        "TestListIsCappedAtSixtyFourSortedByName",
        "TestRequesterFactionIsKeptPastTheCap",
    ]


    def function_body(source, signature, next_signature):
        start = source.index(signature)
        end = source.index(next_signature, start)
        return source[start:end]


    def function_tail(source, signature):
        return source[source.index(signature):]


    def list_packet_body():
        return function_tail(read(FACTION_LIST), "Json FactionService::FactionListPacket(")


    def test_factionlist_is_admitted_and_dispatched():
        assert 'a_type.starts_with("faction")' in read(PROTOCOL_IMPL)
        dispatch = function_body(
            read(FACTIONS),
            "std::optional<std::vector<OutboundFrame>> FactionService::DispatchType(",
            "std::vector<OutboundFrame> FactionService::ExistingFrames(",
        )
        assert 'a_type == "factionList"' in dispatch
        assert "HandleFactionList(a_self, a_state, a_peers, a_packet, a_monotonicSeconds)" in dispatch


    def test_handler_validates_and_rate_limits_per_session():
        source = read(FACTIONS)
        handler = function_body(
            source,
            "std::vector<OutboundFrame> FactionService::HandleFactionList(",
            "std::optional<std::vector<OutboundFrame>> FactionService::Dispatch(",
        )
        assert "F4T::FactionValidation::NormalizeFactionListRequest(a_packet)" in handler
        assert "a_state.lastFactionListRequestMonotonic" in handler
        assert "limits.listRequestIntervalSeconds" in handler
        assert "return RejectMalformed();" in handler
        assert "return { ReliableFrame(a_self.connectionId, FactionListPacket(a_peers, a_self)) };" in handler
        rules = read(RULES)
        assert "double listRequestIntervalSeconds{ 1.0 };" in read(SERVER_CONFIG)
        assert "constexpr std::size_t kMaximumFactionListEntries = 64;" in rules
        assert "std::optional<double> lastFactionListRequestMonotonic;" in read(FACTIONS_H)
        assert "HandleFactionList(const FactionPeer& a_self" in read(FACTIONS_H)
        assert "Json FactionListPacket(const Peers& a_peers, const FactionPeer& a_viewer) const;" in read(FACTIONS_H)


    def test_list_packet_exposes_only_contract_fields():
        body = list_packet_body()
        keys = set(re.findall(r'\{ "(\w+)", ', body)) - {"type", "factions"}
        assert keys == CONTRACT_FIELDS
        assert '{ "type", "factionList" }' in body and '{ "factions", std::move(list) }' in body
        for forbidden in ('"members"', '"officers"', '"profileId"', '"playerId"', '"leaderPlayerId"', '"connectionId"'):
            assert forbidden not in body
        assert "a_viewer.profileId, a_viewer.socialKey, faction.leaderProfileId" in body


    def test_list_is_sorted_by_online_then_case_folded_name_and_capped():
        body = list_packet_body()
        assert "a_left.online > a_right.online" in body
        assert "LowerAscii(entry.second.name)" in body
        assert "a_left.folded < a_right.folded" in body
        assert "rows.resize(kMaximumFactionListEntries)" in body


    def test_requester_faction_is_kept_inside_the_cap():
        body = list_packet_body()
        assert "const auto ownFactionId = FactionIdOf(a_viewer.profileId);" in body
        assert "std::find_if(rows.begin(), rows.end()" in body
        assert "static_cast<std::size_t>(own - rows.begin()) >= kMaximumFactionListEntries" in body
        assert "rows[kMaximumFactionListEntries - 1] = *own;" in body
        assert body.index("rows[kMaximumFactionListEntries - 1] = *own;") < body.index("rows.resize(kMaximumFactionListEntries);")


    def test_validator_is_declared_defined_and_covered():
        assert "NormalizeFactionListRequest(const nlohmann::json& a_packet);" in read(VALIDATION_H)
        validation = read(VALIDATION)
        assert "std::optional<nlohmann::json> NormalizeFactionListRequest(const nlohmann::json& a_packet)" in validation
        assert 'return Json{ { "type", "factionList" } };' in validation
        assert 'NormalizeFactionListRequest(Json{ { "type", "factionList" } })->at("type") == "factionList"' in read(VALIDATION_TESTS)
        assert "!NormalizeFactionListRequest(Json::array())" in read(VALIDATION_TESTS)


    def test_unit_suite_is_registered_and_runs_every_case():
        xmake = read(XMAKE)
        assert 'server_test("CommonwealthOnlineServerFactionListTests", "tests/ServerFactionListTests.cpp", "CommonwealthOnlineServerProtocol")' in xmake
        assert 'add_files("src/ServerFactionList.cpp")' in xmake
        source = read(TEST)
        for name in TEST_NAMES:
            assert f"void {name}()" in source
            assert f"{name}();" in source


    def test_touched_files_keep_one_line_ending_style_and_size_caps():
        for name in TOUCHED:
            data = (ROOT / name).read_bytes()
            assert data.count(b"\r\n") in (0, data.count(b"\n")), name
        for name in (FACTIONS, FACTION_LIST, FACTIONS_H, TEST):
            assert len((ROOT / name).read_bytes().splitlines()) <= 1000, name


    def main():
        tests = [value for key, value in sorted(_merge_ns.items()) if key.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"faction list server contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_005_test_inventory_gain_contract():
    """Contract moved verbatim from tests/audit/test_inventory_gain_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_inventory_gain_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]

    SNAPSHOT_SOURCE = (ROOT / "co-core" / "src" / "world" / "F4TInventorySnapshot.cpp").read_text(encoding="utf-8")
    STORE_SOURCE = (ROOT / "CO-SERVER" / "server-cpp" / "src" / "DurableStateFileStore.cpp").read_text(encoding="utf-8")
    BOOTSTRAP_SOURCE = (ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerBootstrap.cpp").read_text(encoding="utf-8")
    SERVER_README = (ROOT / "CO-SERVER" / "server-cpp" / "README.md").read_text(encoding="utf-8")


    def test_the_gain_limit_runs_after_the_identical_items_check_and_before_the_commit():
        unchanged = SNAPSHOT_SOURCE.index('items == inventory.at("items")')
        guard = SNAPSHOT_SOURCE.index('a_gain(inventory.at("items"), items, revision)')
        candidate = SNAPSHOT_SOURCE.index("Json candidate{")
        assert unchanged < guard < candidate
        assert "kGainLimitedReason" in SNAPSHOT_SOURCE


    def test_the_durable_store_passes_the_gain_check_and_charges_only_committed_snapshots():
        assert "Reconcile(a_root, a_profileId, a_request, gain);" in STORE_SOURCE
        committed = STORE_SOURCE.index("if (committed) {")
        charge = STORE_SOURCE.index("_gainBuckets[a_profileId] = std::move(pendingBuckets);")
        assert committed < charge


    def test_the_server_bootstrap_installs_the_configured_gain_policy():
        assert "SetInventoryGainPolicy(F4T::InventorySnapshot::DefaultGainPolicy(gainMode));" in BOOTSTRAP_SOURCE


    def test_the_server_readme_lists_the_gain_limited_reason():
        assert "gainLimited" in SERVER_README


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"inventory gain contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_006_test_npc_ledger_contract():
    """Contract moved verbatim from tests/audit/test_npc_ledger_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_npc_ledger_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def main() -> int:
        failures: list[str] = []

        session_types = read("CO-SERVER/server-cpp/src/ServerSessionTypes.h")
        npc_hit = read("CO-SERVER/server-cpp/src/ServerNpcHitImpl.inc")
        npc_hit_checks = read("CO-SERVER/server-cpp/src/ServerNpcHitChecks.cpp")
        protocol = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
        quest_assist = read("CO-SERVER/server-cpp/src/ServerQuestAssist.cpp")
        ledger_header = read("CO-SERVER/server-cpp/src/ServerNpcLedger.h")

        require(
            "npcLedger.Quarantined(" in npc_hit,
            "FindSnapshotNpc must refuse quarantined NPC references through npcLedger.Quarantined",
            failures,
        )
        require(
            "npcLedger.ObserveSnapshot(" in protocol,
            "HandleNpcState must feed every authority snapshot into npcLedger.ObserveSnapshot",
            failures,
        )
        require(
            "npcLedger.KillClaimed(" in quest_assist and "npcLedger.RecordKill(" in quest_assist,
            "kill credits must be gated by npcLedger.KillClaimed and recorded with npcLedger.RecordKill",
            failures,
        )
        require(
            "kMaximumNpcDamageSumPerWindow" in npc_hit_checks,
            "NpcDamageRateAllows must enforce the per-target damage sum ceiling",
            failures,
        )
        require(
            "class Ledger" in ledger_header,
            "ServerNpcLedger.h must declare NpcLedger::Ledger",
            failures,
        )
        for constant in (
            "kNpcRespawnCooldownSeconds = 1800.0",
            "kNpcKillCreditCooldownSeconds = 1800.0",
            "kNpcQuarantineSeconds = 120.0",
            "kNpcMaximumSpeedPerSecond = 3000.0",
            "kNpcTeleportSlack = 1500.0",
            "kNpcContinuityMaxGapSeconds = 5.0",
            "kNpcLedgerMaximumEntries = 8192",
            "kMaximumNpcDamageSumPerWindow = 1000.0",
        ):
            require(constant in session_types, f"ServerSessionTypes.h must define {constant}", failures)

        if failures:
            for failure in failures:
                print(f"FAIL {failure}")
            return 1
        print("PASS NPC ledger contract")
        return 0


    if True:
        _merge_exit(main())

def _section_007_test_pr70_post_commit_regressions():
    """Contract moved verbatim from tests/audit/test_pr70_post_commit_regressions.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_pr70_post_commit_regressions.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def main() -> int:
        failures: list[str] = []
        durable_header = read("CO-SERVER/server-cpp/include/DurableStateFileStore.h")
        durable = read("CO-SERVER/server-cpp/src/DurableStateFileStore.cpp")
        pipeline = read("CO-SERVER/server-cpp/src/DurableWritePipeline.cpp")
        ban_header = read("CO-SERVER/server-cpp/include/BanStore.h")
        ban = read("CO-SERVER/server-cpp/src/BanStorePersistence.cpp")
        runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
        runtime = runtime.replace('#include "ServerRuntimeAdminHooks.inc"', read("CO-SERVER/server-cpp/src/ServerRuntimeAdminHooks.inc"))

        require("TakePersistenceWarning" in durable_header, "PR70-COMMIT: durable state must expose committed-with-warning status", failures)
        require("if (committed)" in pipeline and "RestoreSections(previous)" in pipeline and "_persistenceWarning" in durable, "PR70-COMMIT: durable state must roll back only before commit", failures)
        require("a_committed && _postCommitHook" in pipeline, "PR70-COMMIT: durable state tests need a post-commit fault seam", failures)
        require("TakePersistenceWarning" in ban_header, "PR70-COMMIT: ban state must expose committed-with-warning status", failures)
        require("if (a_committed)" in ban and "_persistenceWarning" in ban and "return;" in ban, "PR70-COMMIT: ban state must retain committed memory and return a warning", failures)
        require("AppendPersistenceWarning" in runtime and "ReportPersistenceWarning" in runtime, "PR70-COMMIT: runtime must surface persistence warnings", failures)
        require("sendFramesTracked(frames, monotonicNow, unixNow);" in runtime and runtime.index("sendFramesTracked(frames, monotonicNow, unixNow);") < runtime.rindex("activateSessions(sessionReadyDelivered);") and "disconnectTracked, sessionReadyDelivered);\n\t\tactivateSessions(sessionReadyDelivered);" in runtime, "PR70-COMMIT: a session must only be admitted after its sessionReady frame was delivered", failures)
        require("sessionAdmissions.Commit" in runtime[runtime.index("activateSessions = [&]"):], "PR70-COMMIT: admission commit must live in activateSessions", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("PR 70 post-commit regression contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_008_test_pr70_world_state_regressions():
    """Contract moved verbatim from tests/audit/test_pr70_world_state_regressions.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_pr70_world_state_regressions.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def main() -> int:
        failures: list[str] = []
        runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
        runtime_support = read("CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp")
        persistence_gate = read("CO-SERVER/server-cpp/src/PersistenceGate.cpp")

        time_start = runtime.find(".setWorldTime =")
        time_end = runtime.find(".setWorldWeather =", time_start)
        time_block = runtime[time_start:time_end]
        weather_start = time_end
        weather_end = runtime.find(".worldMode =", weather_start)
        weather_block = runtime[weather_start:weather_end]
        helper_start = runtime.find("auto updateWorldState = ")
        helper_block = runtime[helper_start:runtime.find(chr(10) + chr(9) + "};", helper_start)] if helper_start >= 0 else ""

        def ordered(block: str) -> bool:
            if "updateWorldState(" in block:
                block = helper_block
            persist = block.find("UpdateServerWorldState")
            mutate = max(block.find("serverWorldState["), block.find("serverWorldState.update("))
            broadcast = block.find("BroadcastWorldPacket")
            return min(persist, mutate, broadcast) >= 0 and persist < mutate < broadcast
        require(
            0 <= time_start < time_end and ordered(time_block),
            "PR70-WORLD: time persistence must precede live mutation and broadcast",
            failures,
        )
        require(
            0 <= weather_start < weather_end and ordered(weather_block),
            "PR70-WORLD: weather persistence must precede live mutation and broadcast",
            failures,
        )

        send_start = runtime_support.find("void SendWorldStateToConnection(")
        send_end = runtime_support.find("\n\tvoid BroadcastWorldPacket", send_start)
        send_block = runtime_support[send_start:send_end]
        require("weatherConsoleArg" not in send_block, "PR70-WORLD: reconnect replay must not expose weatherConsoleArg", failures)
        require("weatherFormId" in send_block, "PR70-WORLD: reconnect replay must include weatherFormId", failures)
        require("weatherConsoleArg" not in persistence_gate[persistence_gate.find("SessionReadyDelivered"):], "PR70-WORLD: persistence replay must not expose weatherConsoleArg", failures)
        require("weatherFormId" in persistence_gate[persistence_gate.find("SessionReadyDelivered"):], "PR70-WORLD: persistence replay must include weatherFormId", failures)
        require("weatherConsoleArg" in weather_block, "PR70-WORLD: server persistence may retain weatherConsoleArg internally", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("PR 70 world-state regression contracts passed")
        return 0


    if True:
        _merge_exit(main())


def _section_009_test_server_protocol_bigobj_flags():
    """MSVC needs /bigobj for ServerProtocol.cpp, which includes the grown ServerProtocolImpl.inc."""
    from pathlib import Path

    ROOT = Path(_MERGE_ROOT)
    xmake = (ROOT / "CO-SERVER/server-cpp/xmake.lua").read_text(encoding="utf-8")
    target = xmake.split('target("CommonwealthOnlineServerProtocol")', 1)[1].split("\ntarget(", 1)[0]
    assert 'add_files("src/ServerProtocol.cpp")' in target, "the protocol target must still compile ServerProtocol.cpp"
    assert 'add_cxxflags("/bigobj", {tools = "cl"})' in target, "the protocol target must pass /bigobj to cl only"
    assert target.index('if is_plat("windows") then') < target.index('add_cxxflags("/bigobj"'), "/bigobj must stay inside the Windows branch"


_SECTIONS = [
    ("tests/audit/test_afk_kick_contract.py", _section_001_test_afk_kick_contract),
    ("tests/audit/test_combat_damage_authority_contract.py", _section_002_test_combat_damage_authority_contract),
    ("tests/audit/test_esp_content_contract.py", _section_003_test_esp_content_contract),
    ("tests/audit/test_faction_list_server_contract.py", _section_004_test_faction_list_server_contract),
    ("tests/audit/test_inventory_gain_contract.py", _section_005_test_inventory_gain_contract),
    ("tests/audit/test_npc_ledger_contract.py", _section_006_test_npc_ledger_contract),
    ("tests/audit/test_pr70_post_commit_regressions.py", _section_007_test_pr70_post_commit_regressions),
    ("tests/audit/test_pr70_world_state_regressions.py", _section_008_test_pr70_world_state_regressions),
    ("CO-SERVER/server-cpp/xmake.lua", _section_009_test_server_protocol_bigobj_flags),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_misc_contracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
