from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(name):
    return (ROOT / name).read_text(encoding="utf-8")


def function_body(text, signature):
    start = text.index(signature)
    indent = text[text.rfind("\n", 0, start) + 1:start]
    assert indent.strip() == "", signature
    return text[start:text.index("\n" + indent + "}\n", start)]


CLIENT = "CO-BETA/plugin/src/F4TWorkshopReplicationClient.cpp"
SERVER_DOORS = "CO-SERVER/server-cpp/src/ServerWorkshopDoors.h"
SERVER_IMPL = "CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc"
SERVER_CAMPS = "CO-SERVER/server-cpp/src/ServerWorkshopCamps.h"
SERVER_PROTOCOL = "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc"
PROTOCOL_HEADER = "co-core/include/F4TWorkshopProtocol.h"
PROTOCOL = "co-core/src/protocol/F4TWorkshopRaidProtocol.cpp"
WORLD_REFS = "CO-BETA/plugin/src/F4TWorkshopWorldRefs.cpp"
DOOR_LOCK_CPP = "CO-BETA/plugin/src/F4TWorkshopDoorLock.cpp"
DOORS = "CO-BETA/plugin/src/F4TWorkshopReplicationDoors.cpp"
OBJECTS = "CO-BETA/plugin/src/F4TWorkshopReplicationObjects.cpp"


def test_inbound_packets_are_queued_on_receive_and_dispatched_on_the_tick():
    client = read(CLIENT)
    assert 'a_type == "doorState"' in function_body(client, "static bool IsInboundType(std::string_view a_type)")
    handler = function_body(client, "bool TryHandlePacket(std::string_view a_type, const Json& a_packet)")
    assert "if (!IsInboundType(a_type)) { return false; }" in handler
    assert "g_inbound.emplace_back(std::string{ a_type }, a_packet);" in handler
    dispatch = function_body(client, "static bool DispatchInbound(std::string_view a_type, const Json& a_packet)")
    assert 'if (a_type == "doorState") {' in dispatch
    assert "return HandleDoorState(a_packet);" in dispatch
    drain = function_body(client, "static void DrainInbound()")
    assert drain.index("std::scoped_lock lock(g_mutex);") < drain.index("inbound.swap(g_inbound);") < drain.index("DispatchInbound(type, packet);")
    tick = function_body(client, "void Tick()")
    assert tick.index("DrainInbound();") < tick.index("CollectDoorOps(doorOps, now);") < tick.index("for (auto& packet : doorOps) {")


def test_remote_door_state_is_validated_and_applied_outside_the_lock():
    handle = function_body(read(DOORS), "bool HandleDoorState(const Json& a_packet)")
    assert "!zone->is_number_unsigned() || !objectId->is_number_unsigned() || !open->is_boolean() || !locked->is_boolean()" in handle
    assert "if (!ref || !IsDoorRef(*ref)) {" in handle
    lookup = handle.index("auto handle = FindPlacedObjectHandle(buildZoneId, id);")
    find = function_body(read(OBJECTS), "RE::ObjectRefHandle FindPlacedObjectHandle(Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_objectId)")
    assert find.index("std::scoped_lock lock(g_mutex);") < find.index("g_zones.find(a_buildZoneId)") < find.index("zone->second.objects.find(a_objectId)")
    release = handle.index("const auto ref = handle.get();")
    apply = handle.index("const auto applied = ApplyDoorState(*ref, open->get<bool>(), locked->get<bool>());")
    assert handle.count("std::scoped_lock lock(g_mutex);") == 1
    assert lookup < release < apply < handle.index("std::scoped_lock lock(g_mutex);") < handle.index("found->second.doors[id] = applied;")


