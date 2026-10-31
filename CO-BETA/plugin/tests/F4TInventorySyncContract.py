from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def read_code(relative):
    return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


def function_body(code, signature):
    start = code.index(signature)
    brace = code.index("{", start)
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[brace:index + 1]
    raise AssertionError(f"unterminated body for {signature}")


def regex_in_order(text, *patterns):
    position = 0
    for pattern in patterns:
        found = re.compile(pattern, re.S).search(text, position)
        if not found:
            return False
        position = found.end()
    return True


sync_header = read_code("include/F4TInventorySync.h")
sync = read_code("src/F4TInventorySync.cpp")
inventory = read_code("src/F4TNetworkingInventory.cpp")
inbound = read_code("src/F4TNetworkingInbound.cpp")
networking = read_code("src/F4TNetworking.cpp")
runtime = read_code("src/F4TReplicationRuntime.cpp")
contract = strip_comments((ROOT / "co-core/src/world/F4TInventoryContract.cpp").read_text(encoding="utf-8"))
contract_header = strip_comments((ROOT / "co-core/include/F4TInventoryContract.h").read_text(encoding="utf-8"))

check("kCoTestFeatures" not in sync_header and "F4TTestFeatures.h" not in sync_header,
      "the inventory sync must be unconditional, not behind the test-feature switch")

check("RE::" not in sync, "F4TInventorySync.cpp must not call the engine (no RE::)")
check("RE::" not in inventory, "F4TNetworkingInventory.cpp must not call the engine (no RE::)")
check("SendPacketBytes" not in sync, "F4TInventorySync.cpp must send only through the networking send functions")

entry_points = (
    ("void NoteWelcome(", "void NoteWelcome(const nlohmann::json& a_welcome)"),
    ("void NoteOwnedResult(", "void NoteOwnedResult(const OwnedResult& a_result)"),
    ("void NoteTransferChunk(", "void NoteTransferChunk(const nlohmann::json& a_chunk)"),
    ("std::optional<std::string> BeginOwnedDrop(", "std::optional<std::string> BeginOwnedDrop(const std::string& a_itemKey, std::uint32_t a_count)"),
    ("std::optional<std::string> BeginOwnedPickup(", "std::optional<std::string> BeginOwnedPickup(EntityId a_entityId)"),
    ("bool ReserveOwnedEntity(", "bool ReserveOwnedEntity(EntityId a_entityId)"),
    ("bool BindOwnedEntity(", "bool BindOwnedEntity(EntityId a_entityId, const std::string& a_refKey)"),
    ("bool UnbindOwnedEntity(", "bool UnbindOwnedEntity(EntityId a_entityId)"),
    ("void Tick(", "void Tick(std::uint64_t a_nowMs)"),
    ("void Reset(", "void Reset()"),
    ("Status GetStatus(", "Status GetStatus()"),
    ("std::optional<ServerInventoryView> ServerInventory(", "std::optional<ServerInventoryView> ServerInventory()"),
)
for declaration, signature in entry_points:
    check(declaration in sync_header, f"{signature} must be declared in F4TInventorySync.h")

for signature in ("void NoteWelcome(", "void NoteOwnedResult(", "void NoteTransferChunk(", "void Tick(", "void Reset()", "Status GetStatus()"):
    check("std::scoped_lock lock(g_mutex)" in function_body(sync, signature),
          f"{signature} must take the state lock")

cap = re.search(r"kMaximumInboundResults = (\d+);", sync_header)
check(cap is not None and cap.group(1) == "64", "the inbound result queue must be capped at 64")
queue = function_body(sync, "void NoteOwnedResult(")
check(regex_in_order(queue, r"std::scoped_lock lock\(g_mutex\)",
                     r"g_inbound\.size\(\) >= kMaximumInboundResults",
                     r"g_droppedResults \+= 1",
                     r"g_inbound\.push_back"),
      "NoteOwnedResult must count and drop an overflow instead of queueing it")

