#!/usr/bin/env python3
"""test_misc_contracts_part2: consolidated contract sections.

Consolidated from 4 single-contract scripts so the group runs one process
instead of 4. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_misc_contracts_part2.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok




def _section_009_test_proxy_sneak_action_contract():
    """Contract moved verbatim from tests/audit/test_proxy_sneak_action_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_proxy_sneak_action_contract.py")
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
        cell_change = release_body.find('ClearSlotEquipmentIfApplied(slot, *proxy, "local cell change")')
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


    if True:
        _merge_exit(main())

def _section_010_test_share_log_previous_session():
    """Contract moved verbatim from tests/audit/test_share_log_previous_session.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_share_log_previous_session.py")
    from pathlib import Path
    import re

    ROOT = Path(__file__).resolve().parents[2]
    PLUGIN_PREVIOUS = ROOT / "CO-BETA/plugin/src/F4TPreviousLog.cpp"
    PLUGIN_PREVIOUS_HEADER = ROOT / "CO-BETA/plugin/include/F4TPreviousLog.h"
    UI_SHARE = ROOT / "CommonwealthOnlineUI/src/F4TShareLog.cpp"
    CORE_REDACTION = ROOT / "co-core/src/runtime/F4TLogRedaction.cpp"
    ATTRIBUTION = "// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission."

    plugin = PLUGIN_PREVIOUS.read_text(encoding="utf-8")
    share = UI_SHARE.read_text(encoding="utf-8")
    core = CORE_REDACTION.read_text(encoding="utf-8")

    assert "\"-previous.log\"" in plugin, "the plugin must rotate the previous session to <plugin>-previous.log"
    assert "PreInitPluginName()" in plugin, "rotation must use the pre-init plugin name, not the post-init one"
    assert "std::filesystem::rename(current, folder / (name + \"-previous.log\"), error);" in plugin, (
        "the rotation must replace an older previous log through the non-throwing std::filesystem::rename"
    )
    assert "if (error || size == 0) {" in plugin, "a missing or zero-byte current log must never replace the previous session"
    assert "kClientLogStems[] = { \"CommonwealthOnline\", \"CommonwealthOnlineNG\", \"CommonwealthOnlineOG\" }" in share, (
        "the UI must try the AE, NG and OG plugin log names"
    )
    assert "kPreviousLogSuffix = \"-previous.log\"" in share, (
        "the UI must read the rotated previous-session log written by the plugin"
    )

    share_body = share.split("Result Share()")[1]
    previous_read = share_body.index("ReadLogTail(previousLog, previousRaw)")
    composed = share_body.index("ComposeSharedLog(previousRaw, raw)")
    assert previous_read < composed, "the previous session must be read before the logs are composed"
    assert "RedactLog(" not in share_body, "redaction must happen inside ComposeSharedLog so both sessions are covered"
    assert "ReadLogTail(previousLog, previousRaw)" in share_body, "the previous session must be read through the capped tail reader"
    assert "for (const auto stem : kClientLogStems) {" in share_body and "clientLog.empty()" in share_body, (
        "the UI must pick a current log name that exists"
    )
    client_loop = share_body.split("for (const auto stem : kClientLogStems) {", 1)[1].split("if (clientLog.empty())", 1)[0]
    assert "break;" not in client_loop, "the UI must not stop at the first current log name that exists"
    assert "std::error_code error;" in client_loop and "std::filesystem::last_write_time(candidate, error)" in client_loop, (
        "the current log lookup must use the non-throwing last_write_time overload"
    )
    assert "written > newest" in client_loop, "the UI must share the current log with the newest last-write time"
    assert "clientLog.stem().string() + std::string{ kPreviousLogSuffix }" in share_body, (
        "the previous log must share the stem of the chosen current log"
    )
    assert "Could not read {}.\", previousLog.string()" not in share_body, "an unreadable previous log must not fail the share"
    assert "previousUnreadable = true;" in share_body and "Could not read the previous session log" in share_body, (
        "an unreadable previous log must be shared as a note on the current log"
    )
    assert share_body.index("ComposeSharedLog(previousRaw, raw)") < share_body.index("Could not read the previous session log"), (
        "the unreadable-previous note must be appended after the logs are composed"
    )

    compose_body = core.split("std::string ComposeSharedLog(")[1]
    assert compose_body.index("RedactLog(a_previousRaw)") < compose_body.index("composed += current;"), (
        "the previous session must come first, redacted"
    )
    assert "===== previous session =====" in compose_body, "the previous session needs a separator"
    assert "===== current session =====" in compose_body, "the current session needs a separator"
    assert "if (a_previousRaw.empty()) {" in compose_body, "without a previous session the output must be the current log only"
    assert "return current;" in compose_body, "without a previous session the output must be the current log only"

    for path in (UI_SHARE, CORE_REDACTION, PLUGIN_PREVIOUS, PLUGIN_PREVIOUS_HEADER):
        raw = path.read_bytes()
        assert raw.decode("utf-8").splitlines()[0] == ATTRIBUTION, f"{path} must carry the FathersFallout attribution"
        assert b"\x00" not in raw, f"{path} must not contain NUL bytes"
        assert raw.count(b"\n") == raw.count(b"\r\n"), f"{path} must use CRLF line endings"

    assert not re.search(r"FathersFallout", share.split("\n", 1)[1]), "FathersFallout must not appear in UI strings"
    assert not re.search(r"FathersFallout", core.split("\n", 1)[1]), "FathersFallout must not appear in co-core strings"

    print("test_share_log_previous_session: all checks passed.")

def _section_011_test_social_mask_contract():
    """Contract moved verbatim from tests/audit/test_social_mask_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_social_mask_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")


    handlers = read("CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp")
    reconcile = read("CO-SERVER/server-cpp/src/ServerProtocolReconcile.cpp")
    chat = read("CO-SERVER/server-cpp/src/ServerChat.cpp")
    chat_header = read("CO-SERVER/server-cpp/src/ServerChat.h")
    factions = read("CO-SERVER/server-cpp/src/ServerFactions.cpp")
    faction_list = read("CO-SERVER/server-cpp/src/ServerFactionList.cpp")
    invites = read("CO-SERVER/server-cpp/src/ServerFactionInvites.cpp")
    graph = read("CO-SERVER/server-cpp/src/ServerSocialGraph.cpp")
    graph_header = read("CO-SERVER/server-cpp/src/ServerSocialGraph.h")
    store = read("CO-SERVER/server-cpp/src/DurableStateKnownPeople.cpp")
    store_header = read("CO-SERVER/server-cpp/include/DurableStateFileStore.h")
    gate = read("CO-SERVER/server-cpp/src/PersistenceGate.cpp")
    gate_header = read("CO-SERVER/server-cpp/include/PersistenceGate.h")
    impl = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
    setup = read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc")
    server_xmake = read("CO-SERVER/server-cpp/xmake.lua")
    client_chat = read("CO-BETA/plugin/src/F4TChat.cpp")
    client_factions = read("CO-BETA/plugin/src/F4TFactions.cpp")

    assert "socialGraph.NameViewFor(" in handlers, "live playerState fanout must split display and disclosed names per recipient"
    assert 'relay.erase("characterName")' in handlers, "live playerState fanout must omit characterName for strangers"
    assert 'if (!playerKey.empty()) relay["playerKey"] = playerKey;' in handlers, "live playerState fanout must carry playerKey only when one is derived"
    assert "MaskRelayedName(packet, a_target, peer);" in reconcile, "catch-up playerState replay must mask names per viewer through the shared relay masker"
    assert "SenderNameView(" in chat, "chat send and history must split display and disclosed names per viewer"
    assert 'masked.erase("fromName")' in chat, "chat send and history must omit fromName for strangers"
    assert '"fromProfileId"' in chat and 'erase("fromProfileId")' in chat, "history must carry the sender profile for masking and strip it on the wire"
    assert "const SocialGraph& social" in chat_header, "chat service must hold the social graph"
    assert "RosterPacketFor(" in factions, "roster must be built per viewer"
    assert "PeerNameView(" in factions and "PeerNameView(" in invites, "roster and invite names must split display and disclosed names per viewer"
    assert 'if (view.disclosedName) player["name"]' in factions, "roster must omit name for strangers and keep it for known viewers"
    assert '{ "displayName", view.displayName }' in factions, "faction member rows must always carry displayName"
    assert 'member["profileId"]' in factions and 'member["name"]' in factions, "faction member rows must gate profileId and name on disclosure"
    assert 'received["fromName"]' in invites or 'pending["fromName"]' in invites, "invites must keep fromName only for disclosed viewers"
    assert "SameFaction(" in factions, "faction membership must imply known for roster display"
    assert '"Stranger"' in graph or "kStrangerName" in graph, "unknown viewers must see the Stranger placeholder"
    assert "|| Knows(" in graph, "masks must consult the known-people set"
    assert "kMaximumKnownPerProfile" in graph_header, "the known set must be capped"
    assert '"knownPeople"' in store, "known people must persist under the knownPeople root key"
    assert "GetKnownPeople" in store_header and "SaveKnownPeople" in store_header, "the store must expose known-people accessors"
    assert "ReadStoredKnownPeople" in gate_header and "SaveKnownPeople" in gate_header, "the gate must expose known-people accessors"
    assert "socialGraph{" in impl, "the protocol host must own the social graph"
    assert "EnableSocial" in setup, "server setup must enable the social graph"
    assert 'add_files("src/ServerSocialGraph.cpp")' in server_xmake, "the server build must compile the social graph"
    assert 'server_test("CommonwealthOnlineServerSocialMaskTests", "tests/ServerSocialMaskTests.cpp", "CommonwealthOnlineServerProtocol")' in server_xmake, "the server mask tests must be registered"
    assert "ResolveShownName(" in client_chat, "client chat must resolve displayName first so stranger messages are not dropped"
    assert client_factions.count("ResolveShownName(") >= 4, "client factions must resolve displayName first in invites, members, and roster"
    assert '{ "leader", leaderView.displayName }' in faction_list, "faction list must show the leader as display text, never the raw name"

    identity = read("co-core/include/F4TIdentityProtocol.h")
    compat = read("co-core/src/session/F4TClientCompatibility.cpp")
    welcome = read("CO-SERVER/server-cpp/src/ServerWelcome.cpp")
    protocol_impl_h = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.h")
    factions_h = read("CO-SERVER/server-cpp/src/ServerFactions.h")

    assert 'SocialNamesCapability = "social-names-v1"' in identity, "the social-names capability must be named in one shared constant"
    assert "F4T::Identity::SocialNamesCapability" in compat, "client hellos must claim the social-names capability"
    assert '"social-names-v1"' in welcome, "the welcome must advertise the social-names capability"
    assert "SessionHasSocialNames" in protocol_impl_h, "the protocol host must gate legacy display text on the hello capability"
    assert "socialNames" in chat_header and "socialNames" in factions_h, "chat and faction peers must carry the viewer capability"
    assert 'masked["fromName"] = view.displayName' in chat, "legacy chat and history must carry display text in fromName"
    assert 'relay["characterName"] = view.displayName' in handlers, "legacy live relay must carry display text in characterName"
    assert 'a_packet["characterName"] = view.displayName' in graph, "legacy join refresh must carry display text in characterName"
    assert 'member["name"] = view.displayName' in factions, "legacy faction rows must carry display text in name"
    assert 'player["name"] = view.displayName' in factions, "legacy roster rows must carry display text in name"
    assert 'pending["fromName"] = view.displayName' in invites, "legacy pending invites must carry display text in fromName"
    assert 'received["fromName"] = inviteView.displayName' in invites, "legacy invite frames must carry display text in fromName"

    friends = read("CO-SERVER/server-cpp/src/ServerFriends.cpp")
    client_friends = read("CO-BETA/plugin/src/F4TFriends.cpp")
    wheel = read("CO-BETA/plugin/src/F4TInteractWheel.cpp")

    assert "social.NameViewFor(" in friends, "a friend request must run the inviter name through the viewer mask"
    assert '{ "displayName", a_view.displayName }' in friends, "a friend request must carry display text for the recipient"
    assert 'if (view.disclosedName) packet["fromName"]' in friends, "a friend request must keep fromName only for disclosed viewers"
    assert "AreFriends(" in graph and "if (AreFriends(" in graph, "a friend name must survive the known-people cap"
    assert "return !AreFriends(" in graph, "the known-people eviction must never drop a friend"
    assert '{ "friends", std::move(friendStore) }' in graph, "friendships must persist next to knownPeople"
    assert "LoadFriends(a_stored)" in graph, "a store without a friends key must still load"
    assert "ForgetProfile" in graph, "slot deletion must clean friendships up on both sides"
    assert "F4T::Identity::FriendsCapability" in friends, "the friend service must gate on the shared friends capability"
    assert 'FriendsCapability = "friends-v1"' in identity, "the friends capability must be named in one shared constant"
    assert "F4T::Identity::FriendsCapability" in compat, "client hellos must claim the friends capability"
    assert '"friends-v1"' in welcome, "the welcome must advertise the friends capability"
    assert "friendService" in impl, "the protocol host must own the friend service"
    assert "DispatchFriendPacket(" in impl, "protocol dispatch must route friend packets"
    assert "PollFriendRequests" in impl, "the host must poll friend request expiry"
    assert "PollExpiry(" in friends and "friendService.PollExpiry(" in friends, "friend requests must expire without a friend packet arriving"
    assert "HasFriends(*peer->session)" in friends, "a client without friends-v1 must never receive a friend packet"
    assert "KnownListCopy(" in friends and "RestoreKnownList(" in friends, "a failed save must also unlearn the names"
    assert 'add_files("src/ServerFriends.cpp")' in server_xmake, "the server build must compile the friend service"
    assert 'server_test("CommonwealthOnlineServerFriendTests", "tests/ServerFriendTests.cpp", "CommonwealthOnlineServerProtocol")' in server_xmake, "the friend tests must be registered"
    assert '"requestExpired"' in friends and '"declined"' in friends, "requests that end without a friendship must disclose no name"
    assert "Friends::ServerSupports()" in wheel, "the wheel must leave the friend slice out when the server has no friends"
    assert '"friends-v1"' in client_friends, "the client must read the friends capability from the welcome"
    assert '"friendRequestReceived"' in client_friends, "the client must handle incoming friend requests"

