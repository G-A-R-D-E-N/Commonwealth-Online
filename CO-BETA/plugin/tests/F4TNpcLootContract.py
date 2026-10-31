from pathlib import Path
import re
import sys

PLUGIN = Path(__file__).resolve().parents[1]
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(relative):
    return (PLUGIN / relative).read_text(encoding="utf-8")


def body(text, signature):
    start = text.find(signature)
    if start < 0:
        return ""
    start = text.find("{", start)
    depth = 1
    end = start + 1
    while end < len(text) and depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


xmake = read("xmake.lua")
check("F4T_NPC_" not in xmake and "co_test_features" not in xmake, "xmake.lua must define no loot switch")
header = read("include/F4TNpcSync.h")
check("F4T_NPC_" not in header and "F4T_CO_TEST_FEATURES" not in header, "F4TNpcSync.h must carry no loot feature macro")
for part in ("Capture", "Mirror", "Take"):
    name = f"src/F4TNpcSyncLoot{part}.cpp"
    check(f'remove_files("{name}")' not in xmake, f"{name} must build in every target")
for path in (PLUGIN / "src").glob("F4TNpcSyncLoot*.cpp"):
    text = path.read_text(encoding="utf-8")
    check("GetNPC()" not in text and "IsDead(false)" not in text, f"{path.name}: stable identity/death rules")

loot = read("src/F4TNpcSyncLoot.cpp")
capture = read("src/F4TNpcSyncLootCapture.cpp")
mirror = read("src/F4TNpcSyncLootMirror.cpp")
take = read("src/F4TNpcSyncLootTake.cpp")
internal = read("include/F4TNpcSyncLootInternal.h")

sink = body(capture, "ProcessEvent(")
check(bool(sink), "capture: ContainerChangeSink::ProcessEvent not found")
check("RemoveItem" not in sink and "AddItem" not in sink, "capture sink must never call RemoveItem/AddItem")
check(sink.count("scoped_lock") == 1 and "g_pendingMutex" in sink, "capture sink must take only its own mutex")
check("itemCount <= 0" in sink, "capture sink must skip a non-positive itemCount")
check("overflow = true" in sink, "capture sink must raise the overflow flag instead of dropping events")
check("contains(" in sink, "capture sink must coalesce repeated container events")
check(body(capture, "void CaptureTick(").count("changes.overflow") == 1 and
      "g_hostNpcIdsByActorFormId" in body(capture, "void CaptureTick("),
      "CaptureTick must rescan the tracked containers when the sink overflowed")

removal = body(loot, "std::uint32_t RemoveLootFromContainer(")
check(bool(removal), "loot: RemoveLootFromContainer not found")
check("plan.rbegin()" in removal and "plan.rend()" in removal, "removal must apply the plan in descending stack-index order")
check("plan.begin()" not in removal, "removal must not apply the plan in ascending stack-index order")
check("BSAutoReadLock" in removal.split("RemoveItem(")[0], "removal must not hold the read lock across RemoveItem")
check(removal.count("CountLootItem(") == 2 and "before - after" in removal,
      "removal must measure the removed count from before/after totals")
check("removed += " not in removal, "removal must not add up the planned counts")

grant = body(take, "NpcLootSync::TakeResult GrantTake(")
check(bool(grant), "take: GrantTake not found")
check("a_itemId, removed, true" in grant, "GrantTake must return the measured removed count")
check("Expect(a_container.GetFormID(), a_itemId, granted" not in grant, "GrantTake must not expect the planned count")
check("a_itemId, removed, NowMilliseconds()" in grant, "GrantTake must record the measured count in the echo ledger")
check(grant.find("RemoveLootFromContainer(") < grant.find("Ledger().Expect("),
      "GrantTake must measure the removal before recording the ledger expectation")

mirror_tick = body(mirror, "void MirrorTick(")
check(bool(mirror_tick), "mirror: MirrorTick not found")
check("void MirrorTick(const AuthorityScope& a_scope)" in internal, "MirrorTick must receive the authority scope")
check("Detail::MirrorTick(scope)" in loot, "Loot::Tick must pass the scope to MirrorTick")
check("TryGetPuppetByActorFormId" in mirror_tick and "puppet->sourceFormId" in mirror_tick,
      "mirror must resolve the container actor's puppet identity")
