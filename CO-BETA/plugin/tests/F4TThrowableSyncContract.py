from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
PLUGIN_DIR = ROOT / "CO-BETA/plugin"

header = (PLUGIN_DIR / "include/F4TThrowableSync.h").read_text(encoding="utf-8")
source = (PLUGIN_DIR / "src/F4TThrowableSync.cpp").read_text(encoding="utf-8")

assert "F4TTestFeatures.h" not in header, "an always-on feature must not depend on the test-features switch"
for name in ("kEnableThrowableSync", "kEquippedThrowableIndexVerified"):
    assert name not in header, f"throwable sync must ship on; {name} must not exist"
    assert name not in source, f"throwable sync must ship on; {name} must not exist"

for path in [*(PLUGIN_DIR / "include").rglob("*.h"), *(PLUGIN_DIR / "src").rglob("*.cpp")]:
    text = path.read_text(encoding="utf-8")
    assert not re.search(r"kEnableThrowableSync\s*=(?!=)", text), f"{path} redefines the throwable sync gate"
    assert not re.search(r"kEquippedThrowableIndexVerified\s*=(?!=)", text), f"{path} redefines the RE verification gate"

assert "if constexpr (kEnableThrowableSync)" not in source, "the apply path must not be compile-time gated"
assert "kEnableEquipmentModSync" not in source, "the throwable paths must not ride the equipment mod sync switch"
assert "if constexpr" not in source, "capture and unequip must run unconditionally on every runtime"
assert re.search(
    r"const std::uint32_t stackId = F4T::EquipmentInstance::FindEquippedStack\(",
    source), "unequip must always resolve the equipped stack id"
assert re.search(
    r"^\t\tcaptured->mods = F4T::EquipmentInstance::CaptureEquippedItemMods\(",
    source, re.MULTILINE), "the captured throwable must always carry its mods"
assert "if constexpr" not in source.split("ApplyThrowablePlan")[1], "the applier body must run unconditionally"
assert "return ApplyOutcome::kApplied;" in source, "the applier must report a real hand change"
assert "return ApplyOutcome::kFailed;" in source, "the applier must report failures separately from no-ops"
assert source.count("ApplyThrowablePlan(") == 1, "the applier must have a single definition"
assert source.count("ApplyAction::kSkip") == 1, "only the applier's own skip check may short-circuit the plan"
assert source.count("LogThrottledWarning(") >= 3, "every apply failure must log throttled instead of crashing"
assert source.count("F4TProxyActorControllerInternal.h") == 1, "the throttled logger must come from the existing proxy helper"

assert "kThrowableScanIndexCount" not in header, "capture must not scan an unverified equip index"
assert "kThrowableScanIndexCount" not in source, "capture must not scan an unverified equip index"
assert "equipIndex.index = kRightHandEquipIndex;" in source, "capture must read the right-hand equip index"
assert not re.search(r"equipIndex\.index = (?!kRightHandEquipIndex)\w", source), (
    "capture must not read a hardcoded equip index other than the right hand"
)
assert source.count("GetEquippedItem") == 3, "the capture plus the two right-hand helpers are the only equipped reads"
assert source.count("if (!item || !item->object") == 2, "every equipped-item read must be null-checked"
assert "if (!equipManager)" in source, "the equip manager must be null-checked before use"
assert "IsThrownWeapon()" in source, "capture must classify through the CommonLib weapon predicate"
assert "CaptureFromThrownWeapon(weapon->GetFormID(), weapon->IsThrownWeapon())" in source, (
    "the capture must feed the CommonLib thrown-weapon predicate into the pure classifier"
)
# TODO(RE): concept-level/unverified; the equip-index ownership assertions below are pinned to index 0.
assert source.count("UnequipRightHand(*equipManager, a_proxy)") == 2, (
    "the applier must clear the right-hand index for the clear plan and for a different occupant"
)
assert source.index("UnequipRightHand(*equipManager, a_proxy)") < source.index("EquipRightHand(*equipManager"), (
    "a different weapon in the hand must be cleared before the throwable is equipped"
)
applier_body = source.split("ApplyThrowablePlan")[1]
assert "RemoveTrackedModdedStack(a_slot, a_proxy);" in applier_body, (
    "the applier must drop the modded stack it cleared or replaced"
)
assert "a_slot.syncedModdedWeaponFormId = a_plan.formId;" in applier_body, (
    "the applier must track the modded stack it equipped"
)
assert "weaponInstance{ weapon, equipped.instanceData }" in source, (
    "the attach must carry the instance data of a modded throwable"
)
assert "EquipSyncedWeaponWithMods(" in source, "the throwable must apply its recipe through the shared modded-weapon helper"
assert re.search(r"EquipRightHand\(\*equipManager, a_proxy, \*weapon, a_mods\)", source), (
    "the applier must forward the captured recipe into the equip"
)
assert source.index("F4T::EquipmentInstance::FindStack(a_proxy, a_weapon.GetFormID()") < source.index(
    "AddObjectToContainer"
), "equip must reuse an existing stack instead of adding another copy on every apply"