def test_remote_door_state_never_reactivates_a_door_that_already_matches():
    apply = function_body(read(DOORS), "DoorSeen ApplyDoorState(RE::TESObjectREFR& a_ref, bool a_open, bool a_locked)")
    assert "if (DoorIsOpen(a_ref) != a_open) {" in apply
    unlock = apply.index("refLock->SetLocked(false);")
    activate = apply.index("a_ref.ActivateRef(player, nullptr, 1, false, false, false);")
    relock = apply.index("WorkshopDoorLock::SetDoorLocked(a_ref, a_locked);")
    assert unlock < activate < relock
    assert "refLock->SetLocked(a_locked)" not in apply
    assert "return DoorSeen{ DoorIsOpen(a_ref), DoorIsLocked(a_ref) };" in apply


def test_local_door_changes_are_sent_once_per_open_close_lock_and_unlock():
    collect = function_body(read(DOORS), "void CollectDoorOps(std::vector<Json>& a_outbound, std::chrono::steady_clock::time_point a_now)")
    assert "if (a_now < g_nextDoorPoll) {" in collect
    assert "const auto seen = zone.doors.try_emplace(objectId, current).first;" in collect
    assert "if (current.open != seen->second.open) {" in collect
    assert 'DoorOpPacket(buildZoneId, objectId, current.open ? "open" : "close")' in collect
    assert "if (current.locked != seen->second.locked) {" in collect
    assert 'DoorOpPacket(buildZoneId, objectId, current.locked ? "lock" : "unlock")' in collect
    assert "seen->second = current;" in collect


def test_door_recognition_and_state_read_the_engine_flags():
    doors = read(DOORS)
    assert "base->GetFormType() == RE::ENUM_FORM_ID::kDOOR" in function_body(doors, "bool IsDoorRef(const RE::TESObjectREFR& a_ref)")
    assert "OPEN_STATE::kOpening" in function_body(doors, "bool DoorIsOpen(const RE::TESObjectREFR& a_ref)")
    assert "RE::REFR_LOCK::Flags::kLocked" in function_body(doors, "bool DoorIsLocked(RE::TESObjectREFR& a_ref)")


def test_door_packets_match_the_server_wire_contract():
    doors = read(DOORS)
    packet = function_body(doors, "Json DoorOpPacket(Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_objectId, std::string_view a_op)")
    assert '{ "type", "doorOp" }' in packet
    assert '{ "buildZoneId", a_buildZoneId }' in packet and '{ "objectId", a_objectId }' in packet
    assert '{ "op", std::string{ a_op } }' in packet
    assert 'if (*type == "doorOp") return HandleDoorOp(a_connectionId, session, *packet);' in read(SERVER_PROTOCOL)
    server = read(SERVER_DOORS)
    assert '{ "type", "doorState" }' in server
    assert '{ "open", a_open }' in server and '{ "locked", a_locked }' in server
    protocol = read(PROTOCOL)
    assert 'type->get<std::string>() != "doorOp"' in protocol
    for op in ("open", "close", "lock", "unlock"):
        assert f'name == "{op}"' in protocol
    assert "door.kind = DoorOpKind::Close;" in protocol


def test_protocol_door_op_enum_orders_open_close_lock_unlock():
    header = read(PROTOCOL_HEADER)
    enum_body = header[header.index("enum class DoorOpKind"):]
    enum_body = enum_body[:enum_body.index("};")]
    assert enum_body.index("Open,") < enum_body.index("Close,") < enum_body.index("Lock,") < enum_body.index("Unlock")


def test_inbound_queue_is_bounded_and_cleared_on_reset():
    client = read(CLIENT)
    assert "constexpr std::size_t kMaximumInbound = 4096;" in client
    assert "std::deque<std::pair<std::string, Json>> g_inbound;" in client
    assert 'Warn("workshop_inbound_full", ' in function_body(client, "bool TryHandlePacket(std::string_view a_type, const Json& a_packet)")
    reset = function_body(client, "void Reset()")
    assert "g_inbound.clear();" in reset and "g_nextDoorPoll = {};" in reset


