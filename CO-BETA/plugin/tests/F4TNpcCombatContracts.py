#!/usr/bin/env python3
"""F4TNpcCombatContracts: consolidated contract sections.

Consolidated from 4 single-contract scripts so the group runs one process
instead of 4. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TNpcCombatContracts.py
"""
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[3]


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


def _section_002_f4tdownedcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TDownedContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TDownedContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []
    pending = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def strip_comments(text):
        return re.sub(r"//[^\n]*", "", text)


    def read_code(relative):
        return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


    def function_body(code, signature):
        start = code.index(signature)
        brace = code.index("{", start)
        depth = 0
        for index in range(brace, len(code)):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    return code[brace:index + 1]
        raise AssertionError(f"unterminated body for {signature}")


    def in_order(text, *needles):
        positions = []
        for needle in needles:
            index = text.find(needle)
            if index < 0:
                return False
            positions.append(index)
        return positions == sorted(positions)


    xmake = (PLUGIN / "xmake.lua").read_text(encoding="utf-8")
    glue_header = (PLUGIN / "include/F4TDownedGlue.h").read_text(encoding="utf-8")
    ui_header = (PLUGIN / "include/F4TDownedUi.h").read_text(encoding="utf-8")
    state_header = (PLUGIN / "include/F4TDownedState.h").read_text(encoding="utf-8")
    wire_header = (PLUGIN / "include/F4TDownedWire.h").read_text(encoding="utf-8")
    ENGINE_PARTS = ("src/F4TDownedEngine.cpp", "src/F4TDownedEngineRecovery.cpp")
    engine_raw = "".join((PLUGIN / part).read_text(encoding="utf-8") for part in ENGINE_PARTS)
    engine = "\n".join(read_code(part) for part in ENGINE_PARTS)
    GLUE_PARTS = ("src/F4TDownedGlue.cpp", "src/F4TDownedGlueLocal.cpp", "src/F4TDownedGlueRespawn.cpp",
                  "src/F4TDownedGlueRevive.cpp", "src/F4TDownedGlueEvents.cpp", "src/F4TDownedGlueInternal.h")
    glue_raw = "".join((PLUGIN / part).read_text(encoding="utf-8") for part in GLUE_PARTS)
    glue = "\n".join(read_code(part) for part in GLUE_PARTS)
    runtime = read_code("src/F4TReplicationRuntime.cpp")
    remote_state = read_code("src/F4TRemotePlayerState.cpp")
    inbound_raw = (PLUGIN / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
    vitals_header = (PLUGIN / "include/F4TPlayerVitals.h").read_text(encoding="utf-8")
    co_core_validation = (ROOT / "co-core/src/validation/F4TPlayerStateValidation.cpp").read_text(encoding="utf-8")

    check("inline constexpr std::uint32_t kMaximumRespawnRequestRetries = 8;" in state_header,
          "F4TDownedState.h must cap the respawn request retries at 8")
    check(re.search(r"enum class Phase\s*\{\s*kAlive,\s*kDowned,\s*kDead\s*\};", state_header) is not None,
          "Phase must be kAlive, kDowned, kDead in that order")
    check(re.search(r"enum class RespawnPoint\s*\{\s*kSanctuary,\s*kLastBed,\s*kFriend,\s*kCheckpoint\s*\};", state_header) is not None,
          "RespawnPoint must stay kSanctuary, kLastBed, kFriend and then kCheckpoint, so the wire names of the first three never move")
    check("inline constexpr std::int32_t kMaximumServerRemainingMs = 600000;" in state_header,
          "the server remainingMs clamp must stay at the server 600 s maximum")
    check(re.search(r"enum class EventKind\s*\{\s*kDownedState,\s*kRevived,\s*kDied,\s*kPlayerDied,\s*kRespawnGrant,\s*kRespawnChoice,\s*kReviveStarted,\s*kReviveCancelled,\s*kHeldItemDropped,\s*kDeathBagCreate,\s*kDeathBagHealthGrant,\s*kDeathBagDepositResult,\s*kDeathBagPlacedResult,\s*kDeathBagVoided,\s*kDeathBagDiscovery\s*\};",
                    wire_header) is not None,
          "EventKind must carry the six downed event kinds, then the revive channel, the held-item drop and the six death bag kinds")
    for packet_type in ("downedState", "revived", "died", "playerDied", "respawnGrant", "reviveRequest",
                        "reviveCancel", "checkpoint", "reviveStarted", "reviveCancelled", "heldItemDropped"):
        check(f'"{packet_type}"' in wire_header, f"F4TDownedWire.h must name the {packet_type} packet type")

    state = state_header[state_header.index("class State"):]
    check(re.search(r"\[\[nodiscard\]\] bool IsIncapacitated\(\) const noexcept \{ return phase_ != Phase::kAlive; \}", state) is not None,
          "IsIncapacitated must mean any phase other than kAlive")
    enter_downed = function_body(state, "bool EnterDowned(")
    check("phase_ != Phase::kAlive" in enter_downed and "++downCount_;" in enter_downed
          and in_order(enter_downed, "ClearDeadlines();", "localDeadlineSeconds_ = a_nowSeconds + config_.downedSeconds;"),
          "EnterDowned must refuse unless alive, count the down and start the local timer after clearing the deadlines")
    server_downed = function_body(state, "bool ApplyServerDownedState(")
    check("phase_ == Phase::kDead" in server_downed
          and "(std::clamp)(a_remainingMs, 0, kMaximumServerRemainingMs)" in server_downed,
          "ApplyServerDownedState must refuse while dead and clamp the server remainingMs")
    check(re.search(r"bool ApplyRespawnGrant\(\)\s*\{\s*return ApplyRevived\(\);\s*\}", state) is not None,
          "ApplyRespawnGrant must leave the dead phase through ApplyRevived")
    revived = function_body(state, "bool ApplyRevived(")
    check("phase_ == Phase::kAlive" in revived and "phase_ = Phase::kAlive;" in revived and "ClearDeadlines();" in revived,
          "ApplyRevived must refuse when already alive and clear the deadlines when it succeeds")
    died = function_body(state, "bool ApplyDied(")
    check("phase_ == Phase::kDead" in died and "phase_ = Phase::kDead;" in died,
          "ApplyDied must refuse when already dead")
    tick = function_body(state, "bool Tick(")
    check("phase_ != Phase::kDowned" in tick and "a_nowSeconds < EffectiveDeadlineSeconds()" in tick
          and in_order(tick, "phase_ = Phase::kDead;", "ClearDeadlines();"),
          "Tick must only expire the downed phase, never the alive or dead one")

    for symbol in ("void Tick();", "bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);",
                   "float ResolveLocalPlayerDamage(float a_healthBefore, float a_maximumHealth, float a_damage);",
                   "bool IsLocalPlayerDowned();", "bool IsLocalPlayerDead();", "void NoteWelcome();"):
        check(symbol in glue_header, f"F4TDownedGlue.h must declare {symbol}")
    for symbol in ("ShowDownedOverlay", "HideDownedOverlay", "ShowRespawnMenu", "HideRespawnMenu"):
        check(symbol in ui_header, f"IDownedUiHost must declare {symbol}")
    check("void SetDownedUiHost(IDownedUiHost* a_host);" in ui_header
          and "bool HasDownedUiHost();" in ui_header
          and "void OnRespawnChosen(std::string_view a_point, std::optional<std::uint32_t> a_friendId);" in ui_header,
          "F4TDownedUi.h must declare the host registration, the presence probe and the respawn choice entry point")

    handle = function_body(glue, "bool TryHandlePacket(")
    check(re.search(r"case DecodeStatus::kUnknown:\s*return false;", handle) is not None,
          "TryHandlePacket must return false for a packet type the downed module does not own")
    check(re.search(r"case DecodeStatus::kMalformed:.*?return true;", handle, re.S) is not None,
          "TryHandlePacket must swallow a malformed downed packet and return true")
    check("SetRemotePlayerAuthoritativeDowned(" in handle and
          re.search(r"QueueEvent\(event\);\s*return true;", handle) is not None,
          "TryHandlePacket must apply versioned remote state or queue handled events")
    check("TryDecodeDownedEvent(" in handle, "TryHandlePacket must decode through TryDecodeDownedEvent")

    grant_handle = function_body(read_code("src/F4TDeathBagGrant.cpp"), "bool TryHandlePacket(")
    check('a_type == "deathBagGrantReceipt"' in grant_handle
          and "if (!F4T::DeathBagWire::DecodeGrantReceipt(a_packet)) return false;" in grant_handle,
          "DeathBagGrant::TryHandlePacket must validate deathBagGrantReceipt frames and return true")

    apply_pending = function_body(glue, "void ApplyPendingEvents(")
    for event_kind in ("EventKind::kDownedState", "EventKind::kRevived", "EventKind::kDied",
                       "EventKind::kPlayerDied", "EventKind::kRespawnGrant", "EventKind::kRespawnChoice"):
        check(f"case {event_kind}:" in apply_pending, f"ApplyPendingEvents must dispatch {event_kind}")
    resend_placed = function_body(glue, "void ResendPlaced(")
    reject_placement = function_body(glue, "bool ReturnRejectedPlacement(")
    retry_rejections = function_body(glue, "void RetryUnreturnedRejections(")
    check(in_order(apply_pending, "ResendPlaced(a_nowSeconds);", "ShouldApplyPendingEvents(CanTouchWorld(), !events.empty())")
          and "unacked.delay = (std::min)(unacked.delay * 2.0, kPlacedRetryMaximumSeconds);" in resend_placed
          and "constexpr double kPlacedRetryMaximumSeconds = 30.0;" in glue and "GetConnectionGeneration()" in resend_placed,
          "deathBagPlaced must resend with capped backoff before the events gate and re-arm on reconnect")
    check(in_order(reject_placement, "if (!player || !bag) {", "return false;", "F4T::DeathBagLocal::ReturnAll(", "F4T::DeathBagView::Void(a_deathId, true);",
                   "F4T::DeathBagGrantLedger::RecordPlaceFailure(a_deathId, returned, remaining);", "return true;")
          and "if (!ReturnRejectedPlacement(event.deathId, bag)) g_unreturnedRejections.insert_or_assign(event.deathId, bag);" in apply_pending,
          "a rejected deathBagPlaced must return the items and record the place failure, or keep the rejection when the bag does not resolve")
    check("ReturnRejectedPlacement(it->first, it->second)" in retry_rejections and "g_unreturnedRejections.erase(it)" in retry_rejections
          and in_order(apply_pending, "ShouldApplyPendingEvents(CanTouchWorld(), !events.empty())", "RetryUnreturnedRejections();", "for (const auto& event : events)"),
          "kept rejections must be retried on the touchable tick before the queued events are applied, and dropped only once returned")
    sweep = read_code("src/F4TDeathBagSavedSweep.cpp")
    view_src = read_code("src/F4TDeathBagView.cpp")
    view_header = (PLUGIN / "include/F4TDeathBagView.h").read_text(encoding="utf-8")
    check("void SweepCellSavedContainers(RE::TESObjectCELL& a_cell)" in sweep and "void SweepSavedContainers()" in sweep
          and "F4T::DeathBagLocal::IsBagReference(*a_ref)" in sweep and "!IsViewReference(a_ref)" in sweep
          and "IsAttached()" in sweep and "SweepCellSavedContainers(*cell);" in sweep and "MarkAsDeleted();" in sweep,
          "SweepSavedContainers must sweep every attached cell through SweepCellSavedContainers, which deletes the saved death bags that are not live views")
    check(in_order(sweep, "ref->Disable();", "ref->MarkAsDeleted();")
          and in_order(function_body(view_src, "iterator RemoveView("), "bag->Disable();", "bag->MarkAsDeleted();"),
          "RemoveView and SweepCellSavedContainers must Disable a death bag container before MarkAsDeleted")
    reconcile = function_body(view_src, "void Reconcile(")
    check("bool listed{ false };" in view_src and "std::move(handle), 0.0, true });" in view_src
          and in_order(reconcile, "const bool delisted = it->second.listed && !listedNow;", "it->second.listed = it->second.listed || listedNow;",
                       "settled && (want != wanted.end() || HoldsNothing(*bag) || delisted)"),
          "a view must record that its bag was listed, and be removed once settled when the server stops listing it; an unlisted pending placement must be kept")
    check("void SweepSavedContainers();" in view_header and "void SweepCellSavedContainers(RE::TESObjectCELL& a_cell);" in view_header,
          "F4TDeathBagView.h must declare the saved container sweep functions")
    check(in_order(function_body(view_src, "void Tick("), "if (cellKey != g_lastCellKey) {", "SweepCellSavedContainers(*cell);",
                   "Reconcile(*player, *cell, cellKey, a_nowSeconds);"),
          "Tick must sweep the saved death bags of the current cell on a cell change, before it reconciles the views")
    check("bool FillView(RE::TESObjectREFR& a_bag, const nlohmann::json& a_stacks)" in read_code("src/F4TDeathBagLocal.cpp")
          and "F4T::DeathBagLocal::FillView(*bag, stacks)" in view_src and "bool FillView(" not in view_src,
          "FillView must live in F4TDeathBagLocal.cpp and CreateView must call it through F4T::DeathBagLocal")
    bag_local = read_code("src/F4TDeathBagLocal.cpp")
    read_stacks = function_body(bag_local, "ReadStacks(RE::TESObjectREFR& a_reference")
    check(in_order(read_stacks, "const auto unrepresentable =", "if (unrepresentable && !a_retainUnrepresentable) {", "stacks.push_back({ form, index, stack->GetCount(), quest, unrepresentable });",
                   "if (quest || unrepresentable || IsProtectedPipboyForm(item.object)) {", "a_retained.push_back(")
          and "ReadStacks(a_player, ignored.stacks, ignored.retained, true, true)" in bag_local
          and "ReadStacks(a_player, manifest.stacks, manifest.retained, false, true)" in bag_local
          and "ReadStacks(a_player, ignored.stacks, ignored.retained, false, true)" in bag_local
          and "!Representable(*stack)) continue;" in function_body(bag_local, "bool RecordWorn("),
          "a stack with item data the bag cannot encode (mods) must stay with the player as a retained stack instead of refusing the whole death bag, and must not be unequipped")
    check(in_order(apply_pending, "ShouldApplyPendingEvents(CanTouchWorld(), !events.empty())", "RestoreEvents(events);")
          and apply_pending.index("RestoreEvents(events);") < apply_pending.index("for (const auto& event : events)"),
          "ApplyPendingEvents must put the drained events back when the world is not safe to touch")
    check("kMaximumPendingEvents = 64" in glue and in_order(glue, "g_pendingEvents.size() >= kMaximumPendingEvents", "g_pendingEvents.pop_front();"),
          "the pending event queue must be bounded and drop the oldest event when full")
    resolve_pending = function_body(glue, "void ResolvePendingPlace(")
    deposit_case = function_body(glue, "case EventKind::kDeathBagDepositResult:")
    placed_case = function_body(glue, "case EventKind::kDeathBagPlacedResult:")
    snapshot_case = function_body(glue, "case EventKind::kDeathBagCreate:")
    check(in_order(apply_pending, "ResendPlaced(a_nowSeconds);", "ResendUnplaced(a_nowSeconds);", "ResolvePendingPlaces(a_nowSeconds);", "ShouldApplyPendingEvents(CanTouchWorld(), !events.empty())")
          and in_order(function_body(glue, "void ResolvePendingPlaces("), "if (!CanTouchWorld())", "LoadGeneration()", "ResolvePendingPlace(pending, a_nowSeconds);")
          and in_order(resolve_pending, "CarriedStacks(*player)", "F4T::DeathBagPlaceRecovery::Classify(a_pending.stacks, *carried)",
                       "F4T::DeathBagPlaceRecovery::Exact(a_pending.stacks, *carried)", "F4T::DeathBagLocal::Snapshot(*player, deathId)", "F4T::DeathBagLocal::Place(*player, deathId)")
          and "g_unplacedUnacked[deathId] = UnplacedRetry{ a_nowSeconds, kPlacedRetrySeconds };" in glue,
          "a reload must resolve pending placements once per ledger load, before the queued events, by classifying the carried inventory")
    check(in_order(deposit_case, "RecordPendingPlace(event.deathId,", "SetPendingPlacePhase(event.deathId, \"placing\")", "F4T::DeathBagLocal::Place(*player, event.deathId);",
                   "SetPendingPlacePhase(event.deathId, \"placed\")", "AnnouncePlaced(event.deathId, placed.bag, a_nowSeconds);")
          and "RecordPlaceFailure(event.deathId, true, nlohmann::json::array());" in deposit_case,
          "a deposit must record the pending placement, persist the placing phase before Place, persist the placed phase before announcing it, and treat a failed record as restored")
    check(in_order(placed_case, "ForgetPendingPlace(event.deathId);", "if (!event.deathBagPlaced) {", "if (!bag.get()) {",
                   "if (!ReturnRejectedPlacement(event.deathId, bag)) g_unreturnedRejections.insert_or_assign(event.deathId, bag);"),
          "a placed ack must forget the pending placement and return a rejected bag only when that bag still resolves")
    check("F4T::FailClosedNotice::Show(kSnapshotRefusedNotice);" in snapshot_case and "F4T::DeathBagLocal::Abandon(event.deathId);" in snapshot_case,
          "a refused death bag snapshot must abandon the carried items and show a notice")
    check("F4T::FailClosedNotice::Show(returned ? kRejectedReturnedNotice : kRejectedStuckNotice);" in function_body(glue, "bool ReturnRejectedPlacement("),
          "a returned or stuck rejected placement must show a notice")
    check(in_order(resolve_pending, "CarriedStacks(*player)", "F4T::DeathBagPlaceRecovery::Classify(a_pending.stacks, *carried)",
                   "F4T::DeathBagPlaceRecovery::Exact(a_pending.stacks, *carried)", "F4T::DeathBagPlaceRecovery::Resolve(a_pending.phase, holding, exact)",
                   "F4T::DeathBagLocal::Snapshot(*player, deathId)", "F4T::DeathBagLocal::Place(*player, deathId)")
          and "F4T::DeathBagGrantLedger::RecordPlaceFailure(deathId, true, nlohmann::json::array());" in resolve_pending
          and "g_unplacedUnacked[deathId] = UnplacedRetry{ a_nowSeconds, kPlacedRetrySeconds };" in resolve_pending,
          "a reload must resolve a pending placement through the phase-aware Resolve classifier, and only a placed bag may resend placed")
    check(in_order(function_body(glue, "void ResolvePendingPlaces("), "LoadGeneration()", "F4T::DeathBagPendingLimits::Prune(0);", "ResolvePendingPlace(pending, a_nowSeconds);")
          and in_order(deposit_case, "F4T::DeathBagPendingLimits::Prune(1);", "RecordPendingPlace(event.deathId,"),
          "pending placement records must be pruned by age and count on a reload and before a new deposit is recorded")
    null_start = placed_case.index("if (!bag.get()) {")
    null_branch = placed_case[null_start:placed_case.index("break;", null_start) + len("break;")]
    check(in_order(null_branch, "REX::WARN(\"Rejected death bag placement has no handle after a reload; its items cannot be returned from here.\");",
                   "F4T::FailClosedNotice::Show(kRejectedStuckNotice);", "break;")
          and "ReturnRejectedPlacement" not in null_branch
          and "if (!ReturnRejectedPlacement(event.deathId, bag)) g_unreturnedRejections.insert_or_assign(event.deathId, bag);" in placed_case,
          "a rejected placement with no bag handle must log and show the stuck notice instead of returning items")
    limits_src = read_code("src/F4TDeathBagPendingLimits.cpp")
    check("F4T::DeathBagPlaceRecovery::Expired(ages, now)" in limits_src and "F4T::DeathBagPlaceRecovery::Overflow(ages, a_room)" in limits_src
          and "F4T::DeathBagGrantLedger::ForgetPendingPlace(deathId);" in limits_src and "F4T::DeathBagGrantFiles::NowSeconds()" in limits_src,
          "F4TDeathBagPendingLimits.cpp must drop expired and over-limit pending placement records through the ledger")
    record_pending = function_body(read_code("src/F4TDeathBagGrantLedger.cpp"), "bool RecordPendingPlace(")
    check(in_order(record_pending, "if (!a_stacks.is_array() || a_stacks.empty()) return false;", "const std::scoped_lock lock(g_mutex);", "PendOp(PendingPlace{ a_deathId, \"pending\""),
          "RecordPendingPlace must refuse a non-array or empty stack manifest before it journals a pending placement")

    resolve = function_body(glue, "float ResolveLocalPlayerDamage(")
    check(in_order(resolve, "if (g_state.IsIncapacitated())", "return 0.0F;", "ResolveLethalDamage(")
          and "if (outcome.enteredDowned)" in resolve and "EnterDownedPhase(NowSeconds());" in resolve
          and "return outcome.appliedDamage;" in resolve,
          "ResolveLocalPlayerDamage must drop damage while incapacitated, enter the downed phase on a lethal hit and return the resolved damage")

    enter_down = function_body(glue, "void EnterDownedPhase(")
    check("if (!g_state.EnterDowned(a_nowSeconds))" in enter_down
          and in_order(enter_down, "g_nextPoseRetrySeconds = 0.0;", "ReapplyDownedPose(a_nowSeconds);"),
          "EnterDownedPhase must do nothing when the state machine refuses and reset the pose retry when it accepts")
    check(in_order(enter_down, "g_nextPoseRetrySeconds = 0.0;", "F4T::Downed::CloseChatForDownedPlayer();", "ReapplyDownedPose(a_nowSeconds);"),
          "EnterDownedPhase must close the chat window before the downed pose is applied, so a typing player cannot keep focus while down")
    check(in_order(enter_down, "g_nextPoseRetrySeconds = 0.0;", "CancelBleedoutRecovery();", "ReapplyDownedPose(a_nowSeconds);"),
          "EnterDownedPhase must cancel a pending bleedout recovery before the downed pose is reapplied, so going down again cannot be undone by the earlier revive request")
    enter_dead = function_body(glue, "void EnterDeadPhase(")
    check("SetHealth(*player, *health, 0.0F)" in enter_dead and "SetControlBlocked(*player, true)" in enter_dead,
          "EnterDeadPhase must zero the health and block control")
    check(in_order(enter_dead, "SetControlBlocked(*player, true);", "CancelBleedoutRecovery();", "g_selfReviveRequested.store(false);"),
          "EnterDeadPhase must cancel a pending bleedout recovery, so a death during a recovery cannot stand the player back up")
    engine_kill = function_body(glue, "void HandleEngineLethalDamage(")
    check(in_order(engine_kill, "if (player->IsDead(true))", "if (g_state.GetPhase() == Phase::kDead)", "player->Resurrect(false, true);", "ReseatLocalPlayer();", "EnterDownedPhase(a_nowSeconds);"),
          "an engine kill must stay dead in the dead phase, resurrect and re-seat the player, then enter the downed phase")
    leave = function_body(glue, "void LeaveIncapacitated(")
    check("SetControlBlocked(*player, false)" in leave and "RestoreLocalPlayerHealth()" in leave and "SetPose(*player, false)" in leave,
          "LeaveIncapacitated must restore the pose, unblock control and restore health only when asked")
    check(in_order(leave, "SetControlBlocked(*player, false);", "if (IsEssentialDown(*player)) {", "RequestBleedoutRecovery();", "} else {", "SetPose(*player, false);", "RestoreLocalPlayerHealth();")
          and re.search(r"\} else \{\s*RequestBleedoutRecovery\(\);\s*\}", leave) is not None,
          "LeaveIncapacitated must unblock control, then request the bleedout recovery for an essential-down player or clear the pose otherwise, before restoring health, and must still request the recovery when the world cannot be touched")
    downed_state_event = function_body(glue, "void ApplyDownedStateEvent(")
    check(re.search(r"ApplyServerDownedState\(a_event\.downed, a_event\.remainingMs, a_nowSeconds\)\) \{\s*CancelBleedoutRecovery\(\);", downed_state_event) is not None,
          "a downedState that enters the downed phase for the local player must cancel a pending bleedout recovery, so a stale recovery cannot stand the new downed player up")

    grant = function_body(glue, "void ApplyRespawnGrantEvent(")
    check("const bool engineDead = player && CanTouchWorld() && player->IsDead(true);" in grant
          and "if (!g_state.ApplyRespawnGrant() && !engineDead)" in grant
          and "if (engineDead) {" in grant,
          "a respawn grant must still repair an engine-dead actor after a preceding revived event, while an unexpected grant on a live actor is ignored")
    check(in_order(grant, "HideRespawnMenu();", "HideOverlay();", "g_pendingRespawnPoint.reset();",
                   "g_pendingRespawnFriendId.reset();", "LeaveIncapacitated(false);",
                   "TeleportForRespawn(a_event.point, a_event.friendId, a_event.checkpoint);",
                   "RestoreLocalPlayerHealth();"),
          "ApplyRespawnGrantEvent must clear both pending halves and the UI, then teleport with the granted checkpoint and restore health")
    check(re.search(r"if \(CanTouchWorld\(\)\) \{\s*TeleportForRespawn\(", grant) is not None
          and grant.index("RestoreLocalPlayerHealth();") > grant.index("if (CanTouchWorld())"),
          "the granted teleport must be skipped when the world cannot be touched, and the health restore must still run outside that guard")
    check("g_state.ApplyRespawnGrant()" in grant and "ApplyRespawnGrant()" not in glue.replace(grant, ""),
          "the respawn grant must be the only path out of the dead phase")
    check(in_order(grant, "const bool engineDead = player && CanTouchWorld() && player->IsDead(true);",
                   "g_pendingRespawnFriendId.reset();", "if (engineDead) {", "player->Resurrect(false, true);",
                   "LeaveIncapacitated(false);"),
          "a respawn grant must revive a local player that is still dead before LeaveIncapacitated clears the pose")
    check(in_order(grant, "player->Resurrect(false, true);", "ReseatLocalPlayer();", "LeaveIncapacitated(false);"),
          "a respawn grant that revives an engine-dead player must re-seat it before LeaveIncapacitated clears the pose")

    menu = function_body(glue, "void ShowRespawnMenu(")
    check(in_order(menu, "if (g_checkpoint)", "HasLastBedCell()", "CollectFriends()", "auto* host = g_uiHost.load();")
          and re.search(r"if \(!host\) \{.*?RequestRespawn\(AutomaticRespawnPoint\(\), std::nullopt\);", menu, re.S) is not None,
          "ShowRespawnMenu must offer the captured checkpoint first, gate the last bed and the friends, then respawn directly when no host is registered")
    request = function_body(glue, "void RequestRespawn(")
    check(in_order(request, "IsRespawnRequestValid(a_point, a_friendId)", "g_pendingRespawnPoint = a_point;",
                   "g_respawnRequestAttempts = 0;", "SendRespawnRequest(a_point, a_friendId)")
          and "a_point = FallbackRespawnPoint();" in request,
          "RequestRespawn must reject an invalid friend request, record the pending point and reset the retry counter before sending")
    retry = function_body(glue, "void RetryPendingRespawn(")
    check(in_order(retry, "!g_pendingRespawnPoint || g_state.GetPhase() != Phase::kDead", "ShouldRetryRespawnRequest(",
                   "ComputeRespawnRetryDelay(", "++g_respawnRequestAttempts;", "SendRespawnRequest("),
          "RetryPendingRespawn must only run while dead, back off and give up through ShouldRetryRespawnRequest")

    end_session = function_body(glue, "void EndSession(")
    reset_session = function_body(glue, "void ResetSession(")
    check("ResetSession();" in end_session and "(void)DrainEvents();" in end_session,
          "EndSession must reset the session and drop the queued events")
    check(in_order(end_session, "if (g_state.IsIncapacitated() && !CanTouchWorld()) {", "return;", "ResetSession();", "(void)DrainEvents();"),
          "EndSession must return before any teardown while incapacitated and the world cannot be touched, then reset the session and drop the queued events")
    check("g_state.Reset()" in reset_session and "ApplySessionFlags(false)" in reset_session
          and "ResetReviveHold(g_reviveHold);" in reset_session and "g_remotePoses.clear();" in reset_session,
          "ResetSession must reset the state, clear the poses and undo the session flags")
    check(in_order(reset_session, "if (g_state.IsIncapacitated()) {", "player && CanTouchWorld() && player->IsDead(true)",
                   "player->Resurrect(false, true);", "ReseatLocalPlayer();", "LeaveIncapacitated(true);", "HideOverlay();", "g_state.Reset();"),
          "ResetSession must restore an incapacitated local player, resurrecting and re-seating a still-dead one first, before resetting the state")
    welcome = function_body(glue, "void NoteWelcome()")
    check(in_order(welcome, "(void)DrainEvents();", "g_connectionResetPending.store(true);"),
          "NoteWelcome runs on the receive thread: it must drop the queued events before it raises the reset flag")
    check("inline std::atomic<bool> g_connectionResetPending{ false };" in glue_raw,
          "the connection reset flag must be atomic because the welcome packet arrives on the receive thread")
    check("ApplySessionFlags(false)" in reset_session and "ApplySessionFlags(true)" in function_body(glue, "void Tick()"),
          "the session flags must be applied on the tick and undone when the session ends")

    install = function_body(glue, "void InstallTransport()")
    check("if (g_transportInstalled)" in install and "SetDownedTransport(&g_pluginTransport);" in install,
          "InstallTransport must register the plugin transport exactly once")
    send = function_body(glue, "bool Send(std::string_view a_packet) override")
    check("!F4T::Networking::IsConnectedToServer()" in send and "SendPacketBytes(a_packet)" in send,
          "the plugin transport must refuse to send while disconnected")
    check(re.search(r"std::atomic<F4T::Downed::IDownedUiHost\*> g_uiHost", glue) is not None
          and re.search(r"std::atomic<F4T::Downed::IDownedTransport\*> g_transport", glue) is not None,
          "the UI host and transport pointers must be atomic")
    check(glue.count("g_uiHost.load()") >= 3, "every UI call must load the host pointer once into a local")

    glue_tick = function_body(glue, "void Tick()")
    check(in_order(glue_tick, "InstallTransport();", "InstallReviveInput();", "if (!F4T::Networking::IsConnectedToServer())",
                   "EndSession();", "ApplySessionFlags(true);", "ApplyPendingEvents(now);", "UpdateBleedoutRecovery(now);", "RecordLastBed();",
                   "HandleEngineLethalDamage(now);", "RetryPendingRespawn(now);", "ReapplyDownedPose(now);",
                   "if (!CanTouchWorld())", "UpdateRemotePoses(", "UpdateReviveInteraction(", "UpdateOverlay("),
          "Tick must install, end the session when disconnected, apply the events and only touch the world when allowed")
    check(in_order(glue_tick, "EndSession();", "if (CanTouchWorld() && g_connectionResetPending.exchange(false))", "ResetSession();",
                   "ApplySessionFlags(true);", "ApplyPendingEvents(now);")
          and re.search(r"if \(CanTouchWorld\(\) && g_connectionResetPending\.exchange\(false\)\) \{\s*ResetSession\(\);\s*\}", glue_tick) is not None,
          "Tick must reset the session on the first touchable frame after a welcome, before the queued events are applied")
    check(re.search(r"if \(g_state\.Tick\(now\)\) \{\s*EnterDeadPhase\(now\);\s*ShowRespawnMenu\(\);", glue_tick) is not None,
          "the downed timer must expire into the dead phase and the respawn menu")
    check(re.search(r"#if F4T_DOWNED|F4T_DOWNED", xmake + glue_raw + engine_raw) is None,
          "the downed module must not be compiled behind a feature switch")
    check('add_files("src/**.cpp|' in xmake, "the client targets must still glob the downed sources")
    for part in GLUE_PARTS:
        check(len((PLUGIN / part).read_text(encoding="utf-8").splitlines()) < 1000,
              f"{part} must stay under the 1000-line file limit")
    check("F4T::Downed::Tick();" in runtime, "the replication runtime must call Downed::Tick on the game thread")
    check("F4T::Downed::ResolveLocalPlayerDamage(" in remote_state,
          "ApplyDamageToLocalPlayer must route the network damage through the downed state machine")
    check("F4T::Downed::IsLocalPlayerDead()" in read_code("src/F4TPlayerVitals.cpp"),
          "the published isDead flag must come from the downed phase")

    flags = function_body(engine, "void ApplySessionFlags(")
    check("if (g_sessionFlagsApplied == a_apply)" in flags
          and in_order(flags, "if (!SetDownedFlags(*player, true))", "g_sessionFlagsApplied = a_apply;"),
          "ApplySessionFlags must be idempotent and record the applied state only after the flags are set")
    check("if (!SetDownedFlags(*player, true))" in flags and "SetPose(" not in flags.split("} else {")[0]
          and "boolFlags.set(" not in flags and "boolFlags.reset(" in flags,
          "ApplySessionFlags must set the essential and no-recovery flags without starting the bleedout action")
    check(in_order(flags, "if (a_apply) {", "RE::Actor::BOOL_FLAGS::kMovementBlocked,", "RE::Actor::BOOL_FLAGS::kAttackingDisabled);", "if (!SetDownedFlags(*player, true))")
          and in_order(flags, "} else {", "SetControlBlocked(*player, false);", "if (IsEssentialDown(*player))", "RequestBleedoutRecovery();", "UpdateBleedoutRecovery(NowSeconds());", "if (!SetDownedFlags(*player, false))", "g_bleedoutRecovery.Reset();"),
          "applying the session flags must clear the movement and attack block before setting the flags, and ending the session must hand an essential-down player to the recovery path before clearing the flags")
    check(re.search(r"\} else \{\s*if \(!CanTouchWorld\(\)\) \{\s*return;\s*\}", flags) is not None
          and in_order(flags, "if (!CanTouchWorld())", "if (!SetDownedFlags(*player, false))", "g_bleedoutRecovery.Reset();", "g_sessionFlagsApplied = a_apply;"),
          "ending the session must return before g_sessionFlagsApplied is recorded while the world cannot be touched, so the next offline frame retries the flag clear")
    essential_session_end = flags[flags.index("if (IsEssentialDown(*player)) {"):]
    check(in_order(flags, "if (IsEssentialDown(*player)) {", "if (!g_sessionEndGiveUpCleared) {", "ClearBleedoutGiveUp();", "g_sessionEndGiveUpCleared = true;", "RequestBleedoutRecovery();", "UpdateBleedoutRecovery(NowSeconds());")
          and in_order(essential_session_end, "UpdateBleedoutRecovery(NowSeconds());", "return;", "if (!SetPose(*player, false))", "if (!SetDownedFlags(*player, false))"),
          "ending the session must clear a latched bleedout give-up once per session end before it hands an essential-down player to the recovery, and must stop the pose before the downed flags are cleared, because ApplySessionFlags is the only route that releases them")
    release_path = flags[flags.index("if (!SetDownedFlags(*player, false))"):]
    check(in_order(flags, "if (a_apply) {", "g_sessionEndGiveUpCleared = false;", "player->boolFlags.reset(")
          and in_order(release_path, "if (!SetDownedFlags(*player, false))", "g_bleedoutRecovery.Reset();", "g_sessionEndGiveUpCleared = false;", "g_sessionFlagsApplied = a_apply;"),
          "the once-per-session latch must reset when the flags are applied and again after the release path, so every session end clears a give-up once")
    pose = function_body(engine, "bool SetPose(")
    check("(&a_actor == LocalPlayer() && !a_downed) ? true : SetDownedFlags(a_actor, a_downed)" in pose
          and "const bool stopSettled = !a_downed && !IsInBleedoutAnimation(a_actor);" in pose
          and "return (accepted || stopSettled) && flagsApplied;" in pose,
          "SetPose must keep the local player's flags through a stand-up and treat a refused stop on an actor that is not bleeding out as settled, which ends the proxy stop retry")
    set_flags = function_body(engine, "bool SetDownedFlags(")
    check(in_order(set_flags, "if (a_downed) {", '"SetEssential", true', '"SetNoBleedoutRecovery", true')
          and in_order(set_flags, "} else {", '"SetNoBleedoutRecovery", false', '"SetEssential", false'),
          "SetDownedFlags must set essential before no-recovery when downing, and clear no-recovery before essential when standing up")
    check(engine.count("SetDownedFlags(*player, false)") == 1,
          "only the session-end branch of ApplySessionFlags may clear the downed flags on the local player")
    recovery = function_body(engine, "void UpdateBleedoutRecovery(")
    check("g_bleedoutRecovery.Update(" in recovery and "RecoveryStep::kGaveUp" in recovery and "RecoveryStep::kStoodUp" in recovery,
          "UpdateBleedoutRecovery must drive the recovery through g_bleedoutRecovery.Update and act on its stood-up and gave-up steps")
    gave_up = recovery[recovery.find("case F4T::Downed::RecoveryStep::kGaveUp:"):]
    check(re.search(r"if \(IsEssentialDown\(\*player\)\) \{\s*ExitEssentialDown\(\*player\);\s*if \(IsEssentialDown\(\*player\)\) \{\s*player->Resurrect\(false, true\);\s*ReseatLocalPlayer\(\);\s*\}\s*RequestWeaponReset\(\);\s*\}", gave_up) is not None
          and in_order(gave_up, "ExitEssentialDown(*player);", "SetPose(*player, false);"),
          "giving up must first take the engine's own essential-down exit, resurrect and re-seat only a player it left essential-down, request the weapon reset, then stand it up")
    check(re.search(r"case F4T::Downed::RecoveryStep::kDispatched:\s*ZeroEssentialDownTimer\(\*player\);", recovery) is not None
          and "currentProcess->essentialDownTimer = 0.0F;" in function_body(engine, "void ZeroEssentialDownTimer("),
          "every recovery dispatch must zero the engine's essential-down timer so the engine leaves essential-down as soon as the no-recovery clear lands instead of waiting out the rest of the bleedout time")
    check("exitEssentialDown(&a_actor, false);" in function_body(engine, "void ExitEssentialDown(")
          and "kEssentialDown{ 513258, 2230033, 2230033 }" in engine_raw,
          "the forced exit must call the engine's essential-down routine with enter=false, not only Resurrect, which leaves the character controller in its bleedout state")
    check(in_order(recovery, "player->IsDead(true)", "CancelBleedoutRecovery();", "return;", "g_bleedoutRecovery.Update("),
          "UpdateBleedoutRecovery must cancel the recovery instead of posing an actor that is dying")
    check("IsInBleedoutAnimation(a_actor)" in function_body(engine, "bool IsEssentialDown("),
          "IsEssentialDown must also hold for a live actor still in the bleedout animation")
    check("g_bleedoutRecovery.Request()" in function_body(engine, "void RequestBleedoutRecovery("),
          "RequestBleedoutRecovery must forward to g_bleedoutRecovery.Request")
    check("g_bleedoutRecovery.Cancel(" in function_body(engine, "void CancelBleedoutRecovery("),
          "CancelBleedoutRecovery must forward to g_bleedoutRecovery.Cancel")
    reset_weapon = function_body(engine, "void ResetHeldWeaponState(")
    check(in_order(reset_weapon, "UnequipWeapon(a_actor, *held)", "g_reequip = PendingReequip", "EquipWeapon(a_actor, g_reequip->weapon)")
          and "DrawWeaponMagicHands" not in engine_raw,
          "the post-revive weapon fix must unequip and re-equip the right-hand weapon: a holster is a no-op on a weapon that is already sheathed and left the gun graph broken")
    weapon_log = function_body(engine, "void LogWeaponState(")
    check(in_order(weapon_log, "const bool graphManager =", "graphManager && holder->GetGraphVariableImplInt(")
          and weapon_log.count("GetGraphVariableImplInt(") == 2 and weapon_log.count("graphManager && holder->GetGraphVariableImplInt(") == 2,
          "the weapon state log must not read graph variables when the animation graph manager is missing")
    check(in_order(recovery, "ConsumeWeaponReset(", "ResetHeldWeaponState(*player);", "RetryWeaponReequip(*player, a_nowSeconds);")
          and in_order(function_body(engine, "void ClearPostRecoveryState("), "ForgetCamera();", "ForgetPendingReequip();"),
          "the re-equip must wait for the settled dwell, retry each tick, and ending the session must make the last equip attempt")
    check(re.search(r"if \(IsEssentialDown\(\*player\)\) \{\s*RequestBleedoutRecovery\(\);\s*RequestWeaponReset\(\);\s*\} else \{", leave) is not None,
          "an engine exit from essential-down never reaches the gave-up site, so LeaveIncapacitated must request the weapon reset together with the recovery")
    check(in_order(flags, "if (!SetDownedFlags(*player, false))", "g_bleedoutRecovery.Reset();", "g_sessionFlagsApplied = a_apply;"),
          "ending the session must drop a pending no-recovery resend along with the recovery, once the downed flags are cleared")
    reseat = function_body(engine, "void ReseatLocalPlayer(")
    check(in_order(reseat, "auto* player = LocalPlayer();", "if (!player || !CanTouchWorld())",
                   "player->SetPosition(player->GetPosition(), true);", 'LogInfo("local player re-seated in place.");')
          and "CaptureCheckpoint" not in reseat and "TeleportToCheckpoint" not in reseat,
          "ReseatLocalPlayer must re-seat the local player in place behind the CanTouchWorld guard and must not go through the checkpoint teleport")
    revive_apply = function_body(glue, "void ApplyRevivedEvent(")
    check(in_order(revive_apply, "player->Resurrect(false, true);", "ReseatLocalPlayer();", "LeaveIncapacitated(false);"),
          "a revived event that resurrects an engine-dead player must re-seat it before the pose is cleared")
    downed_layer = function_body(engine, "bool SetControlBlocked(")
    downed_events = ("kMovement", "kJumping", "kFighting", "kSneaking", "kLooking", "kActivate", "kMenu",
                     "kPOVSwitch", "kMainFour", "kWheelZoom", "kVATS", "kConsole")
    check("if (a_blocked && !g_downedInputLayer)" in downed_layer
          and all(f"USER_EVENT_FLAG::{flag}" in downed_layer for flag in downed_events)
          and "EnableUserEvent(layer->layerID, flag, false, RE::UserEvents::SENDER_ID::kScript)" in downed_layer,
          "the downed input layer must be allocated once and must disable movement, jumping, fighting, sneaking, looking, activate, menu, POV switch, main four, wheel zoom, VATS and console")
    check(in_order(downed_layer, "g_downedInputLayer.reset();", "ClearActivateHeld();"),
          "releasing the downed input layer must also clear the held Activate state, since the disabled Activate key-up may never reach the revive handler")
    check("ShowDownedOverlay(std::int32_t a_remainingMs, bool a_rescuerNearby, std::uint32_t a_stimpaks) = 0;" in ui_header
          and "void OnSelfReviveChosen();" in ui_header,
          "IDownedUiHost::ShowDownedOverlay must carry the Stimpak count and F4TDownedUi.h must declare OnSelfReviveChosen")
    self_revive = function_body(glue, "void UpdateSelfRevive(")
    check("g_selfReviveHold" not in glue_raw and "AdvanceReviveHold" not in self_revive and "IsActivateHeld" not in self_revive,
          "the self revive path must not use the Activate hold any more")
    check("OnSelfReviveChosen();" in (PLUGIN / "src/F4TDownedUiHost.cpp").read_text(encoding="utf-8")
          and "g_selfReviveRequested.store(true);" in function_body(glue, "void OnSelfReviveChosen("),
          "the Stimpak respawn choice must route to OnSelfReviveChosen, which only raises the request flag")
    check(in_order(glue_tick, "ReapplyDownedPose(now);", "UpdateSelfRevive(now);", "if (!CanTouchWorld())"),
          "UpdateSelfRevive must run between the pose reapply and the world guard in Tick")
    stale_release = function_body(engine, "void ReleaseStaleControlBlock(")
    check(in_order(stale_release, "if (!g_downedInputLayer || !CanTouchWorld())", "return;", "SetControlBlocked(*player, false);"),
          "a downed input layer that outlived the downed phase must be released only once the world can be touched")
    check("void ReleaseStaleControlBlock();" in (PLUGIN / "src/F4TDownedEngine.h").read_text(encoding="utf-8")
          and in_order(glue_tick, "ReapplyDownedPose(now);", "if (!g_state.IsIncapacitated()) {", "ReleaseStaleControlBlock();"),
          "the tick must retry the layer release while the local player is neither downed nor dead, never during bleedout or death")
    teleport = function_body(engine, "void TeleportForRespawn(")
    check('"coc {}", kSanctuaryCellEditorId' in teleport
          and 'runCommand(std::format("player.moveto {:08X}", proxy->GetFormID()));' in teleport
          and 'runCommand(std::format("coc {}", g_lastBedCellEditorId));' in teleport,
          "TeleportForRespawn must coc Sanctuary, moveto the friend proxy and coc the last-bed cell")
    check(in_order(teleport, "case RespawnPoint::kCheckpoint:", "case RespawnPoint::kFriend:", "case RespawnPoint::kLastBed:",
                   "case RespawnPoint::kSanctuary:")
          and teleport.count("runCommand(sanctuaryCommand);") == 4,
          "every respawn point, the checkpoint included, must fall back to the Sanctuary command")
    check(in_order(teleport, "TeleportToCheckpoint(*a_checkpoint)", "if (!g_lastBedCellEditorId.empty())",
                   "runCommand(sanctuaryCommand);"),
          "a checkpoint that cannot be restored must fall back to the last bed before Sanctuary")

    for target_name in ("CommonwealthOnline",):
        block = re.search(rf'target\("{target_name}"\)(.*?)(?=\ntarget\(|\Z)', xmake, re.S).group(1)
        check("remove_files" not in block or "F4TDownedGlue.cpp" not in block,
              f"{target_name} must compile the downed glue")
    for level_range in ("kMaximumPlayerLevel = 65535", "kMinimumPlayerLevel = 1"):
        check(level_range in vitals_header, f"F4TPlayerVitals.h must declare {level_range}")
        check(level_range in co_core_validation, f"co-core validation must declare {level_range}")

    revive_idle = function_body(engine, "bool PlayStimpakReviveIdle(")
    check(in_order(revive_idle, "const auto positionBefore = a_target.GetPosition();",
                   "a_reviver.PerformAction(action, &a_target);",
                   "const auto positionAfter = a_target.GetPosition();"),
          "PlayStimpakReviveIdle must sample the target's position on both sides of PerformAction")
    check("if (!played)" in revive_idle and "revive idle refused:" in revive_idle,
          "a refused revive idle must log its own line instead of only requested=false")
    check("bleedoutAnimBefore=" in revive_idle and "bleedoutAnimAfter=" in revive_idle
          and "kInBleedoutAnimation" in engine,
          "a refused revive idle must report the target's bleedout animation state before and after the call")
    check("drift=" in revive_idle and "positionBefore.x" in revive_idle and "positionAfter.x" in revive_idle,
          "a refused revive idle must report the target's post-action position and the drift across the call")

    check("F4T::Downed::TryHandlePacket(*packetType, packet)" in inbound_raw, "inbound dispatch must route downed packets through F4T::Downed::TryHandlePacket")
    welcome_handler = function_body(inbound_raw, "void HandleWelcomePacket(")
    check(in_order(welcome_handler, "if (!playerId)", "F4T::Downed::NoteWelcome();", "LogStatusWithLocalPlayerPrefix("),
          "HandleWelcomePacket must hand the downed module its connection reset after the playerId check")
    ui_host_raw = (PLUGIN / "src/F4TDownedUiHost.cpp").read_text(encoding="utf-8")
    main_raw = (PLUGIN / "src/main.cpp").read_text(encoding="utf-8")
    check("SetDownedUiHost(&g_host)" in ui_host_raw and "SetRespawnChosenCallback(OnUiRespawnChosen)" in ui_host_raw,
          "F4TDownedUiHost.cpp must register the Prisma downed UI host and the respawn callback")
    check("RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI20>()" in ui_host_raw
          and "g_uiChat->CloseChatWindow();" in ui_host_raw,
          "CloseChatForDownedPlayer must close the chat through the existing V20 interface, requested once in AttachDownedUi")
    check("F4T::Downed::AttachDownedUi();" in main_raw, "main.cpp must attach the downed UI on game data ready")

    proxy_vitals = read_code("src/F4TProxyVitals.cpp")
    proxy_controller = read_code("src/F4TProxyActorController.cpp")
    reconcile_body = function_body(proxy_vitals, "bool ReconcileHealth(")
    kill_body = function_body(proxy_vitals, "void KillProxy(")
    update_body = function_body(proxy_controller, "bool UpdateRuntimeSlotForRemotePlayer(")
    check("ScopedCoWrite coWrite;" in reconcile_body and "SetHealth(" in reconcile_body,
          "proxy health reconcile must write through SetHealth inside ScopedCoWrite")
    check("ComputeTargetHealth(" in reconcile_body,
          "proxy health target must come from ComputeTargetHealth so a remote proxy is never driven to zero health")
    check("ModActorValue" not in proxy_vitals and "RestoreActorValue" not in proxy_vitals,
          "proxy vitals must not call ModActorValue or RestoreActorValue directly")
    check("ScopedCoKill coKill;" in kill_body and "KillImpl(nullptr, 0.0F, false, false);" in kill_body,
          "proxy death must run KillImpl inside ScopedCoKill")
    check(in_order(update_body, "F4T::ProxyVitals::Apply(", "ApplyProxyAppearanceFromRemoteState(a_slot, *proxy, a_remotePlayer);"),
          "UpdateRuntimeSlotForRemotePlayer must apply proxy vitals before the appearance apply")
    apply_body = function_body(proxy_vitals, "void Apply(")
    proxy_state_header = read_code("include/F4TProxyVitals.h")
    check(in_order(apply_body, "std::addressof(a_proxy) == RE::PlayerCharacter::GetSingleton()", "CanTouchWorld()"),
          "ProxyVitals::Apply must return early for the local player before touching the proxy")
    check("double nextPoseRetrySeconds{ 0.0 };" in proxy_state_header and "constexpr double kPoseRetrySeconds{ 2.0 };" in proxy_vitals,
          "proxy pose retry must rate-limit on a 2 second timestamp kept in the proxy vitals state")
    check(in_order(apply_body, "now >= a_state.nextPoseRetrySeconds", "a_state.nextPoseRetrySeconds = now + kPoseRetrySeconds;",
                   "if (F4T::Downed::Engine::SetPose(a_proxy, a_vitals.isDowned))", "a_state.poseDowned = a_vitals.isDowned;"),
          "ProxyVitals::Apply must set poseDowned only after SetPose succeeds, behind the pose retry timestamp")
    check("double nextHealthReassertSeconds{ 0.0 };" in proxy_state_header and "constexpr double kHealthReassertSeconds{ 1.0 };" in proxy_vitals,
          "proxy health re-assert must rate-limit on a timestamp kept in the proxy vitals state")
    check(in_order(apply_body, "if (ReconcileHealth(a_proxy, a_vitals.healthPercent)) {", "a_state.appliedHealthPercent = a_vitals.healthPercent;"),
          "ProxyVitals::Apply must record the applied health percent only after ReconcileHealth succeeds")
    pending.append("proxy bleedout pose (SetPose on a remote proxy) is not runtime-verified; see downed-persist RT-DP-5b-1")
    pending.append("proxy kill and resurrect on a remote player's death are not runtime-verified on OG, NG or AE")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        for item in pending:
            print(f"PENDING: {item}")
        _merge_exit(1)

    for item in pending:
        print(f"PENDING: {item}")

    print("F4TDownedContract: all checks passed.")

def _section_003_f4tdownedstimpakcapabilitycontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TDownedStimpakCapabilityContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TDownedStimpakCapabilityContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    CAPABILITY = "downed-stimpak-count-v1"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    identity = (ROOT / "CO-BETA/plugin/src/F4TClientIdentity.cpp").read_text(encoding="utf-8")
    state_header = (ROOT / "co-core/include/F4TDownedState.h").read_text(encoding="utf-8")

    constant = re.search(r'StimpakCountCapability\s*\{\s*"([^"]+)"\s*\}', state_header)
    check(constant is not None, "co-core F4TDownedState.h must define StimpakCountCapability as a string literal")
    check(constant is None or constant.group(1) == CAPABILITY,
          f"co-core StimpakCountCapability must equal {CAPABILITY}")
    check(f'emplace_back("{CAPABILITY}")' in identity,
          f"F4TClientIdentity.cpp must advertise {CAPABILITY} in the hello capabilities")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TDownedStimpakCapabilityContract: all checks passed.")


def _section_005_f4tdropmirrorretrycontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TDropMirrorRetryContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TDropMirrorRetryContract.py")
    """Contract: the drop mirror's full retry queue keeps the drop queued, bounded by a retry count and no deadline."""
    from pathlib import Path

    plugin = Path(__file__).resolve().parents[1]
    source = (plugin / "src/F4TDropMirrorRefs.cpp").read_text(encoding="utf-8")

    assert "constexpr std::size_t kMaximumRetries = 256;" in source, "the retry lifetime must stay a 256-retry count"
    assert "if (retries < kMaximumRetries) {" in source, "a deferred drop must only retry below the count bound"
    assert "kRetryDeadline" not in source and "std::chrono" not in source, "the retry lifetime must not come back as a deadline"
    assert "the drop stays queued until a retry slot opens." in source, "a full retry queue must say the drop stays queued"
    assert "keeping drop until a slot opens" not in source, "the misleading retry-queue wording must stay gone"

    print("drop mirror retry contract OK")

def _section_006_f4tequipmentdebouncecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TEquipmentDebounceContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TEquipmentDebounceContract.py")
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

    assert equipment.count("\n") < 1000, "F4TProxyActorControllerEquipment.cpp must stay under 1000 lines"

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


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TDownedContract.py", _section_002_f4tdownedcontract),
    ("CO-BETA/plugin/tests/F4TDownedStimpakCapabilityContract.py", _section_003_f4tdownedstimpakcapabilitycontract),
    ("CO-BETA/plugin/tests/F4TDropMirrorRetryContract.py", _section_005_f4tdropmirrorretrycontract),
    ("CO-BETA/plugin/tests/F4TEquipmentDebounceContract.py", _section_006_f4tequipmentdebouncecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TNpcCombatContracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
