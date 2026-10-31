from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
header = (plugin / "include/F4TDropMirrorRefs.h").read_text(encoding="utf-8")
source = (plugin / "src/F4TDropMirrorRefs.cpp").read_text(encoding="utf-8")
sync = (plugin / "src/F4TInventorySync.cpp").read_text(encoding="utf-8")
runtime = (plugin / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8")
note = (root / "docs/code-notes/inventory-boundaries.md").read_text(encoding="utf-8")

assert "kCoTestFeatures" not in header and "kCoTestFeatures" not in source, "the drop mirror has no test-features switch"
callers = [p.name for p in (plugin / "src").glob("*.cpp") if "F4T::DropMirrorRefs::Tick();" in p.read_text(encoding="utf-8")]
assert callers == ["F4TReplicationRuntime.cpp"]
assert runtime.index("InventorySync::Tick") < runtime.index("DropMirrorRefs::Tick")
assert "kWorkPerTick = 8" in source and "processed < budget" in source
assert "kMaximumRetries = 256" in source and "retries < kMaximumRetries" in source
assert "kRetryDeadline" not in source and "std::chrono::seconds(60)" not in source
assert "the drop stays queued until a retry slot opens" in source
assert "keeping drop until a slot opens" not in source
full_queue = source[source.index("drop_mirror_retry_full"):source.index("if (work.retry)", source.index("drop_mirror_retry_full"))]
assert "continue;" in full_queue and "erase" not in full_queue and "return" not in full_queue, \
    "a full retry queue must keep the drop queued; it must not erase it or leave Tick"
print("full retry queue keeps the drop queued: continue, no erase, no return")
assert "kMirrorDropForcePersist = true" in source
assert "data.forcePersist = kMirrorDropForcePersist;" in source
assert "if (!entry.materialize)" in source and "g_work.erase(entry.entityId)" in source
assert "g_bindings.ShouldMaterialize(entry.entityId)" in sync
assert "g_mirrorOwned" in sync and "g_mirrorOwned.clear();" in sync
assert "BindMirrorEntity" in source and "UnbindMirrorEntity" in source
assert "LookupLoadedModByName" in source and "LookupLoadedLightModByName" in source
assert "LookupForm" not in source and "GetFile(" not in source
assert "PickUp" not in source and "Activate" not in source
assert "data.reference = nullptr;" in source and "data.extra = nullptr;" in source
assert "data.initializeScripts = true;" in source and "data.clearStillLoadingFlag = true;" in source
assert "std::numeric_limits<std::int16_t>::max()" in source
assert "extraList->SetCount" in source and "AddChange(0x400)" in source
assert source.count("->Disable();") == 1 and source.count("->MarkAsDeleted();") == 1
assert re.search(r"if \(work.removed\)\s*\{\s*Despawn\(it->first\);", source)
assert source.index("!cell->IsAttached()") < source.index("Despawn(a_entry.entityId)")
assert source.count("Despawn(") == 5, "only removal, replacement, a same-tick removal or a taken-over entry may despawn"
assert "g_resetRemoved.insert(entityId)" in sync and "++g_mirrorGeneration" in sync
assert "Anchors: `CO-BETA/plugin/src/F4TDropMirrorRefs.cpp:kMirrorDropForcePersist`" in note
assert "`kMirrorDropForcePersist` is **true**" in note
assert "`data.extra = nullptr` as the **INV-9 `extra` decision**" in note
assert "UnbindMirrorEntity" in note and "MarkAsDeleted" in note
assert "EncodeStableDroppedObject" in note and "NormalizeStartingInventory" in note

sync_header = (plugin / "include/F4TInventorySync.h").read_text(encoding="utf-8")
tick = source[source.index("void Tick()"):]
materialize = source[source.index("SpawnResult Materialize("):source.index("void Tick()")]

assert re.search(r"\} else \{\n\t+Warn\(\"drop_mirror_create_failed\"[^\n]*\n\t+return SpawnResult::Deferred;", materialize), \
    "a failed CreateReferenceAtLocation must defer and retry instead of vanishing"
assert materialize.index("CreateReferenceAtLocation(data)") < materialize.index("if (g_refs.contains(a_entry.entityId))"), \
    "the replaced reference must only be despawned once the new one exists"
assert re.search(r"if \(count > 1\) \{", materialize), \
    "a count of one needs neither SetCount nor AddChange"
assert re.search(r"auto\* extraList = ref->extraList\.get\(\);\n\t+if \(!extraList\) \{\n\t+ref->extraList = RE::BSTSmartPointer<RE::ExtraDataList>\{ new RE::ExtraDataList\(\) \};", materialize), \
    "the extra list must be read once and reallocated when the engine created the ref without one"
assert materialize.index("if (count > 1)") < materialize.index("if (!extraList)") < materialize.index("extraList->SetCount("), \
    "SetCount must sit behind the count gate and the null-extraList guard"
assert materialize.index("if (extraList) {") < materialize.index("extraList->SetCount(") < materialize.index("ref->AddChange(0x400)") < materialize.index("} else {"), \
    "SetCount and AddChange share the guarded branch"

assert "MirrorGeneration" not in source and "MirrorGeneration" not in sync and "MirrorGeneration" not in sync_header, \
    "the generation must not be read through a second lock acquisition"
assert "std::uint64_t& a_generation" in sync_header
assert source.count("TakeDropMirrorDiff(generation)") == 1 and "std::uint64_t generation{};" in source
diff_body = sync[sync.index("F4T::Drops::MirrorDiff TakeDropMirrorDiff("):]
diff_body = diff_body[:diff_body.index("\n\t}")]
assert diff_body.count("g_mutex") == 1, "the generation and the diff must share one lock acquisition"
assert re.search(r"std::scoped_lock lock\(g_mutex\);\n\t\ta_generation = g_mirrorGeneration;\n\t\tauto diff = g_dropMirror\.TakeDiff\(\);", diff_body), \
    "the generation must be read inside the same critical section as the diff"

assert tick.count("for (const auto entityId : diff.removed)") == 2, \
    "removed ids are revisited after adds so an id that is both removed and re-added despawns at once"
assert re.search(r"const auto found = g_work\.find\(entityId\);\n\t+if \(found != g_work\.end\(\) && !found->second\.removed\) \{\n\t+Despawn\(entityId\);", tick), \
    "a re-added id must despawn the old reference in the same tick"
assert tick.index("&diff.added, &diff.changed") < tick.index("g_work.find(entityId)")

assert re.search(r"if \(!entry\.materialize\) \{\n\t+if \(g_refs\.contains\(entry\.entityId\)\) \{\n\t+Despawn\(entry\.entityId\);\n\t+\}\n\t+g_work\.erase\(entry\.entityId\);", tick), \
    "an entry another binding took over must despawn the reference the mirror still holds, or the old ref stays bound"

assert re.search(r"const auto budget = std::min\(kWorkPerTick, g_work\.size\(\)\);", source)
assert "processed < budget" in source and "processed < kWorkPerTick" not in source, \
    "one deferred entry must not be retried kWorkPerTick times inside a single tick"

assert "IsMirrorOwned" not in source and "IsMirrorOwned" not in sync and "IsMirrorOwned" not in sync_header, \
    "IsMirrorOwned has no callers"
assert "!a_entry.materialize" not in source, "Tick already erases entries the bindings own"
assert sync.count("void BindMirrorEntity(") == 1 and sync.count("void UnbindMirrorEntity(") == 1, \
    "the mirror bind and unbind entry points stay paired"
assert source.count("BindMirrorEntity(") == 1 and source.count("UnbindMirrorEntity(") == 1, \
    "a mirrored reference is bound on create and unbound on despawn"

print("F4TDropMirrorRefsContract: all checks passed.")
