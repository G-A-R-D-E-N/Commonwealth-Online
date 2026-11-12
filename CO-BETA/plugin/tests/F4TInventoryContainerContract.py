from pathlib import Path


plugin = Path(__file__).resolve().parents[1]
repo = plugin.parents[1]
module_header = (plugin / "include/F4TInventoryContainerSync.h").read_text(encoding="utf-8")
module = (plugin / "src/F4TInventoryContainerSync.cpp").read_text(encoding="utf-8")
sync_header = (plugin / "include/F4TInventorySync.h").read_text(encoding="utf-8")
sync = (plugin / "src/F4TInventorySync.cpp").read_text(encoding="utf-8")
networking_header = (plugin / "include/F4TNetworking.h").read_text(encoding="utf-8")
networking = (plugin / "src/F4TNetworkingInventory.cpp").read_text(encoding="utf-8")
inbound = (plugin / "src/F4TNetworkingInbound.cpp").read_bytes().decode("utf-8")
hub_header = (plugin / "include/F4TContainerChangedHub.h").read_text(encoding="utf-8")
hub = (plugin / "src/F4TContainerChangedHub.cpp").read_text(encoding="utf-8")
ledger = (repo / "co-core/src/world/F4TDropLedger.cpp").read_text(encoding="utf-8")
wire = (repo / "co-core/src/world/F4TInventoryWire.cpp").read_text(encoding="utf-8")
contract = (repo / "co-core/src/world/F4TInventoryContract.cpp").read_text(encoding="utf-8")


