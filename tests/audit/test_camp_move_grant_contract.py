#!/usr/bin/env python3
"""Contract: a camp move culls every piece and refunds half the component materials through a durable acked grant."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def between(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first)]


def test_move_culls_all_and_commits_the_grant_atomically():
    camps = read("CO-SERVER/server-cpp/src/ServerWorkshopCamps.h")
    move = between(camps, "std::vector<OutboundFrame> HandleCampMove(", "std::vector<OutboundFrame> HandleCampStore(")
    assert "CullOutsideRadius" not in move and "CullAll()" in move
    assert move.index("candidate.CullAll()") < move.index("HalfRefund(") < move.index("CommitCampMove(") < move.index("zone->second = std::move(candidate);")
    commit = between(move, "CommitCampMove(", "zone->second = std::move(candidate);")
    assert "rollback();" in commit and "FoundStatus::GrantFailed" in commit
    assert move.index("NormalizeCampMove(") < move.index("FindCampMove(") < move.index("ValidateMove(")
    assert "MakeGrantPacket(withTime.operationId, grant.chunks)" in move


def test_grant_is_resent_until_acked_and_acked_by_the_client():
    assert "CampGrantFrames(a_connectionId)" in read("CO-SERVER/server-cpp/src/ServerProtocolApi.inc")
    assert '*type == "campGrantAck"' in read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
    store = read("CO-SERVER/server-cpp/src/DurableStateFileStoreCamps.cpp")
    assert "MutateProfilesAndPersistLocked({ a_profileId }, kWorkshopSections" in store
    assert "}, true, true);" in store
    client = read("CO-BETA/plugin/src/F4TWorkshopCampGrant.cpp")
    assert "NoteTradeCommitted(chunk.revision, {}, chunk.lines)" in client
    assert client.index("ApplyGrant(grant, a_missing)") < client.index("MakeGrantAck(grant.operationId)")
    assert "g_receipts" in client and "AddLootToContainer" in client


def test_camp_grant_receipts_bracket_the_grant_and_precede_the_ack():
    client = read("CO-BETA/plugin/src/F4TWorkshopCampGrant.cpp")
    assert "F4T::ServerProfileStore::ConnectedWorldId()" in client
    assert "F4T::AppliedOperations::ReadScope(a_scope)" in client
    assert "F4T::CampGrants::FormatWorlds(" in client and "F4T::CampGrants::ParseWorlds(" in client and "F4T::CampGrants::FitWorlds(" in client
    assert client.index("F4T::CampGrants::ApplyOnce(") < client.index("MakeGrantAck(grant.operationId)")
    assert client.index("F4T::CampGrants::MarkLanded(") < client.index("MakeGrantAck(operationId)")
    core = read("co-core/src/world/F4TCampGrants.cpp")
    apply_once = between(core, "ApplyOutcome ApplyOnce(", "\n\t}\n")
    assert apply_once.index("a_apply(missing)") < apply_once.index("a_recount()") < apply_once.index("CountsRose(") < apply_once.index("found->state = ReceiptState::Applied;") < apply_once.index("ApplyOutcome::Hold", apply_once.index("found->state = ReceiptState::Applied;"))


def test_costs_table_is_packaged_and_loaded_beside_the_zone_file():
    assert 'LoadWorkshopCosts(a_zonesFile.parent_path() / "workshop-costs.json");' in read("CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc")
    assert "workshop-costs.json" in read("CO-SERVER/server-cpp/scripts/package-native-release.sh")
    assert "workshop-costs.json" in read("CO-SERVER/server-cpp/scripts/verify-native-package.sh")
    assert (ROOT / "CO-SERVER/server-cpp/data/workshop-costs.json").is_file()


def test_review_fixes_are_in_place():
    camps = read("co-core/include/F4TWorkshopCamps.h")
    assert "noexcept" not in between(camps, "ValidateMove(", ";")
    store = read("CO-BETA/plugin/src/F4TWorkshopCampStore.cpp")
    assert "a_zone && pending->second.buildZoneId != *a_zone" in store
    assert "kMaximumBenchClaimTries" in store and "nextBenchClaimAt" in store
    inbound = read("CO-BETA/plugin/src/F4TWorkshopCampInbound.cpp")
    assert inbound.count('"dropLimit"') == 2 and '"grantFailed"' in inbound
    assert "ResolvePendingCampStore(operation->get<std::uint64_t>(), buildZoneId)" in read("CO-BETA/plugin/src/F4TWorkshopReplicationClient.cpp")
    assert "CampKitDropLimited(a_session.profileId)" in read("CO-SERVER/server-cpp/src/ServerWorkshopCamps.h")


def test_found_reactivation_culls_and_grants_through_one_durable_commit():
    camps = read("CO-SERVER/server-cpp/src/ServerWorkshopCamps.h")
    found = between(camps, "std::vector<OutboundFrame> HandleCampFound(", "std::vector<OutboundFrame> CampMoveReplayFrames(")
    assert "reactivating ? zone->second.CullAll() : zone->second.CullOutsideRadius(withTime.nowUnix)" in found
    assert "HalfRefund(workshopCosts, culled)" in found
    assert "CountOutsideRadius" not in camps and "keptPieces" not in camps
    assert "withTime.ownerProfileId," in found and "request->dropEntityId, refund.lines);" in found
    assert "MakeGrantPacket(request->dropEntityId, grant.chunks)" in found
    store = read("CO-SERVER/server-cpp/src/DurableStateFileStoreCamps.cpp")
    commit = between(store, "F4T::CampGrants::Commit DurableStateFileStore::CommitCamp(", "F4T::CampGrants::Commit DurableStateFileStore::CommitCampMove(")
    assert commit.index("CommitMove(a_root") < commit.index("live.erase(") < commit.index('a_root["workshop"] = a_workshop;')
    assert "MutateProfilesAndPersistLocked({ a_profileId }, kCampSections" in store
    assert "kCampSections[] = { kDropsSection, kWorkshopSection, kSettlementAccessSection }" in read("CO-SERVER/server-cpp/src/DurableStateSections.h")
    assert "DurableStateFileStore::CommitCamp(" not in read("CO-SERVER/server-cpp/src/DurableStateFileStore.cpp")


def test_camp_cooldown_and_probe_gate_found_move_and_reactivation():
    core = read("co-core/src/world/F4TWorkshopCamps.cpp")
    assert "bool MovedRecently(const CampRecord& a_camp, std::uint64_t a_nowUnix) noexcept" in core
    assert "if (MovedRecently(camp, a_request.nowUnix)) {" in core
    move = between(core, "FoundStatus ValidateMove(", "Workshop::BuildZonePolicy MakeCampPolicy(")
    assert move.index("if (a_camp.stored) {") < move.index("if (a_underAttack) {") < move.index("if (MovedRecently(a_camp, a_request.nowUnix)) {")
    assert "if (MovedRecently(a_camp, a_request.nowUnix)) {" in move
    assert "std::fabs(a_request.z - a_request.probe.height) > MaximumProbeDropUnits" in core
    assert "MaximumProbeDropUnits" in read("co-core/include/F4TWorkshopCamps.h")


def test_acked_grant_records_outlive_the_pending_cap():
    header = read("co-core/include/F4TCampGrants.h")
    assert "kMaximumRecords = 32" in header and "kMaximumAckedRecords = 512" in header
    core = read("co-core/src/world/F4TCampGrants.cpp")
    assert "IsAckedRecord" in core and "kMaximumAckedRecords" in core and "if (pending >= kMaximumRecords) return false;" in core
    assert "TestAckedReplayWindowOutlivesPendingCap" in read("co-core/tests/F4TCampGrantTests.cpp")


def test_power_costs_messages_are_single_line_escapes():
    power = read("CO-SERVER/server-cpp/src/ServerWorkshopPower.h")
    assert 'keeping the previous table.\\n";' in power
    assert 'lists no prices; camp moves and camp reactivations are refused.\\n";' in power
    assert 'is missing; camp moves and camp reactivations are refused.\\n";' in power


def test_camp_move_and_reactivation_refuse_when_no_refund_prices_are_loaded():
    camps = read("CO-SERVER/server-cpp/src/ServerWorkshopCamps.h")
    assert camps.count("workshopCosts.Size() == 0") == 2
    assert camps.count("F4T::WorkshopCamps::FoundStatus::RefundUnavailable") == 2
    assert "case FoundStatus::RefundUnavailable:" in read("co-core/src/world/F4TWorkshopCamps.cpp")
    assert '{ "refundUnavailable", "Camp refused:' in read("CO-BETA/plugin/src/F4TWorkshopCampInbound.cpp")


def test_client_camp_senders_and_refusal_text():
    inbound = read("CO-BETA/plugin/src/F4TWorkshopCampInbound.cpp")
    assert '{ "notFound", "Camp refused: that camp is packed up or no longer on the server." }' in inbound
    assert "std::optional<Surface> SurfaceAt(" in read("CO-BETA/plugin/src/F4TCampGround.cpp")
    assert "return std::nullopt;" in read("CO-BETA/plugin/src/F4TCampGround.cpp")
    assert "keptPieces" not in inbound
    sender = read("CO-BETA/plugin/src/F4TWorkshopCampMoveSubmit.cpp")
    assert '{ "type", "campMove" }' in sender and '{ "navmeshHit", ground.has_value() }' in sender
    assert '{ "type", "campRemove" }' in sender
    header = read("CO-BETA/plugin/include/F4TWorkshopReplicationClient.h")
    assert "bool SubmitCampMove(Workshop::BuildZoneId a_buildZoneId);" in header
    assert "bool SubmitCampRemove(Workshop::BuildZoneId a_buildZoneId);" in header


def test_camp_delete_asks_for_confirmation_before_sending_remove():
    sender = read("CO-BETA/plugin/src/F4TWorkshopCampMoveSubmit.cpp")
    assert '"delete", "Delete camp"' in sender and 'request.title = "Delete camp?";' in sender
    assert '"confirm-delete", "Delete camp"' in sender and '"keep", "Keep camp"' in sender
    confirm = sender[sender.index("void OnCampDeleteAlertResponse("):sender.index("void ShowCampDeleteAlert(")]
    assert 'std::string_view{ a_actionId } != "confirm-delete"' in confirm
    assert "SubmitCampRemove(zone)" in confirm
    move = sender[sender.index("void OnCampMoveAlertResponse("):sender.index("bool SubmitCampMove(")]
    assert "ShowCampDeleteAlert(zone);" in move and "SubmitCampRemove" not in move
    server = read("CO-SERVER/server-cpp/src/ServerWorkshopCamps.h")
    remove = server[server.index("std::vector<OutboundFrame> HandleCampRemove("):]
    not_owner = remove.index("MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::NotOwner)")
    end = remove.find("\n\t\t}\n\n", not_owner)
    remove = remove[:end] if end != -1 else remove
    assert "campIt->ownerProfileId != a_session.profileId" in remove and "FoundStatus::NotOwner" in remove


def test_camp_delete_refusal_shows_its_own_notice():
    inbound = read("CO-BETA/plugin/src/F4TWorkshopCampInbound.cpp")
    rejected = inbound[inbound.index("bool HandleCampRejected("):]
    assert rejected.index("ResolvePendingCampStore(operationId)") < rejected.index("ResolvePendingCampRemove(operationId)") < rejected.index("ResolvePendingCamp(operationId, false);")
    assert "\"Could not delete the camp.\"" in inbound
    replication = read("CO-BETA/plugin/src/F4TWorkshopReplicationClient.cpp")
    assert "ResolvePendingCampRemove(removed->get<std::uint64_t>());" in replication
    assert "RememberPendingCampRemove(operationId);" in read("CO-BETA/plugin/src/F4TWorkshopCampMoveSubmit.cpp")
    assert "g_pendingCampRemoves.erase(a_operationId) != 0" in read("CO-BETA/plugin/src/F4TWorkshopCampStore.cpp")


def test_out_of_reach_camp_store_and_remove_report_too_far():
    camps = read("co-core/include/F4TWorkshopCamps.h")
    assert "RefundUnavailable," in camps and camps.index("RefundUnavailable,") < camps.index("TooFar")
    core = read("co-core/src/world/F4TWorkshopCamps.cpp")
    assert "case FoundStatus::TooFar:" in core and 'return "tooFar";' in core
    assert read("CO-SERVER/server-cpp/src/ServerWorkshopCamps.h").count("FoundStatus::TooFar") == 2
    inbound = read("CO-BETA/plugin/src/F4TWorkshopCampInbound.cpp")
    assert '{ "tooFar", "Could not pack up the camp: walk back to your camp and try again." }' in inbound
    assert '"Could not delete the camp: walk back to your camp and try again."' in inbound
    assert "CampRemoveRefusalNotice(status)" in inbound


def main():
    tests = [value for key, value in sorted(globals().items()) if key.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"camp move grant contract: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
