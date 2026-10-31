from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
table = (plugin / "include/F4TInventoryHookTable.h").read_text(encoding="utf-8")
header = (plugin / "include/F4TInventoryHooks.h").read_text(encoding="utf-8")
hooks = (plugin / "src/F4TInventoryHooks.cpp").read_text(encoding="utf-8")
main_cpp = (plugin / "src/main.cpp").read_text(encoding="utf-8")

FINDINGS = "source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/findings.jsonl"
README = "source:RE-Work/subsystems/commonwealth-online-re/inventory-boundaries-co183/README.md"

# Expected RVA values are copied from the plugin table rows; a plugin table edit must fail this contract.
PLAYER_VTABLE = {"og": 0x2D438E8, "ng": 0x2369AD8, "ae": 0x256C938}
DROP_OBJECT = {"og": 0xE9ECA0, "ng": 0xCD4EA0, "ae": 0xD5AEC0}
PICK_UP_OBJECT = {"og": 0xEA6BB0, "ng": 0xCDCE20, "ae": 0xD62E60}
CONTAINER_VTABLE = {"og": 0x2D86418, "ng": 0x23A98A8, "ae": 0x25AC6C8}
DO_ITEM_TRANSFER = {"og": 0x1265810, "ng": 0xFB8E20, "ae": 0x103EE80}
DROP_SLOT, PICK_UP_SLOT, TRANSFER_SLOT = 0xEB, 0xEC, 0x15


def function_body(name: str) -> str:
    match = re.search(rf"\n(\t+)[^\n]*\b{name}\(", hooks)
    assert match is not None, name
    indent = match.group(1)
    body_start = hooks.index(f"\n{indent}{{\n", match.end()) + len(f"\n{indent}{{\n")
    return hooks[body_start:hooks.index(f"\n{indent}}}\n", body_start)]


def table_row(name: str) -> dict[str, int]:
    block = re.search(rf"inline constexpr RuntimeSlots {name}\{{(.*?)\n\t\}};", table, re.DOTALL)
    assert block is not None, name
    for line in block.group(1).splitlines():
        if re.search(r"0x[0-9A-Fa-f]", line):
            assert "// source:" in line, line
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)", re.sub(r"//[^\n]*", "", block.group(1)))]
    assert len(values) == 8, (name, values)
    return {
        "player_vtable": values[0], "drop_slot": values[1], "drop_rva": values[2],
        "pick_up_slot": values[3], "pick_up_rva": values[4],
        "container_vtable": values[5], "transfer_slot": values[6], "transfer_rva": values[7],
    }


for key, name in (("og", "kOg"), ("ng", "kNg"), ("ae", "kAe")):
    row = table_row(name)
    assert row["player_vtable"] == PLAYER_VTABLE[key], (name, "PlayerCharacter primary vtable")
    assert row["drop_slot"] == DROP_SLOT and row["drop_rva"] == DROP_OBJECT[key], (name, "DropObject")
    assert row["pick_up_slot"] == PICK_UP_SLOT and row["pick_up_rva"] == PICK_UP_OBJECT[key], (name, "PickUpObject")
    assert row["container_vtable"] == CONTAINER_VTABLE[key], (name, "ContainerMenu primary vtable")
    assert row["transfer_slot"] == TRANSFER_SLOT and row["transfer_rva"] == DO_ITEM_TRANSFER[key], (name, "DoItemTransfer")

assert FINDINGS in table and README in table, "every table value must cite its RE source"
assert "#pragma once" in header
assert "\tbool Install();\n" in header
assert "kEnableInventoryObserverHooks" not in header
assert "kEnableInventoryObserverHooks" not in table
assert "kEnableInventoryObserverHooks" not in main_cpp

assert "kCoTestFeatures" not in hooks and '#include "F4TTestFeatures.h"' not in hooks

install = function_body("Install")
assert install.lstrip().startswith("const auto target = VerifiedTarget();"), install[:200]
for resolver in ("VerifiedTarget()", "SlotsForTarget(", "InstallVtableSlot("):
    assert hooks.count(resolver) == install.count(resolver) + 1, f"{resolver} must be called only from Install"
assert "RE::" not in install, "Install resolves no engine type directly"