check("a_scope.epoch" in mirror_tick and "loot.identity.authorityEpoch" in mirror_tick,
      "mirror must reject a packet from another authority epoch")
check("loot.authorityPlayerId" in mirror_tick and "GetNpcAuthorityForScope" in mirror_tick,
      "mirror must reject a packet from another authority")

reset_queues = body(loot, "void ResetQueues()")
check(bool(reset_queues), "loot: ResetQueues not found")
check("Detail::ResetQueues()" in body(loot, "void Reset()"), "Loot::Reset must clear the transport queues")
for queue in ("g_inboundLoot", "g_inboundTakes", "g_inboundResults", "g_outboundLoot", "g_outboundResults", "g_ledger"):
    check(f"{queue}.clear()" in reset_queues or f"{queue} = " in reset_queues, f"ResetQueues must reset {queue}")

def squash(text):
    return " ".join(text.split())


net_transport = read("src/F4TNetworkingNpcLoot.cpp")
net_inbound = read("src/F4TNetworkingInbound.cpp")
net_internal = read("src/F4TNetworkingInternal.h")
net_public = read("include/F4TNetworking.h")

check('remove_files("src/F4TNetworkingNpcLoot.cpp")' not in xmake, "src/F4TNetworkingNpcLoot.cpp must build in every target")
for declaration in ("bool SendNpcLootPacket(", "bool SendNpcLootTakePacket(", "bool SendNpcLootResultPacket(",
                    "void FlushNpcLootOutbound()"):
    check(declaration in net_public, f"include/F4TNetworking.h must declare {declaration[:-1]}")
    check(declaration in net_transport, f"src/F4TNetworkingNpcLoot.cpp must define {declaration[:-1]}")
for handler, packet_type in (("HandleNpcLootPacket", "npcLoot"),
                             ("HandleNpcLootTakePacket", "npcLootTake"),
                             ("HandleNpcLootResultPacket", "npcLootResult")):
    check(f"void {handler}(const Json& a_packet);" in net_internal,
          f"F4TNetworkingInternal.h must declare {handler}")
    check(f"void {handler}(const Json& a_packet)" in net_transport,
          f"F4TNetworkingNpcLoot.cpp must define {handler}")
    check(f'*packetType == "{packet_type}"' in net_inbound,
          f"F4TNetworkingInbound.cpp must dispatch {packet_type}")
    check(f"{handler}(packet);" in net_inbound, f"F4TNetworkingInbound.cpp must call {handler}")

flush = body(net_transport, "void FlushNpcLootOutbound()")
check("DrainOutboundLoot()" in flush and "SendNpcLootPacket(" in flush,
      "flush must send every queued loot packet")
check("DrainOutboundTakeResults()" in flush and "SendNpcLootResultPacket(" in flush,
      "flush must send every queued take result")

loot_tick = body(loot, "void Tick(")
check("Networking::FlushNpcLootOutbound()" in loot_tick, "Loot::Tick must flush the outbound transport")
check(loot_tick.find("Detail::CaptureTick(scope)") < loot_tick.find("Networking::FlushNpcLootOutbound()") and
      loot_tick.find("Detail::MirrorTick(scope)") < loot_tick.find("Networking::FlushNpcLootOutbound()") and
      loot_tick.find("Detail::TakeTick(scope)") < loot_tick.find("Networking::FlushNpcLootOutbound()"),
      "Loot::Tick must flush after the capture, mirror and take ticks")
check("Detail::CaptureTick(scope); Detail::TakeTick(scope); if (!a_isHost) { Detail::MirrorTick(scope); }" in squash(loot_tick),
      "Loot::Tick must run TakeTick on both roles, outside the host branch, and MirrorTick on the non-host path only")

enqueue = body(loot, "bool EnqueueOutboundLoot(")
check(bool(enqueue), "loot: EnqueueOutboundLoot not found")
check(enqueue.find("kMaximumQueuedPackets") < enqueue.find("Revisions().Next("),
      "EnqueueOutboundLoot must reject a full queue before taking a revision")