assert "RE::WEAPON_TYPE" not in header, "The pure header must stay engine-free for the test target"
assert "WEAPON_TYPE" not in header, "the pure header must not copy CommonLib's thrown-weapon type numbers"
assert "kGrenadeWeaponType" not in header and "kMineWeaponType" not in header, (
    "the throwable classification must come from the CommonLib weapon predicate, not a copied type table"
)

assert "GetFormByID<RE::TESObjectWEAP>" in source, "The applied form must resolve as a WEAP before any equip"
assert source.index("GetFormByID<RE::TESObjectWEAP>") < source.index("EquipRightHand(*equipManager"), (
    "Resolve the throwable form before equipping"
)

local_state = (PLUGIN_DIR / "src/F4TLocalPlayerState.cpp").read_text(encoding="utf-8")
capture_body = local_state.split("std::vector<RemoteEquippedItem> CaptureLocalEquippedItems")[1].split("\n\t}")[0]
assert "#include \"F4TThrowableSync.h\"" in local_state, "the capture site must include the throwable header"
assert "CaptureEquippedThrowable(a_player)" in capture_body, "the local state must capture the equipped throwable"
assert capture_body.index("rightHand") < capture_body.index("CaptureEquippedThrowable(a_player)"), (
    "the throwable must be captured after the right-hand entry"
)
# TODO(RE): concept-level/unverified; the right-hand index below is the index-0 read this file already pins.
right_hand_body = local_state.split("std::uint32_t GetEquippedWeaponFormId")[1].split("\n\t}")[0]
assert "F4T::ThrowableSync::RightHandEntryFormId(" in right_hand_body, (
    "a thrown weapon in the hand must not also be published as the rightHand slot"
)
assert "IsThrownWeapon()" in right_hand_body, (
    "the right-hand entry must classify through the CommonLib weapon predicate"
)

proxy_equipment = (PLUGIN_DIR / "src/F4TProxyActorControllerEquipment.cpp").read_text(encoding="utf-8")
apply_body = proxy_equipment.split("void ApplyProxyEquipmentFromRemoteState")[1]
assert "#include \"F4TThrowableSync.h\"" in proxy_equipment, "the apply site must include the throwable header"
assert "FindThrowableEntry(a_remotePlayer.equippedItems)" in apply_body, (
    "the proxy must read the throwable entry out of the received state"
)
assert "ResolveApplyPlan(" in apply_body, "the proxy must decide through the pure plan table"
assert re.search(r"ApplyThrowablePlan\(\s*a_proxy\b", apply_body), "the proxy must apply the resolved throwable plan"
assert apply_body.index('item.slot == "rightHand"') < apply_body.index("ApplyThrowablePlan("), (
    "the throwable must be applied after the right-hand weapon block"
)
assert "IsThrowableSlot(item.slot)" in apply_body, (
    "the armor loop must skip the throwable slot instead of warning about it"
)
assert "ThrowableOwnsRightHand(" in apply_body, "the proxy must ask the pure right-hand ownership predicate"
assert "ThrowableUnresolved(" in apply_body, "the proxy must ask the pure unresolved-throwable predicate"
assert "!throwableOwnsRightHand && !throwableUnresolved &&" in apply_body, (
    "an unresolved throwable must leave the proxy hand untouched instead of unequipping it"
)
assert "proxy_weapon_form_missing_" in apply_body, (
    "an unresolved throwable must log a throttled weapon-form-missing warning"
)
assert "!throwableOwnsRightHand &&" in apply_body, (
    "the right-hand weapon block must not manage a slot the throwable owns"
)
assert re.search(
    r"ApplyThrowablePlan\(\s*a_proxy, a_slot, throwablePlan, a_remotePlayer\.weaponDrawn, throwableMods\)",
    apply_body), "the proxy must forward the resolved plan and recipe into the applier"
assert "ApplyAction::kSkip" in apply_body, "the proxy must not call the applier for a skipped plan"
assert "allApplied && !throwableUnresolved && throwableOutcome != F4T::ThrowableSync::ApplyOutcome::kFailed) {" in apply_body, (
    "an unresolved or failed throwable apply must leave the equipment cache unset so the next pass retries"
)
assert re.search(
    r"ApplyOutcome::kApplied\) \{\s*changed = true;.*?weaponVisualAttached = true;",
    apply_body, re.DOTALL), "a successful throwable apply must mirror the weapon path's visual-attach handling"

print("F4TThrowableSyncContract: all checks passed.")

# Constraint: engine claims here are unverified (source: text pins only).