def function_body(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n\n", start)]


assert len(module.splitlines()) < 750, "F4TInventoryContainerSync.cpp is over 750 lines"
assert len(sync.splitlines()) < 750, "F4TInventorySync.cpp is over 750 lines"
assert ".inc" not in module, "the container module must not pull in an .inc file"

for name in ("Activate", "Reset", "Busy", "Pump", "NoteResult", "Abandon"):
    assert f"{name}(" in module_header, f"{name} missing from the container module header"
assert "F4T::ContainerChangedHub::Subscribe(OnContainerChange)" in module, "the hook must subscribe through the container hub"
assert "As<RE::Actor>()" in module, "actor containers must be excluded"
assert "FormIdentity::Kind::Runtime" in module, "runtime-identity refs must not be sent"
assert "kOperationTimeoutMs" in module, "pending container ops must time out"
assert "BuildContainerRequest(" in module and "NormalizeContainerResult(" in module, "wire builder and result normalizer must be used"
assert sync.count("F4T::InventoryContainerSync::Busy()") >= 2, "both snapshot gates must check the container counter"
assert "ContainerTick(a_nowMs);" in sync and "F4T::InventoryContainerSync::Activate(sessionNonce, F4T::ClientIdentity::ConnectedServerKey()," in sync, "container send must be ticked and activated on welcome with the server identity"
assert "F4T::InventoryContainerSync::Reset();" in sync, "session reset must clear the container queue"
assert "void NoteContainerResult(const nlohmann::json& a_result);" in sync_header, "NoteContainerResult must be declared"
assert "void NoteContainerResult(const nlohmann::json& a_result)" in sync, "NoteContainerResult must be defined"
assert "bool SendInventoryContainerRequest(const nlohmann::json& a_request, std::uint64_t a_generation);" in networking_header, "container sender must be declared"
assert '"inventoryContainerRequest"' in networking, "container sender must send the container type"
assert '} else if (*packetType == "inventoryContainerResult") {' in inbound, "container result route missing"
assert "F4T::InventorySync::NoteContainerResult(packet);" in inbound, "container result must reach InventorySync"
assert inbound.count("\n") == inbound.count("\r\n"), "F4TNetworkingInbound.cpp must keep CRLF endings"
assert "committedCount" in ledger and "committedCount" in wire, "the ledger and the wire must carry committedCount"
assert "if (count == 0) return" in ledger, "a take with nothing held must be a zero-commit"
assert "BuildContainerRequest(" in contract, "the container builder must exist in co-core"

assert "std::optional<nlohmann::json> Pump(" in module_header, "Pump must return the request alone; the resync flag is gone"
assert "resync" not in module.lower(), "the container module must never request a resync"
assert "kMaximumSends = 4" in module and "kMinimumSendIntervalMs = 300" in module, "container sends must be capped and at least 300 ms apart"
assert "g_queue.push_front(std::move(move));" in module, "a retriable refusal must requeue at the front"
assert '#include "F4TDropLedger.h"' in module and "tail.count += move.count;" in module and "F4T::Drops::kMaximumDropCount" in module, "queued puts must coalesce at the tail within the wire cap"
assert "for (auto it = g_queue.rbegin(); it != g_queue.rend(); ++it)" in module and "it->count += move.count;" in module, "queued takes must coalesce by key across the whole queue"
assert "return !a_queued.take;" in module, "the put cap must count queued puts only, so a take backlog never refuses a put"
assert "Dropped put of {} from {} in {}" in module, "an overflow warning must name the dropped put's cell, container and item"
assert "a_change.barterMenuOpen" in module, "container moves must be skipped while the barter menu is open"
assert "bool barterMenuOpen;" in hub_header, "the hub must carry the barter flag"
assert "RE::BarterMenu::MENU_NAME" in hub and "OnGameThread() && BarterMenuOpen()" in hub, "the barter flag must be read on the game thread when the event fires"
assert "g_serverInventory" in function_body(sync, "void ContainerTick("), "the container tick must stay quiet without server inventory"
assert "g_resyncNeeded" not in function_body(sync, "void ContainerTick("), "the container tick must not set the resync flag"
assert "g_resyncNeeded" not in function_body(sync, "void NoteContainerResult("), "a container result must not set the resync flag"
assert "inventory_container_refused" in function_body(sync, "void NoteContainerResult("), "a refused container move must be logged with its reason"

pump = function_body(module, "std::optional<nlohmann::json> Pump(")
note_result = function_body(module, "std::optional<Settled> NoteResult(")
assert "nlohmann::json request;" in module and "bool resendPending{};" in module, "the in-flight move must keep its built request for a same-id resend"
assert "return g_inFlight->request;" in pump, "a timed-out move must be resent with the stored request and operation id"
assert "g_inFlight.reset();" not in pump.split("if (g_inFlight && !g_inFlight->resendPending")[1].split("if (!g_active")[0], "a timeout must schedule a resend, not drop the move"
assert "ScheduleResend(a_nowMs)" in pump and "ScheduleResend(g_inFlight->sentMs)" in note_result, "timeouts and rateLimited/persistence refusals must schedule a same-id resend"
assert "ScheduleResend(g_inFlight->sentMs)" in module[module.index("void Abandon()"):], "a failed send must keep the move pending for a resend"
assert "kMaximumResends = 4" in module and "kGiveUpMs = 60000" in module, "resends must be bounded"
assert "inventory_container_gave_up" in module and "inventory_container_discarded" in function_body(module, "void CarryLocked()"), "a give-up and a discarded put must be logged"
assert 'reason == "staleRevision" && move.sends < kMaximumSends' in note_result, "a stale refusal must requeue under a new operation id"
assert "g_queue.push_front(std::move(move));" in note_result.split('reason == "staleRevision"')[1], "only a stale refusal requeues"

carry = module[module.index("void CarryLocked()"):module.index("void OnContainerChange(")]
assert "kSlowResendMs = 30000" in module and "kTakeGiveUpMs = 24ull * 60 * 60 * 1000" in module and "kMaximumCarried = 32" in module, "take resend and carry-over bounds must be 30 s, 24 h and 32 takes"
assert "a_flight.move.take ? kTakeGiveUpMs : kGiveUpMs" in module and module.count("GiveUpAfterMs(*g_inFlight)") >= 2 and "(!g_inFlight->move.take && g_inFlight->resends >= kMaximumResends)" in module, "only puts give up after the resend cap; takes keep resending until the age limit"
assert "move.take && g_inFlight->resends >= kMaximumResends ? a_nowMs + kSlowResendMs : 0" in module and "a_nowMs < g_inFlight->notBeforeMs" in pump, "a take past the fast resends must resend every 30 s"
assert "move.operationId = g_inFlight->operationId;" in carry and "move.request = g_inFlight->request;" in carry, "the in-flight take must be carried with its original operation id and request"
assert "if (queued.take)" in carry and "++droppedPuts;" in carry, "queued takes must be carried and puts dropped with a log"
assert "g_carryIdentity != g_identity" in carry and "g_carry.size() >= kMaximumCarried" in carry, "the carry must be keyed by identity and bounded"
activate = module[module.index("void Activate("):module.index("void Reset()")]
assert "CarryLocked();" in activate and "g_queue = std::move(g_carry);" in activate and "g_carryIdentity == identity" in activate, "a matching session must restore the carried takes at the front of the queue"
assert "kTakeGiveUpMs" in activate and "ConnectedServerKey()" in sync, "carried takes expire after 24 h and are keyed by the server identity"
assert "request = move.request;" in pump and "move.operationId.clear();" in note_result, "a carried take is resent under its original id and a stale requeue takes a new id"
assert "it->operationId.empty()" in module, "a take with a carried operation id must not absorb new takes"
assert "CarryLocked();" in function_body(module, "void Reset()"), "a session reset must carry takes instead of dropping them"
