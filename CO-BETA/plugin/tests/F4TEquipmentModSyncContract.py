from pathlib import Path
import re
# TODO-verify: concept-level wire and plan checks, engine facts unverified; source:docs/code-notes/power-armor-sync.md.


ROOT = Path(__file__).resolve().parents[3]
PLUGIN_DIR = ROOT / "CO-BETA/plugin"

state_h = (PLUGIN_DIR / "include/F4TRemotePlayerState.h").read_text(encoding="utf-8")
instance_h = (PLUGIN_DIR / "include/F4TEquipmentInstance.h").read_text(encoding="utf-8")
capture_cpp = (PLUGIN_DIR / "src/F4TLocalPlayerState.cpp").read_text(encoding="utf-8")
apply_cpp = (PLUGIN_DIR / "src/F4TProxyActorControllerEquipment.cpp").read_text(encoding="utf-8")
power_armor_cpp = (PLUGIN_DIR / "src/F4TPowerArmorSync.cpp").read_text(encoding="utf-8")
power_armor_apply_cpp = (PLUGIN_DIR / "src/F4TProxyActorControllerPowerArmor.cpp").read_text(encoding="utf-8")
weapon_mods_cpp = (PLUGIN_DIR / "src/F4TProxyActorControllerWeaponMods.cpp").read_text(encoding="utf-8")
state_tests = (PLUGIN_DIR / "tests/F4TEquipmentStateTests.cpp").read_text(encoding="utf-8")

assert "kEnableEquipmentModSync" not in state_h, "the mod sync gate constant must be gone, not merely true"
assert "F4T::kCoTestFeatures" not in state_h, "Mod sync must not follow the test-features switch"
assert "F4T_TARGET_OG" not in state_h and "F4T_TARGET_NG" not in state_h and "F4T_TARGET_AE" not in state_h, "the gate must not vary by runtime target"
for name in ("RuntimeTarget", "RuntimeVersion", "IsVerifiedRuntime", "kVerifiedRuntimeVersion", "kTargetOg"):
    assert name not in state_h, f"{name} was part of the runtime guard and must be gone"
assert '#include "F4TTestFeatures.h"' not in state_h

assert not (PLUGIN_DIR / "src/F4TEquipmentInstance.cpp").exists(), "the runtime guard source file must be gone"
for name in ("IsRuntimeSupported", "GetFileVersion", "REX::WARN"):
    assert name not in instance_h, f"the engine-facing accessors must not fail closed on {name}"

assert instance_h.count("if (!a_stack.extra) {") == 1, "ReadStackMods must null-check the stack extra list"
assert "if (!instanceExtra || !instanceExtra->values) {" in instance_h, "ReadStackMods must null-check the extra and its data buffer before GetIndexData"
assert instance_h.index("!a_stack.extra") < instance_h.index("GetByType<RE::BGSObjectInstanceExtra>()") < instance_h.index("GetIndexData()")
assert "if (!list) {" in instance_h and instance_h.index("if (!list) {") < instance_h.index("list->data")
assert "if (!item.object || item.object->GetFormID() != a_baseFormId) {" in instance_h, "the inventory walk must null-check each item"
assert "for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackId) {" in instance_h, "the stack walk must null-check every link"
assert "if (mods.size() > EquipmentState::kMaximumItemMods) {" in instance_h, "an oversized recipe must be dropped, not truncated"

for source in [*(PLUGIN_DIR / "include").rglob("*.h"), *(PLUGIN_DIR / "src").rglob("*.cpp"), *(PLUGIN_DIR / "tests").rglob("*.cpp")]:
    text = source.read_text(encoding="utf-8")
    assert "kEnableEquipmentModSync" not in text, f"{source} still references the removed mod sync gate"
assert "kEnableEquipmentModSync" not in state_tests
assert "static_assert(RemotePlayerState::kEnableEquipmentModSync)" not in state_tests
assert "Equipment mod sync is disabled" not in apply_cpp
assert "proxy_equipment_recipe_ignored_" not in apply_cpp

capture_calls = [m.start() for m in re.finditer(r"CaptureEquippedItemMods\(", capture_cpp)]
assert len(capture_calls) == 2, "Capture must capture every synced slot plus the right hand"
assert re.search(
    r"const auto armorRecipes = F4T::EquipmentInstance::CaptureEquippedItemMods\(a_player, armorFormIds\);",
    capture_cpp), "Armor recipes must be captured in one inventory pass per publish"
assert re.search(
    r"if \(armorFormIds\[i\] != 0\) \{\s*item\.mods = armorRecipes\[i\];",
    capture_cpp), "Armor recipe capture must run on every build"
assert re.search(
    r"armorFormIds\.push_back\(GetEquippedArmorFormId\(a_player, slot\.bipedObject\)\);",
    capture_cpp), "Armor recipe capture must read the equipped biped form"
assert re.search(
    r"if \(rightHandWeaponFormId != 0\) \{\s*equippedItems\.back\(\)\.mods = F4T::EquipmentInstance::CaptureEquippedItemMods\(",
    capture_cpp), "Capture must publish the recipe it read"
