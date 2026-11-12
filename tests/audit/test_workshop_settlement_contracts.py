#!/usr/bin/env python3
"""test_workshop_settlement_contracts: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_workshop_settlement_contracts.py
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


def _section_002_test_workshop_piece_table_contract():
    """Contract moved verbatim from tests/audit/test_workshop_piece_table_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_workshop_piece_table_contract.py")
    import json
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    DATA = ROOT / "CO-SERVER" / "server-cpp" / "data" / "workshop-pieces.json"
    SAMPLE = ROOT / "CO-SERVER" / "server-cpp" / "workshop-pieces.sample.json"
    TIERS = {"wood", "scrap", "concrete", "reinforced"}
    DEVICE_CLASSES = {"other", "light", "water", "door", "turret"}
    POWER_KEYS = {"outputWatts", "drawWatts", "needsPower", "deviceClass"}


    def is_plain_int(value):
        return isinstance(value, int) and not isinstance(value, bool)


    def check_table(path):
        table = json.loads(path.read_text(encoding="utf-8"))
        pieces = table["pieces"]
        assert isinstance(pieces, list) and pieces, "table is empty"
        keys = set()
        for piece in pieces:
            plugin = piece["plugin"]
            local_id = piece["localFormId"]
            assert isinstance(plugin, str) and 1 <= len(plugin.encode("utf-8")) <= 255, piece
            assert plugin == plugin.lower(), plugin
            assert is_plain_int(local_id) and 1 <= local_id <= 0xFFFFFF, piece
            assert (plugin, local_id) not in keys, (plugin, local_id)
            keys.add((plugin, local_id))
            assert piece["tier"] in TIERS, piece
            assert isinstance(piece.get("door", False), bool), piece
            assert isinstance(piece.get("foundation", False), bool), piece
            if "power" in piece:
                power = piece["power"]
                assert POWER_KEYS <= set(power), piece
                assert is_plain_int(power["outputWatts"]) and power["outputWatts"] >= 0, piece
                assert is_plain_int(power["drawWatts"]) and power["drawWatts"] >= 0, piece
                assert isinstance(power["needsPower"], bool), piece
                assert power["deviceClass"] in DEVICE_CLASSES, piece
        return table


    assert check_table(DATA) == check_table(SAMPLE)

