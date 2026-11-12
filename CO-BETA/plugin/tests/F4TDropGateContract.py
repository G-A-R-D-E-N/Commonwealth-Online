"""Contract: the owned-drop gate (F22) and its starting-inventory key translation (F24).
Verified record source:CO-BETA/plugin/lib/commonlibf4/include/RE/T/TESForm.h,
source:CO-BETA/plugin/lib/commonlibf4/include/RE/B/BGSObjectInstance.h,
source:CO-BETA/plugin/lib/commonlibf4/include/RE/T/TESDataHandler.h, and the in-repo
precedent CO-BETA/plugin/src/F4TObjectSync.cpp."""
from pathlib import Path
import re


plugin = Path(__file__).resolve().parents[1]
header = (plugin / "include/F4TInventoryDropGate.h").read_text(encoding="utf-8")
impl = (plugin / "src/F4TInventoryDropGate.cpp").read_text(encoding="utf-8")
hooks = (plugin / "src/F4TInventoryHooks.cpp").read_text(encoding="utf-8")
sync = (plugin / "src/F4TInventorySync.cpp").read_text(encoding="utf-8")
gate = (plugin / "src/F4TInventoryPickupGate.cpp").read_text(encoding="utf-8")
mirror_header = (plugin / "include/F4TDropMirrorRefs.h").read_text(encoding="utf-8")
mirror = (plugin / "src/F4TDropMirrorRefs.cpp").read_text(encoding="utf-8")
downed = "".join((plugin / part).read_text(encoding="utf-8") for part in ("src/F4TDownedEngine.cpp", "src/F4TDownedEngineRecovery.cpp"))
xmake = (plugin / "xmake.lua").read_text(encoding="utf-8")

for label, text in (("F4TInventoryDropGate.h", header), ("F4TInventoryDropGate.cpp", impl)):
    assert len(text.splitlines()) < 1000, f"{label} is over 1000 lines"
    assert ".inc" not in text, f"{label} must not pull in an .inc file"

assert "namespace F4T::InventoryDropGate" in header, "gate must live in F4T::InventoryDropGate"
assert "RE::ObjectRefHandle* (*" in header and "const RE::BGSObjectInstance&" in header, \
    "Original must match the DropObject vtable signature"
assert re.search(r"bool Hold\(\s*const RE::BGSObjectInstance& a_object,", header), "Hold declaration missing"
assert "void Complete(const std::string& a_operationId, bool a_committed," in header, "Complete declaration missing"

assert "SplitFormId(form->GetFormID())" in impl, "engine form must split into plugin and local id"
assert "LookupLoadedLightModByIndex" in impl and "LookupLoadedModByIndex" in impl, "plugin name must come from the load order"
assert "IsValidRecordIdentity" in impl, "record identity must be validated before keying"
assert "MakeItemKey" in impl, "item key must use the shared normalizer"
assert "ClassifyDrop(context)" in impl, "drop must go through the shared classifier"
assert "DropVerdict::PassThrough" in impl, "passthrough drops must reach the engine"
assert "DropVerdict::Block" in impl, "blocked drops must be swallowed without an engine call"
assert "BeginOwnedDrop(*itemKey, context.count)" in impl, "gated drop must enqueue a server drop request"
assert "kMaximumPendingOperations" in impl, "gate must bound held requests"
assert "AddItem" not in impl, "gate must never mint items into the player inventory"
assert "thread_local bool g_replayingDrop" in impl, "replay must guard against re-entry"
assert "hasStackData" in impl and "? &stackData : nullptr" in impl, "replay must preserve a null stack-data pointer"
assert impl.index("ReserveOwnedEntity(a_entityId)") < impl.index("a_request.original("), \
    "entity must be reserved before the engine drop is replayed"
assert "BindReference(a_entityId, *createdRef)" in impl, "replayed ref must join the pickup gate"
assert "UnbindOwnedEntity(a_entityId)" in impl, "a ref-less replay must release the reservation"
assert '#include "F4TDropMirrorRefs.h"' in impl, "replay must see the drop mirror"
assert "bool IsMirrored(F4T::Drops::EntityId a_entityId);" in mirror_header, "drop mirror must expose IsMirrored"
mirror_check = mirror[mirror.index("bool IsMirrored("):mirror.index("void Tick()")]
assert "g_refs.find(a_entityId)" in mirror_check and "found->second.get()" in mirror_check, "a live mirror ref must count"
assert "g_work.find(a_entityId)" in mirror_check and "!pending->second.removed" in mirror_check, "a queued mirror must count"
assert impl.index("a_request.original(") < impl.index("DropMirrorRefs::IsMirrored(a_entityId)") < impl.index("BindReference(a_entityId, *createdRef)"), \
    "mirror check must run after the engine drop and before binding"
