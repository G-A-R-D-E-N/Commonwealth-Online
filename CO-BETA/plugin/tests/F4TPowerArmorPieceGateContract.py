from pathlib import Path
# TODO-verify: concept-level piece gate and capture checks, engine facts unverified; source:CodeNotes-GARDEN power-armor-sync.


ROOT = Path(__file__).resolve().parents[3]
PLUGIN_DIR = ROOT / "CO-BETA/plugin"

power_armor_h = (PLUGIN_DIR / "include/F4TPowerArmorSync.h").read_text(encoding="utf-8")
power_armor_cpp = (PLUGIN_DIR / "src/F4TPowerArmorSync.cpp").read_text(encoding="utf-8")
apply_cpp = (PLUGIN_DIR / "src/F4TProxyActorControllerPowerArmor.cpp").read_text(encoding="utf-8")

assert "inline bool PiecesMayApply(std::uint32_t a_frameFormId, std::uint32_t a_enteredFrameFormId)" in power_armor_h, "the piece gate must be a pure predicate on PowerArmorSync"

gate_body = apply_cpp[apply_cpp.index("void ApplyProxyPowerArmorFromRemoteState("):]
assert gate_body.index("ApplyProxyPowerArmorFrame(") < gate_body.index("PiecesMayApply(frameFormId, a_slot.appliedPowerArmorFrameFormId)"), "the frame enter must be attempted before the piece gate is read"
assert gate_body.index("PiecesMayApply(frameFormId, a_slot.appliedPowerArmorFrameFormId)") < gate_body.index("ApplyProxyPowerArmorPlan("), "pieces must not be equipped on a puppet before its frame is entered"
assert "PlanPieceOperations(a_slot.lastAppliedPowerArmorState, current)" in gate_body, "the piece plan is still computed every tick"

capture_start = power_armor_cpp.index("std::optional<PowerArmorState> CaptureLocalPowerArmor(")
capture_body = power_armor_cpp[capture_start:power_armor_cpp.index("bool ProxyReportsInPowerArmor(", capture_start)]
scan = "for (std::int32_t slot = 0; slot < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++slot) {"
assert scan in capture_body, "capture must scan every biped slot for worn power armor pieces, not only the apparel slots"
assert capture_body.index(scan) < capture_body.index("for (const auto& item : candidates) {"), "the biped scan must feed the piece candidates before they are filtered"
assert "std::format(\"biped{}\", slot)" in capture_body, "pieces found outside the apparel slots get a biped slot name"
assert "CaptureEquippedItemMods(a_player, armor->GetFormID())" in capture_body, "scanned pieces must keep their mods"
assert "IsPowerArmorPiece(*armor)" in capture_body[capture_body.index(scan):capture_body.index("candidates.push_back")], "only power armor ARMO forms may be scanned as pieces"
assert "a_item.formId == armor->GetFormID()" in capture_body, "a piece already captured from the apparel list must not be captured twice"

remote_state_cpp = (PLUGIN_DIR / "src/F4TRemotePlayerState.cpp").read_text(encoding="utf-8")
update_body = remote_state_cpp[remote_state_cpp.index("void UpdateRemotePlayer("):remote_state_cpp.index("bool HasRemotePlayer(")]
assert "if (a_state.powerArmorInFrame) {" in update_body, "a remote player in a suit must be read as in a suit after the sticky merge"
assert update_body.index("if (!a_state.hasPowerArmorUpdate && existing->second.hasPowerArmorUpdate)") < update_body.index("if (a_state.powerArmorInFrame) {"), "the suit guard must run after the sticky power armor merge"
assert update_body.index("if (a_state.powerArmorInFrame) {") < update_body.index("ApplyAuthoritativeDowned(a_state);"), "the suit guard must clear sneak and crouch before the state is stored"
assert "a_state.isSneaking = false;" in update_body and "a_state.isCrouching = false;" in update_body, "a remote player in a suit must not sneak or crouch on the puppet"

enter_body = power_armor_cpp[power_armor_cpp.index("bool ApplyProxyPowerArmorFrame("):]
assert "SwitchActorToPowerArmor(a_proxy, *frameRef)" in enter_body, "the enter goes through the Papyrus SwitchToPowerArmor native"
assert enter_body.index("SwitchActorToPowerArmor(a_proxy, *frameRef)") < enter_body.index("a_frame.entered = true;"), "the frame counts as entered only once the switch is queued"

print("F4TPowerArmorPieceGateContract: all checks passed.")