check(enqueue.find("push_back") < enqueue.find("Revisions().Next("),
      "EnqueueOutboundLoot must take the revision only after a successful enqueue")
check("CountAvailableLoot" not in loot, "loot must keep a single container count helper")

capture_tick = body(capture, "void CaptureTick(")
check(squash("if (!EnqueueOutboundLoot(std::move(loot))) { ++entry; continue; }") in squash(capture_tick),
      "CaptureTick must keep the container dirty when the enqueue fails")
check("IsLootableContainer(" in capture_tick and "IsContainerInScope(" in capture_tick,
      "CaptureTick must publish only a dead, in-scope container")
check("IsLootableContainer(" in mirror_tick and "IsContainerInScope(" in mirror_tick,
      "MirrorTick must apply loot only to a dead, in-scope container")
check("std::numeric_limits<std::uint32_t>::max" in mirror_tick and "a_scope.epoch == 0" in mirror_tick,
      "MirrorTick must reject epoch 0 and an epoch above the uint32 maximum")
check(mirror_tick.find("loots.empty()") < mirror_tick.find("GetNpcAuthorityForScope"),
      "MirrorTick must skip the authority lookup when the inbound queue is empty")

root = PLUGIN.parents[1]
core_sync = (root / "co-core/src/npc/F4TNpcLootSync.cpp").read_text(encoding="utf-8")
identity = body(core_sync, "Json IdentityToJson(")
check(bool(identity), "co-core: IdentityToJson not found")
check("a_identity.worldspaceId == 0 ? std::string{} : FormatHex(a_identity.worldspaceId)" in squash(identity),
      "interior loot must publish an empty worldspace id, not the hex of zero")

worldspace_helper = body(net_transport, "std::string FormatScopeWorldspaceId(")
check(bool(worldspace_helper) and
      "a_worldspaceFormId == 0 ? std::string{} : FormatScopeFormId(a_worldspaceFormId)" in squash(worldspace_helper),
      "transport must format a zero worldspace scope id as an empty string")
check(net_transport.count("FormatScopeWorldspaceId(identity.worldspaceId)") == 2,
      "npcLoot and npcLootTake must both use the empty-aware worldspace formatter")
check("IsValidLootScope(cellId, worldspaceId)" in net_transport,
      "the scope checks must use the same worldspace string as the authority lookup")

send_loot = body(net_transport, "bool SendNpcLootPacket(")
check(bool(send_loot), "transport: SendNpcLootPacket not found")
check(send_loot.count("*a_retryable = true;") == 2,
      "only the not-connected and send-failed paths may ask the capture for a retry")
check("bool* a_retryable = nullptr" in net_public, "the send must report whether a failure is transient")
check("MarkLootDirty(" in flush and "retryable" in flush,
      "flush must re-mark the container dirty when a transient send fails")
check("void MarkLootDirty(std::uint32_t a_containerFormId);" in internal,
      "F4TNpcSyncLootInternal.h must declare MarkLootDirty")
mark_dirty = body(capture, "void MarkLootDirty(")
check(bool(mark_dirty) and "NpcIdForLootContainer(" in mark_dirty and "g_dirtyContainers.try_emplace(" in mark_dirty,
      "MarkLootDirty must only re-mark a container that is still tracked")

check(squash("if (!IsLootableContainer(*container)) { ++entry; continue; }") in squash(capture_tick),
      "CaptureTick must keep a tracked container dirty while it is not lootable yet")
check("g_publishedContainers.insert(" in capture_tick and
      "g_publishedContainers.contains(" in capture_tick,
      "CaptureTick must publish a tracked container once when it first becomes lootable")
check("g_publishedContainers.clear()" in body(capture, "void ResetCapture()"),
      "ResetCapture must clear the published-once set")

check("g_pendingLoot" in mirror_tick and "kPendingLootLifetimeTicks" in mirror_tick,
      "MirrorTick must retry loot that arrived before the puppet was lootable")
check("g_pendingLoot[loot.identity.npcId] = PendingLoot{" in mirror_tick,
      "MirrorTick must keep only the latest packet per npc id")
check("g_pendingLoot.clear()" in body(mirror, "void ResetMirror()"),
      "ResetMirror must clear the pending loot")