assert "createdRef->MarkAsDeleted();" in impl, "a replayed duplicate of an existing mirror must be removed"
dup_branch = impl[impl.index("DropMirrorRefs::IsMirrored(a_entityId)"):impl.index("BindReference(a_entityId, *createdRef)")]
assert "InventorySync::UnbindOwnedEntity(a_entityId);" in dup_branch, "the duplicate branch must release the owned-entity reservation"
assert "F4T::RetiredQueue<F4T::Drops::EntityId, Request, kMaximumRetiredRequests> g_retired;" in gate, "retired pickups must use the keyed RetiredQueue"
assert "g_retired.Push(request.bound.entityId, request);" in gate and "g_retired.Take(a_entityId)" in gate, \
    "retired pickups must be pushed by entity and taken by entity"
assert "pop_front" not in gate and "g_retired.begin()" not in gate, "the gate must not hand-roll the retired queue loops"
assert "DropObject(object, nullptr, 1, &position, nullptr)" in downed, "bleed-out drop must still go through the gate"
assert "bypass" not in downed.lower(), "bleed-out drop must not bypass the owned-drop gate"
assert "owned-drop gate holds or refuses" in downed, "a gate-held bleed-out drop must not be reported as refused"

complete = impl[impl.index("void Complete("):]
assert complete.index("g_requests.erase(entry);") < complete.index("ReplayRequest(*request, *a_entityId);"), \
    "held request must leave the map before replay"
assert "if (!a_committed)" in complete, "reject/timeout must not replay"
assert "ReplayRequest(*request, *a_entityId);" in complete, "commits must replay with the carried entity"

assert "using DropObjectFn = F4T::InventoryDropGate::Original;" in hooks, "hook must share the gate signature"
assert "#include \"F4TInventoryDropGate.h\"" in hooks, "hook must include the gate"
hook = hooks[hooks.index("RE::ObjectRefHandle* DropObjectHook("):hooks.index("void PickUpObjectHook(")]
assert hook.index("InventoryDropGate::Hold(") < hook.index("return original(a_this, a_result,"), \
    "gate must run before the engine drop"
assert "return new (a_result) RE::ObjectRefHandle();" in hook, "a held drop must leave an empty handle"
assert "#include <new>" in hooks, "placement new requires <new>"

assert "struct CompletedOp" in sync and "std::optional<EntityId> entityId;" in sync, "completed ops must carry the entity"
assert "result.entityId" in sync, "commits must forward the server-assigned entity"
assert "InventoryDropGate::Complete(completedOp.operationId, completedOp.committed, completedOp.entityId)" in sync, \
    "tick must complete held drops"
assert sync.count("InventoryDropGate::Complete(operationId, false, std::nullopt)") == 2, \
    "welcome and reset must fail held drops closed"
assert "if (F4T::InventoryPickupGate::Complete(completedOp.operationId, completedOp.committed)) {" in sync, \
    "pickup completions must keep working"
commit_loop = sync[sync.index("InventoryPickupGate::Complete(completedOp"):sync.index("InventoryDropGate::Complete(completedOp.operationId")]
assert "g_resyncNeeded = true;" in commit_loop, "a committed pickup that cannot replay must request a resync"
late = sync[sync.index("void HandleLateCommit("):sync.index("bool DrainInbound(")]
assert "ReplayRetired" not in late, "late commits must not replay the pickup under the sync lock"
tick = sync[sync.index("void Tick("):sync.index("void Reset()")]
assert "\t\t}\n\n\t\tfor (const auto entityId : replays) {" in tick, "replays must run after the sync lock is released"
assert "ResetRetired();" in sync[sync.index("void Reset()"):sync.index("Status GetStatus()")], \
    "session reset must clear retired pickups"
retired_queue = (plugin / "../../co-core/include/F4TRetiredQueue.h").read_text(encoding="utf-8")
assert "#include \"F4TRetiredQueue.h\"" in gate, "the gate must include the shared retired queue"
assert retired_queue.index("std::erase_if(m_entries") < retired_queue.index("m_entries.size() >= Capacity") < retired_queue.index("m_entries.pop_front();"), \
    "a retired push must replace the same entity first, then evict oldest-first at the cap"
hold = gate[gate.index("bool Hold("):gate.index("bool ReplayRequest(")]
assert hold.index("kMaximumPendingOperations") < hold.index("FailClosedNotice::Show(") < hold.index("return true;"), \
    "a full pending queue must refuse the pickup with a visible notice and keep it held"
replay = gate[gate.index("bool ReplayRequest("):gate.index("bool Complete(")]
assert replay.index("a_request.original(") < replay.rindex("return true;"), "a replay counts only after the engine pickup runs"
assert "return request && ReplayRequest(*request);" in gate[gate.index("bool ReplayRetired("):gate.index("void ResetRetired(")], \
    "ReplayRetired must report whether the replay ran"
assert "return !ReplayRequest(request);" in gate[gate.index("bool Complete("):gate.index("bool ReplayRetired(")], \
    "a committed pickup must report a failed replay for resync"

assert 'add_files("src/**.cpp' in xmake, "plugin sources must stay globbed"
assert "F4TInventoryDropGate.cpp" not in xmake, "gate source must ride the glob, not a special case"

print("drop gate contract OK")
