from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
movement_header = (plugin / "include/F4TLocalPlayerMovement.h").read_text(encoding="utf-8")
movement = (plugin / "src/F4TLocalPlayerMovement.cpp").read_text(encoding="utf-8")
engine = (plugin / "src/F4TDownedEngine.cpp").read_text(encoding="utf-8")
publish = (plugin / "src/F4TReplicationRuntimePublish.cpp").read_text(encoding="utf-8")

assert "void RequestExplicitTeleport();" in movement_header
assert "std::atomic<bool> s_pendingExplicitTeleport{ false };" in movement
assert "void RequestExplicitTeleport()\n\t{\n\t\ts_pendingExplicitTeleport.store(true);\n\t}" in movement
assert movement.count("s_pendingExplicitTeleport") == 3
assert re.search(
    r"const auto explicitTeleport = s_pendingExplicitTeleport\.exchange\(false\);\n"
    r"\t\tif \(HasWorldspaceChanged\(a_previousLocation, a_currentLocation\)\) \{",
    movement,
)
assert "if (explicitTeleport || HasTeleportDistance(a_previousTransform, a_currentTransform)) {" in movement
assert "GetSpecialMovementType(lastKnownLocation, currentLocation, lastSentTransform, currentTransform);" in publish

assert '#include "F4TLocalPlayerMovement.h"' in engine
assert engine.count("RequestExplicitTeleport(") == 1
assert re.search(
    r"void TeleportForRespawn\([^{]*\{\n\t\tF4T::LocalPlayerMovement::RequestExplicitTeleport\(\);\n",
    engine,
)

print("F4TExplicitTeleportContract: all checks passed.")