assert instance_h.count("BSAutoReadLock") == 2, "One inventory read lock per publish plus the single-stack lookup"
assert re.search(
    r"inline FoundStack FindStackIndexed\(RE::Actor& a_actor, std::uint32_t a_baseFormId, Match&& a_match\)",
    instance_h), "A tracked stack is located by form id and stack id"
assert re.search(
    r"inline std::vector<std::optional<std::vector<RemoteItemMod>>> CaptureEquippedItemMods\(\s*RE::Actor& a_actor,\s*const std::vector<std::uint32_t>& a_baseFormIds\)",
    instance_h), "The batch capture overload must exist"

assert "proxy_equipment_recipe_ignored_" not in apply_cpp
assert "preserving current proxy equipment" not in apply_cpp
assert apply_cpp.count("RemoveTrackedModdedStack(a_slot, a_proxy);") == 3
assert re.search(
    r"if \(item\.mods && !item\.mods->empty\(\)\) \{\s*rightHandMods = std::addressof\(\*item\.mods\);",
    apply_cpp), "Received mods must be used whenever a recipe arrives"
assert (apply_cpp + weapon_mods_cpp).count("EquipSyncedWeaponWithMods(") == 2
assert re.search(r"if \(rightHandMods\) \{\s*const auto modded = EquipSyncedWeaponWithMods\(", apply_cpp)
assert re.search(
    r"const auto stackId = F4T::EquipmentInstance::FindEquippedStack\(",
    apply_cpp), "Unequip must resolve the equipped stack id on every build"
assert re.search(r"if \(rightHandWeaponFormId != 0 && currentWeaponFormId == rightHandWeaponFormId\)", apply_cpp)
assert "weaponModsStale = false" in apply_cpp

ledger_h = (PLUGIN_DIR / "include/F4TProxyEquipmentStacks.h").read_text(encoding="utf-8")
apply_h = (PLUGIN_DIR / "include/F4TProxyEquipmentApply.h").read_text(encoding="utf-8")
stacks_cpp = (PLUGIN_DIR / "src/F4TProxyActorControllerEquipmentStacks.cpp").read_text(encoding="utf-8")
slot_h = (PLUGIN_DIR / "include/F4TProxyActorControllerInternal.h").read_text(encoding="utf-8")

assert "F4T::ProxyEquipmentStacks::Ledger syncedEquipmentStacks;" in slot_h, "Each proxy slot owns its stack ledger"
assert "struct TrackedStack" in ledger_h and "std::uint32_t stackId{};" in ledger_h, "A tracked stack carries its stack id"
assert "void Track(TrackedStack a_stack)" in ledger_h and "std::vector<TrackedStack> ForgetAll()" in ledger_h
assert "bool KeepTrackedStack(" in ledger_h and "bool SlotNeedsWork(" in ledger_h, "The apply decisions must live in the engine-free ledger header"
assert "bool MatchesTrackedStack(" in ledger_h, "Stack identity must live in the engine-free ledger header"
assert "bool RetryTrackedStack(" in ledger_h, "The removal outcome rule must live in the engine-free ledger header"
stacks_tests = (PLUGIN_DIR / "tests/F4TProxyEquipmentStacksTests.cpp").read_text(encoding="utf-8")
assert "F4T::ProxyEquipmentStacks::KeepTrackedStack(" in stacks_tests and "F4T::ProxyEquipmentStacks::SlotNeedsWork(" in stacks_tests, "The stack test must call the shared decisions"
assert "F4T::ProxyEquipmentStacks::MatchesTrackedStack(" in stacks_tests and "F4T::ProxyEquipmentStacks::RetryTrackedStack(" in stacks_tests, "The stack test must call the shared identity and removal rules"
assert "[[nodiscard]] F4T::ProxyEquipmentStacks::StackRemovalResult RemoveTrackedStack(" in apply_h, "Removal must report why it did not remove the stack and which index it took; source:docs/code-notes/equipment-state.md"
assert re.search(
    r"RemoveTrackedStack\(\s*F4T::ProxyEquipmentStacks::Ledger& a_ledger,\s*RE::Actor& a_proxy,\s*const F4T::ProxyEquipmentStacks::TrackedStack& a_tracked,\s*std::string_view a_logKey\)",
    apply_h), "A removal must be able to shift the ledger's stack ids; source:docs/code-notes/equipment-state.md"
