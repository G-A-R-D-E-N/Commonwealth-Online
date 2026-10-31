import glob as _glob
from pathlib import Path
import re

root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
header = (plugin / "include/F4TLocalPlayerTransformSend.h").read_text(encoding="utf-8")
owner = (plugin / "src/F4TLocalPlayerTransformSend.cpp").read_text(encoding="utf-8")
outbound = (plugin / "src/F4TNetworkingOutbound.cpp").read_text(encoding="utf-8")
networking = (plugin / "include/F4TNetworking.h").read_text(encoding="utf-8")
gns_internal = (plugin / "include/F4TNetworkingGnsInternal.h").read_text(encoding="utf-8")
gns = (plugin / "src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")
publish = (plugin / "src/F4TReplicationRuntimePublish.cpp").read_text(encoding="utf-8")
movement = (plugin / "src/F4TLocalPlayerMovement.cpp").read_text(encoding="utf-8")
tests = (plugin / "tests/F4TLocalPlayerTransformSendTests.cpp").read_text(encoding="utf-8")
xmake = (plugin / "xmake.lua").read_text(encoding="utf-8")

assert "kTransformSendInterval = std::chrono::milliseconds{ 50 }" in header
assert "kTransformSendInterval = std::chrono::milliseconds{ 33 }" not in header
assert "bool ShouldPublishTransform(const TransformPublishState& a_state)" in header
assert "kMovingTransformSendTolerance = std::chrono::milliseconds{ 8 }" in header
assert "bool IsWithinSendInterval(std::chrono::steady_clock::duration a_sinceLastSend, bool a_isMoving)" in header
assert "bool movingEdge{};" in header
assert "bool Send(" in header
assert "TransformAnimationFields" not in header
assert "GetPlayerAimPitchRadians" not in header

assert "namespace F4T::LocalPlayerTransformSend" in owner
assert "F4T::TransformAnimationFields::WriteAnimationFields(" in owner
assert "F4T::Networking::Detail::SendPacketBytes(" in owner
assert '"type", "transform"' in owner
assert "GetPlayerAimPitchRadians" not in owner

assert '"type", "transform"' not in outbound
assert "bool SendTransformPacket(" not in outbound
assert "SendTransformPacketLegacy" not in outbound
assert "WriteAnimationFields" not in outbound
assert "bool SendWorldStatePacket(" in outbound

assert "SendTransformPacketLegacy" not in gns_internal
assert "SendTransformPacketLegacy" not in gns
assert '#include "F4TReplicationRuntime.h"' in networking
assert "float a_turnDeltaDegreesPerSecond" not in networking
assert "const F4T::ReplicationRuntime::PlayerMovementState& a_movement," in networking

assert "F4T::LocalPlayerTransformSend::Send(" in gns
assert '#include "F4TLocalPlayerTransformSend.h"' in gns
assert "CanPublishGameplayTransform()" in gns
assert "HasBlockingGameMenu()" in gns
assert "HasGameplayReadyParentCell(*player)" in gns

assert publish.count("F4T::Networking::SendTransformPacket(") == 2
assert '#include "F4TLocalPlayerTransformSend.h"' in publish
assert "constexpr auto kTransformSendInterval = 100ms;" not in publish
assert "withinSendInterval = F4T::LocalPlayerTransformSend::IsWithinSendInterval(currentTime - lastSendTime, networkMovementState.isMoving)," in publish
assert ".isMoving = networkMovementState.isMoving," in publish
assert ".movingEdge = networkMovementState.isMoving != lastSentMovementState.isMoving" in publish
assert "lastSentMovementState.isSneaking != networkMovementState.isSneaking ||" in publish
assert "lastSentMovementState.isCrouching != networkMovementState.isCrouching;" in publish
assert re.search(
    r"if \(!publishState\.bypassesThrottle && !publishState\.hasThrottledChange && !publishState\.isMoving\) \{\n"
    r"\s*lastDebugSampleTransform = currentTransform;\n"
    r"\s*lastDebugSampleTime = currentTime;\n"
    r"\s*return;\n"
    r"\s*\}",
    publish,
)
assert re.search(r"ShouldPublishTransform\(publishState\)\) \{\n\s*return;\n\s*\}", publish)

assert "GetPlayerAimPitchRadians" not in movement
assert "SendPacketBytes" not in movement
assert "WriteAnimationFields" not in movement
assert "LocalPlayerTransformSend" not in movement
assert "SendTransformPacket(" not in movement
assert "LocalPlayerTransformSend" not in outbound
assert "SendTransformPacket(" not in outbound

assert "milliseconds{ 50 }" in tests
assert "milliseconds{ 33 }" not in tests
assert "IsWithinSendInterval(now - lastSend, a_isMoving)" in tests
for row in [
    "assert(!Publishes(false, false, false));",
    "assert(!Publishes(false, false, true));",
    "assert(Publishes(false, true, false));",
    "assert(!Publishes(false, true, true));",
    "assert(Publishes(true, false, false));",
    "assert(Publishes(true, false, true));",
    "assert(Publishes(true, true, true));",
]:
    assert row in tests, row

_transform_builders = [
    path
    for path in _glob.glob(str(plugin / "src/*.cpp"))
    if '"type", "transform"' in Path(path).read_text(encoding="utf-8")
]
assert _transform_builders == [str(plugin / "src/F4TLocalPlayerTransformSend.cpp")], _transform_builders

_transform_senders = sorted(
    Path(path).name
    for path in _glob.glob(str(plugin / "src/*.cpp"))
    if "SendTransformPacket(" in Path(path).read_text(encoding="utf-8")
)
assert _transform_senders == ["F4TNetworkingGns.cpp", "F4TReplicationRuntimePublish.cpp"], _transform_senders

assert '"SendTransformPacket=SendTransformPacketLegacy"' not in xmake
assert '"ConnectAndValidateServer=ConnectAndValidateServerLegacy"' in xmake
assert "|F4TLocalPlayerTransformSend" not in xmake

print("F4TLocalPlayerTransformSendContract: all checks passed.")
