from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
PLUGIN_DIR = ROOT / "CO-BETA/plugin"

capture_cpp = (PLUGIN_DIR / "src/F4TRangedCombatCapture.cpp").read_text(encoding="utf-8")
networking_cpp = (PLUGIN_DIR / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
remote_cpp = (PLUGIN_DIR / "src/F4TRemotePlayerState.cpp").read_text(encoding="utf-8")

assert "F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId" in capture_cpp
target_check = re.search(
    r"if \(!targetPlayerId\) \{.*?return RE::BSEventNotifyControl::kContinue;\s*\}",
    capture_cpp,
    re.DOTALL
)
assert target_check is not None, "Capture must reject hits that do not map to an active remote player proxy"

assert "const bool isUnarmedPunch =" in capture_cpp
assert "const bool isRangedHit = F4T::WeaponClassification::IsRangedArchetype(weaponType);" in capture_cpp
assert "const bool isMeleeWeaponHit =" in capture_cpp
assert "!isUnarmedPunch && F4T::WeaponClassification::UseMeleeBehavior(weaponType);" in capture_cpp

assert '#include "F4TPvPCombatContract.h"' in networking_cpp
assert "F4T::PvPCombatContract::ValidateHitRouting(" in networking_cpp
assert "F4T::PvPCombatContract::InboundHitTracker g_incomingCombatHits;" in remote_cpp
assert "g_incomingCombatHits.QueueHit(a_senderId, a_sequence, a_damage)" in remote_cpp
assert "g_incomingCombatHits.PurgeSender(a_playerId)" in remote_cpp
assert "g_incomingCombatHits.pendingHits" in remote_cpp

remove_fn = re.search(
    r"bool RemoveRemotePlayer\(std::uint32_t a_playerId\)\s*\{(.*?)\n\t\}",
    remote_cpp,
    re.DOTALL
)
assert remove_fn is not None, "RemoveRemotePlayer definition not found"
remove_body = remove_fn.group(1)
assert "g_incomingCombatHits.PurgeSender(a_playerId)" in remove_body, (
    "RemoveRemotePlayer must reset shared hit state for the removed player"
)

contract_h = (PLUGIN_DIR / "include/F4TPvPCombatContract.h").read_text(encoding="utf-8")
behavior_cpp = (PLUGIN_DIR / "tests/F4TPvPCombatBehaviorTests.cpp").read_text(encoding="utf-8")
workflow_yml = (ROOT / ".github/workflows/co-beta-combat-action-contract.yml").read_text(encoding="utf-8")

assert "IsValidDamage" in contract_h
assert "ValidateHitRouting" in contract_h
assert "InboundHitTracker" in contract_h

assert "VerifyRoutingValidation" in behavior_cpp
assert "VerifySequenceDeduplicationAndStaleness" in behavior_cpp
assert "VerifyDisconnectReset" in behavior_cpp
assert "VerifyPendingHitsPurgedOnDisconnect" in behavior_cpp
assert "VerifyQueueCapacityCapping" in behavior_cpp

assert "F4TPvPCombatBehaviorTests.cpp" in workflow_yml
assert "co-pvp-combat-behavior-test" in workflow_yml

print("F4TPvPCombatContract: all checks passed.")