assert re.search(
    r"a_proxy\.RemoveItem\(removeData\);\s*//[^\n]*\n\s*const auto unlinked = CountStacks\(a_proxy, a_tracked\.formId\) < stacksBefore;\s*if \(unlinked\) \{\s*a_ledger\.ShiftStackIdsAfterRemoval\(a_tracked\.formId, found\.stackId\);\s*\}\s*a_ledger\.RefreshStackIds\(",
    stacks_cpp), "Only an unlinked stack may shift the null-extra entries above it, and the ledger refresh follows the shift; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert "return { StackRemoval::kRemoved, found.stackId, unlinked };" in stacks_cpp, "A removal must report whether it unlinked the stack; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert stacks_cpp.count("if (removal.unlinked) {") == 2, "Both pending-list shifts must be gated on the unlink; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert re.search(
    r"void RemoveTrackedStacks\(F4T::ProxyEquipmentStacks::Ledger& a_ledger, RE::Actor& a_proxy, std::string_view a_logKey\)\s*\{\s*auto pending = a_ledger\.ForgetAll\(\);\s*DropVanishedNullExtra\(a_proxy, pending\);\s*(?:if \(!pending\.empty\(\) && F4T::DebugLogging::IsEnabled\(\)\) \{[\s\S]*?\}\s*)?for \(const auto& tracked : pending\) \{\s*const auto removal = RemoveTrackedStack\(a_ledger, a_proxy, tracked, a_logKey\);\s*(?://[^\n]*\n\s*)?if \(removal\.outcome == StackRemoval::kRemoved\) \{\s*if \(removal\.unlinked\) \{\s*F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval\(pending, tracked\.formId, removal\.stackId\);\s*\}\s*RefreshStackIds\(a_proxy, pending\);\s*continue;\s*\}\s*if \(F4T::ProxyEquipmentStacks::RetryTrackedStack\(removal\.outcome\)\) \{\s*(?:if \(F4T::DebugLogging::IsEnabled\(\)\) \{[\s\S]*?\}\s*)?a_ledger\.Track\(tracked\);",
    stacks_cpp), "Clearing a slot must drop every tracked stack and keep a blocked one for the next pass; live ids re-resolved per source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert "RemoveTrackedStacks(a_slot.syncedEquipmentStacks, a_proxy, std::to_string(a_slot.remotePlayerId));" in apply_cpp, "Slot clear must release the tracked stacks"
assert re.search(
    r"const auto added = AddTrackedArmorCopy\(\s*a_slot\.syncedEquipmentStacks,\s*a_proxy,\s*\*armor,\s*target\.mods,",
    apply_cpp), "Armor must be applied with its received recipe"
assert "const auto* targetMods = ResolveTargetMods(item);" in apply_cpp
assert re.search(
    r"if \(a_item\.mods && !a_item\.mods->empty\(\)\) \{\s*return std::addressof\(\*a_item\.mods\);",
    stacks_cpp), "Armor recipes must be used whenever they arrive"
assert re.search(
    r"slotNeedsWork = F4T::ProxyEquipmentStacks::SlotNeedsWork\(",
    apply_cpp), "Slot staleness must be decided by the shared helper"
assert re.search(
    r"const bool keep = F4T::ProxyEquipmentStacks::KeepTrackedStack\(\s*wantedForm,\s*found\.stack != nullptr,",
    stacks_cpp), "Retention must be decided by the shared helper"
assert re.search(
    r"const auto removal = keep\s*\? F4T::ProxyEquipmentStacks::StackRemovalResult\{\}\s*: RemoveTrackedStack\(a_ledger, a_proxy, tracked, a_logKey\);\s*if \(removal\.outcome == StackRemoval::kRemoved\) \{\s*(?://[^\n]*\n\s*)?if \(removal\.unlinked\) \{\s*F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval\(pending, tracked\.formId, removal\.stackId\);\s*F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval\(kept, tracked\.formId, removal\.stackId\);\s*\}\s*RefreshStackIds\(a_proxy, pending\);\s*RefreshStackIds\(a_proxy, kept\);\s*continue;\s*\}\s*if \(keep \|\| F4T::ProxyEquipmentStacks::RetryTrackedStack\(removal\.outcome\)\) \{\s*(?:if \(F4T::DebugLogging::IsEnabled\(\)\) \{[\s\S]*?\}\s*)?a_ledger\.Track\(tracked\);",
    stacks_cpp), "Retention must keep what it cannot remove, shift the drained and kept ids above a removed stack, and re-resolve live ids per source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert re.search(
    r"const bool recipeMatches = found\.stack && F4T::EquipmentInstance::SameMods\(",
    apply_cpp), "A tracked stack may only be reused when its recipe matches"
assert re.search(
    r"if \(existing\.stack\) \{\s*return AddedArmorStack\{ existing\.stackId, nullptr \};",
    stacks_cpp), "A stack this client did not add must not be tracked"
assert re.search(
    r"const TrackedStack added\{ a_armor\.GetFormID\(\), found\.stackId, found\.stack->extra\.get\(\) \};\s*if \(F4T::ProxyEquipmentStacks::RetryTrackedStack\(RemoveTrackedStack\(a_ledger, a_proxy, added, a_logKey\)\.outcome\)\) \{\s*a_ledger\.Track\(added\);",
    stacks_cpp), "A failed recipe must not leave the added copy behind; source:docs/code-notes/equipment-state.md"
