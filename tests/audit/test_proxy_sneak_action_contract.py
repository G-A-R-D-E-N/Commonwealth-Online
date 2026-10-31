from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "CO-BETA" / "plugin"
SNEAK_ACTION_HEADER = PLUGIN / "include" / "F4TProxySneakAction.h"
SNEAK_ACTION_SOURCE = PLUGIN / "src" / "F4TProxySneakAction.cpp"
CONTROLLER_SOURCES = sorted((PLUGIN / "src").glob("F4TProxyActorController*.cpp")) + [PLUGIN / "include" / "F4TProxyActorControllerInternal.h"]
ANIMATION_SYNC_SOURCE = PLUGIN / "src" / "F4TProxyAnimationSync.cpp"
LOCAL_MOVEMENT_SOURCE = PLUGIN / "src" / "F4TLocalPlayerMovement.cpp"
LOCAL_STATE_SOURCE = PLUGIN / "src" / "F4TLocalPlayerState.cpp"
NETWORKING_SOURCE = PLUGIN / "src" / "F4TNetworking.cpp"
REMOTE_ACTION_HEADER = PLUGIN / "include" / "F4RemoteActionComponent.h"
REMOTE_ACTION_SOURCE = PLUGIN / "src" / "F4RemoteActionComponent.cpp"
REMOTE_PLAYER_HEADER = PLUGIN / "include" / "F4TRemotePlayerState.h"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []

    sneak_header = read(SNEAK_ACTION_HEADER) if SNEAK_ACTION_HEADER.exists() else ""
    require(
        re.search(
            r"bool RequestProxySneakState\(\s*SneakRequestState& a_state,\s*RE::Actor& a_proxy,\s*bool a_requestedSneaking,\s*bool a_allowActionFallback\);",
            sneak_header,
        )
        is not None,
        "F4TProxySneakAction.h must declare RequestProxySneakState with the per-proxy request state and the fallback flag",
        failures,
    )
    require("inline constexpr std::uint32_t kMaxActionAttemptsPerRequest = 3;" in sneak_header, "the PerformAction fallback must be capped at 3 attempts per request", failures)
    require("kGiveUp" in sneak_header and "struct SneakRequestState" in sneak_header, "the sneak outcome must include kGiveUp and the per-proxy SneakRequestState", failures)
    require(
        "return a_actionAttempts < kMaxActionAttemptsPerRequest ? SneakRequestOutcome::kUseActionFallback : SneakRequestOutcome::kGiveUp;" in sneak_header,
        "the resolver must fall back to PerformAction only below the attempt cap and give up at the cap",
        failures,
    )
    require("return a_count <= 5 || a_count % 300 == 0;" in sneak_header, "the trace limiter must log the first 5 and then every 300th", failures)

    sneak = read(SNEAK_ACTION_SOURCE) if SNEAK_ACTION_SOURCE.exists() else ""
    animation_sync = read(ANIMATION_SYNC_SOURCE)
    for removed in ("kEnableProxySneakAction", "kEnableProxySneakGraphEvents", "kEnableProxySneakSync", "kEnableProxyCrouchSync"):
        require(removed not in sneak_header, f"{removed} must be gone: proxy sneak and crouch are always on", failures)
        require(removed not in sneak, f"{removed} must be gone from the sneak action source", failures)
        require(removed not in animation_sync, f"{removed} must be gone from the animation sync source", failures)

    require('#include "F4TProxySneakAction.h"' in read(ANIMATION_SYNC_SOURCE.parents[1] / "include" / "F4TProxyAnimationSync.h"), "animation sync must include the shared sneak path constant", failures)
    require('kSneakStart = "sneakStart"' in animation_sync and 'kSneakStop = "sneakStop"' in animation_sync, "graph sneak events must keep the lowercase sneakStart and sneakStop names", failures)
    require(
        "CrouchStart" not in animation_sync and "CrouchStop" not in animation_sync,
        "the unrecognized CrouchStart/CrouchStop events must not come back: the crouch flag drives the confirmed sneak path",
        failures,
    )

    animation_sync_header = read(PLUGIN / "include" / "F4TProxyAnimationSync.h")
    require(
        "const auto sneakGraphWritten = ApplySneakStateToGraph(a_proxy, desired.isSneaking);" in animation_sync,
        "animation sync must keep the iIsInSneak write result instead of discarding it",
        failures,
    )
    require(
        "F4T::ProxySneakAction::ShouldTraceCount(++a_state.sneakGraphWrites)" in animation_sync
        and "[RT-VERIFY 364-B] iIsInSneak graph write" in animation_sync
        and "std::uint64_t sneakGraphWrites{ 0 };" in animation_sync_header,
        "the iIsInSneak write result must be logged under [RT-VERIFY 364-B], rate limited per proxy",
        failures,
    )
    require(
        "const auto sneakChanged = a_state.lastIsSneaking != desired.isSneaking;" in animation_sync
        and "if (initial || sneakChanged || traceSneakWrite) {" in animation_sync,
        "the iIsInSneak write log must fire on the first apply, on a sneak change and on the rate-limited trace, not on every desired-state change",
        failures,
    )
    require(
        "GetProxySneakGraphApplyRecord" not in animation_sync and "g_proxySneakGraphApply" not in animation_sync,
        "the sneak graph apply record must be gone: the sneak request is resolved per tick by the sneak action",
        failures,
    )
    require(
        "desired.isSneaking = desired.isSneaking || desired.isCrouching;" in animation_sync,
        "the remote crouch flag must drive the always-on sneak graph path",
        failures,
    )
    require(
        "std::remainder(a_desired.direction - a_state.lastDirection, 2.0F * std::numbers::pi_v<float>)" in animation_sync,
        "the direction change check must be wrap-aware across plus or minus pi",
        failures,
    )
    require(
        re.search(r"DetectSneakTransition\(\s*wasInitialized,\s*a_previous\.lastIsSneaking,\s*a_desired\.isSneaking\)", animation_sync) is not None
        and "fireEvent(GraphEvent::kSneakStart);" in animation_sync
        and "fireEvent(GraphEvent::kSneakStop);" in animation_sync,
        "sneakStart and sneakStop must only be sent on a change of the wanted sneak state",
        failures,
    )
    require(
        animation_sync.count("F4T::ProxyWalkStart::SendEvents(") == 2 and "fireEvent(GraphEvent::kMoveStart)" not in animation_sync,
        "both the walk start and the landing into a walk must send through the moveStart walk start",
        failures,
    )

    require("GetDefaultObject<RE::BGSAction>(RE::DEFAULT_OBJECT::kActionSneak)" in sneak, "sneak action must resolve kActionSneak through BGSDefaultObjectManager", failures)
    require("PerformAction(action, nullptr)" in sneak, "sneak action must use Actor::PerformAction", failures)
    require(
        re.search(r"const auto setCalled = stanceBefore != a_requestedSneaking;", sneak) is not None
        and "a_proxy.SetSneaking(a_requestedSneaking)" in sneak,
        "the sneak request must re-assert the stance with Actor::SetSneaking on every mismatch, not only on a request change",
        failures,
    )
    require(
        "SetGraphVariableBool(" not in sneak and "kIsSneaking" not in sneak and "kBIsSneaking" not in sneak,
        "the sneak request must not write IsSneaking or bIsSneaking: neither name is a graph variable in OG, NG or AE (#407 comment 21727)",
        failures,
    )
    require("constexpr auto kSneakActionRetryInterval = std::chrono::milliseconds(500);" in sneak, "sneak action retries must be limited to one per 500 ms", failures)
    require(
        "ShouldTraceCount(++a_state.traceCount)" in sneak and "[RT-VERIFY 364-B]" in sneak and "TODO-verify(#407)" in sneak,
        "the sneak request must log under [RT-VERIFY 364-B] with the rate limit and keep the TODO-verify marker",
        failures,
    )
    require(
        "ShouldLogFallback" not in sneak and "GraphSneakApplyRecord" not in sneak and "nextSneakActionAttemptTime" not in sneak,
        "the removed fallback log helper, graph apply record and per-slot attempt time must not come back",
        failures,
    )
    require(
        re.search(
            r"auto outcome = ResolveSneakRequest\(\s*stanceAfterSet == a_requestedSneaking,\s*now < a_state\.nextAttemptTime,\s*a_state\.actionAttempts\);",
            sneak,
        )
        is not None,
        "the sneak request must resolve the proxy stance after SetSneaking, the retry window and the attempt count",
        failures,
    )
    require(
        "if (!a_allowActionFallback && outcome == SneakRequestOutcome::kUseActionFallback) {" in sneak
        and "outcome = SneakRequestOutcome::kPending;" in sneak,
        "the action fallback must be deferred to pending while the weapon graph settles",
        failures,
    )

    resolved = sneak.find("auto outcome = ResolveSneakRequest(")
    deferred = sneak.find("if (!a_allowActionFallback && outcome == SneakRequestOutcome::kUseActionFallback) {")
    fallback = sneak.find("if (outcome == SneakRequestOutcome::kUseActionFallback) {")
    arm = sneak.find("ArmSneakRetryWindow(now, a_state.nextAttemptTime, kSneakActionRetryInterval);", fallback)
    perform = sneak.find("PerformAction(action, nullptr)")
    require(
        -1 < resolved < deferred < fallback < arm < perform,
        "the sneak request must resolve first, defer the fallback while settling, arm the retry window before PerformAction, and only then fall back",
        failures,
    )
    require(
        0 <= sneak.find("stanceAfterSet == a_requestedSneaking,") < fallback,
        "PerformAction must sit behind the stance-mismatch branch of the resolver",
        failures,
    )
    require(
        re.search(r"if \(!action\) \{.*?\} else \{\s*const auto accepted = a_proxy\.PerformAction\(action, nullptr\);", sneak, re.DOTALL) is not None
        and sneak.find("if (!action)") < perform,
        "the sneak action must log and skip when the default object is missing, before PerformAction",
        failures,
    )
    require(
        sneak.count("ArmSneakRetryWindow(now, a_state.nextAttemptTime, kSneakActionRetryInterval);") == 2,
        "each request change and each PerformAction fallback must arm the retry throttle",
        failures,
    )
    require(
        "++a_state.actionAttempts;" in sneak and "a_state.actionAttempts < kMaxActionAttemptsPerRequest" not in sneak,
        "each PerformAction fallback must count one attempt against the per-request cap",
        failures,
    )
    require(
        re.search(
            r"inline void ArmSneakRetryWindow\(\s*std::chrono::steady_clock::time_point a_now,\s*std::chrono::steady_clock::time_point& a_nextAttemptTime,\s*std::chrono::milliseconds a_retryInterval\)\s*\{\s*if \(a_now >= a_nextAttemptTime\) \{\s*a_nextAttemptTime = a_now \+ a_retryInterval;\s*\}\s*\}",
            sneak_header,
        )
        is not None,
        "the retry window must be armed only after the previous deadline passed, so pending frames cannot push the fallback out forever",
        failures,
    )

    resolve_body = sneak_header[sneak_header.find("ResolveSneakRequest") :]
    require(
        re.search(r"if \(a_stanceMatchesRequest\) \{\s*return SneakRequestOutcome::kSettled;", resolve_body) is not None,
        "the resolver must settle as soon as the proxy stance matches the request",
        failures,
    )
    require(
        re.search(r"if \(a_withinRetryWindow\) \{\s*return SneakRequestOutcome::kPending;", resolve_body) is not None,
        "the resolver must defer while the retry window is open",
        failures,
    )
    require(
        re.search(r"return a_actionAttempts < kMaxActionAttemptsPerRequest \? SneakRequestOutcome::kUseActionFallback : SneakRequestOutcome::kGiveUp;", resolve_body) is not None,
        "the action fallback must be reachable only after an expired retry window and must give up at the attempt cap",
        failures,
    )

    movement = read(LOCAL_MOVEMENT_SOURCE)
    require(
        re.search(r"bool GetCrouchState\(RE::PlayerCharacter& a_player\)\s*\{\s*return F4T::LocalPlayerState::GetLocalPlayerCrouching\(a_player\);\s*\}", movement) is not None,
        "the sender must publish the real crouch state instead of a constant",
        failures,
    )
    require("GetCrouchState(a_player)," in movement, "the movement state must carry the crouch state onto the wire", failures)

    local_state = read(LOCAL_STATE_SOURCE)
    require(
        re.search(r"bool GetLocalPlayerCrouching\(const RE::PlayerCharacter& a_player\)\s*\{\s*return a_player\.stance == static_cast<std::uint32_t>\(RE::ACTOR_STANCE::kSneaking\);\s*\}", local_state) is not None,
        "crouch is the sneak stance; the cover stances are not crouch",
        failures,
    )

    controller = "\n".join(read(path) for path in CONTROLLER_SOURCES)
    lifecycle = read(PLUGIN / "src" / "F4TProxyActorControllerSlotLifecycle.cpp")
    actions = read(PLUGIN / "src" / "F4TProxyActorControllerActions.cpp")
    call = re.compile(
        r"const auto sneakCommitted = sneakReady && F4T::ProxySneakAction::RequestProxySneakState\(\s*a_slot\.sneakRequest,\s*a_proxy,\s*a_remotePlayer\.isSneaking,\s*true\);"
    )
    update = "a_slot.lastObservedSneakingState = a_remotePlayer.isSneaking;"
    call_match = call.search(controller)
    require(len(call.findall(controller)) == 1, "controller must request the proxy sneak state on observed change", failures)
    require(
        call_match is not None and call_match.start() < controller.find(update),
        "sneak request must run before the observed sneak state is updated",
        failures,
    )
    require(
        0 <= controller.find("a_slot.hasLoggedInitialAnimationState = true;") < controller.find("if (IsProxyWeaponGraphSettling(a_slot)) {")
        and 0 <= controller.find("a_slot.lastObservedMovingState = a_remotePlayer.isMoving;") < controller.find("if (IsProxyWeaponGraphSettling(a_slot)) {"),
        "the initial-logged flag and the movement observations must be set before the weapon settle return, or the info log repeats every settle frame",
        failures,
    )
    require(
        controller.count(update) == 1
        and controller.find("if (sneakCommitted) {") < controller.find(update),
        "the observed sneak state must only be committed when the request was accepted or already matches",
        failures,
    )
    require(
        re.search(
            r"if \(IsProxyWeaponGraphSettling\(a_slot\)\) \{\s*if \(sneakReady\) \{\s*F4T::ProxySneakAction::RequestProxySneakState\(a_slot\.sneakRequest, a_proxy, a_remotePlayer\.isSneaking, false\);\s*\}\s*return;",
            controller,
        )
        is not None,
        "the stance re-assert must run before the weapon graph settle return, with the action fallback disabled while settling",
        failures,
    )
    require(
        "SneakRequestState sneakRequest{};" in read(PLUGIN / "include" / "F4TProxyActorControllerInternal.h")
        and "a_slot.sneakRequest = {};" in controller,
        "the per-proxy sneak request state must be stored with the slot and cleared with the observation state",
        failures,
    )
    reset_slot = re.search(r"void ResetSlotObservationState\(ProxyActorSlot& a_slot\)\s*\{((?:[^{}]|\{\})*)\}", controller)
    require(
        reset_slot is not None
        and re.search(r"a_slot\.sneakRequest = \{\};.*?ResetSlotAppliedAnimationState\(a_slot\);", reset_slot.group(1), re.DOTALL) is not None,
        "slot observation reset must clear the sneak request and the applied animation state that holds the walk start retry",
        failures,
    )
    require(
        "sneakReady && now >= a_slot.sneakRequest.nextAttemptTime && !IsProxyWeaponGraphSettling(a_slot) && (!a_slot.sneakRequest.lastOutcome || *a_slot.sneakRequest.lastOutcome != F4T::ProxySneakAction::SneakRequestOutcome::kGiveUp)) ||" in controller,
        "the debug changed log for sneak must wait for the retry window and the proxy 3D readiness in the unobserved case too, not only the mismatch case, and stay quiet while the weapon graph settles",
        failures,
    )

    reset_slot_sneak = re.search(
        r"void ResetSlotSneakObservationState\(ProxyActorSlot& a_slot, std::string_view a_reason\)\s*\{((?:[^{}]|\{[^{}]*\})*)\}",
        controller,
    )
    reset_slot_sneak_body = reset_slot_sneak.group(1) if reset_slot_sneak else ""
    require(
        reset_slot_sneak is not None
        and all(
            token in reset_slot_sneak_body
            for token in (
                "a_slot.lastObservedSneakingState.reset();",
                "a_slot.sneakRequest = {};",
                "ResetSlotAppliedAnimationState(a_slot);",
                "[RT-VERIFY 364-B] sneak observation reset",
            )
        )
        and "ResetSlotActionReplayState" not in reset_slot_sneak_body,
        "the sneak observation reset must clear the observed sneaking state, the sneak request and the applied animation state, log under [RT-VERIFY 364-B] with the player id, and keep the replay sequence",
        failures,
    )
    release_start = lifecycle.find("void ReleaseAllSlotProxiesInOldCell(")
    release_body = lifecycle[release_start : lifecycle.find("void MarkSlotHeld(", release_start)]
    proxy_gone = release_body.find("if (!proxy) {")
    proxy_gone_reset = release_body.find('ResetSlotSneakObservationState(slot, "proxy handle expired");')
    cell_change = release_body.find('ClearSlotEquipmentIfApplied(slot, *proxy, "local cell change");')
    cell_change_reset = release_body.find('ResetSlotSneakObservationState(slot, "local cell change");')
    require(
        0 <= proxy_gone < proxy_gone_reset < release_body.find("slot.proxyHandle.reset();", proxy_gone)
        and 0 <= cell_change < cell_change_reset < release_body.find("NeutralizeProxyActor(slot, *proxy, a_player, ProxyNeutralizationReason::kHolding);"),
        "the sneak observation reset must run on the expired proxy path and on the local cell change release path",
        failures,
    )

    update_start = actions.find("void UpdateProxyAnimationStateDebug(")
    update_body = actions[update_start : actions.find("void SyncProxyAnimationFromMotion(", update_start)]
    ready_gate = update_body.find("const auto sneakReady = ProxyHasLoaded3D(a_proxy) && !a_slot.proxyPending3DRecovery;")
    require(
        0 <= ready_gate < update_body.find("RequestProxySneakState(")
        and update_body.count("RequestProxySneakState(") == 2
        and "if (sneakReady) {" in update_body
        and "sneakReady && F4T::ProxySneakAction::RequestProxySneakState(" in update_body,
        "UpdateProxyAnimationStateDebug must gate every sneak request, including the weapon settle path, on the proxy 3D readiness before RequestProxySneakState",
        failures,
    )
    require(
        "[RT-VERIFY 364-B] sneak request deferred: proxy 3D not ready" in update_body
        and "ShouldTraceCount(++a_slot.sneakRequest.deferredCount)" in update_body,
        "the deferred sneak request must log under [RT-VERIFY 364-B] with a count-gated trace",
        failures,
    )

    require(
        re.search(r"SendEvents\([^;]*,\s*1\);", animation_sync) is None
        and animation_sync.count("++a_walkStartSends") == 2
        and "std::uint32_t walkStartSends{ 0 };" in read(PLUGIN / "include" / "F4TProxyAnimationSync.h"),
        "walk start sends must count per slot through walkStartSends, never a literal 1",
        failures,
    )

    walk_start = read(PLUGIN / "src" / "F4TProxyWalkStart.cpp")
    require(
        "moveStartAnimated" not in walk_start
        and "TrySendGraphEvent(a_proxy, GraphEvent::kMoveStart)" in walk_start,
        "a walk start must send moveStart only: moveStartAnimated is absent on every build",
        failures,
    )
    require(
        "constexpr std::uint32_t kStanceCheckTicks = 15;" in walk_start
        and "a_state.refusedTicks % kStanceCheckTicks == 0" in walk_start
        and "GetGraphVariableImplInt(RE::BSFixedString{ F4T::ProxyAnimationSync::GraphVar::kIsInSneak }" in walk_start,
        "a refused walk start must read iIsInSneak every 15 refused ticks",
        failures,
    )
    require(
        "const auto resendDue = stanceOff && a_state.stanceResentFor != a_isSneaking;" in walk_start
        and "a_state.stanceResentFor = a_isSneaking;" in walk_start
        and "a_state.walkStart.stanceResentFor.reset();" in animation_sync
        and "a_state.stanceResentFor.reset();" in walk_start,
        "a refused walk start must resend the stance once per wanted stance and episode, then keep retrying the walk start",
        failures,
    )
    require(
        "constexpr std::uint32_t kMaxRefusedTicks = 120;" in walk_start
        and "a_state.refusedTicks > kMaxRefusedTicks" in walk_start
        and "walk start gave up" in walk_start,
        "the walk start retry must stop after a bounded number of refused ticks and log the give-up",
        failures,
    )
    require(
        walk_start.count("[RT-VERIFY 364-B]") >= 4,
        "walk start logs must carry the [RT-VERIFY 364-B] tag",
        failures,
    )
    require("SetSneaking(" not in controller, "controller must not call Actor::SetSneaking", failures)
    require("VisualSneak" not in controller, "controller must not keep the dead visual sneak path or its flags", failures)
    require("actorStateFlags" not in controller, "controller must not log actorStateFlags", failures)

    networking = read(NETWORKING_SOURCE)
    require("actorStateFlags" not in networking, "receiver must not parse actorStateFlags", failures)
    require("flags1=" not in networking, "receiver must not log actorStateFlags", failures)

    remote_action_header = read(REMOTE_ACTION_HEADER)
    remote_action_source = read(REMOTE_ACTION_SOURCE)
    require("actorStateFlags" not in remote_action_header, "RemoteActionSnapshot must not carry actorStateFlags", failures)
    require("ApplyActorStateFromSnapshot" not in remote_action_header, "stub declaration must be removed", failures)
    require("ApplyActorStateFromSnapshot" not in remote_action_source, "stub definition and call must be removed", failures)

    remote_player = read(REMOTE_PLAYER_HEADER)
    require("actionEvents" in remote_player, "RemoteActionEvent list must remain", failures)
    require(
        remote_player.count("actorStateFlags") == 0,
        "RemoteActionEvent and RemotePlayerState must not carry actorStateFlags",
        failures,
    )

    outbound = read(PLUGIN / "src" / "F4TNetworkingOutbound.cpp")
    publish = read(PLUGIN / "src" / "F4TReplicationRuntimePublish.cpp")
    local_movement = read(LOCAL_MOVEMENT_SOURCE)
    transform_send = read(PLUGIN / "src" / "F4TLocalPlayerTransformSend.cpp")
    require(
        "F4T::TransformAnimationFields::WriteAnimationFields(" in transform_send,
        "the transform builder must encode animationDirection, aimPitch and turnDelta",
        failures,
    )
    require(
        re.search(r"SendTransformPacket\(\s*currentTransform,\s*currentLocation,\s*networkMovementState,", publish) is not None,
        "the periodic transform send must pass networkMovementState",
        failures,
    )
    require(
        re.search(r"SendTransformPacket\(\s*currentTransform,\s*currentLocation,\s*debugMovementState,", publish) is not None,
        "the initial transform send must pass debugMovementState",
        failures,
    )
    for field in ("animationDirectionDegrees", "aimPitchDegrees", "turnDeltaDegreesPerSecond"):
        require(
            transform_send.count(f"a_movement.{field}") >= 1,
            f"the transform builder must encode {field} from the movement state",
            failures,
        )
        require(
            local_movement.count(f"state.{field} =") == 1,
            f"GetPlayerMovementState must fill {field}",
            failures,
        )
    for source in (animation_sync, local_movement, outbound, read(PLUGIN / "include" / "F4TProxyRemoteAnimationState.h")):
        for gate in ("kEnableProxyDirectionSync", "kEnableProxyAimStateSync", "IsRuntimeSupported"):
            require(gate not in source, f"{gate} must be gone: aim, direction and crouch sync are always on", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("Proxy sneak action contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