check("RE::PlayerCharacter::GetSingleton()" in sink and "g_pendingChanges.takes" in sink,
      "capture sink must record takes by the local player only")
check("SendTake(" in body(capture, "void CaptureTick(") and "changes.takes" in body(capture, "void CaptureTick("),
      "CaptureTick must send the recorded takes on the non-host path")
check("void SendTake(" in internal, "F4TNpcSyncLootInternal.h must declare SendTake")
send_take = body(take, "void SendTake(")
check(bool(send_take) and "Networking::SendNpcLootTakePacket(" in send_take and "g_pendingTakes[operationId]" in send_take,
      "SendTake must send the take and remember the operation")
roll_back = body(take, "void RollBackTake(")
check(bool(roll_back) and "RemoveLootFromContainer(*player" in roll_back,
      "RollBackTake must remove the rolled-back item from the local player")
tick_take = body(take, "void TakeTick(")
resolve = body(take, "void ResolvePendingTake(")
check(bool(resolve) and "UnmatchedTakeCount(operation, a_result)" in resolve and "a_reason, false);" in resolve and "g_pendingTakes.erase(pending)" in resolve,
      "ResolvePendingTake must drop the matched take and roll back its unfilled part")
check("ResolvePendingTake(result" in tick_take and "ResolvePendingTake(result" in body(take, "void ResetTake()"),
      "TakeTick and ResetTake must both resolve drained results through ResolvePendingTake")
unmatched = body(take, "std::uint32_t UnmatchedTakeCount(")
check("itemId != a_operation.itemId" in unmatched and "return a_operation.count;" in unmatched and
      "ClampToAvailable(a_operation.count, count)" in unmatched,
      "UnmatchedTakeCount must refund the full count on reject or item mismatch and the shortfall on partial accept")
check('"expired", true)' in tick_take and "entry = g_pendingTakes.erase(entry)" in tick_take,
      "TakeTick must roll back and drop expired takes")
check("kPendingTakeLifetimeMilliseconds = 4000;" in take,
      "pending takes must outlive the server 3 s result lifetime")
roll_back = body(take, "void RollBackTake(")
check(bool(roll_back) and "RemoveLootFromContainer(*player" in roll_back and "AddLootToContainer(*container" in roll_back and "a_restoreToContainer" in roll_back and
      "GetParentCell()" in roll_back,
      "RollBackTake must remove from the player, restore the puppet container, and skip when the player has no cell")
check(send_take.count("RollBackTake(") >= 3 and '"refused", true)' in send_take and '"send failed", true)' in send_take,
      "SendTake must roll back every refusal after the puppet is confirmed")
check("PendingTake{ a_containerFormId," in send_take, "a pending take must remember the container it came from")
check("SessionNonce()" in send_take and 'std::format("{:08X}-{}"' in send_take,
      "operation ids must carry the per-load session nonce")
check("static const std::uint32_t nonce = std::random_device{}();" in take,
      "the session nonce must be drawn once per plugin load")
reset_take = body(take, "void ResetTake()")
check("DrainTakeResults()" in reset_take and "ResolvePendingTake(result" in reset_take,
      "ResetTake must honour queued results before rolling back")
check('RollBackTake(entry.second.containerFormId, entry.second.itemId, entry.second.count, "reset", true)' in reset_take and "g_pendingTakes.clear()" in reset_take,
      "ResetTake must roll back still-pending takes before clearing them")
capture_reset = body(capture, "void ResetCapture()")
check("changes.takes" in capture_reset and "\"capture reset\", true)" in squash(capture_reset) and "g_nonHostTick" in capture_reset,
      "ResetCapture must roll back unsent takes on a non-host tick")
check("scoped_lock" not in capture_reset,
      "ResetCapture must not hold the sink mutex while rolling back, because engine events re-enter the sink")
check("void AddLootToContainer(RE::TESObjectREFR& a_container, std::uint32_t a_itemId, std::uint32_t a_count);" in internal,
      "F4TNpcSyncLootInternal.h must declare AddLootToContainer")
check("void RollBackTake(std::uint32_t a_containerFormId" in internal, "F4TNpcSyncLootInternal.h must declare RollBackTake")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)
print("F4TNpcLootContract: all checks passed.")