assert stacks_cpp.count("CreateInstanceData(") == 1, "Armor instance data comes from the added stack extra"
assert stacks_cpp.count("HasInstanceDataFailure(failureKey)") == 1 and stacks_cpp.count("RememberInstanceDataFailure(failureKey)") == 1, "A recipe copy whose instance data failed must be remembered and not re-added; source:docs/code-notes/CO-BETA-plugin.md"
failure_guard = "if (a_ledger.HasInstanceDataFailure(failureKey)) {"
assert failure_guard in stacks_cpp, "The recipe memo must gate the recipe copy on a bare HasInstanceDataFailure(failureKey), never negated or disabled; source:docs/code-notes/CO-BETA-plugin.md"
add_tracked_body = stacks_cpp[stacks_cpp.index("std::optional<AddedArmorStack> AddTrackedArmorCopy("):]
memo_pos = add_tracked_body.index("HasInstanceDataFailure(failureKey)")
assert memo_pos < add_tracked_body.index("AddObjectToContainer(", memo_pos), "The recipe memo must be checked before the recipe copy is added inside AddTrackedArmorCopy; source:docs/code-notes/CO-BETA-plugin.md"
failure_body = stacks_cpp[stacks_cpp.index(failure_guard):]
failure_body = failure_body[:failure_body.index("\n\t\t}\n")]
assert "return std::nullopt;" in failure_body and "return AddedArmorStack" not in failure_body, "A remembered recipe must return nullopt so the base copy fallback runs"
assert re.search(
    r"const auto found = F4T::EquipmentInstance::FindStack\(a_proxy, a_armor\.GetFormID\(\),\s*\[&\]\(const RE::BGSInventoryItem::Stack& a_stack\) \{ return a_stack\.extra\.get\(\) == extraList\.get\(\); \}\);\s*const bool owned = found\.stack != nullptr;",
    stacks_cpp), "Only the extra pointer match proves ownership; source:docs/code-notes/CO-BETA-plugin.md"
not_owned_pos = add_tracked_body.index("if (!owned) {")
not_owned_body = add_tracked_body[not_owned_pos:]
not_owned_body = not_owned_body[:not_owned_body.index("\n\t\t}\n")]
assert "RemoveTrackedStack(" not in not_owned_body, "A pointer miss must not remove any stack; source:docs/code-notes/CO-BETA-plugin.md"
assert "return std::nullopt;" in not_owned_body, "A pointer miss must return before the instance data memo; source:docs/code-notes/CO-BETA-plugin.md"
assert re.search(r"\n\t\t\treturn std::nullopt;\Z", not_owned_body), "A pointer miss must end in its own unconditional return std::nullopt; source:docs/code-notes/CO-BETA-plugin.md"
assert not_owned_pos < add_tracked_body.index("RememberInstanceDataFailure(failureKey)"), "A pointer miss must return before the instance data failure memo; source:docs/code-notes/CO-BETA-plugin.md"
assert re.search(
    r"a_ledger\.Track\(TrackedStack\{ a_armor\.GetFormID\(\), found\.stackId, found\.stack->extra\.get\(\) \}\);\s*return AddedArmorStack\{ found\.stackId, instanceData \};",
    stacks_cpp), "Only a mods copy this client added is tracked"
assert re.search(
    r"const auto removal = RemoveTrackedStack\(a_slot\.syncedEquipmentStacks, a_proxy, \*tracked, std::to_string\(a_remotePlayer\.playerId\)\);\s*if \(removal\.outcome == StackRemoval::kRemoved\) \{\s*allApplied = false;\s*continue;\s*\}",
    apply_cpp), "A stale stack that could not be removed must fall through to the add path in the same pass; source:docs/code-notes/CO-BETA-plugin.md"
assert '"proxy_ledger_forget_" + std::string{ a_logKey }' in stacks_cpp, "The ledger forget throttle must be keyed per remote player"
assert '"proxy_ledger_retrack_" + std::string{ a_logKey }' in stacks_cpp and '"proxy_361_ledger_keep_" + std::string{ a_logKey }' in stacks_cpp and '"proxy_ledger_retain_" + std::string{ a_logKey }' in stacks_cpp, "The retrack, keep and retain throttles must be keyed per remote player"
assert 'a_tracked.extra ? "361-F" : "361-K"' in stacks_cpp, "A null-extra removal census must log under 361-K"
assert "if (!object || found.stack->IsEquipped()) {" in stacks_cpp, "An equipped or unresolved tracked stack must be blocked, never removed"
assert "!F4T::ProxyEquipmentStacks::HasTrackedForm(kept, tracked.formId)" in stacks_cpp, "Retention must keep at most one ledger row per form id"
assert re.search(
    r"\[\[nodiscard\]\] F4T::EquipmentInstance::FoundStack FindTrackedStack\(\s*RE::Actor& a_proxy,\s*const F4T::ProxyEquipmentStacks::TrackedStack& a_tracked\)",
    apply_h), "One shared lookup identifies a tracked stack"
assert re.search(
    r"a_slot\.syncedEquipmentStacks\.Track\(\s*TrackedStack\{ rightHandWeaponFormId, added\.stackId, added\.stack->extra\.get\(\) \}\);",
    apply_cpp), "A base weapon copy must be tracked where it is added"
