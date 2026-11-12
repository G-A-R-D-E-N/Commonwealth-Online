# address source:commonlibf4-nomad
from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
header = (plugin / "include/F4TProxyPathPredicates.h").read_text(encoding="utf-8")
predicates = (plugin / "src/F4TProxyPathPredicates.cpp").read_text(encoding="utf-8")
controller = "\n".join(path.read_text(encoding="utf-8") for path in sorted((plugin / "src").glob("F4TProxyActorController*.cpp")) + [plugin / "include/F4TProxyActorControllerInternal.h"])
controller_header = (plugin / "include/F4TProxyActorController.h").read_text(encoding="utf-8")
npc_sync = (plugin / "src/F4TNpcSyncPuppets.cpp").read_text(encoding="utf-8")

assert "struct PathState" in header
assert "PathState ReadPathState(RE::Actor& a_actor);" in header

ids = re.findall(r"inline constexpr REL::ID (\w+)\{ (\d+), (\d+), (\d+) \};", predicates)
assert ids == [
    ("kIsPathValid", "1522194", "2230273", "2230273"),
    ("kIsPathing", "989661", "2230276", "2230276"),
    ("kIsPathingComplete", "817283", "2230274", "2230274"),
], ids
assert "F4T_TARGET_" not in predicates
assert "F4T::RuntimeSelect::VerifiedVersion()" in predicates
assert "static const bool verified = [] {" in predicates
assert "REX::FModule::GetExecutingModule().GetFileVersion()" in predicates
assert "REX::WARN(" in predicates

assert re.search(r"if \(!a_actor\.actorMover \|\| !a_actor\.movementController\) \{\s*return \{\};", predicates)
assert predicates.index("return {};") < predicates.index("CallPathPredicate(kIsPathing, a_actor)")
assert re.search(r"PathState ReadPathState\(RE::Actor& a_actor\)\s*\{\s*if \(!IsVerifiedRuntime\(\)\) \{\s*return \{\};", predicates)
assert predicates.index("IsVerifiedRuntime()) {") < predicates.index("a_actor.actorMover")
assert predicates.index("IsVerifiedRuntime()) {") < predicates.index("CallPathPredicate(kIsPathing, a_actor)")

for stale in ("2230279", "2234312"):
    for path in [*(plugin / "src").glob("*.cpp"), *(plugin / "include").rglob("*.h")]:
        assert stale not in path.read_text(encoding="utf-8"), f"{path} still binds stale id {stale}"

assert "SetAvoidanceDisabled" not in npc_sync
assert "a_actor.StopCombat();" in npc_sync
assert "a_actor.ClearAttackStates();" in npc_sync

assert not re.search(r"\.(IsPathing|IsPathValid|IsPathingComplete)\(", controller)
assert controller.count("F4T::ProxyPathPredicates::ReadPathState(a_proxy)") == 1
assert "SetAvoidanceDisabled" not in controller
assert '"F4TProxyPathPredicates.h"' in controller or '"F4TProxyPathPredicates.h"' in controller_header

print("F4TProxyPathPredicateContract: all checks passed.")
