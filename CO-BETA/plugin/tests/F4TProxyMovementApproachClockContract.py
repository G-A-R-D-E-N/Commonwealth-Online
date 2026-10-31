from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
movement_header = (plugin / "include/F4TProxyMovementSmoothing.h").read_text(encoding="utf-8")
internal_header = (plugin / "include/F4TProxyActorControllerInternal.h").read_text(encoding="utf-8")
movement = (plugin / "src/F4TProxyActorControllerMovement.cpp").read_text(encoding="utf-8")
slot_resolution = (plugin / "src/F4TProxyActorControllerSlotResolution.cpp").read_text(encoding="utf-8")
slot_lifecycle = (plugin / "src/F4TProxyActorControllerSlotLifecycle.cpp").read_text(encoding="utf-8")
controller = (plugin / "src/F4TProxyActorController.cpp").read_text(encoding="utf-8")
tests = (plugin / "tests/F4TProxyMovementSmoothingTests.cpp").read_text(encoding="utf-8")

sources = (
    sorted((plugin / "src").glob("*.cpp"))
    + sorted((plugin / "src").glob("*.h"))
    + sorted((plugin / "include").glob("*.h"))
)
for path in sources:
    assert "g_lastPositionApproachTime" not in path.read_text(encoding="utf-8"), (
        f"{path} still holds the shared approach clock"
    )

assert "inline float FrameDeltaSeconds(double& a_lastSampleSeconds, double a_nowSeconds)" in movement_header
assert "inline constexpr double kDefaultFrameDeltaSeconds = 1.0 / 60.0;" in movement_header
assert "inline constexpr double kMinFrameDeltaSeconds = 1.0 / 120.0;" in movement_header
assert "inline constexpr double kMaxFrameDeltaSeconds = 0.1;" in movement_header
assert movement_header.index("inline float FrameDeltaSeconds(") < movement_header.index(
    "inline void NoteRemoteSample("
)

assert re.search(r"double lastApproachSampleSeconds\{ 0\.0 \};", internal_header), (
    "ProxyActorSlot must own the approach clock"
)
assert re.search(
    r"void MoveProxyToRemotePlayer\(\s*RE::Actor& a_proxy,\s*const RemotePlayer& a_remotePlayer,\s*"
    r"const RE::PlayerCharacter& a_player,\s*ActiveProxyKind a_proxyKind,\s*bool a_sameCellPassed,\s*"
    r"ProxyActorSlot& a_slot\);",
    internal_header,
), "MoveProxyToRemotePlayer must take the owning slot"
assert re.search(
    r"bool TryUpdatePlacedFallbackForRemotePlayer\(\s*const RemotePlayer& a_remotePlayer,\s*"
    r"RE::TESObjectCELL& a_parentCell,\s*const RE::PlayerCharacter& a_player,\s*bool a_sameCellPassed,\s*"
    r"ProxyActorSlot& a_slot\);",
    internal_header,
), "TryUpdatePlacedFallbackForRemotePlayer must forward the owning slot"

assert movement.count("F4T::ProxyMovementSmoothing::FrameDeltaSeconds(") == 2, (
    "the chrono wrapper and the slot approach clock are the only consumers"
)
assert "F4T::ProxyMovementSmoothing::kPositionSmoothingRatePerSecond" in movement
assert re.search(
    r"ProxyMovementSmoothing::FrameDeltaSeconds\(\s*a_slot\.lastApproachSampleSeconds,\s*"
    r"SteadySeconds\(std::chrono::steady_clock::now\(\)\)\)",
    movement,
), "MoveProxyToRemotePlayer must advance the slot's own approach clock"
assert re.search(
    r"previousSeconds = SteadySeconds\(a_lastTime\);.*?F4T::ProxyMovementSmoothing::FrameDeltaSeconds\(",
    movement,
    re.S,
), "the chrono wrapper must delegate to the shared clamp rule"

reset_match = re.search(
    r"void ResetSlotMovementState\(ProxyActorSlot& a_slot\)\s*\{(.*?)\n\t\}",
    slot_resolution,
    re.S,
)
assert reset_match, "ResetSlotMovementState not found"
reset_body = reset_match.group(1)
assert "a_slot.lastSmoothMoveTime = {};" in reset_body
assert "a_slot.lastApproachSampleSeconds = 0.0;" in reset_body
assert reset_body.index("F4T::ProxyMovementSmoothing::Reset(") < reset_body.index(
    "a_slot.lastApproachSampleSeconds = 0.0;"
)

assert re.search(r"bool TryUpdatePlacedFallbackForRemotePlayer\((?:.|\n)*?ProxyActorSlot& a_slot\)", slot_lifecycle)
assert re.search(
    r"ActiveProxyKind::kPlacedFallback,\s*a_sameCellPassed,\s*a_slot\);",
    slot_lifecycle,
), "the fallback path must pass its slot to MoveProxyToRemotePlayer"

assert "fallbackCandidateSlot = slot;" in controller
assert re.search(r"if \(fallbackCandidate && fallbackCandidateSlot\)", controller)
assert re.search(r"fallbackCandidateSameCellPassed,\s*\*fallbackCandidateSlot\);", controller)

assert 'RunTest("test_per_proxy_approach_clock_keeps_frame_delta"' in tests
assert "test_per_proxy_approach_clock_keeps_frame_delta()" in tests

print("F4TProxyMovementApproachClockContract: all checks passed.")