assert apply_cpp.count("syncedEquipmentStacks.Track(") == 1, "The applier writes the ledger only for the base weapon copy; the retention pass lives in RetainSyncedStacks"
assert stacks_cpp.count("a_ledger.Track(") == 5, "Only stacks this client added are tracked, plus the blocked one a slot clear keeps"
assert "void ShiftTrackedStackIdsAfterRemoval(" in ledger_h and "struct StackRemovalResult" in ledger_h, "The index shift and the removal result must live in the engine-free ledger header"
assert "void ShiftStackIdsAfterRemoval(std::uint32_t a_formId, std::uint32_t a_removedStackId)" in ledger_h
assert "F4T::ProxyEquipmentStacks::ShiftTrackedStackIdsAfterRemoval(" in stacks_tests, "The stack test must call the shared index shift"
assert "void RefreshStackIds(" in ledger_h and "RefreshTrackedStackIds(" in ledger_h, "The live id re-resolution must live in the engine-free ledger header; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert stacks_cpp.count("RefreshStackIds(a_proxy, pending);") == 2 and stacks_cpp.count("RefreshStackIds(a_proxy, kept);") == 1 and stacks_cpp.count("a_ledger.RefreshStackIds(") == 1, "Every removal site must re-resolve the tracked ids from the live chain; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert re.search(
    r"const auto removal = RemoveTrackedStack\(a_slot\.syncedEquipmentStacks, a_proxy, tracked, std::to_string\(a_slot\.remotePlayerId\)\);\s*if \(!F4T::ProxyEquipmentStacks::RetryTrackedStack\(removal\.outcome\)\) \{\s*a_slot\.syncedModdedWeaponExtra = nullptr;",
    apply_cpp), "A blocked modded weapon removal must keep the slot pointer; source:docs/code-notes/equipment-state.md"
assert re.search(
    r"void ResetSlotEquipmentTracking\(ProxyActorSlot& a_slot, bool a_forgetTrackedStacks\)\s*\{\s*F4T::PowerArmorSync::DestroyPlacedFrame\([^;]*\);\s*a_slot\.placedPowerArmorFrame\.placements = 0;\s*a_slot\.hasAppliedEquipmentState = false;\s*a_slot\.lastAppliedEquipmentItems\.clear\(\);\s*if \(a_forgetTrackedStacks\) \{\s*const auto forgotten = a_slot\.syncedEquipmentStacks\.ForgetAll\(\)\.size\(\);\s*F4T::DebugLogging::Log\([^;]*\);\s*\}\s*a_slot\.syncedEquipmentStacks\.ForgetInstanceDataFailures\(\);\s*F4T::DebugLogging::Log\([^;]*\);\s*a_slot\.syncedModdedWeaponExtra = nullptr;\s*a_slot\.syncedModdedWeaponFormId = 0;\s*a_slot\.lastAppliedPowerArmorState = \{\};\s*a_slot\.appliedPowerArmorFrameFormId = 0;\s*\}",
    apply_cpp), "A slot reset clears applied state; forget=true also drops the ledger on proxy-gone paths; source:docs/code-notes/equipment-state.md"
assert re.search(r"piece\.mods = [^\n]*CaptureEquippedItemMods\(", power_armor_cpp), "Power armor piece mod capture must be assigned unconditionally"
assert "if constexpr" not in power_armor_cpp, "the power armor piece path must not be gated at compile time"
instance_h = (PLUGIN_DIR / "include/F4TEquipmentInstance.h").read_text(encoding="utf-8")
assert "ResolveModdedStack" in instance_h, "the modded-stack builder must live in one shared helper"
assert instance_h.index("GetFormByID<RE::BGSMod::Attachment::Mod>") < instance_h.index("new RE::BGSObjectInstanceExtra"), "Resolve mod forms before allocating"
assert instance_h.index("SameMods(*stackMods, *a_mods)") < instance_h.index("new RE::BGSObjectInstanceExtra"), "Reuse an existing stack with identical mods before allocating"
assert weapon_mods_cpp.count("ResolveModdedStack(") == 1, "the weapon path must go through the shared helper"
assert power_armor_cpp.count("ResolveModdedStack(") == 1, "the power armor path must go through the shared helper"
assert "new RE::BGSObjectInstanceExtra" not in apply_cpp, "the weapon path must not rebuild the stack itself"
assert "new RE::BGSObjectInstanceExtra" not in power_armor_cpp, "the power armor path must not rebuild the stack itself"
assert re.search(
    r"if \(resolved\.status != F4T::EquipmentInstance::ModdedStackStatus::kResolved\) \{\s*return \{\};\s*\}",
    weapon_mods_cpp), "an unresolved weapon recipe must leave the inventory untouched"

plan_index = power_armor_cpp.index("PowerArmorApplyResult ApplyProxyPowerArmorPlan(")
frame_index = power_armor_cpp.index("bool ApplyProxyPowerArmorFrame(")
assert plan_index < frame_index, "the frame apply must be defined after the plan"
plan_body = power_armor_cpp[plan_index:frame_index]
assert "if (op.kind == PowerArmorOpKind::kUnequip)" in plan_body, "the plan must branch on the unequip kind"
assert plan_body.index("if (op.kind == PowerArmorOpKind::kUnequip)") < plan_body.index("ResolveEquipStack("), "the unequip branch must come first"
assert "if (!armor)" in plan_body, "an unresolved piece form must be checked"
assert plan_body.index("if (!armor)") < plan_body.index("ResolveEquipStack("), "the null check must run before the stack is resolved"
assert plan_body.index("ResolveEquipStack(") < plan_body.index("EquipOnProxy("), "the stack must resolve before the equip"
assert re.search(r"if \(!EquipOnProxy\([^)]*\)\) \{\s*LogThrottledWarning\([\s\S]*?continue;\s*\}", plan_body), "a failed piece equip must log and skip only that piece"

frame_body = power_armor_cpp[frame_index:]
assert "if (a_frameBaseFormId == 0)" in frame_body, "the frame form must be checked for zero"
assert "a_frame.entered = true;" in frame_body, "the enter must record the placed frame as entered"
assert "++a_frame.placements;" in frame_body, "every placement attempt must be counted"
assert "a_frame.preexistingRefFormIds = FrameRefIds(" in frame_body, "a placement must snapshot the cell before it places"
assert re.search(r"ActorInPowerArmor\(a_proxy\)\) \{\s*a_frame\.placements = 0;\s*return true;", frame_body), "a proxy confirmed in frame must reset the consecutive placement count"
assert re.search(r"if \(inFrame\) \{\s*a_frame\.placements = 0;", frame_body), "a confirmed enter must reset the consecutive placement count"
assert "FindNewFrameRef(*cell, a_proxy, a_frameBaseFormId, a_frame.preexistingRefFormIds)" in frame_body, "the placement search must pass the proxy and the placement base form"
destroy_body = power_armor_cpp[power_armor_cpp.index("void DestroyPlacedFrame("):frame_index]
assert "DestroyProxyActor(*frameRef);" in destroy_body, "destroying a placed frame must delete its reference"
assert destroy_body.index("frameBase->GetFormID() == a_frame.baseFormId") < destroy_body.index("DestroyProxyActor(*frameRef);"), "only a reference whose base is the placed frame's base may be deleted"
assert "a_frame.waitTicks > 0" in destroy_body and "FindNewFrameRef(" in destroy_body, "a placement still in flight must be swept before it is forgotten"
find_body = power_armor_cpp[power_armor_cpp.index("RE::TESObjectREFR* FindNewFrameRef("):power_armor_cpp.index("namespace F4T::PowerArmorSync")]
assert "g_claimedFrameRefIds.contains(" in find_body and "kStop" not in find_body and "Length()" in find_body, "a found frame must be the nearest unclaimed reference"
assert "a_frame.baseFormId = a_frameBaseFormId;" in frame_body, "the placed frame must record its base form so the leave path can find it"
assert frame_body.index("if (a_frameBaseFormId == 0)") < frame_body.index("ActorInPowerArmor(a_proxy)"), "the zero check must run before the in-frame test"
assert "GetFormByID<RE::TESObjectARMO>" not in frame_body, "the frame must never resolve as an ARMO"
assert power_armor_cpp.count("kExtraPowerArmorHandleOffset") == 2, "the ExtraPowerArmor handle is only read, never hand-written; the frame is entered through the engine routine"
assert "lastUsedPowerArmor" not in power_armor_cpp, "lastUsedPowerArmor is not written on enter and must not be the occupied frame (#407 comment 21727)"
assert "GetOccupiedFrameHandle(a_player).get()" in power_armor_cpp, "the occupied frame must come from the ExtraPowerArmor handle"
resolve_body = power_armor_cpp[power_armor_cpp.index("RE::TESObjectARMO* ResolveFrameArmor("):power_armor_cpp.index("RE::BIPOBJECT* FindEquippedBipedObject(")]
assert "RE::ENUM_FORM_ID::kFURN ? GetDefaultFrame() : nullptr" in resolve_body, "a FURN frame base must resolve to the default frame chassis so capture can skip it (#407 comment 21727)"
assert "0x0003430B" in power_armor_cpp, "the FurnitureTypePowerArmor keyword must be validated"
for literal in ("1187476", "2219442", "550299", "2219450"):
    assert literal in power_armor_cpp, f"engine id {literal} must be wired"
assert frame_body.index("ActorInPowerArmor(a_proxy)") < frame_body.index("enterPowerArmor(&a_proxy, frameRef, false);"), "the in-frame test must come before enter"
assert frame_body.index("frameRef->Get3D() == nullptr") < frame_body.index("enterPowerArmor(&a_proxy, frameRef, false);"), "the frame must be loaded before enter"
assert "kFrameWaitTickLimit" in frame_body, "the frame wait must be bounded"
unequip_frame_index = power_armor_cpp.index("bool UnequipProxyPowerArmorFrame(")
unequip_frame_body = power_armor_cpp[unequip_frame_index:]
assert re.search(r"if \(a_frame\.baseFormId == 0\) \{\s*return false;", unequip_frame_body), "an unequip without a placed frame must be refused"
assert unequip_frame_body.index("exitPowerArmor(a_proxy);") < unequip_frame_body.index("DestroyPlacedFrame(a_frame, &a_proxy);"), "the exit must run before the placed frame is deleted"
assert power_armor_apply_cpp.index("ApplyProxyPowerArmorFrame(") < power_armor_apply_cpp.index("PlanPieceOperations("), "the enter call must run before the piece plan"
assert power_armor_cpp.count("enterPowerArmor(&a_proxy, frameRef, false);") == 1, "the placed frame must be entered exactly once"
assert frame_body.index("if (a_frame.entered)") < frame_body.index("enterPowerArmor(&a_proxy, frameRef, false);"), "an entered frame must only be polled, never entered again"
assert "address() == 0" not in power_armor_cpp, "the engine ids are gated at compile time, not checked at runtime"
assert "#error \"A Commonwealth Online runtime target is required\"" in power_armor_cpp, "an unknown runtime must fail to compile"
assert "UnequipFrameFromBiped" not in power_armor_cpp, "the biped furniture fallback must be gone"
assert "player.placeatme" not in frame_body, "the frame must be placed at the proxy, not at the local player"
assert "{:08X}.placeatme {:08X} 1" in frame_body, "the frame placement must target the proxy form id"
assert "kMaxFramePlacements" in frame_body, "frame placements must be capped per session"
reset_index = apply_cpp.index("void ResetSlotEquipmentTracking(ProxyActorSlot& a_slot, bool a_forgetTrackedStacks)")
destroy_index = apply_cpp.index("F4T::PowerArmorSync::DestroyPlacedFrame(a_slot.placedPowerArmorFrame, a_slot.proxyHandle.get().get());")
assert reset_index < destroy_index < apply_cpp.index("void ClearSlotEquipmentIfApplied(", reset_index), "slot equipment reset must destroy the tracked frame"
placement_cpp = (PLUGIN_DIR / "src/F4TProxyActorControllerSlotPlacement.cpp").read_text(encoding="utf-8")
placement_match = re.search(r"^\s*void PromoteSlotPlaceAtMeProxy\(", placement_cpp, re.M)
assert placement_match, "PromoteSlotPlaceAtMeProxy definition must exist"
assert "ResetSlotEquipmentTracking(a_slot, true);" in placement_cpp[placement_match.start():placement_cpp.index("\n\t}\n", placement_match.start())]
resolution_cpp = (PLUGIN_DIR / "src/F4TProxyActorControllerSlotResolution.cpp").read_text(encoding="utf-8")
respawn_match = re.search(r"^\s*void ResetSlotForRespawn\(ProxyActorSlot& a_slot\)", resolution_cpp, re.M)
assert respawn_match, "ResetSlotForRespawn definition must exist"
assert "ResetSlotEquipmentTracking(a_slot, true);" in resolution_cpp[respawn_match.start():resolution_cpp.index("\n\t}\n", respawn_match.start())]
assert "ResetSlotEquipmentTracking(slot, true)" in (PLUGIN_DIR / "src/F4TProxyActorControllerSlotLifecycle.cpp").read_text(encoding="utf-8")
assert "ResetSlotEquipmentTracking(slotValue, true)" in (PLUGIN_DIR / "src/F4TProxyActorController.cpp").read_text(encoding="utf-8"), "proxy-gone resets must forget the equipment ledger"

assert re.search(
    r"syncedEquipmentStacks\.Size\(\) == 0 && !a_slot\.syncedModdedWeaponExtra",
    apply_cpp[apply_cpp.index("void ClearSlotEquipmentIfApplied("):]), "ClearSlot early-return must not skip while tracked stacks or a modded weapon pointer remain"
clear_body = apply_cpp[apply_cpp.index("void ClearSlotEquipmentIfApplied("): apply_cpp.index("void ApplyProxyEquipmentFromRemoteState(")]
assert clear_body.count("ResetSlotEquipmentTracking(a_slot);") >= 2 and "ResetSlotEquipmentTracking(a_slot, true)" not in clear_body, "ClearSlotEquipmentIfApplied must keep the default forget=false reset so blocked ledger entries can retry"
equipment_apply_body = apply_cpp[
    apply_cpp.index("void ApplyProxyEquipmentFromRemoteState("):
]
assert re.search(
    r"if \(currentFormId != 0 && !F4T::PowerArmorSync::IsPowerArmorPieceForm\(currentFormId\)\) \{\s*changed = UnequipSyncedArmor\(",
    equipment_apply_body), "the per-slot unequip must skip every power armor piece form; PowerArmorSync owns those slots"
assert equipment_apply_body.index("GetEquippedArmorBipedObject(a_proxy, *bipedObject)") < equipment_apply_body.index("IsPowerArmorPieceForm(currentFormId)"), "the equipped form must be read before the power armor guard"
assert equipment_apply_body.index("IsPowerArmorPieceForm(currentFormId)") < equipment_apply_body.index("UnequipSyncedArmor("), "the power armor guard must run before the unequip"

capture_index = power_armor_cpp.index("CaptureLocalPowerArmor(")
plan_index = power_armor_cpp.index("bool ProxyReportsInPowerArmor(")
capture_body = power_armor_cpp[capture_index:plan_index]
assert re.search(
    r"if \(state\.frameBaseFormId != 0 && \(item\.formId == state\.frameBaseFormId \|\| \(frameArmor && item\.formId == frameArmor->GetFormID\(\)\)\)\) \{\s*continue;",
    capture_body), "the frame form and its worn ARMO must never be captured as a piece; source:docs/code-notes/power-armor-sync.md"
assert capture_body.index("if (state.frameBaseFormId != 0 && (item.formId == state.frameBaseFormId") < capture_body.index("IsPowerArmorPiece(*armor)"), "the frame dedupe must run before the keyword test"

assert "proxy_equipment_recipe_ignored_" not in apply_cpp, "the gate-off warning path must be gone"
assert "preserving current proxy equipment" not in apply_cpp
assert apply_cpp.count("RemoveTrackedModdedStack(a_slot, a_proxy);") == 3
assert re.search(r"if \(weaponDiffers\) \{\s*RemoveTrackedModdedStack\(a_slot, a_proxy\);", apply_cpp)
assert re.search(
    r"if \(item\.mods && !item\.mods->empty\(\)\) \{\s*rightHandMods = std::addressof\(\*item\.mods\);",
    apply_cpp), "Received mods must always be used"
assert (apply_cpp + weapon_mods_cpp).count("EquipSyncedWeaponWithMods(") == 2
assert re.search(r"if \(rightHandMods\) \{\s*const auto modded = EquipSyncedWeaponWithMods\(", apply_cpp)
assert re.search(
    r"const (?:std::uint32_t|auto) stackId = F4T::EquipmentInstance::FindEquippedStack\(a_proxy, item->object->GetFormID\(\)\)\.stackId;",
    apply_cpp), "Equipped stack lookup for unequip must run unconditionally"
assert re.search(r"if \(rightHandWeaponFormId != 0 && currentWeaponFormId == rightHandWeaponFormId\)", apply_cpp)
assert "weaponModsStale = false" in apply_cpp
assert "if constexpr" not in apply_cpp

assert re.search(r"LogThrottledWarning\(\s*std::string\{ \"proxy_weapon_mod_missing_\" \}", weapon_mods_cpp), "an unresolvable mod form must skip that recipe with a throttled log"
assert re.search(r"if \(!resolved\.found\.stack \|\| !resolved\.found\.stack->extra\) \{\s*return \{\};", weapon_mods_cpp), "a missing stack must skip the recipe, not the feature"
assert re.search(r"if \(!resolved\.instanceData\) \{\s*return result;", weapon_mods_cpp), "missing instance data must fall back to the base weapon"

assert re.search(
    r"if \(item\.formId != 0 && FindArmorTarget\(wantedArmor, item\.formId\) == nullptr\) \{\s*wantedArmor\.push_back\(\{ item\.formId, targetMods \}\);",
    apply_cpp), "The wanted armor takes the first occurrence of a form id"
assert re.search(
    r"const auto\* wanted = FindArmorTarget\(wantedArmor, item\.formId\);\s*if \(wanted && FindArmorTarget\(armorToEquip, item\.formId\) == nullptr\) \{\s*armorToEquip\.push_back\(\*wanted\);",
    apply_cpp), "The equip list takes the wanted entry, so both lists use one occurrence rule"
assert re.search(
    r"std::optional<std::uint32_t> LiveStackId\(RE::Actor& a_proxy, const TrackedStack& a_tracked\)\s*\{\s*if \(!a_tracked\.extra\) \{[\s\S]*?if \(plainCount != 1\) \{\s*return std::nullopt;\s*\}\s*return plainId;\s*\}\s*const auto found = FindTrackedStack\(a_proxy, a_tracked\);",
    stacks_cpp), "A null-extra entry re-resolves only to the sole null-extra stack of its form; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert "bool mergeReorder{ false };" in stacks_tests, "The stack model must move the equipped stack to the chain head on a merge; source:E:/GardenEngine/re-answers/2026-10-23-pr361-static-re.md"
assert "void DropVanishedNullExtra(RE::Actor& a_proxy, std::vector<TrackedStack>& a_stacks)" in stacks_cpp, "A null-extra entry whose stack is gone must be dropped before a pass; source:docs/code-notes/equipment-state.md"
assert re.search(r"auto pending = a_ledger\.ForgetAll\(\);\s*DropVanishedNullExtra\(a_proxy, pending\);\s*std::vector<TrackedStack> kept;\s*for \(const auto& tracked : pending\)", stacks_cpp), "The retention pass must drop vanished null-extra entries before its loop; source:docs/code-notes/equipment-state.md"
assert stacks_cpp.count("DropVanishedNullExtra(a_proxy, pending);") == 2, "Both the removal pass and the retention pass must drop vanished null-extra entries first; source:docs/code-notes/equipment-state.md"
assert "void DropVanishedNullExtra(FakeProxy& a_proxy, std::vector<TrackedStack>& a_stacks)" in stacks_tests and stacks_tests.count("DropVanishedNullExtra(a_proxy, pending);") == 2, "The stack model must drop vanished null-extra entries in both passes the same way; source:docs/code-notes/equipment-state.md"
print("F4TEquipmentModSyncContract: all checks passed.")
