"""Contract: drop mirror refs never reach the CO save (audit 3 H4). Verified record source:CodeNotes-GARDEN history/audit3-drop-mirror-save.md."""
from pathlib import Path

plugin = Path(__file__).resolve().parents[1]
guard = (plugin / "src/F4TSessionSaveGuard.cpp").read_text(encoding="utf-8")
mirror = (plugin / "src/F4TDropMirrorRefs.cpp").read_text(encoding="utf-8")
mirror_h = (plugin / "include/F4TDropMirrorRefs.h").read_text(encoding="utf-8")
safety = (plugin / "src/F4TProxySaveSafety.cpp").read_text(encoding="utf-8")


def function_body(code, signature):
    start = code.index(signature)
    indent = code[code.rindex("\n", 0, start) + 1:start]
    return code[start:code.index("\n" + indent + "}\n", start) + len(indent) + 3]


call = "if (F4T::DropMirrorRefs::DestroyAll()) {"
assert '#include "F4TDropMirrorRefs.h"' in guard, "the save guard must reach the drop mirror teardown"
assert call in guard and guard.index(call) < guard.index("const auto outerFrameSave = t_frameSaveInProgress;"), "a save must destroy the drop mirrors before the world is written"
assert "ParkedSuitMirror::DestroyAll()" in guard and guard.index("ParkedSuitMirror::DestroyAll()") < guard.index(call), "the drop mirror teardown sits next to the parked suit teardown"
pending = guard[guard.index(call):guard.index("const auto outerFrameSave = t_frameSaveInProgress;")]
assert "NoteEntered(owner, false);" in pending and "return false;" in pending, "a teardown still pending must refuse the save so it retries"
assert "bool DestroyAll();" in mirror_h and "bool OwnsRef(std::uint32_t a_refFormId);" in mirror_h, "the teardown and the ownership check must be declared"

destroy_all = function_body(mirror, "void DestroyAllOnGameThread()")
assert "DeleteRef(handle);" in destroy_all and "g_work.emplace(entityId, Work{ g_spawned.at(entityId) });" in destroy_all, "a teardown must re-queue every mirror so the next tick re-creates it"
assert "insert_or_assign" not in destroy_all, "a teardown must not overwrite pending work (a pickup removal or a newer entry)"
assert "else if (pending->second.removed) {" in destroy_all and "g_spawned.erase(entityId);" in destroy_all, "a drop picked up just before the save must stay removed, not be re-created"
assert "UnbindMirrorEntity" in destroy_all and "g_refs.clear();" in destroy_all, "a teardown must clear the live refs and their bindings"
assert "InventorySync::Reset" not in destroy_all and "g_dropMirror" not in destroy_all, "a teardown must leave the ledger state alone"
destroy_all_entry = function_body(mirror, "bool DestroyAll()")
assert "IsOnRecordedGameThread()" in destroy_all_entry and "AddTask" in destroy_all_entry, "off the game thread the teardown must queue a game-thread task"
assert "IsProxyTickBlocked()" in function_body(mirror, "void Tick()"), "mirrors must not be re-created while a save is being written"

materialize = function_body(mirror, "SpawnResult Materialize(")
assert "F4T::ProxySaveSafety::RecordStandIn(ref->GetFormID());" in materialize and "g_spawned[a_entry.entityId] = a_entry;" in materialize, "a created mirror ref must be recorded as a stand-in and kept for re-creation"
assert "F4T::ProxySaveSafety::ForgetStandIn(ref->GetFormID());" in function_body(mirror, "void DeleteRef("), "a despawned mirror ref must leave the stand-in set"

sweep = function_body(safety, "void SweepSavedStandInObjects()")
assert "ref->As<RE::Actor>()" in sweep and "DropMirrorRefs::OwnsRef(formId)" in sweep and "DisableAndDeleteRef(*ref);" in sweep, "the load sweep must delete recorded non-actor refs that no live mirror owns"
after_load = function_body(safety, "void AfterLoadSweep(")
assert after_load.index("SweepSavedStandInObjects();") < after_load.index("PruneStandInFormIds();"), "the object sweep must run before the stand-in prune"
print("drop mirror save contract ok")
