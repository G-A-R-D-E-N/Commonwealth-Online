from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
gate = (plugin / "src/F4TInventoryPickupGate.cpp").read_text(encoding="utf-8")
hooks = (plugin / "src/F4TInventoryHooks.cpp").read_text(encoding="utf-8")
sync = (plugin / "src/F4TInventorySync.cpp").read_text(encoding="utf-8")


def body(code, signature):
    start = code.index("{", code.index(signature))
    depth = 0
    for end in range(start, len(code)):
        depth += (code[end] == "{") - (code[end] == "}")
        if depth == 0:
            return code[start + 1:end]
    raise AssertionError(signature)


assert "kEnableInventoryPickupGate" not in gate and "kCoTestFeatures" not in gate, "the pickup gate is unconditional, not a test-features switch"
assert '#include "F4TTestFeatures.h"' not in gate
assert "thread_local bool g_replayingPickup = false;" in gate, "replay requires a thread-local re-entry flag"
lookup = body(gate, "std::optional<Bound> FindBound(")
assert lookup.lstrip().startswith("const auto found = g_bound.find(a_formId);"), "bound lookup must not always succeed"
assert "if (found == g_bound.end()) {\n\t\t\t\treturn std::nullopt;" in lookup
assert 'FindOwnedEntity(std::to_string(a_formId))' in lookup
assert "if (!entity || *entity != found->second.entityId)" in lookup
assert lookup.count("return std::nullopt;") == 2
assert lookup.rstrip().endswith("return found->second;")
assert "std::scoped_lock lock(g_mutex)" in body(sync, "std::optional<EntityId> FindOwnedEntity(")
assert "return g_bindings.FindEntity(a_refKey);" in sync

hold = body(gate, "bool Hold(")
assert hold.lstrip().startswith("if (g_replayingPickup || !a_reference) {\n\t\t\treturn false;"), "replay and null-reference calls must pass through"
assert hold.index("std::scoped_lock lock(g_mutex)") < hold.index("FindBound(")
assert "const auto bound = FindBound(a_reference->GetFormID());" in hold
assert "if (!bound) {\n\t\t\treturn false;" in hold
assert re.findall(r"(?:->|\.)\s*(\w+)\s*\(", hold) == ["GetFormID", "size", "emplace"], "hook may only read identity, inspect its queue and enqueue"
assert "GetHandle" not in hold and ".get()" not in hold and "original(" not in hold
assert hold.count("BeginOwnedPickup(bound->entityId)") == 1
assert "kMaximumPendingOperations" in hold
assert "request.bound.entityId == bound->entityId" in hold
assert "Request{ *bound, a_count, a_sounds, a_original }" in hold

wrapper = body(hooks, "void PickUpObjectHook(")
assert wrapper.count("original(a_this, a_objREFR, a_count, a_playPickUpSounds);") == 1
assert "if (F4T::InventoryPickupGate::Hold(a_objREFR, a_count, a_playPickUpSounds, original)) {\n\t\t\t\treturn;" in wrapper
assert wrapper.index("a_objREFR->GetFormID()") < wrapper.index("original(a_this,"), "the form id must be captured before the original runs"
assert wrapper.index("original(a_this,") < wrapper.index("ShouldLog("), "observation stays after the original"
assert wrapper.split("original(a_this, a_objREFR, a_count, a_playPickUpSounds);", 1)[1].count("a_objREFR") == 0, "no reference access after the original call"
assert re.findall(r"->(\w+)\(", wrapper) == ["GetFormID"]

complete = body(gate, "void Complete(")
assert complete.index("if (!a_committed)") < complete.index("request.bound.handle.get()")
assert "reference->IsDeleted()" in complete and "reference->IsDisabled()" in complete
assert complete.index("if (!reference") < complete.index("struct ReplayScope")
assert "ReplayScope() { g_replayingPickup = true; }" in complete
assert "~ReplayScope() { g_replayingPickup = false; }" in complete
assert complete.index("} replayScope;") < complete.index("request.original(")
assert complete.count("request.original(") == 1
assert gate.count("request.original(") == 1
assert "InventoryPickupGate::Complete(operationId, committed);" in body(sync, "void Tick(")
assert 'completed.emplace_back(outcome.operationId, false);' in sync
assert 'a_completed.emplace_back(result.operationId, true);' in sync
assert 'a_completed.emplace_back(result.operationId, false);' in sync
bind = body(gate, "bool BindReference(")
assert "kBOOK" in bind and "kNOTE" in bind
assert "a_reference.GetHandle()" in bind and "BindOwnedEntity(" in bind
assert "kMaximumBoundEntities" in bind
for path in (plugin / "src/F4TInventoryHooks.cpp", plugin / "src/F4TInventoryPickupGate.cpp", plugin / "src/F4TNetworking.cpp"):
    assert len(path.read_text(encoding="utf-8").splitlines()) <= 750, path.name
print("F4TInventoryPickupGateContract: all checks passed.")