def _section_003_test_workshop_raid_sender_contract():
    """Contract moved verbatim from tests/audit/test_workshop_raid_sender_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_workshop_raid_sender_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    PROTOCOL_HEADER = "co-core/include/F4TWorkshopProtocol.h"
    PROTOCOL = "co-core/src/protocol/F4TWorkshopRaidProtocol.cpp"
    CLIENT_HEADER = "CO-BETA/plugin/include/F4TSettlementRaidClient.h"
    CLIENT = "CO-BETA/plugin/src/F4TSettlementRaidClient.cpp"
    UI_CLIENT = "CO-BETA/plugin/src/F4TUIClient.cpp"
    NETWORKING = "CO-BETA/plugin/src/F4TNetworking.cpp"
    INBOUND = "CO-BETA/plugin/src/F4TNetworkingInbound.cpp"
    FACTION_VIEW = "CommonwealthOnlineUI/src/F4TFactionView.cpp"
    FACTION_JS = "CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/faction.js"
    FACTION_BUILD = "CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/faction.build.js"
    SERVER_DEFENSES = "CO-SERVER/server-cpp/src/ServerWorkshopDefenses.h"
    SERVER_POLICY = "CO-SERVER/server-cpp/src/ServerWorldPolicy.inc"

    ENCODERS = {
        "MakeRaidDeclare": "raidDeclare",
        "MakeSettlementWindowSet": "settlementWindowSet",
        "MakeSettlerRecruit": "settlerRecruit",
        "MakeSettlerAssign": "settlerAssign",
        "MakeSettlerRecover": "settlerRecover",
    }
    OPS = tuple(ENCODERS.values())
    ASSIGNMENTS = ("guard", "farm", "scavenge", "purifier", "idle")
    FILE_LIMIT = 750


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    def definition(text, name):
        start = text.index(f"nlohmann::json {name}(")
        return text[start:text.index("\n\t}\n", start)]


    def test_encoders_are_declared_and_defined():
        header = read(PROTOCOL_HEADER)
        source = read(PROTOCOL)
        for name, packet in ENCODERS.items():
            assert f"[[nodiscard]] nlohmann::json {name}(" in header, name
            assert f'{{ "type", "{packet}" }}' in definition(source, name), name


    def test_encoder_bodies_carry_the_wire_keys():
        source = read(PROTOCOL)
        assert '{ "settlerId", a_settlerId }' in definition(source, "MakeSettlerAssign")
        assert '{ "assignment", std::string{ a_assignment } }' in definition(source, "MakeSettlerAssign")
        assert '{ "settlerId", a_settlerId }' in definition(source, "MakeSettlerRecover")
        window = definition(source, "MakeSettlementWindowSet")
        for key in ("windowStartHour", "windowDurationHours", "timezoneOffsetHours"):
            assert f'"{key}"' in window, key


    def test_server_parses_the_settler_keys_the_encoders_send():
        defenses = read(SERVER_DEFENSES)
        assert 'a_packet.find("settlerId")' in defenses
        assert 'a_packet.find("assignment")' in defenses
        for value in ASSIGNMENTS:
            assert f'value == "{value}"' in defenses, value


    def test_client_encodes_through_co_core_and_validates_with_declared_api():
        client = read(CLIENT)
        for name in ENCODERS:
            assert f"F4T::WorkshopProtocol::{name}(" in client, name
        for symbol in (
            "F4T::WorkshopProtocol::Codec::ReadU64(",
            "F4T::WorkshopRaids::ValidWindowSet(",
            "F4T::WorkshopRaids::DeclarationWarningSeconds",
            "F4T::Factions::Payload::IsSafeToken(",
            "std::chrono::current_zone()->get_info(",
        ):
            assert symbol in client, symbol


    def test_client_reads_the_state_keys_the_server_sends():
        client = read(CLIENT)
        for key in (
            "declarerProfileId",
            "readyMonotonic",
            "expiresMonotonic",
            "settlerId",
            "assignment",
            "dead",
            "health",
            "windowStartHour",
            "timezoneOffsetHours",
        ):
            assert f'"{key}"' in client, key
        raid_state = definition(read(PROTOCOL), "MakeRaidDeclareState")
        for key in ("declarerProfileId", "readyMonotonic", "expiresMonotonic"):
            assert f'"{key}"' in raid_state, key


    def test_client_assignments_match_the_server_list():
        client = read(CLIENT)
        for value in ASSIGNMENTS:
            assert f'"{value}"' in client, value


    def test_plugin_hooks_are_wired():
        inbound = read(INBOUND)
        ui = read(UI_CLIENT)
        networking = read(NETWORKING)
        assert "if (F4T::SettlementRaidClient::TryHandlePacket(*packetType, packet)) { return; }" in inbound
        assert ui.count("F4T::SettlementRaidClient::Reset();") == 1
        assert networking.count("F4T::SettlementRaidClient::Reset();") == 2
        assert "F4T::SettlementRaidClient::WithSettlementRaid(" in ui
        assert "F4T::SettlementRaidClient::HandleAction(a_actionJson)" in ui
        assert "F4T::SettlementAccessClient::WithBuildAccess(F4T::Factions::BuildStateJson())" in ui


    def test_faction_ops_are_allowed_and_routed_to_the_build_module():
        view = read(FACTION_VIEW)
        for op in OPS:
            assert f'a_op == "{op}"' in view, op
        assert "return window.coFactionBuild ? window.coFactionBuild.settlementAction(data) : null;" in read(FACTION_JS)
        build = read(FACTION_BUILD)
        assert "function settlementAction(data)" in build
        assert 'document.addEventListener("change"' in build
        assert "settlementAction: settlementAction" in build


    def test_windowed_capability_is_advertised_only_in_windowed_mode():
        policy = read(SERVER_POLICY)
        start = policy.index("void AppendOptionalCapabilities(")
        body = policy[start:policy.index("bool EnableFactions(", start)]
        assert 'if (workshopRaidConfig.mode == F4T::WorkshopRaids::RaidMode::Windowed) a_capabilities.push_back("settlement-raid-windowed-v1");' in body
        assert "settlement-raid-windowed-v1" not in read("CO-SERVER/server-cpp/src/ServerWelcome.cpp")


    def test_client_learns_the_windowed_capability_from_welcome_and_capabilities():
        client = read(CLIENT)
        assert 'constexpr std::string_view kWindowedCapability = "settlement-raid-windowed-v1";' in client
        assert "void NoteCapabilities(const Json& a_packet)" in client
        assert "void NoteCapabilities(const nlohmann::json& a_packet);" in read(CLIENT_HEADER)
        assert '"windowedRaids", windowed' in client
        reset = client[client.index("void Reset()"):client.index("bool HandleAction(")]
        assert "g_windowedRaids = false;" in reset
        assert read(INBOUND).count("F4T::SettlementRaidClient::NoteCapabilities(a_packet);") == 2


    def test_build_module_hides_raid_controls_unless_the_server_is_windowed():
        build = read(FACTION_BUILD)
        assert "s.settlementRaid.windowedRaids === true" in build
        assert build.count("OPEN_RAIDS_NOTE") == 3
        assert "Raids are open on this server." in build
        assert "windowed ? windowControls(zone, info, esc) : OPEN_RAIDS_NOTE" in build
        assert "? '<button type=\"button\" class=\"faction-btn is-danger\" data-op=\"raidDeclare\"" in build


    def test_touched_sources_stay_under_the_file_limit():
        for name in (CLIENT, CLIENT_HEADER, UI_CLIENT, NETWORKING, INBOUND, FACTION_VIEW, PROTOCOL, PROTOCOL_HEADER):
            lines = len(read(name).splitlines())
            limit = 775 if name == INBOUND else FILE_LIMIT
            assert lines < limit, f"{name}: {lines} lines (limit {limit})"


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"workshop raid sender contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_004_test_workshop_repair_contract():
    """Contract moved verbatim from tests/audit/test_workshop_repair_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_workshop_repair_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    def function_body(text, signature):
        start = text.index(signature)
        indent = text[text.rfind("\n", 0, start) + 1:start]
        assert indent.strip() == "", signature
        return text[start:text.index("\n" + indent + "}\n", start)]


    IMPL = "CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc"
    CLIENT = "CO-BETA/plugin/src/F4TWorkshopReplicationClient.cpp"
    QUEST_LOGIC = "CO-BETA/plugin/src/F4TQuestLogic.cpp"
    PROTOCOL = "co-core/src/protocol/F4TWorkshopProtocol.cpp"
    PROTOCOL_HEADER = "co-core/include/F4TWorkshopProtocol.h"
    PROTOCOL_TESTS = "co-core/tests/F4TWorkshopProtocolTests.cpp"
    PERSISTENCE = "co-core/src/protocol/F4TWorkshopPersistence.cpp"
    PERSISTENCE_TESTS = "co-core/tests/F4TWorkshopPersistenceTests.cpp"
    XMAKE_TESTS = "co-core/xmake-tests.lua"


    def test_enable_keeps_an_unmigratable_root_and_stays_disabled():
        body = function_body(read(IMPL), "bool EnableWorkshopPersistence(PersistenceGate* a_persistence)")
        assert "if (!stored.is_null()) {" in body
        assert "stays disabled so the stored root is not overwritten" in body
        assert "Json::object()" not in body
        assert body.index("MigrateWorkshopRoot(migrated)") < body.index("workshopPersistence = a_persistence;")


    def test_configure_enables_persistence_before_reading_the_zone_file():
        body = function_body(read(IMPL), "std::size_t ConfigureWorkshop(PersistenceGate* a_persistence, const std::filesystem::path& a_zonesFile)")
        assert body.index("(void)EnableWorkshopPersistence(a_persistence);") < body.index("std::ifstream input(a_zonesFile, std::ios::binary);")


    def test_flush_takes_a_copy_and_restores_it_when_the_save_throws():
        impl = read(IMPL)
        body = function_body(impl, "void FlushDirtyWorkshopZones(double a_unixSeconds, std::vector<OutboundFrame>& a_frames)")
        copy = body.index("const auto dirtyZones = workshopDirtyZones;")
        assert copy < body.index("workshopDirtyZones.clear();") < body.index("for (const auto buildZoneId : dirtyZones) {")
        restore = body.index("if (!SaveWorkshopRoot()) {")
        assert "workshopDirtyZones.insert(dirtyZones.begin(), dirtyZones.end());" in body[restore:]
        save = function_body(impl, "bool SaveWorkshopRoot()")
        catch = save.index("} catch (const std::exception& error) {")
        assert save.index("if (workshopPersistence->SaveWorkshop(workshopStored)) return true;") < catch
        assert "workshopRootDirty = true;" in save[catch:]
        assert "return false;" in save[catch:]


    def test_snapshot_requests_need_a_position_inside_the_zone():
        body = function_body(read(IMPL), "std::vector<OutboundFrame> HandleWorkshopSnapshotRequest(")
        lookup = body.index("const auto zone = workshopZones.find(request->buildZoneId);")
        gate = body.index("if (!SessionWithinZoneReach(a_session, zone->second)) {")
        unchanged = body.index("if (request->haveRevision == zone->second.CurrentRevision()) {")
        assert lookup < gate < unchanged
        reply = body[unchanged:]
        assert '{ "complete", false },' in reply
        assert '{ "unchanged", true },' in reply
        assert '"complete", true' not in body


    def test_client_ignores_unchanged_snapshots_before_touching_zone_state():
        body = function_body(read(CLIENT), "bool HandleSnapshot(const Json& a_packet)")
        unchanged = body.index("if (snapshot->unchanged) {")
        zone = body.index("auto& zone = g_zones[snapshot->buildZoneId];")
        assert unchanged < zone
        assert "return true;" in body[unchanged:zone]
        assert body.index("if (snapshot->complete) {") < body.index("FinishSnapshot(zone);")


    def test_snapshot_unchanged_flag_is_a_strict_boolean():
        protocol = read(PROTOCOL)
        assert "(unchanged != a_packet.end() && !unchanged->is_boolean())" in protocol
        assert "snapshot.unchanged = unchanged != a_packet.end() && unchanged->get<bool>();" in protocol
        assert "bool unchanged{};" in read(PROTOCOL_HEADER)
        tests = read(PROTOCOL_TESTS)
        assert "unchangedSnapshot->unchanged && !unchangedSnapshot->complete" in tests
        assert 'unchangedPacket["unchanged"] = "yes";' in tests


    def test_persistence_root_migration_is_tested_and_registered():
        assert "if (!a_root.is_object()) {" in read(PERSISTENCE)
        tests = read(PERSISTENCE_TESTS)
        assert "assert(!MigrateWorkshopRoot(root));" in tests
        assert "Json root = Json::object();" in tests
        assert "Json root = Json::array();" in tests
        assert 'core_test("CoreWorkshopPersistenceTests", "CommonwealthOnlineCoreWorkshopPersistenceTests", "F4TWorkshopPersistenceTests.cpp")' in read(XMAKE_TESTS)


    STRUCTURE = "CO-SERVER/server-cpp/src/ServerWorkshopStructure.h"
    REPAIR = "CO-SERVER/server-cpp/src/ServerWorkshopRepair.h"
    CHARGE = "CO-SERVER/server-cpp/src/ServerSettlementCharge.h"
    RAIDS = "CO-SERVER/server-cpp/src/ServerWorkshopRaids.h"
    HEALTH = "CO-SERVER/server-cpp/src/ServerWorkshopHealth.h"
    DEFENSES = "CO-SERVER/server-cpp/src/ServerWorkshopDefenses.h"
    STORE_HEADER = "CO-SERVER/server-cpp/include/DurableStateFileStore.h"
    ADMIN_MENU = "CO-SERVER/server-cpp/src/ServerAdminMenu.cpp"
    SERVER_XMAKE = "CO-SERVER/server-cpp/xmake.lua"


    def test_configure_refuses_zones_without_durable_persistence():
        body = function_body(read(IMPL), "std::size_t ConfigureWorkshop(PersistenceGate* a_persistence, const std::filesystem::path& a_zonesFile)")
        refuse = body.index("if (!zones->empty() && workshopPersistence == nullptr) {")
        assert body.index("(void)EnableWorkshopPersistence(a_persistence);") < body.index("std::ifstream input(a_zonesFile, std::ios::binary);") < refuse
        assert "refusing to register zones" in body[refuse:]


    def test_failed_root_save_keeps_the_root_dirty_for_retry():
        impl = read(IMPL)
        flush = function_body(impl, "void FlushDirtyWorkshopZones(double a_unixSeconds, std::vector<OutboundFrame>& a_frames)")
        retry = flush.index("if (!SaveWorkshopRoot()) {")
        assert flush.index("const bool writeRetryDue = workshopPersistence->Store().LastWriteFailed()") < flush.index("if (!changed && !workshopRootDirty && !writeRetryDue) {") < flush.index("workshopRootDirty = false;") < retry
        assert "workshopRootDirty = true;" in flush[retry:]
        assert "bool workshopRootDirty{};" in impl
        assert "bool LastWriteFailed() const { return _pipeline->LastWriteFailed(); }" in read(STORE_HEADER)


    def test_only_the_settlement_shield_protects_and_legacy_first_seen_keys_are_dropped():
        impl = read(IMPL)
        enable = function_body(impl, "bool EnableWorkshopPersistence(PersistenceGate* a_persistence)")
        for key in ("profileFirstSeen", "profileFirstSeenEpoch", "profileFirstSeenPreexisting"):
            assert f'"{key}"' in enable
        assert "workshopStored.erase(legacyKey)" in enable and "workshopRootDirty" in enable
        for removed in ("NoteProfileSeen", "NewPlayerProtectedUntil", "workshopProfileFirstSeen", "profileFirstSeenLookup",
                        "PlayerRaidProtectionFrames", "SetOwnerProtectedUntilLookup", "ProtectionEpochFirstSeen"):
            assert removed not in read(DEFENSES) and removed not in impl


    def test_guards_die_when_the_workbench_or_a_defense_object_falls():
        structure = read(STRUCTURE)
        assert structure.count("OwnerSettlerFrames(hit->buildZoneId, KillZoneGuards(hit->buildZoneId, unixSeconds))") == 2
        assert "if (settlement && settlement->workbenchBroken && !wasBroken) {" in structure
        assert "if (destroyed && holdsGuards) {" in structure
        assert structure.index("const bool holdsGuards = workshopDefenseForms.Find(object->form).has_value();") < structure.index("auto candidate = zone->second;")
        kill = function_body(read(DEFENSES), "std::vector<F4T::Workshop::ObjectId> KillZoneGuards(")
        assert "settler.dead = true;" in kill
        assert "settler.deadUntilUnix = a_unixSeconds + F4T::WorkshopSettlers::DeadRecoverySeconds;" in kill


    def test_structure_hits_gate_on_zone_membership_reach_and_cooldown_key():
        structure = read(STRUCTURE)
        helper = function_body(structure, "[[nodiscard]] static bool SessionWithinZoneReach(")
        assert "a_session.lastMovement && a_zone.WithinRadius(a_session.lastMovement->x, a_session.lastMovement->y, a_session.lastMovement->z)" in helper
        body = function_body(structure, "std::vector<OutboundFrame> HandleStructureHit(")
        gate = body.index("if (!a_session.workshopZonesInside.contains(hit->buildZoneId) || !SessionWithinZoneReach(a_session, zone->second)) {")
        key = body.index("const auto hitKey = std::tuple{ a_connectionId, hit->buildZoneId, hit->weaponKind };")
        stamp = body.index("workshopStructureHitReady[hitKey] = a_monotonicSeconds + F4T::WorkshopRaids::StructureHitIntervalSeconds(hit->weaponKind);")
        assert gate < key < stamp
        assert "DeclarationReadyForZone(a_buildZoneId, a_profileId, a_monotonicSeconds)" in function_body(read(RAIDS), "[[nodiscard]] std::optional<std::pair<F4T::WorkshopRaids::HitRefusal, std::uint64_t>> RaidRefusalFor(")
        assert "workshopStructureHitReady.size() >= F4T::WorkshopRaids::MaximumStructureHitCooldowns" in body


    def test_structure_hits_check_equipment_and_standing_before_raid_rules():
        structure = read(STRUCTURE)
        body = function_body(structure, "std::vector<OutboundFrame> HandleStructureHit(")
        assert "claimed weapon does not match equipment" in structure
        assert body.index("if (!EquipmentAllowsHit(a_session, *hit, a_monotonicSeconds)) {") < body.index("const auto standing = StandingOf(hit->buildZoneId, a_session.profileId);")
        assert "HitRefusal::FactionMate, 0, a_monotonicSeconds" in body
        assert "? ShieldRefusalFor(hit->buildZoneId, unixSeconds)" in body and ": RaidRefusalFor(hit->buildZoneId, a_session.profileId, unixSeconds, a_monotonicSeconds);" in body
        assert "IsProtected(hit->buildZoneId" not in body and "CanBuild(hit->buildZoneId" not in body
        assert "const auto standing = StandingOf(a_zoneId, session.profileId);" in read(DEFENSES)
        assert "standing != F4T::WorkshopRaids::SettlementStanding::Raider ||" in read(DEFENSES)
        assert "RefuseCaptureOnRaid(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds)" in read(IMPL)


    def test_structure_repair_cooldown_is_checked_before_charge_and_stamped_after_it():
        repair = function_body(read(REPAIR), "std::vector<OutboundFrame> HandleStructureRepair(")
        check = repair.index("if (ready != workshopStructureRepairReady.end() && a_monotonicSeconds < ready->second) {")
        begin = repair.index("BeginSettlementCharge(")
        stamp = repair.index("workshopStructureRepairReady[repairKey] = a_monotonicSeconds + F4T::WorkshopRaids::StructureRepairCooldownSeconds;")
        assert check < begin < stamp
        commit = function_body(read(CHARGE), "std::optional<std::vector<OutboundFrame>> CommitStructureRepair(")
        assert commit.index("candidate.RepairObject(a_charge.objectId") < commit.index("zone->second = std::move(candidate);") < commit.index("MarkWorkshopDirty(a_charge.buildZoneId);")
        assert "std::erase_if(workshopStructureRepairReady" in read(HEALTH)
        assert "expiresMonotonic" not in read(CHARGE)


    def test_raid_declarations_and_window_sets_follow_the_zone_rules():
        raids = read(RAIDS)
        assert "found->second.declarerProfileId == a_attackerProfileId;" in raids
        declare = function_body(raids, "std::vector<OutboundFrame> HandleRaidDeclare(")
        assert "declaration.expiresMonotonic = declaration.readyMonotonic + (config.windowDurationHours * 3600.0);" in declare
        window = function_body(raids, "std::vector<OutboundFrame> HandleSettlementWindowSet(")
        assert "F4T::WorkshopRaids::RaidDeclarationLive(pending->second, a_monotonicSeconds)" in window
        assert window.index("RaidDeclarationLive(") < window.index("workshopRaidWindows[window->buildZoneId] = *window;")
        assert "std::map<std::tuple<std::uint32_t, F4T::Workshop::BuildZoneId, F4T::WorkshopMaterials::WeaponKind>, double> workshopStructureHitReady;" in raids
        assert "std::map<std::pair<F4T::Workshop::BuildZoneId, F4T::Workshop::ObjectId>, double> workshopStructureRepairReady;" in raids


    def test_admin_menu_does_not_pull_in_the_runtime_support_header():
        assert "ServerRuntimeSupport.h" not in read(ADMIN_MENU)


    def test_raid_server_tests_are_registered():
        assert 'server_test("CommonwealthOnlineServerWorkshopRaidTests", "tests/ServerWorkshopRaidTests.cpp", "CommonwealthOnlineServerProtocol")' in read(SERVER_XMAKE)


    def test_camp_reactivation_server_tests_are_registered():
        assert 'server_test("CommonwealthOnlineServerCampReactivationTests", "tests/ServerCampReactivationTests.cpp", "CommonwealthOnlineServerProtocol")' in read(SERVER_XMAKE)
        assert "TestCampReactivatesStoredCamp" not in read("CO-SERVER/server-cpp/tests/ServerWorkshopTests.cpp")


    def test_none_class_held_weapons_are_refused_as_weapon_unknown():
        assert "!HeldWeaponKind(a_session)) {" in read(STRUCTURE)
        assert "F4T::WorkshopRaids::HitRefusal::WeaponUnknown, 0, a_monotonicSeconds);" in read(STRUCTURE)
        assert "TestNoneClassHeldItemIsRefused();" in read("CO-SERVER/server-cpp/tests/ServerWorkshopWeaponClassTests.cpp")
        assert "kNoneClassFormId" in read("CO-SERVER/server-cpp/tests/ServerHeldWeaponTestSupport.h")


    def test_camp_kit_match_compares_the_lowercase_plugin_name():
        client = read(CLIENT)
        start = client.index("bool IsCampKitForm(const Workshop::FormKey& a_form) noexcept")
        body = client[start:client.index("\n\t}\n", start)]
        assert "return a_form.plugin == kCampKitPlugin && a_form.localFormId == kCampKitLocalFormId;" in body
        assert 'constexpr std::string_view kCampKitPlugin = "commonwealthonline.esp";' in client
        assert "constexpr std::uint32_t kCampKitLocalFormId = 0x00997F;" in client
        assert "TODO-verify" not in body
        assert "return Key{ ToLowerAscii(file->plugin), split.localId };" in read(QUEST_LOGIC)


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"workshop repair contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())


