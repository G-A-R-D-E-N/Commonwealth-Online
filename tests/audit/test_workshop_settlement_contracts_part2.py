#!/usr/bin/env python3
"""test_workshop_settlement_contracts_part2: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_workshop_settlement_contracts_part2.py
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




def _section_005_test_workshop_server_contract():
    """Contract moved verbatim from tests/audit/test_workshop_server_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_workshop_server_contract.py")
    import json
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    IMPL = "CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc"
    SETUP = "CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc"
    TRANSACTIONS = "co-core/include/F4TWorkshopTransactions.h"
    PROTOCOL = "co-core/src/protocol/F4TWorkshopProtocol.cpp"
    SAMPLE = "CO-SERVER/server-cpp/workshop-zones.sample.json"
    STRUCTURE = "CO-SERVER/server-cpp/src/ServerWorkshopStructure.h"
    RAIDS = "CO-SERVER/server-cpp/src/ServerWorkshopRaids.h"
    DEFENSES = "CO-SERVER/server-cpp/src/ServerWorkshopDefenses.h"
    HEALTH = "CO-SERVER/server-cpp/src/ServerWorkshopHealth.h"
    PROTOCOL_IMPL = "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc"
    ADMIN = "CO-SERVER/server-cpp/src/ServerAdminMenu.cpp"
    RAID_HEADER = "co-core/include/F4TWorkshopRaids.h"
    RAID_CORE = "co-core/src/world/F4TWorkshopRaids.cpp"
    RAID_PROTOCOL = "co-core/src/protocol/F4TWorkshopRaidProtocol.cpp"
    SETTLERS = "co-core/src/world/F4TWorkshopSettlers.cpp"
    CAMPS = "CO-SERVER/server-cpp/src/ServerWorkshopCamps.h"
    POWER = "CO-SERVER/server-cpp/src/ServerWorkshopPower.h"
    CAMP_CORE = "co-core/src/world/F4TWorkshopCamps.cpp"
    POWER_CORE = "co-core/src/world/F4TWorkshopPower.cpp"
    PIECES_CORE = "co-core/src/world/F4TWorkshopPieces.cpp"
    UPKEEP_CORE = "co-core/src/world/F4TWorkshopUpkeep.cpp"
    CAMP_HEADER = "co-core/include/F4TWorkshopCamps.h"
    PIECES_HEADER = "co-core/include/F4TWorkshopPieces.h"
    POWER_HEADER = "co-core/include/F4TWorkshopPower.h"
    SETTLEMENT_TESTS = "co-core/tests/F4TWorkshopSettlementTests.cpp"
    XMAKE_TESTS = "co-core/xmake-tests.lua"
    SESSION_TYPES = "CO-SERVER/server-cpp/src/ServerSessionTypes.h"
    SETTLEMENT_ACCESS = "CO-SERVER/server-cpp/src/ServerSettlementAccess.cpp"
    SETTLEMENT_ACCESS_HEADER = "CO-SERVER/server-cpp/src/ServerSettlementAccess.h"
    WORLD_POLICY = "CO-SERVER/server-cpp/src/ServerWorldPolicy.inc"
    TOUCHED = [
        IMPL, SETUP, TRANSACTIONS, PROTOCOL, STRUCTURE, RAIDS, DEFENSES, HEALTH, PROTOCOL_IMPL, ADMIN,
        RAID_HEADER, RAID_CORE, RAID_PROTOCOL, SETTLERS,
        CAMPS, POWER, CAMP_CORE, POWER_CORE, PIECES_CORE, UPKEEP_CORE,
        CAMP_HEADER, PIECES_HEADER, POWER_HEADER, SETTLEMENT_TESTS,
        "CO-SERVER/server-cpp/tests/ServerWorkshopTests.cpp",
        "co-core/tests/F4TWorkshopTransactionsTests.cpp",
        "co-core/tests/F4TWorkshopProtocolTests.cpp",
        "co-core/tests/F4TWorkshopRaidsTests.cpp",
        "co-core/tests/F4TWorkshopSettlersTests.cpp",
    ]


    def test_production_registers_zones_from_the_zone_file():
        setup = read(SETUP)
        assert 'protocol.ConfigureWorkshop(persistence, baseDirectory / "workshop-zones.json")' in setup
        impl = read(IMPL)
        assert "ParseBuildZones(parsed)" in impl
        assert "EnableWorkshopPersistence(a_persistence)" in impl


    def test_sample_zone_file_has_the_required_fields():
        sample = json.loads(read(SAMPLE))
        assert sample["version"] == 1
        required = {"buildZoneId", "space", "centerX", "centerY", "centerZ", "radius", "maxScale", "maxObjects"}
        assert sample["zones"]
        for zone in sample["zones"]:
            assert required <= set(zone)
            assert zone["buildZoneId"] > 0 and zone["radius"] > 0 and zone["maxScale"] > 0 and zone["maxObjects"] > 0


    def test_commits_reach_the_zone_and_stale_guests_get_a_snapshot():
        impl = read(IMPL)
        assert "peer.workshopZonesInside.contains(request->buildZoneId)" in impl
        assert "ApplyStatus::StaleRevision" in impl
        stale = impl[impl.index("ApplyStatus::StaleRevision"):]
        assert "TakeToken" in stale[:400] and "WorkshopSnapshotFrames(a_connectionId, zone->second)" in stale[:700]


    def test_hello_snapshots_only_cover_the_current_position():
        impl = read(IMPL)
        body = impl[impl.index("ExistingWorkshopSnapshotFrames"):]
        body = body[:body.index("WorkshopEntryFrames")]
        assert "if (!a_session.lastMovement) return frames;" in body and "zone.WithinRadius(position.x" in body


    def test_form_id_range_and_tombstone_quota():
        transactions = read(TRANSACTIONS)
        assert "MaximumLocalFormId = 0x00FFFFFF" in transactions
        assert "MaximumTombstonesPerProfile = 256" in transactions
        assert "a_localFormId > MaximumLocalFormId" in transactions
        assert "ownedByProfile >= MaximumTombstonesPerProfile" in transactions
        assert "IsRuntimeFormId" not in transactions


    def test_zone_state_is_saved_before_the_live_zone_is_replaced():
        impl = read(IMPL)
        save = "SaveZoneWithPower(*applied"
        swap = "zone->second = std::move(*applied);"
        assert save in impl and swap in impl
        assert impl.index(save) < impl.index(swap)
        assert "MakeZoneState" in impl and "RestoreZoneState" in impl
        assert "MakeZoneState" in read(PROTOCOL)


    def test_touched_files_stay_under_the_line_limit():
        for name in TOUCHED:
            assert len(read(name).splitlines()) < 1000, name


    def between(text, start, end):
        begin = text.index(start)
        return text[begin:text.index(end, begin)]


    def test_structure_hit_uses_the_explosive_table_and_a_cooldown():
        structure = read(STRUCTURE)
        assert "F4T::WorkshopRaids::ExplosiveDamage(hit->explosive)" in structure
        assert "hit->damage" not in structure and "explosiveKind" not in structure
        assert "StructureHitDamageAllowed" not in structure and "NoteProfileSeen" not in structure
        assert "workshopStructureHitReady" in structure and "StructureHitIntervalSeconds" in structure
        assert "OwnerLocalHourForZone(a_buildZoneId, a_unixSeconds)" in read(RAIDS) and "RaidRefusalFor(hit->buildZoneId, a_session.profileId, unixSeconds, a_monotonicSeconds)" in structure
        protocol = read("co-core/include/F4TWorkshopProtocol.h")
        assert "WorkshopRaids::ExplosiveKind explosive{};" in protocol and "explosiveKind" not in protocol
        assert 'a_packet.find("explosive")' in read(PROTOCOL)


    def test_first_seen_is_stamped_at_verified_bind_and_persisted():
        admin = read(ADMIN)
        bind = between(admin, "void ProtocolHost::Impl::BindSessionProfileVerified", "void ProtocolHost::Impl::BindSessionSteamId")
        assert "NoteProfileSeen(a_profileId" in bind
        defenses = read(DEFENSES)
        assert 'workshopStored["profileFirstSeen"]' in defenses and "void LoadProfileFirstSeenFromStore()" in defenses
        assert "LoadProfileFirstSeenFromStore();" in read(IMPL)
        settler = between(defenses, "std::vector<OutboundFrame> HandleSettlerAssign(", "F4T::WorkshopSettlers::Table workshopSettlers;")
        assert "NoteProfileSeen" not in settler


    def test_raid_declarations_expire_and_reset_per_utc_day():
        raids = read(RAIDS)
        assert "RaidDeclarationLive(pending, a_monotonicSeconds)" in raids
        assert "zoneId == declare->buildZoneId" in raids
        assert "UtcDayIndex(static_cast<std::uint64_t>(a_unixTimeSeconds))" in raids
        assert "workshopRaidDeclareCounts.clear();" in raids
        health = read(HEALTH)
        assert "std::erase_if(workshopRaidDeclarations" in health
        assert "std::erase_if(workshopStructureHitReady" in health
        assert "a_monotonicSeconds, a_unixTimeSeconds)" in read(PROTOCOL_IMPL)


    def test_settlement_windows_use_the_owner_timezone_and_persist():
        raids = read(RAIDS)
        assert "F4T::WorkshopRaids::OwnerLocalHour(a_unixSeconds, offsetHours)" in raids
        assert "return found->second.windowStartHour;" not in raids
        assert "window->timezoneOffsetHours" in raids and 'workshopStored["raidWindows"]' in raids
        assert "LoadRaidWindowsFromStore();" in read(IMPL)
        assert "windowDurationHours >= 24.0F" in read(RAID_CORE)
        assert '"timezoneOffsetHours"' in read(RAID_PROTOCOL)


    def test_settler_ids_are_minted_and_assignments_stay_in_the_zone():
        settler = between(read(DEFENSES), "std::vector<OutboundFrame> HandleSettlerAssign(", "std::string_view SettlerCapRefusal(")
        assert "OwnerOf(zoneId) != a_session.profileId" in settler
        assert "MintSettlerId()" not in settler and "CanRecoverSettler(settler)" in settler
        assert "found->buildZoneId != zoneId" in settler
        assert "CanBuild" not in settler
        commit = between(read(DEFENSES), "std::optional<std::vector<OutboundFrame>> CommitSettlerRecruit(", "F4T::WorkshopSettlers::Table workshopSettlers;")
        assert "MintSettlerId()" in commit and "Upsert(settler)" in commit
        assert "SettlerCapRefusal(a_charge.buildZoneId, a_charge.profileId, false)" in commit
        assert 'if (*type == "settlerRecruit") return HandleSettlerRecruit(' in read(PROTOCOL_IMPL)
        table = read(SETTLERS)
        assert "m_nextSettlerId = restored.empty() ? 1 : restored.rbegin()->first + 1;" in table
        assert "newCount" not in table


    def test_repair_is_capped_per_operation_and_placement_counts_objects():
        assert "MaxRepairFractionPerOp = 0.25F" in read(TRANSACTIONS)
        assert "CappedRepairAmount(a_amount, it->second.maxHealth)" in read(TRANSACTIONS)
        impl = read(IMPL)
        assert "zone->second.ObjectCount() >= F4T::WorkshopScale::MaxObjectsPerSettlement" in impl
        assert "Objects().size() >= F4T::WorkshopScale::MaxObjectsPerSettlement" not in impl


    def test_camp_handlers_derive_placement_and_time_on_the_server():
        camps = read(CAMPS)
        protocol = read(PROTOCOL_IMPL)
        assert "HandleCampFound(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)" in protocol
        assert "HandleCampMove(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)" in protocol
        assert "HandleCampStore(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)" in protocol
        assert "CampPlacementFor(a_session, request->space.plugin, request->x, request->y, request->z, a_monotonicSeconds)" in camps
        assert "kMaximumCampPlacementAgeSeconds = 3.0" in read(SESSION_TYPES)
        assert "age > kMaximumCampPlacementAgeSeconds" in camps
        assert "workshopNoBuild.Spaces()" in camps and "!F4T::WorkshopCamps::IsCampZoneId(zoneId)" in camps
        assert "const auto placement = CampPlacementFor(" in camps
        assert "withTime.nowUnix = static_cast<std::uint64_t>(a_unixTimeSeconds);" in camps
        assert "withTime.nowUnix == 0" not in camps
        assert "SaveWorkshopCampsDurable()" in camps


    def test_camp_rollbacks_restore_the_captured_zone():
        camps = read(CAMPS)
        found = between(camps, "std::vector<OutboundFrame> HandleCampFound(", "std::vector<OutboundFrame> HandleCampMove(")
        assert found.index("StageCamp(") < found.index("Store().CommitCamp(") < found.index("if (grant.outcome != F4T::CampGrants::Outcome::Committed)") < found.index("settlementAccess.CommitCamp(")
        rollback = between(found, "if (grant.outcome != F4T::CampGrants::Outcome::Committed)", "auto settlementFrames = settlementAccess.CommitCamp(")
        assert "ApplyCampZone(previousZone);" in rollback and "workshopStored = previousStored;" in rollback
        assert "return { ReliableFrame(" in rollback
        assert camps.count("RestoreSettlement(") >= 3
        assert "bool SettlementAccessService::RestoreSettlement(" in read(SETTLEMENT_ACCESS)
        assert "bool RestoreSettlement(" in read(SETTLEMENT_ACCESS_HEADER)
        assert camps.count("PersistWorkshopCamps();") >= 5 and camps.count("PersistAuthSteam();") >= 5
        assert "previousUpkeep" not in camps and "restoreFailure" not in camps
        assert camps.count("ApplyCampZone(previousZone)") >= 3
        assert "workshopCamps = previousCamps;" in camps


    def test_camp_load_never_probes_or_force_stores_a_valid_camp():
        load = between(read(CAMPS), "void LoadWorkshopCampsFromStore()", "void PersistWorkshopCamps()")
        assert "ValidateFound(" not in load
        assert "record.stored = true;" in load


    def test_camp_store_releases_the_settlement_then_saves_before_erasing_the_zone():
        store = between(read(CAMPS), "std::vector<OutboundFrame> HandleCampStore(", "std::vector<OutboundFrame> HandleCampRemove(")
        release = store.index("SetOwner(FactionPeers(), workshopZones, request->buildZoneId, {}, failure)")
        assert store.index("SaveZoneWithPower(zone->second)") < release < store.index("workshopZones.erase(request->buildZoneId)")
        assert "AppendFrames(frames, std::move(settlementFrames));" in store
        assert store.index("ClearAuthSteam(request->buildZoneId);") < store.index("CommitCampStore(")
        assert "workshopAuthSteamByZone = previousAuth;" in store


    def test_camp_move_relocates_culls_and_keeps_the_space():
        move = between(read(CAMPS), "std::vector<OutboundFrame> HandleCampMove(", "std::vector<OutboundFrame> HandleCampStore(")
        assert "placement->space.plugin != campIt->space.plugin" in move
        assert move.index("RelocateCenter(withTime.x, withTime.y, withTime.z)") < move.index("candidate.CullAll()")
        assert "FanoutPowerState(withTime.buildZoneId)" in move


    def test_pieces_load_from_the_zone_folder_and_fill_placed_objects():
        impl = read(IMPL)
        assert 'LoadWorkshopPieces(a_zonesFile.parent_path() / "workshop-pieces.json");' in impl
        assert "FillPieceSpec(*request);" in impl
        assert "placed = workshopPieces.Find(request->form).power;" in impl
        assert "void LoadWorkshopPieces(const std::filesystem::path& a_path)" in read(POWER)
        assert "lists no pieces" in read(POWER) and "is missing; pieces use wood defaults." in read(POWER)


    def test_power_nodes_are_pruned_on_every_save_and_health_tick():
        impl = read(IMPL)
        assert "PrunePowerNodes(a_zone);" in read(POWER)
        assert read(HEALTH).count("PrunePowerNodes(entry.second);") >= 2
        assert "SaveZoneWithPower(*applied, placed)" in impl
        assert "RemoveNode(result" not in impl


    def test_settlement_owner_records_the_camp_kind():
        assert "IsCampZoneId(a_buildZoneId) ? F4T::SettlementAccess::SettlementKind::Camp" in read(IMPL)


    def test_power_state_fans_out_on_placement_scrap_and_structure_loss():
        assert "AppendFrames(frames, FanoutPowerState(request->buildZoneId));" in read(IMPL)
        assert "AppendFrames(frames, FanoutPowerState(hit->buildZoneId));" in read(STRUCTURE)


    def test_structure_damage_uses_the_piece_tier():
        structure = read(STRUCTURE)
        assert "F4T::WorkshopMaterials::DamageForHit(" in structure
        assert "workshopPieces.Find(object->form).tier" in structure
        assert "F4T::WorkshopRaids::ExplosiveDamage(hit->explosive)" in structure


    def test_tombstone_restore_rejects_profileless_entries_and_evicts_zero_time():
        upkeep = read(UPKEEP_CORE)
        assert "tombstone.profileId.empty()" in upkeep
        assert "destroyedUnixSeconds == 0" not in upkeep


    def test_windowed_capability_follows_the_configured_raid_mode():
        policy = read(WORLD_POLICY)
        start = policy.index("void AppendOptionalCapabilities(")
        body = policy[start:policy.index("bool EnableFactions(", start)]
        assert 'if (workshopRaidConfig.mode == F4T::WorkshopRaids::RaidMode::Windowed) a_capabilities.push_back("settlement-raid-windowed-v1");' in body
        assert "protocol.SetWorkshopRaidConfig(options.workshopRaid);" in read(SETUP)
        assert "workshopRaidConfig = a_config;" in read(IMPL)
        assert "BuildCapabilities()" in read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")


    def test_power_and_upkeep_states_have_plugin_receivers():
        replication = read("CO-BETA/plugin/src/F4TWorkshopReplicationClient.cpp")
        access = read("CO-BETA/plugin/src/F4TSettlementAccessClient.cpp")
        header = read("co-core/include/F4TWorkshopProtocol.h")
        inbound = between(replication, "static bool IsInboundType(std::string_view a_type)", "static bool DispatchInbound(")
        assert 'a_type == "powerState"' in inbound
        assert "return HandlePowerState(a_packet);" in replication
        assert "F4T::WorkshopProtocol::ParsePowerState(a_packet)" in replication
        routing = between(access, "bool TryHandlePacket(std::string_view a_type, const Json& a_packet)", "void Reset()")
        assert 'a_type == "upkeepState"' in routing
        assert "HandleUpkeepPacket(a_packet);" in routing
        assert "F4T::WorkshopProtocol::ParseUpkeepState(a_packet)" in access
        assert "ParsePowerState(const nlohmann::json& a_packet)" in header
        assert "ParseUpkeepState(const nlohmann::json& a_packet)" in header


    def test_wire_connect_is_bound_to_the_committed_wire_object():
        replication = read("CO-BETA/plugin/src/F4TWorkshopReplicationClient.cpp")
        raid = read("co-core/src/protocol/F4TWorkshopRaidProtocol.cpp")
        server = read("CO-SERVER/server-cpp/src/ServerWorkshopPower.h")
        assert "QueueWireConnect(zone, commit->buildZoneId, commit->objectId, pending->second.localHandle);" in replication
        assert '{ "wireId", a_wireId }' in read("CO-BETA/plugin/src/F4TWorkshopWireConnect.cpp")
        assert "if (wire.kind == WireOpKind::Connect) {" in raid
        assert "zone->second.Find(wire->wireId) != nullptr" in server
        assert "graph.Connect(wire->fromId, wire->toId, wire->wireId)" in server
        assert "(void)graph.RemoveWire(wireId);" in server


    def test_settlement_unit_tests_are_registered():
        assert 'core_test("CoreWorkshopSettlementTests", "CommonwealthOnlineCoreWorkshopSettlementTests", "F4TWorkshopSettlementTests.cpp")' in read(XMAKE_TESTS)


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"workshop server contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_006_test_workshop_zones_data():
    """Contract moved verbatim from tests/audit/test_workshop_zones_data.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_workshop_zones_data.py")
    import json
    import math
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    EXPECTED_CENTERS = {
        1: (-76967.41, 67777.91, 7529.0),
        2: (46842.82, -4646.61, 575.99),
        3: (17321.81, 13151.21, 1191.91),
        4: (69546.16, 88423.57, 1795.57),
        5: (-6392.31, 46395.61, 1105.74),
        6: (38521.23, 31611.44, 898.89),
        7: (90857.59, 37769.87, 992.44),
        8: (-30583.13, -64083.1, 1463.82),
        9: (55392.0, 47605.3, 731.47),
        10: (-47565.0, 16621.47, 2671.99),
        11: (29790.29, 66834.8, 2156.95),
        12: (-20706.63, -18532.13, 715.8),
        13: (8712.39, -65830.34, 532.82),
        14: (90560.85, 60331.67, 1246.82),
        15: (5143.5, -95302.77, 624.72),
        16: (64136.28, 3531.74, 669.35),
        17: (-45666.39, -4482.37, 1583.14),
        18: (-10832.56, 100275.74, 3856.0),
        19: (-70384.33, 79519.95, 7360.03),
        20: (-79047.53, 89586.51, 7846.0),
        21: (50747.33, 70409.78, 1392.68),
        22: (-35732.77, -91515.23, 1831.88),
        23: (76494.48, -64030.71, 1464.63),
        24: (-40650.17, 60418.43, 3906.36),
        25: (-85924.73, 38877.23, 6650.6),
        26: (6169.99, 46319.42, 499.52),
        27: (-22190.01, 89346.05, 6139.54),
        28: (49563.08, -46795.05, 831.99),
        29: (56640.95, -76524.32, 519.22),
        30: (4067.42, 61930.87, 602.6),
        31: (55640.21, 42610.6, 1061.82),
        32: (-21793.06, 4657.16, 782.0),
        33: (24161.47, 44910.05, 1560.15),
        34: (21882.12, 31539.97, 949.63),
        35: (1666.9, 8097.7, 96.0),
        36: (-2600.0, 1920.0, -128.0),
    }

    EXPECTED_SPACES = {
        1: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        2: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        3: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        4: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        5: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        6: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        7: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        8: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        9: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        10: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        11: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        12: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        13: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        14: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        15: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        16: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        17: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        18: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        19: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        20: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        21: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        22: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        23: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        24: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        25: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        26: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        27: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        28: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        29: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        30: ("DLCCoast.esm", 2831, (-30000.0, 70000.0, -10000.0, 75000.0, -5000.0, 12000.0)),
        31: ("DLCCoast.esm", 2831, (-30000.0, 70000.0, -10000.0, 75000.0, -5000.0, 12000.0)),
        32: ("DLCCoast.esm", 2831, (-30000.0, 70000.0, -10000.0, 75000.0, -5000.0, 12000.0)),
        33: ("DLCCoast.esm", 2831, (-30000.0, 70000.0, -10000.0, 75000.0, -5000.0, 12000.0)),
        34: ("DLCNukaWorld.esm", 10511, (-10000.0, 35000.0, -10000.0, 45000.0, -5000.0, 12000.0)),
        35: ("DLCworkshop03.esm", 3889, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
        36: ("DLCRobot.esm", 2211, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    }


    def check_inside(label, point, bounds):
        assert bounds[0] <= point[0] <= bounds[1], label
        assert bounds[2] <= point[1] <= bounds[3], label
        assert bounds[4] <= point[2] <= bounds[5], label


    sample = json.loads(read("CO-SERVER/server-cpp/workshop-zones.sample.json"))
    assert sample["version"] == 1
    zones = sample["zones"]
    assert len(zones) == len(EXPECTED_CENTERS) == 36

    ids = [zone["buildZoneId"] for zone in zones]
    assert len(set(ids)) == len(ids), "duplicate buildZoneId"
    assert set(ids) == set(EXPECTED_CENTERS), "zone id set changed"

    required = {"buildZoneId", "space", "centerX", "centerY", "centerZ",
                "radius", "workbench", "captureRadius", "maxScale", "maxObjects"}
    LABELS = {
        12: "attributed by marker chain (REFR 1F0711, LCTN 1B8C53, marker 16CA1D)",
        22: "inferred (Farm05 REFR 1E81EA by elimination)",
    }
    EXPECTED_RADII = {
        1: (6946.84, 6946.84),
        2: (3664.17, 3664.17),
        3: (4624.7, 4624.7),
        4: (4472.03, 4472.03),
        5: (3197.94, 3197.94),
        6: (4790.36, 4790.36),
        7: (3866.84, 3866.84),
        8: (4713.99, 4713.99),
        9: (4421.9, 4421.9),
        10: (4013.67, 4013.67),
        11: (4519.43, 4519.43),
        12: (2703.63, 2703.63),
        13: (3021.9, 3021.9),
        14: (4687.26, 4687.26),
        15: (3414.19, 3414.19),
        16: (5090.38, 5090.38),
        17: (3015.88, 3015.88),
        18: (3536.2, 3536.2),
        19: (5288.77, 5288.77),
        20: (9179.26, 9179.26),
        21: (5442.82, 5442.82),
        22: (4735.06, 4735.06),
        23: (14253.3, 14253.3),
        24: (6949.04, 6949.04),
        25: (4878.29, 4878.29),
        26: (4148.75, 4148.75),
        27: (3464.17, 3464.17),
        28: (6434.21, 6434.21),
        29: (5812.6, 5812.6),
        30: (7047.06, 7047.06),
        31: (9164.74, 9164.74),
        32: (5678.65, 5678.65),
        33: (8309.92, 8309.92),
        34: (7636.53, 7636.53),
        35: (23655.05, 23655.05),
        36: (3798.65, 3798.65),
    }
    for zone in zones:
        assert required <= set(zone), zone["buildZoneId"]
        assert zone["name"] and zone["provenance"], zone["buildZoneId"]
        zone_id = zone["buildZoneId"]
        plugin, local_form_id, bounds = EXPECTED_SPACES[zone_id]
        assert zone["space"]["plugin"] == plugin, zone_id
        assert zone["space"]["localFormId"] == local_form_id, zone_id
        for field in ("centerX", "centerY", "centerZ", "radius", "captureRadius", "maxScale"):
            assert math.isfinite(zone[field]), (zone_id, field)
        assert zone["radius"] > 0, zone_id
        assert zone["radius"] < 60000, zone_id
        assert zone["captureRadius"] >= 0, zone_id
        assert zone["maxScale"] > 0 and zone["maxObjects"] > 0, zone_id
        expected = EXPECTED_CENTERS[zone_id]
        actual = (zone["centerX"], zone["centerY"], zone["centerZ"])
        assert all(math.isclose(a, e, abs_tol=0.005) for a, e in zip(actual, expected)), zone_id
        check_inside(("center", zone_id), actual, bounds)
        bench = zone["workbench"]
        bench_point = (bench["x"], bench["y"], bench["z"])
        assert all(math.isfinite(v) for v in bench_point), zone_id
        check_inside(("workbench", zone_id), bench_point, bounds)
        expected_radii = EXPECTED_RADII[zone_id]
        actual_radii = (zone["radius"], zone["captureRadius"])
        assert all(math.isclose(a, e, abs_tol=0.005) for a, e in zip(actual_radii, expected_radii)), zone_id
        assert all(math.isclose(b, c, abs_tol=0.005) for b, c in zip(bench_point, actual)), zone_id

    names = {zone["buildZoneId"]: zone["name"] for zone in zones}
    provenance = {zone["buildZoneId"]: zone["provenance"] for zone in zones}
    assert names[12] == "Hangman's Alley" and names[22] == "Somerville Place"
    assert {zone_id: provenance[zone_id] for zone_id in LABELS} == LABELS

def _section_007_test_camp_kit_contract():
    """Contract moved verbatim from tests/audit/test_camp_kit_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_camp_kit_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    def function_body(text, signature):
        start = text.index(signature)
        indent = text[text.rfind("\n", 0, start) + 1:start]
        assert indent.strip() == "", signature
        return text[start:text.index("\n" + indent + "}\n", start)]


    RUNTIME = "CO-BETA/plugin/src/F4TReplicationRuntime.cpp"


    def test_runtime_drains_container_changes_into_the_camp_kit_quest():
        runtime = read(RUNTIME)
        assert "F4T::ContainerChangedHub::Drain(OnContainerChanged);" in runtime
        assert "F4T::ContainerChangedHub::Drain();" not in runtime
        assert runtime.index("F4T::ContainerChangedHub::Install();") < runtime.index("F4T::ContainerChangedHub::Drain(OnContainerChanged);")


    def test_camp_kit_quest_advances_only_for_the_player_inventory_add():
        runtime = read(RUNTIME)
        body = function_body(runtime, "void OnContainerChanged(const F4T::ContainerChangedHub::ContainerChange& a_change)")
        assert "a_change.itemCount <= 0" in body and "a_change.newContainer != player->GetFormID()" in body
        assert body.index("a_change.newContainer != player->GetFormID()") < body.index("F4T::QuestKey::FromFormId(a_change.baseObj")
        assert "F4T::WorkshopReplicationClient::IsCampKitForm({ key->plugin, key->localId })" in body
        assert 'constexpr std::string_view kWelcomeQuestEditorId = "CO_WelcomeQuest";' in runtime
        assert "RE::TESForm::GetFormByEditorID<RE::TESQuest>(RE::BSFixedString{ kWelcomeQuestEditorId })" in body
        assert "constexpr std::uint16_t kCampKitCompleteStage = 20;" in runtime
        assert "quest->currentStage >= kCampKitCompleteStage" in body
        assert "quest->SetStage(kCampKitCompleteStage)" in body


    def test_welcome_quest_starts_once_then_sets_the_objective_before_completion():
        runtime = read(RUNTIME)
        body = function_body(runtime, "void EnsureWelcomeQuest()")
        assert "constexpr std::uint16_t kObjectiveStage = 10;" in runtime
        assert "constexpr std::chrono::seconds kWelcomeQuestRetryInterval{ 1 };" in runtime
        assert "constexpr std::chrono::seconds kWelcomeQuestBackoff{ 5 };" in runtime
        assert "!IsInWorld()" in body
        assert "RE::TESForm::GetFormByEditorID<RE::TESQuest>(RE::BSFixedString{ kWelcomeQuestEditorId })" in body
        assert body.index("quest->currentStage >= kCampKitCompleteStage") < body.index("quest->SetStage(kObjectiveStage)")
        assert body.index('"startquest CO_WelcomeQuest"') < body.index("quest->SetStage(kObjectiveStage)")
        assert body.count('"startquest CO_WelcomeQuest"') == 1
        assert body.count("quest->SetStage(kObjectiveStage)") == 1
        assert body.index("g_welcome.nextAttempt = now + kWelcomeQuestBackoff;") > body.index("quest->SetStage(kObjectiveStage)")
        assert body.count("REX::WARN(") == 1 and "failureLogged" in body
        sync_script = (ROOT / "CO-BETA/creation-kit/scripts/source/COQuestSyncScript.psc").read_text(encoding="utf-8")
        assert "DisplayWelcomeObjective()" in sync_script[sync_script.index("Event OnTimer"):sync_script.index("Function ReportQuestStates")]
        assert "welcome.SetObjectiveDisplayed(10, True)" in sync_script
        assert "cqf CO_WelcomeQuest SetObjectiveDisplayed" not in body
        assert runtime.count("EnsureWelcomeQuest();") == 1
        assert runtime.index("F4T::ContainerChangedHub::Drain(OnContainerChanged);") < runtime.index("EnsureWelcomeQuest();")
        assert "ResetWelcomeQuest();" in runtime[runtime.index("F4T::QuestSync::BeginSettle();"):runtime.index("F4T::Networking::UpdateConnectionMaintenance();")]


    def test_welcome_quest_does_not_read_the_flags_for_running_state():
        runtime = read(RUNTIME)
        assert "data.flags" not in function_body(runtime, "void EnsureWelcomeQuest()")
        assert "RE::UI::GetSingleton()" in function_body(runtime, "bool IsInWorld()")
        assert "player->GetParentCell()" in function_body(runtime, "bool IsInWorld()")


    HOOKS = "CO-BETA/plugin/src/F4TInventoryHooks.cpp"
    REPLICATION = "CO-BETA/plugin/src/F4TWorkshopReplicationClient.cpp"
    CAMP_INBOUND = "CO-BETA/plugin/src/F4TWorkshopCampInbound.cpp"
    PICKUP_GATE = "CO-BETA/plugin/src/F4TInventoryPickupGate.cpp"
    COMMIT_HOOKS = "CO-BETA/plugin/src/F4TWorkshopCommitHooks.cpp"
    CAMP_STORE = "CO-BETA/plugin/src/F4TWorkshopCampStore.cpp"
    CAMPS = "CO-SERVER/server-cpp/src/ServerWorkshopCamps.h"
    TRANSACTIONS = "co-core/include/F4TWorkshopTransactions.h"


    def between(text, start, end):
        begin = text.index(start)
        return text[begin:text.index(end, begin)]


    def test_pickup_hook_routes_a_bound_camp_kit_to_camp_found_before_the_hold():
        hooks = read(HOOKS)
        body = function_body(hooks, "void PickUpObjectHook(")
        predicate = function_body(hooks, "bool CampKitPickupStarted(RE::Actor* a_actor, RE::TESObjectREFR* a_reference)")
        assert "a_actor != RE::PlayerCharacter::GetSingleton()" in predicate
        assert "F4T::DropMirrorRefs::IsMirrored(*entityId)" in predicate
        assert "F4T::InventorySync::GetStatus().sessionActive" in predicate
        assert "F4T::InventoryPickupGate::FindBoundEntity(a_reference->GetFormID())" in predicate
        assert "F4T::WorkshopReplicationClient::SubmitCampFound(*a_reference, *entityId)" in predicate
        assert "F4T::WorkshopReplicationClient::IsCampKitRef(*a_reference)" in predicate
        assert "F4T::InventoryPickupGate::TakeCampRefusal(*entityId)" in predicate
        assert "cell->IsInterior()" in predicate and "F4T::InventoryPickupGate::RefuseCamp(*entityId)" in predicate
        assert body.index("CampKitPickupStarted(a_this, a_objREFR)") < body.index("F4T::InventoryPickupGate::Hold(")
        assert "CampKitPickupStarted(a_this, a_objREFR)" in body[:body.index("F4T::InventoryPickupGate::Hold(")]
        assert body.index("CampKitPickupStarted(a_this, a_objREFR)") < body.index("original(a_this, a_objREFR, a_count, a_playPickUpSounds);")


    def test_pickup_gate_exposes_the_bound_entity_under_its_lock():
        gate = read(PICKUP_GATE)
        body = function_body(gate, "std::optional<F4T::Drops::EntityId> FindBoundEntity(std::uint32_t a_formId)")
        assert "const std::scoped_lock lock(g_mutex);" in body
        assert "FindBound(a_formId)" in body


    def test_a_refused_camp_lets_the_next_pickup_through_once():
        gate = read(PICKUP_GATE)
        body = function_body(gate, "bool TakeCampRefusal(F4T::Drops::EntityId a_entityId)")
        assert "const std::scoped_lock lock(g_mutex);" in body
        assert "g_campRefused.erase(a_entityId) > 0" in body


    def test_camp_found_carries_the_drop_entity_and_no_interior_flag():
        body = function_body(read("CO-BETA/plugin/src/F4TWorkshopCampFoundSubmit.cpp"), "bool SubmitCampFound(const RE::TESObjectREFR& a_ref, std::uint32_t a_dropEntityId)")
        assert '{ "dropEntityId", a_dropEntityId }' in body
        assert '"interior"' not in body
        assert "PendingCamp{ a_dropEntityId, HandleOf(a_ref)" in body


    def test_rejected_camp_keeps_the_kit_and_timed_out_camps_are_forgotten():
        client = read(REPLICATION)
        rejected = function_body(read(CAMP_INBOUND), "bool HandleCampRejected(const nlohmann::json& a_packet)")
        assert "Despawn(" not in rejected
        assert "ResolvePendingCamp(operationId, false);" in rejected
        assert "F4T::InventoryPickupGate::RefuseCamp(pending->second.dropEntityId);" in function_body(read("CO-BETA/plugin/src/F4TWorkshopCampFoundSubmit.cpp"), "bool ResolvePendingCamp(std::uint64_t a_operationId, bool a_placed, std::optional<Workshop::BuildZoneId> a_zone)")
        tick = function_body(client, "void Tick()")
        assert "RefuseCamp(a_entry.second.dropEntityId);" in tick
        assert "std::erase_if(g_pendingCamps" in tick and "kPendingTimeout" in tick


    def test_placing_a_camp_kit_sends_nothing_to_the_server():
        commit = read(COMMIT_HOOKS)
        body = function_body(commit, "bool PlaceHook(const RE::Workshop::ContextData& a_context)")
        assert "!F4T::WorkshopReplicationClient::IsCampKitRef(*placed)" in body
        assert "SubmitCampFound" not in body


    ENTRY_HOOK = "CO-BETA/plugin/src/F4TWorkshopEntryHook.cpp"
    CAMP_GROUND = "CO-BETA/plugin/src/F4TCampGround.cpp"
    CLIENT_HEADER = "CO-BETA/plugin/include/F4TWorkshopReplicationClient.h"


    def test_camp_store_is_sneak_gated_with_a_throttled_hint_and_queues_once():
        store = read(CAMP_STORE)
        body = function_body(store, "bool SubmitCampStore(const RE::TESObjectREFR& a_workbench, bool a_sneaking)")
        hint = body.index('"Sneak + activate to pack up your camp."')
        packing = body.index('"Packing up your camp..."')
        assert body.index("if (!a_sneaking) {") < hint < packing
        assert "std::chrono::seconds(10)" in body[:hint] and "return false;" in body[:hint]
        assert "placed->objectId != F4T::WorkshopRaids::WorkbenchObjectId" in body
        assert "g_ownCamps.contains(placed->buildZoneId)" in body
        assert packing < body.index("std::any_of(g_pendingCampStores") < body.index("g_pendingCampStores[operationId] = ") < body.index('{ "type", "campStore" }')
        assert "bool SubmitCampStore(const RE::TESObjectREFR& a_workbench, bool a_sneaking);" in read(CLIENT_HEADER)


    def test_camp_store_refusals_resolve_only_store_operations_with_their_own_wording():
        resolve = function_body(read(CAMP_STORE), "bool ResolvePendingCampStore(std::uint64_t a_operationId, std::optional<Workshop::BuildZoneId> a_zone)")
        assert "pending->second.buildZoneId != *a_zone" in resolve and "g_pendingCampStores.erase(pending)" in resolve
        rejected = function_body(read(CAMP_INBOUND), "bool HandleCampRejected(const nlohmann::json& a_packet)")
        assert rejected.index("ResolvePendingCampStore(operationId)") < rejected.index("ResolvePendingCamp(operationId, false);")
        assert "CampStoreRefusalNotice(status) : removeRefused ? CampRemoveRefusalNotice(status) : CampRefusalNotice(status, distance, required, subject)" in rejected
        assert "Could not pack up the camp" in read(CAMP_INBOUND)


    def test_entry_hook_offers_the_bench_to_the_camp_store_before_the_settlement_window():
        hook = read(ENTRY_HOOK)
        body = function_body(hook, "void WorkshopEntryDetour(RE::TESObjectREFR* a_workbench) noexcept")
        store = body.index("SubmitCampStore(*a_workbench, player && player->IsSneaking())")
        assert '#include "RE/P/PlayerCharacter.h"' in hook
        assert "auto* player = RE::PlayerCharacter::GetSingleton();" in body[:store]
        assert "return;" in body[store:body.index("CannotBuildAtWorkbench(")]
        assert store < body.index("CannotBuildAtWorkbench(") < body.index("s_entryHook.hook->original<WorkshopEntryFn>()(a_workbench);")


    def test_nearby_workshop_prompt_never_packs_up_a_camp():
        hook = read(ENTRY_HOOK)
        body = function_body(hook, "void WorkshopEntryDetour(RE::TESObjectREFR* a_workbench) noexcept")
        assert "kStartNearbyWorkshop{ 937340, 2194880, 2194880 }" in hook
        assert "kStartNearbyEntryReturnOffset{ 0x41 }" in hook
        assert "_ReturnAddress()" in body and "kStartNearbyWorkshop.address() + kStartNearbyEntryReturnOffset" in body
        assert body.index("fromNearbyPrompt") < body.index("SubmitCampStore(") and "!fromNearbyPrompt && F4T::WorkshopReplicationClient::SubmitCampStore(" in body


    def test_owned_camp_bench_is_claimed_for_own_camps_only_and_ticked_by_the_client():
        store = read(CAMP_STORE)
        claim = function_body(store, "bool SetBenchOwnedByPlayer(RE::TESObjectREFR& a_bench)")
        assert 'FindBoundObject(handle, "WorkshopScript", false, object, false)' in claim
        assert 'DispatchMethodCall(static_cast<std::uint64_t>(handle), "WorkshopScript", "SetOwnedByPlayer", callback, true)' in claim
        tick = function_body(store, "void TickCampStores()")
        assert "zone->second.campBenchOwned" in tick and "SetBenchOwnedByPlayer(*ref)" in tick
        assert tick.index("SetBenchOwnedByPlayer(*ref)") < tick.index("zone->second.campBenchOwned = true;")
        assert "for (const auto zoneId : g_ownCamps)" in tick
        client = read(REPLICATION)
        assert "TickCampStores();" in function_body(client, "void Tick()")
        assert "a_zone.campBenchOwned = false;" in function_body(client, "void SpawnCampWorkbench(ZoneState& a_zone)")
        assert "ForgetCampStores();" in function_body(client, "void Reset()")


    def test_reactivated_camp_culls_every_piece_and_refunds_half():
        camps = read(CAMPS)
        found = between(camps, "std::vector<OutboundFrame> HandleCampFound(", "std::vector<OutboundFrame> CampMoveReplayFrames(")
        assert "reactivating ? zone->second.CullAll() : zone->second.CullOutsideRadius(withTime.nowUnix)" in found
        assert "HalfRefund(workshopCosts, culled)" in found
        assert "CountOutsideRadius" not in camps and "keptPieces" not in camps
        assert "CountOutsideRadius" not in read(TRANSACTIONS)
        assert found.index("CullAll()") < found.index("Store().CommitCamp(") < found.index("MakeGrantPacket(request->dropEntityId, grant.chunks)")
        assert "withTime.ownerProfileId," in found and "request->dropEntityId, refund.lines);" in found
        assert "keptPieces" not in read(CAMP_INBOUND)


    def test_camp_workbench_is_grounded_before_it_spawns_and_the_probe_stays_on_the_loaded_cell():
        inbound = function_body(read(CAMP_INBOUND), "std::optional<Workshop::ObjectState> ParseCampWorkbench(const nlohmann::json& a_camp)")
        assert "const auto surface = F4T::CampGround::SurfaceAt(workbench.transform.x, workbench.transform.y, z->get<float>());" in inbound
        assert "workbench.transform.z = surface ? surface->z : z->get<float>();" in inbound
        ground = read(CAMP_GROUND)
        assert "cell->IsAttached()" in ground and "return std::nullopt;" in ground and "return a_z;" not in ground
        assert "pick.SetStartEnd(" in ground and "cell->Pick(pick)" in ground and "pick.HasHit()" in ground
        assert "pick.GetHitFraction()" in ground


    def test_camp_ground_probe_sets_a_pick_layer_before_the_cast_and_never_raises_the_bench_far():
        ground = function_body(read(CAMP_GROUND), "std::optional<Surface> SurfaceAt(float a_x, float a_y, float a_z)")
        assert '#include "RE/C/COL_LAYER.h"' in read(CAMP_GROUND)
        layer = ground.index("pick.castQuery.m_filterData.m_collisionFilterInfo = static_cast<std::uint32_t>(RE::COL_LAYER::kDroppingPick);")
        assert ground.index("pick.SetStartEnd(") < layer < ground.index("cell->Pick(pick)")
        assert "if (surface > a_z + kMaxRaiseUnits) {" in ground and "return Surface{" in ground
        assert "constexpr float kMaxRaiseUnits = 32.0F;" in read(CAMP_GROUND)


    QUEST_SYNC = "CO-BETA/creation-kit/scripts/source/COQuestSyncScript.psc"


    def test_register_camp_appends_the_bench_the_way_the_parent_does():
        script = read(QUEST_SYNC)
        start = script.index("Function RegisterCamp()")
        body = script[start:script.index("EndFunction", start)]
        assert 'Game.GetFormFromFile(0x000C1AEB, "Fallout4.esm")' in body
        assert "Game.FindClosestReferenceOfTypeFromRef(" in body
        assert "benchRef as WorkshopScript" in body
        assert "workshopParent = workshop.WorkshopParent" in body
        assert body.index("workshops.Find(workshop) >= 0") < body.index("workshops.Add(workshop)")
        assert body.index("Int slot = locations.Find(campLocation)") < body.index("workshops.Add(workshop)")
        assert body.index("workshops.Add(workshop)") < body.index("workshop.InitWorkshopID(slot)")
        assert "locations.Add(campLocation)" in body
        assert body.index("workshop.myLocation = campLocation") < body.index("workshop.SetOwnedByPlayer(true)")
        assert 'workshop.RegisterForCustomEvent(workshopParent, "WorkshopDailyUpdate")' in body
        assert body.count("Debug.Trace(") == 4
        assert "Int slot = locations.Find(campLocation)" in body and "workshops[slot].IsDeleted()" in body
        assert "workshops[slot] = workshop" in body and "slot = workshops.Length - 1" in body


    CAMP_REGISTRATION = "CO-BETA/plugin/src/F4TCampRegistration.cpp"


    def test_camp_created_branch_requests_the_camp_registration_after_the_spawn():
        client = read(REPLICATION)
        apply = function_body(client, "void ApplyCampWorkbench(const Workshop::ObjectState& a_workbench, bool a_own)")
        assert '#include "F4TCampRegistration.h"' in client
        assert apply.count("F4T::CampRegistration::RequestRegistration(a_workbench.buildZoneId, a_workbench.transform, a_own);") == 1
        assert "MoveRef(*ref, a_workbench.transform)" in apply and "Despawn(zoneState.objects[F4T::WorkshopRaids::WorkbenchObjectId])" in apply
        assert apply.index("SpawnCampWorkbench(zoneState);") < apply.index("F4T::CampRegistration::RequestRegistration(")
        placement = function_body(read(CAMP_INBOUND), "bool HandleCampPlacement(const nlohmann::json& a_packet)")
        assert 'ApplyCampWorkbench(*workbench, a_packet.value("own", false));' in placement


    def test_camp_list_spawns_each_camp_through_the_shared_helper():
        body = function_body(read(CAMP_INBOUND), "bool HandleCampList(const nlohmann::json& a_packet)")
        assert "ParseCampWorkbench(camp)" in body
        assert "ApplyCampWorkbench(*workbench, own);" in body
        assert "F4T::ClientIdentity::ConnectedProfileId()" in body


    def test_camp_workbench_spawns_only_in_its_own_worldspace():
        body = function_body(read(REPLICATION), "void SpawnCampWorkbench(ZoneState& a_zone)")
        assert "SpaceOf(*player) == a_zone.campWorkbench->space" in body
        assert "SpawnRemote(*a_zone.campWorkbench)" in body


    def test_camp_registration_issues_the_register_command_once_through_the_guard():
        camp = read(CAMP_REGISTRATION)
        tick = function_body(camp, "void Tick()")
        assert camp.count('"cqf CO_CharacterSetupQuest RegisterCamp"') == 1
        assert 'F4T::ConsoleGuard::ExecutePluginCommand("cqf CO_CharacterSetupQuest RegisterCamp");' in tick
        assert "RE::Console::ExecuteCommand" not in camp
        assert tick.index("DistanceSquared(player->GetPosition(), it->second.centre) > range") < tick.index("ExecutePluginCommand(")
        assert tick.index("BenchLoadedAt(*cell, it->second.centre)") < tick.index("ExecutePluginCommand(")
        assert tick.index("ExecutePluginCommand(") < tick.index("g_registered.insert(it->first);")


    def test_camp_registration_waits_for_the_zone_and_registers_once_per_session():
        camp = read(CAMP_REGISTRATION)
        tick = function_body(camp, "void Tick()")
        assert tick.index("ZoneRevision(it->first)") < tick.index("ExecutePluginCommand(")
        assert "g_pending.erase(it)" in tick and "g_registered.insert(it->first);" in tick
        assert "if (!a_ownCamp || g_registered.contains(a_buildZoneId))" in function_body(camp, "void RequestRegistration(")
        assert "g_registered.clear();" in function_body(camp, "void Reset()")


    def test_camp_registration_pumps_from_the_runtime_and_resets_with_the_session():
        runtime = read(RUNTIME)
        assert '#include "F4TCampRegistration.h"' in runtime
        assert runtime.count("F4T::CampRegistration::Tick();") == 1
        assert runtime.index("EnsureWelcomeQuest();") < runtime.index("F4T::CampRegistration::Tick();")
        generation = runtime[runtime.index("F4T::QuestSync::BeginSettle();"):runtime.index("F4T::Networking::UpdateConnectionMaintenance();")]
        assert "F4T::CampRegistration::Reset();" in generation


    def test_duplicate_camp_found_is_swallowed_and_refusals_reset_with_the_session():
        submit = function_body(read("CO-BETA/plugin/src/F4TWorkshopCampFoundSubmit.cpp"), "bool SubmitCampFound(")
        assert submit.index("dropEntityId == a_dropEntityId") < submit.index("g_nextOperationId++")
        gate = read("CO-BETA/plugin/src/F4TInventoryPickupGate.cpp")
        assert "g_campRefused.clear();" in function_body(gate, "void ResetRetired()")


    def test_only_the_founder_registers_and_a_stored_camp_is_forgotten():
        server = read("CO-SERVER/server-cpp/src/ServerWorkshopCamps.h")
        assert 'ownCreated["own"] = true;' in server and 'ownMoved["own"] = true;' in server
        client = read(REPLICATION)
        stored = client[client.index('if (a_type == "campStored" || a_type == "campRemoved") {'):client.index('if (a_type == "campList"')]
        assert "F4T::CampRegistration::Forget(buildZoneId);" in stored
        camp = read(CAMP_REGISTRATION)
        forget = function_body(camp, "void Forget(")
        assert "g_registered.erase(a_buildZoneId);" in forget and "g_pending.erase(a_buildZoneId);" in forget


    def test_camp_kit_models_are_fallout_4_nifs():
        import re
        import struct
        meshes = ROOT / "CO-BETA/creation-kit/assets/DataFolder/Meshes"
        printer = meshes / "CommonwealthOnline/CAMP/CO_CampPrinter.nif"
        for nif in [printer, *meshes.glob("CommonwealthOnline/Vault/**/*.nif")]:
            data = nif.read_bytes()
            start = data.index(b"\n") + 1
            _, _, _, _, bs_version = struct.unpack_from("<IBIII", data, start)
            assert bs_version == 130, f"{nif.name} has BS version {bs_version}, Fallout 4 needs 130"
        blob = printer.read_bytes()
        assert b"NiTriShape" not in blob and b"BSLightingShaderProperty" in blob
        assert b"Materials\\CommonwealthOnline\\CAMP\\CO_CampPrinter.bgsm" in blob
        assert blob.count(b"bhkNPCollisionObject") == 1, "the camp kit needs a collision object to be pickable"
        assert blob.count(b"bhkPhysicsSystem") == 1, "the camp kit's collision object needs a physics system"
        assert b"bhkBoxShape" not in blob and b"bhkCollisionObject" not in blob
        assert re.search(rb"CommonwealthOnline.{1,2}CAMP.{1,2}CO_CampPrinter.nif", (ROOT / "CO-BETA/creation-kit/tools/add_camp_kit.py").read_bytes())


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"camp kit contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())


_SECTIONS = [
    ("tests/audit/test_workshop_server_contract.py", _section_005_test_workshop_server_contract),
    ("tests/audit/test_workshop_zones_data.py", _section_006_test_workshop_zones_data),
    ("tests/audit/test_camp_kit_contract.py", _section_007_test_camp_kit_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_workshop_settlement_contracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