def test_removed_objects_and_pruned_snapshots_forget_their_door_state():
    client = read(CLIENT)
    assert "a_zone.doors.erase(a_objectId);" in function_body(client, "void RemoveObject(ZoneState& a_zone, Workshop::ObjectId a_objectId)")
    assert "std::erase_if(a_zone.doors, [&a_zone](const auto& a_door) { return !a_zone.objects.contains(a_door.first); });" in client


# TODO-verify: scrap hide uses Disable() and Enable(false) on placed refs, unverified in game
def test_scrap_world_hides_the_placed_ref_by_form_and_position_and_reset_restores_it():
    client = read(CLIENT)
    handle = function_body(client, "bool HandleCommit(const Json& a_packet)")
    scrap_world = handle.index("commit->kind == Workshop::OperationKind::ScrapWorld) {")
    assert handle.index("RemoveObject(zone, commit->objectId);", scrap_world) < handle.index("WorkshopWorldRefs::Hide(*formId, commit->object->transform);", scrap_world)
    assert "if (const auto formId = FormIdFromKey(state.form)) { WorkshopWorldRefs::Hide(*formId, state.transform); }" in function_body(client, "void FinishSnapshot(ZoneState& a_zone)")
    assert "WorkshopWorldRefs::Restore(a_session->hiddenRefs);" in function_body(client, "static void RetireSession(const std::shared_ptr<RetiredSession>& a_session)")
    assert "retired->hiddenRefs = WorkshopWorldRefs::Detach();" in function_body(client, "void Reset()")
    hide = function_body(read(WORLD_REFS), "void Hide(std::uint32_t a_formId, const Workshop::Transform& a_transform)")
    assert "RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_formId)" in hide
    assert "if (!ref || ref->IsDeleted()) {" in hide and hide.index("kMatchDistanceSq") < hide.index("if (ref->IsDisabled()) {") < hide.index("ref->Disable();")
    assert "dx * dx + dy * dy + dz * dz > kMatchDistanceSq" in hide
    assert "ref->Disable();" in hide and hide.index("std::scoped_lock lock(g_hiddenMutex);") < hide.index("g_hidden.insert(a_formId);")
    assert "MarkAsDeleted" not in hide
    restore = function_body(read(WORLD_REFS), "void Restore(const std::set<std::uint32_t>& a_formIds)")
    assert "ref->Enable(false);" in restore and restore.index("g_retiring.erase(formId);") < restore.index("g_hidden.contains(formId)") < restore.index("ref->Enable(false);")
    detach = function_body(read(WORLD_REFS), "std::set<std::uint32_t> Detach()")
    assert detach.index("std::scoped_lock lock(g_hiddenMutex);") < detach.index("std::exchange(g_hidden, {})")


# TODO-verify: camp workbench base 0x000C1AEB is taken from the Fallout4.esm record dump, not checked in game
def test_camp_workbench_stand_in_is_the_vanilla_settlement_workbench_base():
    assert 'inline const Workshop::FormKey kCampWorkbenchForm{ "Fallout4.esm", 0x000C1AEB };' in read("CO-BETA/plugin/include/F4TWorkshopReplicationClient.h")  # TODO-verify: camp workbench base 0x000C1AEB


def test_camp_workbench_survives_snapshot_pruning_in_camp_zones():
    finish = function_body(read(CLIENT), "void FinishSnapshot(ZoneState& a_zone)")
    assert "if (a_zone.campWorkbench) {" in finish
    assert finish.index("F4T::WorkshopRaids::WorkbenchObjectId") < finish.index("Despawn(handle);")