for name in ("SendInventoryDropRequest", "SendInventoryPickupRequest"):
    check(re.search(rf"bool {name}\(const nlohmann::json& a_request\)", inventory) is not None,
          f"{name} must be defined in F4TNetworkingInventory.cpp")
    check(f"F4T::Networking::{name}(" in function_body(sync, "void Tick("),
          f"Tick must drive {name}")
    callers = sorted(
        path.name
        for path in list((PLUGIN / "src").rglob("*.cpp")) + list((PLUGIN / "src").rglob("*.h")) + list((PLUGIN / "include").rglob("*.h"))
        if f"F4T::Networking::{name}(" in strip_comments(path.read_text(encoding="utf-8"))
    )
    check(callers == ["F4TInventorySync.cpp"],
          f"{name} must be reached only from F4TInventorySync.cpp, found {callers}")

for packet in ("inventoryDropRequest", "inventoryPickupRequest"):
    check(f'"{packet}"' in inventory, f"the transport must pin the wire type {packet}")
    check(f'"{packet}"' in contract, f"{packet} must be the type co-core builds")
for packet in ("inventoryDropResult", "inventoryPickupResult"):
    check(f'"{packet}"' in contract, f"{packet} must be the result type co-core parses")
    check(f'*packetType == "{packet}"' in inbound, f"HandlePacketLine must dispatch {packet}")

for fragment in ("BuildInventoryTransferChunks", "ParseInventoryTransferHeader", "InventoryTransferReceiver::Accept"):
    check(fragment in contract, f"co-core must implement the inventory transfer chunk format: {fragment}")
for fragment in ('kInventoryTransferType = "inventoryTransfer"', "kMaximumInventoryTransferChunkBytes",
                  "kMaximumInventoryTransferBytes", "kMaximumInventoryTransferChunks"):
    check(fragment in contract_header, f"co-core must own the inventory transfer chunk format: {fragment}")

handler = function_body(inventory, "void HandleInventoryResultPacket(")
check(regex_in_order(handler, r"ParseOwnedResult\(", r"if \(!result\)", r"return;", r"NoteOwnedResult\(\*result\)"),
      "HandleInventoryResultPacket must parse with ParseOwnedResult before it enqueues anything")
check("RE::" not in handler and "SendPacketBytes" not in handler, "the inbound result handler must not send or touch the engine")

check("F4T::InventorySync::NoteWelcome(a_packet);" in inbound, "the welcome handler must feed the server inventory view")
check('*packetType == "inventoryTransfer"' in inbound and "F4T::InventorySync::NoteTransferChunk(packet);" in inbound,
      "HandlePacketLine must dispatch inventoryTransfer chunks to the inventory sync")
check("g_transferReceiver.Accept(a_chunk, inventory)" in sync and "g_transferReceiver.Active()" in sync,
      "the inventory sync must reassemble transfer chunks before it exposes the view")
check("g_serverInventory = completed;" in sync and "SetExpectedRevision(completed->revision)" in sync,
      "a completed transfer must apply the view and seed the revision")
check("g_transferReceiver.Reset();" in function_body(sync, "void Reset()"),
      "Reset must drop an in-flight inventory transfer")
check("F4T::InventorySync::Reset();" in networking and networking.count("F4T::InventorySync::Reset();") == 2,
      "both disconnect paths must reset the inventory sync state")
check("F4T::InventorySync::Tick(" in runtime, "F4T::ReplicationRuntime::Tick must drive the inventory sync tick")

late = function_body(sync, "void HandleLateCommit(")
check(regex_in_order(late, r"a_late\.kind == Operation::Pickup", r"g_resyncNeeded = true"),
      "a late-committed pickup must ask for a resync")
check(regex_in_order(late, r"request\.kind = Operation::Pickup", r"request\.entityId = a_late\.entityId", r"a_pending\.Begin\(request\)"),
      "a late-committed drop must queue a compensating pickup for the entity it created")

requeued = re.search(r"ResolveOutcome::Requeued:\s*(.*?)break;", sync, re.S)
check(requeued is not None and requeued.group(1).strip() == "",
      "a requeued operation must be a silent no-op")
check("ResolveOutcome::LateCommit:" in sync and "HandleLateCommit(a_pending, late);" in sync,
      "the drain must route LateCommit results into the late-commit handling")

reset = function_body(sync, "void Reset()")
check("g_pending->Reset()" in reset and "g_pending.reset()" in reset, "Reset must fail every pending operation")
check("g_bindings = F4T::InventoryOps::DropEntityBindings{}" in reset,
      "Reset must unbind every reservation and binding")
check("g_inbound.clear()" in reset and "g_sessionActive = false" in reset,
      "Reset must end the session and drop queued results")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TInventorySyncContract: all checks passed.")