def _section_012_test_structure_hit_client_contract():
    """Contract moved verbatim from tests/audit/test_structure_hit_client_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_structure_hit_client_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    SINK_HEADER = "CO-BETA/plugin/include/F4TStructureHitClient.h"
    SINK = "CO-BETA/plugin/src/F4TStructureHitClient.cpp"
    MAIN = "CO-BETA/plugin/src/main.cpp"
    REPLICATION = "CO-BETA/plugin/src/F4TWorkshopReplicationClient.cpp"
    REPLICATION_HEADER = "CO-BETA/plugin/include/F4TWorkshopReplicationClient.h"
    REPLICATION_OBJECTS = "CO-BETA/plugin/src/F4TWorkshopReplicationObjects.cpp"
    PROTOCOL_HEADER = "co-core/include/F4TWorkshopProtocol.h"
    PROTOCOL = "co-core/src/protocol/F4TWorkshopProtocol.cpp"
    PROTOCOL_TESTS = "co-core/tests/F4TWorkshopProtocolTests.cpp"
    RAIDS_HEADER = "co-core/include/F4TWorkshopRaids.h"
    RAIDS = "co-core/src/world/F4TWorkshopRaids.cpp"
    RAIDS_TESTS = "co-core/tests/F4TWorkshopRaidsTests.cpp"

    FILE_LIMIT = 750

    EXPECTED_SOURCES = {
        0x000EEBED: "GrenadeOrMine",
        0x000E5750: "GrenadeOrMine",
        0x00107BD6: "GrenadeOrMine",
        0x000FF21D: "GrenadeOrMine",
        0x000FF21F: "GrenadeOrMine",
        0x0010A33D: "GrenadeOrMine",
        0x000E56C2: "GrenadeOrMine",
        0x0010771F: "GrenadeOrMine",
        0x0010A340: "GrenadeOrMine",
        0x0010A342: "GrenadeOrMine",
        0x0010A344: "GrenadeOrMine",
        0x0010C669: "GrenadeOrMine",
        0x000EECE9: "GrenadeOrMine",
        0x000E5752: "GrenadeOrMine",
        0x00245F21: "GrenadeOrMine",
        0x0017A57F: "GrenadeOrMine",
        0x0017B3B3: "GrenadeOrMine",
        0x0017B3B2: "GrenadeOrMine",
        0x0015B07E: "GrenadeOrMine",
        0x0023D535: "GrenadeOrMine",
        0x0003F6F8: "Missile",
        0x000BD56F: "MiniNuke",
        0x00065DF2: "MiniNuke",
        0x001A7FF2: "MiniNuke",
        0x001C6BFF: "MiniNuke",
        0x00246741: "MiniNuke",
    }
    SHARED_EXPLOSIONS = (0x000FD3AA, 0x000E574F, 0x001A43EF)


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    def definition(text, signature, closer="\n\t}\n"):
        start = text.index(signature)
        return text[start:text.index(closer, start)]


    def test_sink_is_registered_once_from_player_polling():
        main = read(MAIN)
        assert '#include "F4TStructureHitClient.h"' in main
        assert main.count("\t\tF4T::StructureHitClient::Install();\n") == 1
        assert main.index("F4T::StructureHitClient::Install();") > main.index("F4T::WorkshopCommitHooks::Install();")


    def test_sink_listens_to_the_hit_event_source_without_inline_hooks():
        source = read(SINK)
        assert "RE::TESHitEvent::GetEventSource()" in source
        assert "eventSource->RegisterSink(&s_sink);" in source
        assert "F4TInlineHook" not in source


    def test_sink_only_reports_the_local_players_hits():
        source = read(SINK)
        assert "RE::PlayerCharacter::GetSingleton()" in source
        assert "eventCause.get() == static_cast<RE::TESObjectREFR*>(localPlayer)" in source
        assert "explosive && (a_event.usesHitData || a_event.projectileFormID != 0)" in source
        assert "!causeIsLocalPlayer" in source
        assert source.count("a_event.usesHitData") == 2


    def test_structure_hit_diagnostics_are_throttled_and_precede_drops():
        source = read(SINK)
        assert "constexpr int kDiagnosticLinesPerSecond{ 10 };" in source
        assert "if (linesInWindow >= kDiagnosticLinesPerSecond) {" in source
        assert "StructureHit non-workshop target={:08X} baseType={}" in source
        assert "StructureHit target={:08X} cause={:08X} localCause={} usesHitData={} sourceFormID={:08X} projectileFormID={:08X} weapon={} outcome={} reason={}" in source
        assert source.index("if (ShouldLogStructureHitDiagnostic()) {") < source.index("if (dropReason) {")
        assert source.index("StructureHit projectile target={:08X}") < source.index("if (weaponKind) {")


    def test_weapon_hits_are_classified_from_the_source_form():
        source = read(SINK)
        assert '#include "F4TWeaponClassification.h"' in source
        assert "WeaponKindFor(a_event.sourceFormID)" in source
        assert "if (a_formID == 0) {" in source
        assert "RE::TESForm::GetFormByID<RE::TESObjectWEAP>(a_formID)" in source
        assert "WeaponClassification::ClassifyWeapon(weapon)" in source
        assert "WeaponClassification::IsMeleeArchetype(animation)" in source
        assert "animation != WeaponClassification::WeaponAnimationType::kRangedGun" in source
        for kind in ("Melee", "Heavy", "Rifle", "Light"):
            assert f"return WeaponKind::{kind};" in source, kind
        assert "kWeaponTypeRifleKeywordFormId{ 0x0004A0A1 }" in source
        assert "kWeaponTypeHeavyGunKeywordFormId{ 0x0004A0A3 }" in source
        assert "static const RE::BGSKeyword* heavyKeyword{ nullptr };" in source
        assert "static const RE::BGSKeyword* rifleKeyword{ nullptr };" in source
        assert "if (!heavyKeyword) {" in source
        assert "if (!rifleKeyword) {" in source


    def test_sink_only_targets_objects_the_replication_client_places():
        assert "WorkshopReplicationClient::FindPlacedObject(*eventTarget.get())" in read(SINK)
        body = definition(read(REPLICATION_OBJECTS), "std::optional<PlacedObject> FindPlacedObject(")
        assert "g_zones" in body and "ObjectIdForHandle(zone, handle)" in body and "g_mutex" in body
        assert "[[nodiscard]] std::optional<PlacedObject> FindPlacedObject(const RE::TESObjectREFR& a_ref);" in read(REPLICATION_HEADER)
        assert "[[nodiscard]] std::optional<std::uint64_t> BuildZoneForWorkbench(const RE::TESObjectREFR& a_workbench);" in read(
            "CO-BETA/plugin/include/F4TSettlementCaptureClient.h"
        )


    def test_settlement_workbench_hits_are_reported_as_the_zone_workbench():
        source = read(SINK)
        assert '#include "F4TSettlementCaptureClient.h"' in source
        assert "auto placed = WorkshopReplicationClient::FindPlacedObject(*eventTarget.get());" in source
        assert "SettlementCaptureClient::BuildZoneForWorkbench(*eventTarget.get())" in source
        assert "WorkshopReplicationClient::PlacedObject{ *zone, F4T::WorkshopRaids::WorkbenchObjectId }" in source
        assert "inline constexpr Workshop::ObjectId WorkbenchObjectId = 0;" in read(RAIDS_HEADER)


    def test_sink_sends_only_through_the_co_core_encoder():
        source = read(SINK)
        assert "WorkshopProtocol::MakeStructureHit(" in source
        assert "QueuePacket(WorkshopProtocol::MakeStructureHit(a_hit));" in source
        assert "WorkshopProtocol::StructureHit{ placed->buildZoneId, placed->objectId, explosive.value_or(ExplosiveKind{}), *weaponKind }" in source
        assert "SendPacketBytes(packet.dump())" in source
        assert '"structureHit"' not in source


    def test_explosive_table_matches_the_researched_sources():
        rows = re.findall(r"ExplosiveSource\{ (0x[0-9A-F]{8}), ExplosiveKind::(\w+) \}", read(RAIDS))
        found = {int(hex_id, 16): kind for hex_id, kind in rows}
        assert len(rows) == len(found) == len(EXPECTED_SOURCES)
        assert found == EXPECTED_SOURCES
        for shared in SHARED_EXPLOSIONS:
            assert shared not in found


    def test_encoder_and_kind_names_are_declared_defined_and_tested():
        assert "[[nodiscard]] nlohmann::json MakeStructureHit(const StructureHit& a_hit);" in read(PROTOCOL_HEADER)
        assert "nlohmann::json MakeStructureHit(const StructureHit& a_hit)" in read(PROTOCOL)
        assert "[[nodiscard]] std::string_view ExplosiveKindName(ExplosiveKind a_kind) noexcept;" in read(RAIDS_HEADER)
        names = definition(read(RAIDS), "std::string_view ExplosiveKindName(")
        for name in ("grenade", "missile", "mininuke"):
            assert f'return "{name}";' in names, name
        parse = definition(read(RAIDS), "std::optional<ExplosiveKind> ParseExplosiveKind(")
        for name in ("grenade", "missile", "mininuke"):
            assert f'"{name}"' in parse, name
        assert "MakeStructureHit(*hit)" in read(PROTOCOL_TESTS)
        assert "ExplosiveKindName(ExplosiveKind::MiniNuke)" in read(RAIDS_TESTS)


    def test_new_files_are_crlf_and_under_the_line_limit():
        for name in (SINK_HEADER, SINK):
            raw = (ROOT / name).read_bytes()
            assert raw.count(b"\n") == raw.count(b"\r\n"), name
            assert raw.count(b"\n") < FILE_LIMIT, name


    def test_projectile_impacts_collect_only_unprocessed_local_impacts():
        source = read(SINK)
        body = definition(source, "void CollectLocalImpacts(", "\n\t\t}\n")
        assert "a_projectile->shooter != localPlayer->GetHandle() || a_projectile->explosion" in body
        assert "impact.processed || impact.impactDecalQuery.data != static_cast<std::uint64_t>(-1)" in body
        assert "a_impacts.targets[a_impacts.count++] = impact.collidee;" in body


    def test_projectile_detour_calls_the_original_then_reports_on_the_main_thread():
        source = read(SINK)
        assert "constexpr std::size_t kProcessImpactsSlot{ 208 };" in source
        assert "using ProcessImpactsFn = bool (*)(RE::MissileProjectile*);" in source
        body = definition(source, "bool ProcessImpactsHook(", "\n\t\t}\n")
        assert body.count("} catch (...) {") == 2
        original = body.index("reinterpret_cast<ProcessImpactsFn>(g_missileProcessImpactsOriginal)(a_projectile)")
        assert body.index("CollectLocalImpacts(a_projectile, impacts);") < original < body.index("QueueProjectileImpacts(std::move(impacts));")
        assert "taskInterface->AddTask([impacts = std::move(a_impacts)]() {" in definition(source, "void QueueProjectileImpacts(", "\n\t\t}\n")
        report = definition(source, "void ReportProjectileImpacts(", "\n\t\t}\n")
        assert "WorkshopReplicationClient::FindPlacedObject(*target)" in report
        assert "a_impacts.weaponFormID == 0 ? std::optional<WeaponKind>{} : WeaponKindFor(a_impacts.weaponFormID)" in report
        assert "WorkshopProtocol::StructureHit{ placed->buildZoneId, placed->objectId, ExplosiveKind{}, *weaponKind }" in report


    def test_projectile_hook_is_installed_on_the_verified_vtable_slot_per_build():
        source = read(SINK)
        body = definition(source, "bool InstallProcessImpactsHook()", "\n\t\t}\n")
        for value in ("0xFC4110", "0xDB68A0", "0xE3C8D0"):
            assert value in source, value
        assert "MissileProjectile[0]" in body
        assert "VtableGuard::VerifySlot(request)" in body
        assert "VtableGuard::SlotMatchesExpectedRva(request.slotValue, module.GetBaseAddress(), entry.expected)" in body
        assert "g_missileProcessImpactsOriginal = request.slotValue;" in body
        assert "InstallProcessImpactsHook();" in definition(source, "bool Install()", "\n\t}\n")


    def test_wall_health_bar_only_follows_the_local_players_own_hits():
        hud = read("CO-BETA/plugin/src/F4TStructureDamageHud.cpp")
        assert "WorkshopReplicationClient::NoteLocalStructureHit({ a_hit.buildZoneId, a_hit.objectId });" in definition(read(SINK), "void SendStructureHit(", "\n\t\t}\n")
        note = definition(hud, "void NoteLocalStructureHit(", "\n\t}\n")
        assert "cleared = g_displayed;" in note and "QueueStructureHudClear(cleared->objectId);" in note
        damage = definition(hud, "void PushStructureDamageHud(", "\n\t}\n")
        assert "g_localHit->objectId != shown.objectId" in damage and "g_displayed = shown;" in damage
        destroyed = definition(hud, "void PushStructureDestroyedHud(", "\n\t}\n")
        assert "g_displayed->objectId != a_object" in destroyed
        assert "QueueStructureHudClear(a_object);" in destroyed
        assert '{ "health", 0 }' in definition(hud, "void QueueStructureHudClear(", "\n\t\t}\n")
        handler = definition(read(REPLICATION), "bool HandleStructureDestroyed(", "\n\t\t}\n")
        assert "PushStructureDestroyedHud(zoneId->get<Workshop::BuildZoneId>(), id);" in handler


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"structure hit client contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())


_SECTIONS = [
    ("tests/audit/test_proxy_sneak_action_contract.py", _section_009_test_proxy_sneak_action_contract),
    ("tests/audit/test_share_log_previous_session.py", _section_010_test_share_log_previous_session),
    ("tests/audit/test_social_mask_contract.py", _section_011_test_social_mask_contract),
    ("tests/audit/test_structure_hit_client_contract.py", _section_012_test_structure_hit_client_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_misc_contracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
