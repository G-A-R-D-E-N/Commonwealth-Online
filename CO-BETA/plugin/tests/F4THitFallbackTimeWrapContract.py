from pathlib import Path


plugin = Path(__file__).resolve().parents[1]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


# 1) Both NPC hit-event sinks resolve each actor role from whichever of the two
# parallel sources has it. The hit-data handle keeps priority and the top-level
# event reference is the fallback, so no hit is dropped only because one of the
# two sources is empty.
npc_capture = read(plugin / "src/F4TNpcCombatCapture.cpp")
assert npc_capture.count("HitAttribution::PickReference(") == 3, "every NPC sink role must pick its reference through the shared rule"
assert npc_capture.count("a_event.hitData.target.get()") == 2, "both NPC sinks must keep reading hitData.target"
assert npc_capture.count("a_event.hitData.aggressor.get()") == 2, "both NPC sinks must keep reading hitData.aggressor"
assert npc_capture.count("a_event.target.get()") == 2, "the top-level target must be the target fallback in both sinks"
assert npc_capture.count("cause.get()") == 1, "the top-level cause must be the shooter fallback in the damage sink"
assert "if (!aggressor || !hitTarget)" not in npc_capture, "the damage sink must not require both hit-data handles"
assert "if (!hitTarget || !aggressor)" in npc_capture, "the damage sink must drop the hit only when neither source names the actor"
assert "if (!aggressor || aggressor.get() != localPlayer)" in npc_capture, "the NPC hit sink must keep accepting only the local player as hitData.aggressor"

hit_attribution = read(plugin / "include/F4THitAttribution.h")
assert "constexpr const T* PickReference(const T* a_primary, const T* a_fallback)" in hit_attribution

# The ranged sink already resolved both sources (top-level target first, then the
# handle, both mapped) and accepted either aggressor or cause, so it is unchanged.
ranged_capture = read(plugin / "src/F4TRangedCombatCapture.cpp")
assert ranged_capture.count("TryGetRemotePlayerIdForProxyFormId(") == 2, "the ranged sink must keep probing the top-level target and the hit-data target"
assert "localPlayerIsCause" in ranged_capture, "the ranged sink must keep accepting the top-level cause as the shooter"

# 2) Time comparisons go through the wrap-aware helper, never a raw hour difference.
world_state = read(plugin / "src/F4TWorldStateSync.cpp")
assert world_state.count("F4T::WorldTimeMath::TimeStateIsConsistent(") == 2, "the send and apply checks must both use the wrap-aware consistency rule"
assert "std::abs(a_gameHour" not in world_state, "the send check must not compare raw gameHour values"
assert "std::abs(localGameHour" not in world_state, "the apply check must not compare raw gameHour values"
assert "IsLocalPlayerExterior()" in world_state, "weather apply must still skip interiors"

world_time = read(plugin / "include/F4TWorldTimeMath.h")
for symbol in (
    "float HourDifference(",
    "bool TimeStateIsConsistent(",
):
    assert symbol in world_time, symbol
# The day counter is compared raw, never folded into the hour: whether
# GameDaysPassed already carries the fraction of the day is [needs-runtime], so no
# fold helper may come back into the header.
assert "TotalGameDays" not in world_time, "the day counter must not be folded with the hour"
assert "GameTimeDeltaDays" not in world_time, "the day counter must not be folded with the hour"

# 3) The native tests for both pure helpers are registered for the build.
xmake = read(plugin / "xmake.lua")
assert 'add_files("tests/F4THitAttributionTests.cpp")' in xmake
assert 'add_files("tests/F4TWorldTimeMathTests.cpp")' in xmake
assert 'target("CommonwealthOnlineHitAttributionTests")' in xmake
assert 'target("CommonwealthOnlineWorldTimeMathTests")' in xmake

print("F4THitFallbackTimeWrapContract: all checks passed.")
