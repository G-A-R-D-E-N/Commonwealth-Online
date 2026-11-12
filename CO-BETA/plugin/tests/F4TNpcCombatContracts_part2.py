#!/usr/bin/env python3
"""F4TNpcCombatContracts_part2: consolidated contract sections.

Consolidated from 5 single-contract scripts so the group runs one process
instead of 5. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TNpcCombatContracts_part2.py
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


def _section_008_f4texplicitteleportcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TExplicitTeleportContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TExplicitTeleportContract.py")
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
        r"void TeleportForRespawn\([^{]*\{\n\t\tCancelPendingPlacement\(\);\n\t\tF4T::LocalPlayerMovement::RequestExplicitTeleport\(\);\n",
        engine,
    )

    print("F4TExplicitTeleportContract: all checks passed.")

def _section_009_f4tgraphoverridecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TGraphOverrideContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TGraphOverrideContract.py")
    """Contract: F4T animation-graph override (FathersFallout port, phase 1 review fixes); slot indices are concept-level pins against source:commonlibf4 headers, unverified at runtime."""
    import re
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]
    src = root / "CO-BETA/plugin/src"
    inc = root / "CO-BETA/plugin/include"


    def read(path):
        return path.read_text(encoding="utf-8")


    def block(text, start, end="\n\t}\n"):
        body = text[text.index(start):]
        return body[:body.index(end)]


    override_cpp = read(src / "F4TAnimationGraphOverride.cpp")
    override_h = read(inc / "F4TAnimationGraphOverride.h")
    main_cpp = read(src / "main.cpp")
    sync_cpp = read(src / "F4TProxyAnimationSync.cpp")
    save_safety = read(src / "F4TProxySaveSafety.cpp")
    diagnostics_cpp = read(src / "F4TProxyAnimationSyncDiagnostics.cpp")
    hit_guard = read(src / "F4TProxyAttackHitGuard.cpp")
    remote_action = read(src / "F4RemoteActionComponent.cpp")

    for path in (src / "F4TAnimationGraphOverride.cpp", inc / "F4TAnimationGraphOverride.h"):
        text = read(path)
        assert text.startswith("// License: GPL-3.0 with additional terms (FatherLamb)"), path.name
        assert len(text.splitlines()) <= 1000, path.name

    for name in ("F4TPlayerEventReplay", "F4TProxyProjectileAim"):
        assert not (src / f"{name}.cpp").exists() and not (inc / f"{name}.h").exists(), name
    for text in (main_cpp, sync_cpp, override_cpp, save_safety, diagnostics_cpp):
        assert "PlayerEventReplay" not in text and "ProxyProjectileAim" not in text

    assert "constexpr std::size_t kPreUpdateSlot = 0x16;" in override_cpp
    assert "constexpr std::size_t kNotifySlot = 0x01;" in override_cpp
    assert "kGetActivateTextSlot" not in override_cpp and "ActivateTextHook" not in override_cpp
    assert "InstallNoTalk" not in override_cpp + override_h + sync_cpp
    assert "VtableGuard::VerifySlot(" in override_cpp
    assert "SlotMatchesExpectedRva" not in override_cpp
    assert re.search(r"\*reinterpret_cast<std::uintptr_t\*>\(&vtbl\[[^\]]+\]\)\s*=", override_cpp) is None
    assert override_cpp.count("write_vfunc(") == 2
    assert override_cpp.index("VtableGuard::VerifySlot(preUpdateRequest)") < override_cpp.index("holderVtable.write_vfunc(kPreUpdateSlot")
    assert override_cpp.count("VtableGuard::WriteSucceeded(") == 2

    for name in (
        "DetachMirrorChannels", "SetMirrorSpeed", "ClearMirrorSpeed", "ReleaseMirrorSpeed", "KeepMirrorSpeeds",
        "SetWantDrawn", "HoldWantDrawn", "AllowWeaponSwap", "ProbeEvents", "GetStats", "ClearAll",
        "SetTrace", "PatchedClasses", "g_detached", "g_turnDetached", "g_npcSlots",
    ):
        assert name not in override_cpp and name not in override_h, name

    channel_name = block(override_cpp, "bool ChannelNameEquals(", "\n\t\t}\n")
    assert "std::memcpy(&raw, field, sizeof(raw));" in channel_name
    assert "if (!raw || !PooledEntryReadable(static_cast<const RE::BSStringPool::Entry*>(raw))) {" in channel_name
    assert channel_name.index("PooledEntryReadable(") < channel_name.index("c_str()")
    assert "reinterpret_cast<const RE::BSFixedString*>(field)->c_str()" in channel_name
    assert "_stricmp(name, a_name) == 0" in channel_name
    pooled = block(override_cpp, "bool PooledEntryReadable(", "\n\t\t}\n")
    assert "ReadableBytes(iter, sizeof(*iter))" in pooled and "iter->shallow()" in pooled and "iter = iter->_right;" in pooled

    detach = block(override_cpp, "void Detach(", "\n\t\t}\n")
    assert "std::vector" not in detach and "RawArray local{}" not in detach
    assert detach.count("MatchesAny(arr->data[i], a_names)") == 2
    assert detach.index("RE::BSAutoLock lock{ mgr->updateLock };") < detach.index("++removed;") < detach.index("if (removed == 0)") < detach.index("std::uint32_t kept = 0;") < detach.index("arr->size = kept;")

    claim = block(override_cpp, "void SetProxySpeed(", "\n\t}\n")
    assert "if (free->holder.load(std::memory_order_relaxed) != h) {" in claim
    assert claim.index("free->pitch.store(0.0F, std::memory_order_relaxed);") < claim.index("free->resync.store(false, std::memory_order_relaxed);") < claim.index("free->speed.store(")
    assert "DetachMovementChannels(RE::Actor* a_body)" in override_cpp
    assert "kDirectionDegrees, -F4T::ProxyMovementDirection::GraphDirectionToDegrees(dir));" in override_cpp

    assert "if (g_hookInstalled || g_installRefused || !a_anyBody) {" in override_cpp
    assert override_cpp.count("g_installRefused = true;") == 3

    assert "OursScope() { t_ours = true; }" in override_cpp
    assert "~OursScope() { t_ours = false; }" in override_cpp
    send = block(override_cpp, "bool SendGraphEvent(", "\n\t}\n")
    assert "const OursScope ours;" in send and "t_ours" not in send

    proxy_event = block(sync_cpp, "bool TrySendProxyGraphEvent(")
    assert "detail::TrySendGraphEvent(a_proxy, a_eventName.data())" in proxy_event
    assert "NotifyAnimationGraphImpl" not in proxy_event
    assert "F4T::AnimationGraphOverride::SendGraphEvent(&a_proxy, a_eventName)" in diagnostics_cpp
    assert "if (F4T::AnimationGraphOverride::Installed()) {\n\t\t\treturn F4T::AnimationGraphOverride::SendGraphEvent(&a_proxy, a_eventName);" in diagnostics_cpp

    clear_state = block(sync_cpp, "void ClearProxyAnimationState(")
    assert clear_state.index("F4T::AnimationGraphOverride::ClearProxy(&a_proxy);") < clear_state.index("ApplyDesiredStateToGraph(")
    retire = block(save_safety, "void RetireProxyActorForSave(")
    assert "F4T::AnimationGraphOverride::ClearProxy(actor);" in retire
    assert retire.index("F4T::AnimationGraphOverride::ClearProxy(actor);") < retire.index("DecideProxyRetire(")

    assert "F4T::AnimationGraphOverride::Install(&a_proxy)" in sync_cpp
    assert "F4T::AnimationGraphOverride::DetachMovementChannels(&a_proxy);" in sync_cpp
    assert "F4T::AnimationGraphOverride::SetProxySpeed(" in sync_cpp
    assert "F4T::AnimationGraphOverride::SetProxyPitch(" in sync_cpp
    assert "F4T::AnimationGraphOverride::TakeProxyResync(" in sync_cpp
    assert "SetBodyAim" not in sync_cpp and "InstallSpine" not in sync_cpp

    install_block = main_cpp[main_cpp.index("F4T::ProxyAttackHitGuard::Install();"):]
    assert install_block.index("F4T::ProxyAttackHitGuard::Install();") < install_block.index("F4T::WorkshopCommitHooks::Install();")

    weapon_hook = hit_guard[hit_guard.index("bool WeaponFireHandlerHook("):hit_guard.index("void Install()")]
    assert "return true;" in weapon_hook
    assert "(live projectile, aim redirected)" not in remote_action

    print("F4TGraphOverrideContract: OK")

def _section_010_f4tinventorysnapshotcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TInventorySnapshotContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TInventorySnapshotContract.py")
    """Contract: the owned-inventory snapshot sender and its inbound result route (#704).
    Verified record source: the in-repo precedents CO-BETA/plugin/src/F4TInventorySync.cpp,
    CO-BETA/plugin/src/F4TNetworkingInventory.cpp and CO-BETA/plugin/src/F4TNetworkingInbound.cpp."""
    from pathlib import Path


    plugin = Path(__file__).resolve().parents[1]
    sync_header = (plugin / "include/F4TInventorySync.h").read_text(encoding="utf-8")
    sync = (plugin / "src/F4TInventorySync.cpp").read_text(encoding="utf-8")
    read_header = (plugin / "include/F4TInventorySnapshotRead.h").read_text(encoding="utf-8")
    read = (plugin / "src/F4TInventorySnapshotRead.cpp").read_text(encoding="utf-8")
    net_header = (plugin / "include/F4TNetworking.h").read_text(encoding="utf-8")
    net = (plugin / "src/F4TNetworkingInventory.cpp").read_text(encoding="utf-8")
    inbound = (plugin / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
    gate_header = (plugin / "include/F4TInventoryDropGate.h").read_text(encoding="utf-8")
    gate = (plugin / "src/F4TInventoryDropGate.cpp").read_text(encoding="utf-8")

    for label, text in (("F4TInventorySync.cpp", sync), ("F4TInventorySnapshotRead.cpp", read)):
        assert ".inc" not in text, f"{label} must not pull in an .inc file"

    assert "void NoteSnapshotResult(const nlohmann::json& a_result);" in sync_header, "result hook missing from sync header"
    assert "bool SendInventorySnapshotRequest(const nlohmann::json& a_request, std::uint64_t a_generation);" in net_header, "snapshot send declaration missing"
    assert 'SendInventoryRequest(a_request, "inventorySnapshotRequest", a_generation)' in net, "snapshot must send through the typed sender"
    assert "SendPacketBytesForGeneration(a_request.dump(), a_generation)" in net, "owned requests must be tied to their connection"
    assert '"inventorySnapshotResult"' in inbound and "NoteSnapshotResult(packet)" in inbound, "result must be routed to the sync"

    assert "std::optional<nlohmann::json> ReadLocalItems();" in read_header, "reader declaration missing"
    assert "RE::PlayerCharacter::GetSingleton()" in read and "inventoryList" in read and "rwLock" in read, \
        "reader must read the locked player inventory"
    assert "GetInventoryObjectCount(object)" in read and "TranslateItemKey(object)" in read, "reader must count and key each form"
    assert "count > 65535" not in read, "reader must not cap counts at the drop limit"
    assert "NormalizeSnapshotItems(nlohmann::json::array({ entry }))" in read and "LogThrottledWarning" in read, \
        "reader must filter each entry through the server rules and warn"

    assert "TranslateItemKey(const RE::TESForm* a_form);" in gate_header, "key translation must be exposed"
    assert "std::optional<std::string> TranslateItemKey(const RE::TESForm* a_form)" in gate, \
        "exposed translation must be defined outside the anonymous namespace"

    send = sync[sync.index("void SendSnapshotIfDue("):sync.index("bool DrainInbound(")]
    assert "PendingCount() != 0" in send, "snapshots must wait for no owned operation in flight"
    assert "g_snapshotCommittedRevision == g_pending->ExpectedRevision()" in send, \
        "a matching old snapshot must not suppress reconciliation after the ledger revision changes"
    assert "g_snapshotInFlight" in send and "ExpectedRevision()" in send, "snapshot must carry the base revision and stay single-flight"
    assert "SendInventorySnapshotRequest(request, generation)" in send and "kMaximumPartItems" in send, "snapshot must be sent in parts"
    assert "NormalizeSnapshotItems(*items)" in send, "the whole snapshot must pass the server snapshot rules"
    assert "generation != F4T::Networking::GetConnectionGeneration()" in send, "a snapshot must not cross a reconnect while reading"
    assert "kSnapshotIntervalMs = 2000" in sync, "snapshot cadence must be throttled"
    assert "kSnapshotReplyTimeoutMs = 10000" in sync and "sentMs" in sync, "an unanswered snapshot must clear after a timeout"

    tick = sync[sync.index("void Tick("):sync.index("void Reset()")]
    assert "SendSnapshotIfDue(a_nowMs);" in tick, "tick must drive the snapshot"
    assert "!tradeProcessed && !F4T::TradeClient::HasPendingCommits()" in tick, \
        "snapshots must wait until committed trades have been applied locally"

    result = sync[sync.index("void NoteSnapshotResult("):sync.index("void NoteOwnedResult(")]
    assert "SetExpectedRevision(revision)" in result and "revision > g_pending->ExpectedRevision()" in result, \
        "a result may only raise the expected revision"
    assert "g_serverInventory = std::move(view);" in result, "a committed result must update the view"
    assert "g_snapshotCommitted = inFlight.items;" in result, "only committed snapshots may become the diff baseline"
    assert "g_snapshotCommittedRevision = revision;" in result, "the baseline must keep its committed revision"

    grant_header = (plugin / "include/F4TInventoryGrant.h").read_text(encoding="utf-8")
    grant = (plugin / "src/F4TInventoryGrant.cpp").read_text(encoding="utf-8")
    assert ".inc" not in grant, "F4TInventoryGrant.cpp must not pull in an .inc file"
    assert "void NoteStartingGrant(const nlohmann::json& a_grant);" in grant_header, "grant hook missing from header"
    assert '"startingInventoryGrant"' in inbound and "NoteStartingGrant(packet)" in inbound, "grant must be routed to the grant handler"
    assert '"startingInventoryRecovery"' in inbound and '"startingInventoryVerification"' in inbound, "recovery packets must be routed"
    assert '"startingInventoryBegin"' in grant and '"startingInventoryVerify"' in grant, "grant must use the recovery handshake"

    launch_gate = grant[grant.index("bool CheckLaunchSave("):grant.index("void OnPreLoad(")]
    assert 'recovery->state != "resolved"' in launch_gate, "consumed approvals must not admit another launch"
    recovery_tick = grant[grant.index("void Tick("):grant.index("void NoteStartingGrant(")]
    assert 'g_recovery->state != "unstarted" && g_recovery->state != "resolved"' in recovery_tick, \
        "consumed approvals must not send a verification request"
    assert 'value != "consumed"' in grant, "the consumed recovery state must be recognized"
    assembly = grant[grant.index("void NoteStartingGrant("):]
    assert "g_assembly->nextPart != index" in assembly and "NormalizeItems(complete->items)" in assembly, \
        "grant parts must be assembled in order and validated with the snapshot item rules"
    assert "F4SE::GetTaskInterface()" in assembly and "AddTask(" in assembly, "grant must be applied on the game thread"

    apply = grant[grant.index("void ApplyOnGameThread("):grant.index("void NoteStartingGrant(")]
    assert 'g_recovery->state != "applying"' in apply and 'g_recovery->state = "attempted"' in apply, "a grant attempt must be consumed once"
    assert "GetConnectionGeneration() != a_generation" in apply and "g_assembly->generation != generation" in assembly, "queued grants must be tied to the originating connection"
    assert "SendStartingInventoryGrantAck" not in apply, "item insertion must not acknowledge durable delivery"
    assert "GetSingleton()" in apply and "AddItems(*player, a_items)" in apply, "grant must target the local player"
    assert "GetParentCell()" in apply and "IsLoadingScreenActive()" in apply and "g_loadEpoch != a_loadEpoch" in apply, \
        "grant must require the same loaded world"

    items = grant[grant.index("bool AddItems("):grant.index("void ApplyOnGameThread(")]
    assert "InventoryGrantDelivery::Apply(a_items, ResolveItem" in items, "grant must use the all-forms-first delivery helper"
    assert "AddObjectToContainer(" in items and "ITEM_REMOVE_REASON::kNone" in items, "grant must add through the container call"
    assert "(std::numeric_limits<std::int32_t>::max)()" in items and "remaining -= chunk" in items, \
        "grant counts must be split to the int32 limit"
    assert "TranslateItemKey(object) != a_key" in grant, "a resolved form must translate back to its wire key"

    resolve = grant[grant.index("RE::TESBoundObject* ResolveItem("):grant.index("bool CheckLaunchSave(")]
    assert "LookupLoadedModByName(parts->plugin)" in resolve and "LookupLoadedLightModByName(parts->plugin)" in resolve, \
        "items must resolve through the loaded plugin tables"
    assert "0xFE000000" in resolve and "parts->localId > 0xFFF" in resolve, "light plugin ids must use the small-file encoding"

    print("inventory snapshot contract OK")

def _section_011_f4tlocalplayertransformsendcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TLocalPlayerTransformSendContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TLocalPlayerTransformSendContract.py")
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
    assert "BypassFloorElapsed(currentTime - lastSendTime)" in publish
    assert "RequestExplicitTeleport();" in publish
    assert "kBypassSendFloor = std::chrono::milliseconds{ 33 }" in header
    assert "inline bool BypassFloorElapsed(" in header
    assert "bypassFloorElapsed && (isSpecialMovement" in publish
    assert "hasPendingActionEvents" in publish.split("bypassesThrottle", 1)[1].split("};", 1)[0]
    assert "BypassFloorElapsed(33ms)" in tests or "BypassFloorElapsed(33ms));" in tests
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

def _section_012_f4tnearbyplayerperformancecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TNearbyPlayerPerformanceContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TNearbyPlayerPerformanceContract.py")
    from pathlib import Path
    import re

    plugin = Path(__file__).resolve().parents[1]
    puppets = (plugin / "src/F4TNpcSyncPuppets.cpp").read_text(encoding="utf-8")
    update = puppets.split("void UpdatePuppets(RE::PlayerCharacter& a_player)", 1)[1]
    eligible = update.index("if (!IsStateIdentityUsable(state) || !IsStateInLocalLocation(state, a_player))")
    index = update.index("loadedActors = BuildLoadedActorIndex();")
    assert eligible < index < update.index("HasLocalFollowerMatch(state, a_player, loadedActors)")
    assert "if (!loadedActorsReady)" in update[eligible:index]
    assert update.index("SuppressDuplicateLocalNpcs(states, a_player, loadedActors)") > index
    assert update.index("UpdatePendingSpawn(states)") > index
    assert "kMissingNpcGrace" in update

    visibility = (plugin / "src/F4TProxyActorControllerVisibility.cpp").read_text(encoding="utf-8")
    retention = visibility.split("bool ShouldRetainProxyInPriorLocalCell(", 1)[1].split("std::uint32_t GetProxyVisibilityUpdate3DFlags()", 1)[0]
    assert "GetRemotePlayer(a_playerId)" in retention
    assert "GetRemotePlayerSnapshot()" not in retention
    assert "HasValidRemoteTransform(*remotePlayer)" in retention
    assert "IsRemotePlayerInSameLocation(*remotePlayer, a_player, a_currentCell)" in retention

    remote = (plugin / "src/F4TRemotePlayerState.cpp").read_text(encoding="utf-8")
    lookup = remote.split("std::optional<RemotePlayerState> GetRemotePlayer(", 1)[1].split("void GetRemotePlayerVoiceRoster(", 1)[0]
    assert "std::scoped_lock lock(g_stateMutex)" in lookup
    assert "g_remotePlayers.find(a_playerId)" in lookup
    assert "std::nullopt" in lookup

    placement = (plugin / "src/F4TProxyActorControllerSlotPlacement.cpp").read_text(encoding="utf-8")
    promotion = placement.split("void PromoteSlotPlaceAtMeProxy(", 1)[1]
    assert "GetRemotePlayer(a_slot.remotePlayerId)" in promotion
    assert "GetRemotePlayerSnapshot()" not in promotion
    assert "HasValidRemoteTransform(*remotePlayer)" in promotion
    assert "SetSlotInitialRemoteSpawnTransform(a_slot, *remotePlayer)" in promotion

    header = (plugin / "include/F4TRemotePlayerState.h").read_text(encoding="utf-8")
    projection = header.split("inline RemotePlayerState AnimationSnapshot(", 1)[1].split("struct RemotePlayerVoiceInfo", 1)[0]
    copied = set(re.findall(r"result\.(\w+) = a_state\.\1;", projection))
    assert {"playerId", "angleZ", "isMoving", "isSprinting", "isSneaking", "isJumping", "isCrouching", "weaponDrawn", "movementSpeed", "animationGraphSpeed", "animationDirection", "hasAnimationDirection", "aimPitch", "turnDelta", "hasAimState"} <= copied
    animation_files = ("F4TProxyAnimationSync.cpp", "F4TProxyAnimationSyncState.cpp", "F4TProxyAnimationSyncDiagnostics.cpp")
    for name in animation_files:
        source = (plugin / "src" / name).read_text(encoding="utf-8")
        assert set(re.findall(r"a_remotePlayer\.(\w+)", source)) <= copied, name
    for name in ("F4TProxyActorControllerMovement.cpp", "F4TProxyActorControllerSlotLifecycle.cpp"):
        source = (plugin / "src" / name).read_text(encoding="utf-8")
        assert "lastRemotePlayerSnapshot = a_remotePlayer" not in source
        assert "lastRemotePlayerSnapshot = F4T::RemotePlayerState::AnimationSnapshot(a_remotePlayer)" in source

    controller = (plugin / "src/F4TProxyActorController.cpp").read_text(encoding="utf-8")
    update = controller.split("const auto remotePlayers = GetSortedRemotePlayerSnapshot();", 1)[1].split("ApplySmoothFrameMovement();", 1)[0]
    assert "const RemotePlayer* fallbackCandidate = nullptr" in update
    assert "fallbackCandidate = std::addressof(remotePlayer);" in update
    assert update.index("for (const auto& remotePlayer : remotePlayers)") < update.index("fallbackCandidate = std::addressof(remotePlayer);") < update.index("TryUpdatePlacedFallbackForRemotePlayer(")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TExplicitTeleportContract.py", _section_008_f4texplicitteleportcontract),
    ("CO-BETA/plugin/tests/F4TGraphOverrideContract.py", _section_009_f4tgraphoverridecontract),
    ("CO-BETA/plugin/tests/F4TInventorySnapshotContract.py", _section_010_f4tinventorysnapshotcontract),
    ("CO-BETA/plugin/tests/F4TLocalPlayerTransformSendContract.py", _section_011_f4tlocalplayertransformsendcontract),
    ("CO-BETA/plugin/tests/F4TNearbyPlayerPerformanceContract.py", _section_012_f4tnearbyplayerperformancecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TNpcCombatContracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
