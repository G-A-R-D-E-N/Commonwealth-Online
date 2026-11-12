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
assert complete.index("g_requests.erase(entry);") < complete.index("ReplayRequest(*request, *a_entityId)"), \
    "held request must leave the map before replay"
assert "if (!a_committed)" in complete, "reject/timeout must not replay"
assert "ReplayRequest(*request, *a_entityId)" in complete, "commits must replay with the carried entity"

assert "using DropObjectFn = F4T::InventoryDropGate::Original;" in hooks, "hook must share the gate signature"
assert "#include \"F4TInventoryDropGate.h\"" in hooks, "hook must include the gate"
hook = hooks[hooks.index("RE::ObjectRefHandle* DropObjectHook("):hooks.index("void PickUpObjectHook(")]
assert hook.index("InventoryDropGate::Hold(") < hook.index("return original(a_this, a_result,"), \
    "gate must run before the engine drop"
assert "return new (a_result) RE::ObjectRefHandle();" in hook, "a held drop must leave an empty handle"
assert "#include <new>" in hooks, "placement new requires <new>"

assert "bool PlayerCanPlaceDrop();" in header, "the controller check must be declared on the gate"
player_check = impl[impl.index("bool PlayerCanPlaceDrop()"):impl.index("bool Hold(")]
assert "currentProcess" in player_check and "->middleHigh" in player_check and "charController.get()" in player_check, \
    "the controller check must walk player, process, middle-high and controller"
refused = hook[hook.index("InventoryDropGate::PlayerCanPlaceDrop()"):hook.index("InventoryDropGate::Hold(")]
assert "return new (a_result) RE::ObjectRefHandle();" in refused, "a controller-less drop must leave an empty handle"
assert "ShowHUDMessage" in refused, "a refused drop must tell the player"
notice = refused[refused.index("if (isPlayer"):]
assert "a_this == RE::PlayerCharacter::GetSingleton()" in refused, "the notice must be scoped to the player actor"
assert "ShouldLog(g_dropRefusedNoticeMs)" in notice and "ShowHUDMessage" in notice, "the notice must be player-only and rate limited"
assert "ShouldLog(g_dropRefusedLogMs)" in refused, "the refusal warning must be rate limited"
assert "ShowHUDMessage" not in refused[:refused.index("if (isPlayer")], "no notice may precede the player scope"
replay_block = impl[impl.index("bool ReplayRequest("):impl.index("bool PumpReplays(")]
assert "PlayerCanPlaceDrop()" in replay_block and replay_block.index("PlayerCanPlaceDrop()") < replay_block.index("a_request.original("), \
    "a replay must be refused before the engine drop runs"
assert replay_block.index("PlayerCanPlaceDrop()") < replay_block.index("ReserveOwnedEntity("), \
    "a deferred replay must not reserve the entity"
refusal = replay_block[replay_block.index("PlayerCanPlaceDrop()"):replay_block.index("ReserveOwnedEntity(")]
assert "return false;" in refusal and "return;" not in refusal, "a controller-less replay must report deferral, not discard"
assert "void ReplayRequest(" not in impl and replay_block.count("return true;") >= 3, "every other replay exit must report handled"
assert "if (!ReplayRequest(*request, *a_entityId)) {" in complete and "g_retries.push_back(" in complete, \
    "a deferred committed drop must be queued for retry, not erased"
pump = impl[impl.index("bool PumpReplays("):impl.index("void ResetRetries()")]
assert "kReplayRetryAttempts" in pump and "kReplayRetryIntervalMs" in pump, "retries must be capped and spaced"
assert re.search(r"constexpr std::uint32_t kReplayRetryAttempts = [2-9]\d*;", impl), "the retry cap must allow more than one later poll"
assert "ReplayRequest(retry.request, retry.entityId)" in pump, "a retry must replay through the same path"
assert "gaveUp = true;" in pump and "return gaveUp;" in pump, "the final give-up must be reported"
assert pump.index("g_retries = std::move(waiting);") < pump.index("ReplayRequest(retry"), \
    "retries must run outside the gate mutex because the engine drop re-enters the hook"
assert "g_retries.clear();" in impl[impl.index("void ResetRetries()"):], "a reset must drop deferred replays"
assert "bool PumpReplays(std::uint64_t a_nowMs);" in header and "void ResetRetries();" in header, "retry entry points must be declared"
assert "if (F4T::InventoryDropGate::PumpReplays(a_nowMs)) {" in sync, "tick must pump deferred drop replays"
give_up = sync[sync.index("InventoryDropGate::PumpReplays(a_nowMs)"):]
assert "g_resyncNeeded = true;" in give_up[:give_up.index("}\n")+20], "a replay give-up must request a resync, like the pickup gate"
assert "InventoryDropGate::ResetRetries();" in sync, "reset must clear deferred drop replays"

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

# Regression: the kit dropped from the pre-placed Vault reference hung in the air because its collision has no motion; the replay must ground and level it.
settle = impl[impl.index("void SettleCampKit("):impl.index("bool ReplayRequest(")]
assert "F4T::CampGround::SurfaceAt(position.x, position.y, position.z)" in settle and "ground->z" in settle, "the kit must be lowered to the probed ground"
assert "angle.x = 0.0F" in settle and "angle.y = 0.0F" in settle and "Update3DPosition(true)" in settle, "the kit must be levelled and re-applied to the 3D"
assert re.search(r"if \(!ground\) \{\s*return;", settle), "no ground hit must leave the engine placement untouched"
replay_body = impl[impl.index("bool ReplayRequest("):impl.index("bool PumpReplays(")]
assert "== F4T::WorkshopCamps::CampKitItemKey" in replay_body and "SettleCampKit(*createdRef)" in replay_body, "only the camp kit may be settled"
assert replay_body.index("a_request.original(") < replay_body.index("SettleCampKit(*createdRef)") < replay_body.index("BindReference(a_entityId, *createdRef)"), \
    "the kit must be settled after the engine drop and before it is bound"

camp_found = (plugin / "../../CO-SERVER/server-cpp/src/ServerWorkshopCamps.h").read_text(encoding="utf-8")
camp_found = camp_found[camp_found.index("HandleCampFound("):camp_found.index("CampMoveReplayFrames(")]
assert camp_found.count("campFound refused:") == 8,"every refusal in HandleCampFound must log its own reason, including kit and validation status"

assert 'add_files("src/**.cpp' in xmake, "plugin sources must stay globbed"
assert "F4TInventoryDropGate.cpp" not in xmake, "gate source must ride the glob, not a special case"

print("drop gate contract OK")