slot_install = function_body("InstallVtableSlot")
assert "VtableGuard::VerifySlot(request)" in slot_install
assert "VtableGuard::SlotMatchesExpectedRva(request.slotValue, g_moduleBase, a_expectedRva)" in slot_install
assert slot_install.index("SlotMatchesExpectedRva(") < slot_install.index("vtable.write_vfunc("), "identity check must precede the write"
assert "a_original = request.slotValue;\n\t\t\tvtable.write_vfunc(a_slot, a_replacement);" in slot_install
assert hooks.count("vtable.write_vfunc(") == 1
assert "VtableGuard::WriteSucceeded(" in slot_install
assert "return false;" in slot_install

drop = function_body("DropObjectHook")
assert "RE::ObjectRefHandle* DropObjectHook(\n\t\t\tRE::Actor* a_this,\n\t\t\tRE::ObjectRefHandle* a_result,\n" in hooks, "DropObject detour takes the engine return buffer as a_result"
assert "using DropObjectFn = RE::ObjectRefHandle* (*)(RE::Actor*, RE::ObjectRefHandle*, const RE::BGSObjectInstance&, RE::BSTSmallArray<std::uint32_t, 4>*, std::int32_t, const RE::NiPoint3*, const RE::NiPoint3*);" in hooks
assert "RE::ObjectRefHandle (*)" not in hooks and "RE::ObjectRefHandle DropObjectHook(" not in hooks, "DropObject must not return ObjectRefHandle by value"
assert "static_assert(!std::is_trivially_copyable_v<RE::ObjectRefHandle>," in hooks and "#include <type_traits>" in hooks
assert drop.count("original(") == 1
assert drop.count("return ") == 1
assert drop.rstrip().endswith("return original(a_this, a_result, a_object, a_stackData, a_number, a_point, a_rotate);")
assert "reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_moduleBase" in drop
assert "REX::W32::GetCurrentThreadId()" in drop
assert "GetFormID()" in drop

pick_up = function_body("PickUpObjectHook")
assert pick_up.count("original(") == 1
assert "InventoryPickupGate::Hold(a_objREFR, a_count, a_playPickUpSounds, original)" in pick_up
assert "const auto refFormId = a_objREFR ? a_objREFR->GetFormID() : 0;" in pick_up, "the form id must be captured before the original runs"
assert pick_up.index("a_objREFR->GetFormID()") < pick_up.index("original(a_this,"), "the form id must be captured before the original runs"
assert pick_up.index("original(a_this,") < pick_up.index("ShouldLog("), "observation stays after the original"
assert pick_up.split("original(a_this, a_objREFR, a_count, a_playPickUpSounds);", 1)[1].count("a_objREFR") == 0, "no reference access after the original call"
assert "return;" in pick_up
assert "reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_moduleBase" in pick_up
assert "REX::W32::GetCurrentThreadId()" in pick_up

transfer = function_body("DoItemTransferHook")
assert transfer.count("original(") == 1
assert "return" not in transfer
assert transfer.rstrip().endswith("original(a_this, a_itemIndex, a_count, a_fromContainer);")
assert "reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - g_moduleBase" in transfer
assert "REX::W32::GetCurrentThreadId()" in transfer

assert "ShouldLog(" in hooks and hooks.count("ShouldLog(") == 4, "one throttle helper and one call per wrapper"
assert "RemoveItem" not in hooks
assert "AddInventoryItem" not in hooks
for call in ("->DropObject(", ".DropObject(", "->PickUpObject(", ".PickUpObject(", "->AddItem(", "->AddInventoryItem("):
    assert call not in hooks, call

assert '#include "F4TInventoryHooks.h"' in main_cpp
polling = re.search(r"void\s+StartPlayerPositionPolling\(\)\s*\{(.*?)\n\t\}", main_cpp, re.DOTALL)
assert polling is not None, "StartPlayerPositionPolling not found in main.cpp"
body = polling.group(1)
assert body.index("F4T::WorkshopCommitHooks::Install();") < body.index("F4T::InventoryHooks::Install();")

for fn in ("DropObjectHook", "PickUpObjectHook", "DoItemTransferHook"):
    body = hooks[hooks.index(fn + "("):]
    body = body.split("\n\t\t}\n")[0]
    assert "catch (...)" in body, fn

print("F4TInventoryHooksContract: all checks passed.")