def test_server_door_ops_are_authorized_reach_gated_and_refusals_go_to_the_sender():
    doors = read(SERVER_DOORS)
    handle = function_body(doors, "std::vector<OutboundFrame> HandleDoorOp(")
    assert "if (!door || a_session.profileId.empty()) {" in handle
    assert "a_session.workshopZonesInside.contains(door->buildZoneId) && SessionWithinZoneReach(a_session, zone->second)" in handle
    assert "F4T::WorkshopLocks::CanOpen(authorized, wasLocked)" in handle
    assert "F4T::WorkshopLocks::CanSetLock(authorized)" in handle
    assert "const auto settlement = settlementAccess.Find(door->buildZoneId);" in handle
    assert "const bool authorized = settlement && (settlement->ownerProfileId == a_session.profileId ||" in handle
    assert "std::find(settlement->allowedProfileIds.begin(), settlement->allowedProfileIds.end(), a_session.profileId) != settlement->allowedProfileIds.end());" in handle
    assert "candidate.SetDoorLocked(door->objectId, nextLocked) && SaveWorkshopZone(candidate)" in handle
    assert "MarkWorkshopDirty(door->buildZoneId);" in handle
    refusal = handle.index("if (!accepted) {")
    assert refusal < handle.index("Reject();", refusal) < handle.index("return { ReliableFrame(a_connectionId, DoorStatePacket(door->buildZoneId, door->objectId, wasOpen, wasLocked)) };", refusal)
    assert "if (nextOpen == wasOpen && nextLocked == wasLocked) {" in handle
    assert "if (peer.active && peer.workshopZonesInside.contains(door->buildZoneId)) {" in handle
    assert "connectionId != a_connectionId" not in handle
    frames = function_body(doors, "void AppendDoorStateFrames(")
    assert "if (object.door) {" in frames
    assert "openDoors.contains(std::make_pair(a_zone.Id(), object.objectId))" in frames


def test_join_and_unchanged_snapshots_send_every_door_state_after_the_zone_is_recorded():
    impl = read(SERVER_IMPL)
    frames = function_body(impl, "std::vector<OutboundFrame> WorkshopSnapshotFrames(")
    assert "AppendDoorStateFrames(frames, a_connectionId, a_zone);" in frames
    request = function_body(impl, "std::vector<OutboundFrame> HandleWorkshopSnapshotRequest(")
    recorded = request.index("a_session.workshopZonesInside.insert(request->buildZoneId);")
    assert recorded < request.index("if (request->haveRevision == zone->second.CurrentRevision()) {")
    assert "AppendDoorStateFrames(frames, a_connectionId, zone->second);" in request
    assert "return WorkshopSnapshotFrames(a_connectionId, zone->second);" in request


def test_camp_removal_forgets_door_state_for_the_zone():
    camps = read(SERVER_CAMPS)
    assert "ForgetZoneDoors(a_state.buildZoneId);" in camps
    assert "ForgetZoneDoors(request->buildZoneId);" in camps
    assert "std::erase_if(openDoors, [a_buildZoneId](const auto& a_door) { return a_door.first == a_buildZoneId; });" in read(SERVER_DOORS)


def test_owner_side_lock_toggle_is_queued_only_for_allowlisted_profiles():
    activation = function_body(read(DOORS), "void HandleDoorActivation(RE::TESObjectREFR& a_ref, bool a_sneaking)")
    assert "const bool locked = DoorIsLocked(a_ref);" in activation
    gate = activation.index("F4T::SettlementAccessClient::BuildZonesAllowedFor(F4T::ClientIdentity::ConnectedProfileId()).contains(placed->buildZoneId)")
    hud = activation.index('RE::SendHUDMessage::ShowHUDMessage("This door is locked by the settlement owner."')
    toggle = activation.index("if (a_sneaking) {")
    assert gate < hud < toggle
    assert 'QueuePacket(DoorOpPacket(placed->buildZoneId, placed->objectId, locked ? "unlock" : "lock"));' in activation[toggle:]
    sink = function_body(read(DOOR_LOCK_CPP), "RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent& a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override")
    assert "a_event.actionRef.get() == player" in sink
    assert "F4T::WorkshopReplicationClient::HandleDoorActivation(*a_event.objectActivated, player->IsSneaking());" in sink