def _section_005_test_workshop_attack_notice_contract():
    """Pins the zone attack notice receiver and the turret damage receiver."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_workshop_settlement_contracts.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    CLIENT = "CO-BETA/plugin/src/F4TSettlementRaidClient.cpp"
    REMOTE = "CO-BETA/plugin/src/F4TRemotePlayerState.cpp"
    DEFENSES = "CO-SERVER/server-cpp/src/ServerWorkshopDefenses.h"
    POWER = "CO-SERVER/server-cpp/src/ServerWorkshopPower.h"
    RAIDS = "CO-SERVER/server-cpp/src/ServerWorkshopRaids.h"


    client = read(CLIENT)
    assert "} else if (a_type == \"zoneAttackState\") {" in client
    assert "} else if (a_type == \"defenderEvent\") {" in client
    assert "NoteAttackState(*zoneId, *underAttack);" in client
    assert "F4T::RemotePlayerState::GetAssignedPlayerId();" in client
    assert "ApplyServerDamageToLocalPlayer(damageValue);" in client
    assert "RaidRefusalFor(a_zoneId, session.profileId, a_unixSeconds, a_monotonicSeconds)" in read(DEFENSES)
    assert "NewPlayerProtected" not in read(RAIDS) and "ShieldRefusalFor(a_buildZoneId, a_unixSeconds)" in read(RAIDS)
    assert "{ \"targetPlayerId\", target->playerId }," in read(DEFENSES)
    assert "settlementAccess.CanBuild(a_buildZoneId, peer.profileId)" in read(POWER)
    assert "void ApplyServerDamageToLocalPlayer(float a_damage)" in read(REMOTE)


_SECTIONS = [
    ("tests/audit/test_workshop_piece_table_contract.py", _section_002_test_workshop_piece_table_contract),
    ("tests/audit/test_workshop_raid_sender_contract.py", _section_003_test_workshop_raid_sender_contract),
    ("tests/audit/test_workshop_repair_contract.py", _section_004_test_workshop_repair_contract),
    ("tests/audit/test_workshop_attack_notice_contract.py", _section_005_test_workshop_attack_notice_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_workshop_settlement_contracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
