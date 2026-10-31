from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PLUGIN_DIR = ROOT / "CO-BETA/plugin"
ATTRIBUTION = "// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission."


def at(text, needle):
    assert needle in text, f"missing: {needle}"
    return text.index(needle)


debounce = (PLUGIN_DIR / "include/F4TProxyEquipmentDebounce.h").read_text(encoding="utf-8")
internal = (PLUGIN_DIR / "include/F4TProxyActorControllerInternal.h").read_text(encoding="utf-8")
equipment = (PLUGIN_DIR / "src/F4TProxyActorControllerEquipment.cpp").read_text(encoding="utf-8")
diagnostics = (PLUGIN_DIR / "src/F4TProxyActorControllerDiagnostics.cpp").read_text(encoding="utf-8")

assert "kSettle = std::chrono::milliseconds(350);" in debounce, "the settle window must be 350 ms"
assert "kMinGap = std::chrono::milliseconds(900);" in debounce, "the minimum gap between applies must be 900 ms"
assert "if (a_firstApply) {\n\t\t\treturn true;\n\t\t}" in debounce, "the first outfit must apply at once"
assert "a_now - a_state.wantChangedAt >= kSettle && a_now - a_state.appliedAt >= kMinGap" in debounce, (
    "an apply must wait for the settle window and the minimum gap"
)

assert "lastSeenEquipmentItems;" in internal, "the slot must track the last received equipment"
assert "F4T::ProxyEquipmentDebounce::State equipmentDebounce;" in internal, "the slot must hold the debounce state"
assert "void LogReceivedEquipment(const RemotePlayer& a_remotePlayer);" in internal, "the received-equipment log must be declared"

assert equipment.count("void ApplyProxyEquipmentFromRemoteState(") == 1, "the apply function must have one definition"
body = equipment.split("void ApplyProxyEquipmentFromRemoteState(")[1]
body = body.split("\n\tvoid ", 1)[0]

gate = at(body, "F4T::ProxyEquipmentDebounce::ShouldApply(a_slot.equipmentDebounce, !a_slot.hasAppliedEquipmentState, now)")
assert body.count("F4T::ProxyEquipmentDebounce::ShouldApply(") == 1, "the gate must be called exactly once"
assert "if (!F4T::ProxyEquipmentDebounce::ShouldApply(" in body, "the gate result must be able to hold the apply"
changed = at(body, "if (!AreEquipmentStatesEqual(a_slot.lastSeenEquipmentItems, a_remotePlayer.equippedItems)) {")
recorded = at(body, "a_slot.equipmentDebounce.wantChangedAt = now;")
assert changed < recorded < gate, "a change must record wantChangedAt inside the last-seen check, before the gate decides"
applied = at(body, "a_slot.equipmentDebounce.appliedAt = now;")
equip_manager = at(body, "auto* equipManager = RE::ActorEquipManager::GetSingleton();")
assert gate < applied < equip_manager, "appliedAt must be recorded when the apply proceeds, before equipment is touched"
assert "a_slot.lastSeenEquipmentItems = a_remotePlayer.equippedItems;" in body, "the last-seen list must be updated on change"
assert "LogReceivedEquipment(a_remotePlayer);" in body, "the received-equipment log must come from the diagnostics helper"
assert "proxy_equip_received_items_" not in equipment, "the received-equipment debug block must stay in the diagnostics file"
assert "proxy_equip_received_items_" in diagnostics, "the diagnostics file must own the received-equipment log"

assert equipment.count("\n") < 750, "F4TProxyActorControllerEquipment.cpp must stay under 750 lines"

for path in (
    PLUGIN_DIR / "include/F4TProxyEquipmentDebounce.h",
    PLUGIN_DIR / "include/F4TProxyActorControllerInternal.h",
    PLUGIN_DIR / "src/F4TProxyActorControllerEquipment.cpp",
    PLUGIN_DIR / "src/F4TProxyActorControllerDiagnostics.cpp",
    PLUGIN_DIR / "tests/F4TProxyEquipmentDebounceTests.cpp",
):
    raw = path.read_bytes()
    assert raw.decode("utf-8").splitlines()[0] == ATTRIBUTION, f"{path} must carry the FathersFallout attribution"
    assert b"\x00" not in raw, f"{path} must not contain NUL bytes"
    assert raw.count(b"\n") == raw.count(b"\r\n"), f"{path} must use CRLF line endings"

print("F4TEquipmentDebounceContract: all checks passed.")