def test_locked_doors_and_owned_containers_block_activation_for_outsiders_only():
    collect = function_body(read(DOORS), "void CollectDoorOps(std::vector<Json>& a_outbound, std::chrono::steady_clock::time_point a_now)")
    assert "F4T::SettlementAccessClient::BuildZonesAllowedFor(F4T::ClientIdentity::ConnectedProfileId())" in collect
    assert "F4T::SettlementAccessClient::OwnersByBuildZone()" in collect
    assert "WorkshopDoorLock::RefreshActivationBlock(*ref, allowed.contains(buildZoneId), owner != owners.end() && !owner->second.empty(), door != zone.doors.end() && door->second.locked);" in collect
    assert collect.index("seen->second = current;") < collect.index("F4T::SettlementAccessClient::BuildZonesAllowedFor")
    refresh = function_body(read(DOOR_LOCK_CPP), "void RefreshActivationBlock(RE::TESObjectREFR& a_ref, bool a_ownerSide, bool a_ownedZone, bool a_doorLocked)")
    assert "blocked = !a_ownerSide && a_doorLocked;" in refresh
    assert "blocked = !a_ownerSide && a_ownedZone;" in refresh
    assert "if (a_ref.IsActivationBlocked() != blocked) {" in refresh
    assert "SetActivationBlocked(a_ref, blocked);" in refresh


def test_papyrus_lock_and_block_activation_dispatch_on_object_reference_like_the_camp_store():
    module = read(DOOR_LOCK_CPP)
    dispatch = function_body(module, "bool DispatchObjectReference(RE::TESObjectREFR& a_ref, const char* a_function, bool a_first)")
    assert "handles.GetHandleForObject(kObjectReferenceVmType, std::addressof(a_ref))" in dispatch
    assert 'vm->DispatchMethodCall(static_cast<std::uint64_t>(handle), "ObjectReference", a_function, callback, a_first, false);' in dispatch
    assert 'return DispatchObjectReference(a_ref, "Lock", a_locked);' in module
    assert 'return DispatchObjectReference(a_ref, "BlockActivation", a_blocked);' in module
    assert "source->RegisterSink(&g_sink);" in function_body(module, "void EnsureActivateSink()")
    assert "WorkshopDoorLock::EnsureActivateSink();" in function_body(read(CLIENT), "void Tick()")
    apply = function_body(read(DOORS), "DoorSeen ApplyDoorState(RE::TESObjectREFR& a_ref, bool a_open, bool a_locked)")
    assert "WorkshopDoorLock::SetDoorLocked(a_ref, a_locked);" in apply


def test_reset_swaps_state_under_the_lock_and_does_engine_cleanup_on_the_game_thread():
    client = read(CLIENT)
    reset = function_body(client, "void Reset()")
    for engine_call in ("Despawn(", "RemoveAppliedPowerWires(", "WorkshopWorldRefs::Restore(", "->Disable(", "MarkAsDeleted"):
        assert engine_call not in reset, engine_call
    lock = reset.index("std::scoped_lock lock(g_mutex);")
    assert lock < reset.index("retired->zones = std::move(g_zones);") < reset.index("retired->hiddenRefs = WorkshopWorldRefs::Detach();") < reset.index("g_activeZone.reset();") < reset.index("F4SE::GetTaskInterface()")
    assert "taskInterface->AddTask([retired]() { RetireSession(retired); });" in reset
    retire = function_body(client, "static void RetireSession(const std::shared_ptr<RetiredSession>& a_session)")
    assert "g_zones" not in retire and "g_mutex" not in retire
    assert retire.index("RemoveAppliedPowerWires(zone);") < retire.index("Despawn(handle);") < retire.index("WorkshopWorldRefs::Restore(a_session->hiddenRefs);") < retire.index("ForgetApplyCreatedRefs(a_session->applyCreated);")


def main() -> int:
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"workshop door contract: {len(tests)} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
