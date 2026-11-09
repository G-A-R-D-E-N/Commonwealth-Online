#!/usr/bin/env python3
"""F4TProxyConsoleContracts_part2: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TProxyConsoleContracts_part2.py
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




def _section_011_f4tproxysavesafetycontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TProxySaveSafetyContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TProxySaveSafetyContract.py")
    from pathlib import Path
    import re


    root = Path(__file__).resolve().parents[3]
    plugin = root / "CO-BETA/plugin"
    header = (plugin / "include/F4TProxySaveSafety.h").read_text(encoding="utf-8")
    safety = (plugin / "src/F4TProxySaveSafety.cpp").read_text(encoding="utf-8")
    main = (plugin / "src/main.cpp").read_text(encoding="utf-8")
    standins = (plugin / "src/F4TProxySaveSafetyStandIns.cpp").read_text(encoding="utf-8")
    controller = (plugin / "src/F4TProxyActorController.cpp").read_text(encoding="utf-8")
    tests = (plugin / "tests/F4TProxySaveSafetyTests.cpp").read_text(encoding="utf-8")

    placement = (plugin / "src/F4TProxyActorControllerSlotPlacement.cpp").read_text(encoding="utf-8")
    spawn = (plugin / "src/F4TProxyActorControllerSpawn.cpp").read_text(encoding="utf-8")
    assert "F4T::ProxyTransformFreshness::IsFresh(a_remotePlayer.lastReceivedLocalTime" in placement
    proxy_loop = controller.split("for (const auto& remotePlayer : remotePlayers)", 1)[1].split("HoldDisconnectedSlots(", 1)[0]
    assert proxy_loop.index("seenRemotePlayerIds.insert(remotePlayer.playerId)") < proxy_loop.index("if (!HasValidRemoteTransform(remotePlayer))")
    assert "MarkSlotHeld(*slot, *proxy, ProxyLifecycleState::kHeldNoRemote)" in proxy_loop
    assert proxy_loop.index("if (!sameCellPassed)") < proxy_loop.index("validRemotePlayerIds.insert(remotePlayer.playerId)")
    assert proxy_loop.index("validRemotePlayerIds.insert(remotePlayer.playerId)") < proxy_loop.index("UpdateRuntimeSlotForRemotePlayer(")
    assert "HoldDisconnectedSlots(seenRemotePlayerIds, *parentCell, *player)" in controller
    assert "HoldFallbackIfRepresentedPlayerDisconnected(validRemotePlayerIds, *parentCell, *player)" in controller

    assert re.search(r"void RunDeferredSaveTeardown\(\)\s*\{[\s\S]*?if \(!gate\.TryBeginTeardown\(\)\) \{\s*g_pendingSaveTeardown\.store\(true, std::memory_order_release\);\s*return;\s*\}[\s\S]*?RunSaveTeardown\(\);", safety), "deferred teardown must wait while a save holds the gate"
    assert safety.count("RunDeferredSaveTeardown();") == 3, "queued and fallback teardown must use the gate"
    assert re.search(r"void RunDeferredSaveTeardown\(\)[\s\S]*?RunSaveTeardown\(\);\s*if \(gate\.HasFrames\(\)\) \{\s*g_pendingSaveTeardown\.store\(true, std::memory_order_release\);", safety), "incomplete frame cleanup must remain queued"

    assert "void PrepareForSave();" in header
    assert "void DestroyProxyActor(RE::TESObjectREFR& a_ref);" in header
    assert "bool IsProxyTickBlocked();" in header

    assert re.search(
        r"case F4SE::MessagingInterface::kPreSaveGame:\s*"
        r"(?:F4T::WorldPolicyEnforcer::ReleaseForSave\(\);\s*)?"
        r"F4T::ProxySaveSafety::PrepareForSave\(\);",
        main,
    ), "main.cpp must run the proxy teardown on kPreSaveGame"
    assert re.search(
        r"case F4SE::MessagingInterface::kPostLoadGame:[\s\S]{0,1200}?"
        r"F4T::ProxySaveSafety::AfterLoadSweep\(loadSucceeded, !loadSucceeded\);",
        main,
    ), "main.cpp must sweep saved proxies after a load"
    assert re.search(
        r"AddTask\(\[loadSucceeded, survivalLoad\]\(\) \{(?:(?!\}\);)[\s\S])*?(?:[\w:]+::)?ReapplyAfterSave\(\);(?:(?!\}\);)[\s\S])*?(?:[\w:]+::)?AfterLoadSweep\(loadSucceeded, !loadSucceeded\);(?:(?!\}\);)[\s\S])*?(?:[\w:]+::)?CompleteLoad\(survivalLoad, loadSucceeded\);(?:(?!\}\);)[\s\S])*?(?:[\w:]+::)?StartPlayerPositionPolling\(\);",
        main,
    ), "the kPostLoadGame AddTask lambda must run ReapplyAfterSave, then AfterLoadSweep(loadSucceeded), then StartPlayerPositionPolling"

    assert re.search(
        r"void Update\(\)\s*\{\s*"
        r"F4T::ProxySaveSafety::Tick\(\);\s*"
        r"if \(F4T::ProxySaveSafety::IsProxyTickBlocked\(\)\) \{\s*return;\s*\}",
        controller,
    ), "Update must tick the save safety first and return early when the tick gate blocks"

    assert re.search(
        r"bool IsPlacedFallbackProxyActor\(const RE::Actor& a_actor\) noexcept\s*\{\s*"
        r"return F4T::ProxySaveSafety::IsPlacedFallbackProxy\(\s*IsCkPlacedProxyReference\([^)]*\),\s*a_actor\.IsCreated\(\)\);",
        safety,
    ), "a created proxy whose local form id matches the fallback must not be held as the CK-placed fallback"
    assert re.search(
        r"inline constexpr bool IsPlacedFallbackProxy\(\s*bool a_isCkPlacedProxyReference,\s*bool a_isCreated\)",
        header,
    ), "the created check must live in the pure predicate"
    assert "TestCreatedProxyWithTheFallbackLocalIdIsNotAPlacedFallback();" in tests

    assert re.search(
        r"const auto isCustomProxy = IsCustomPlayerProxyActor\(\*actor\);\s*"
        r"const auto isDeleted = actor->IsDeleted\(\);\s*"
        r"const auto isOwnedByLiveSlot = IsTrackedByLiveSlot\(\*actor\);\s*"
        r"const auto isPlacedFallback = IsPlacedFallbackProxyActor\(\*actor\);\s*"
        r"const auto isCreatedFallbackBase = IsCreatedFallbackBaseProxyActor\(\*actor, a_standInsAllowed\);\s*"
        r"const auto isRecorded = F4T::ProxySaveSafety::IsStandIn\(actor->GetFormID\(\)\);\s*"
        r"const auto decision = F4T::ProxySaveSafety::DecideSavedProxySweep\(\s*"
        r"isCustomProxy,\s*isDeleted,\s*isOwnedByLiveSlot,\s*isPlacedFallback,\s*isCreatedFallbackBase\);",
        safety,
    ), "the saved-proxy sweep must also delete created actors on the Codsworth fallback base"
    assert "TestSavedProxySweepDeletesCreatedFallbackBaseActors();" in tests
    assert "TestCreatedFallbackBaseProxyNeedsCreatedNonPuppetBase();" in tests
    assert "inline constexpr bool IsCreatedFallbackBaseProxy(" in header

    assert re.search(
        r"bool IsCreatedFallbackBaseProxyActor\(const RE::Actor& a_actor, bool a_standInsAllowed\)\s*\{\s*"
        r"const auto\* baseActor = a_actor\.GetNPC\(\);\s*"
        r"return F4T::ProxySaveSafety::IsCreatedFallbackBaseProxy\(\s*"
        r"a_actor\.IsCreated\(\),\s*"
        r"baseActor && baseActor->GetFormID\(\) == kDebugCodsworthBaseFormId,\s*"
        r"F4T::NpcSync::Detail::IsPuppetActor\(a_actor\.GetFormID\(\)\) \|\|\s*"
        r"F4T::NpcSync::Detail::IsRecordedCreatedPuppet\(a_actor\.GetFormID\(\)\),\s*"
        r"a_standInsAllowed && (?:F4T::ProxySaveSafety::)?IsStandIn\(a_actor\.GetFormID\(\)\)\);",
        safety,
    ), "the created fallback-base helper must pass IsCreated(), the CO stand-in list and the NPC-sync puppet check into the pure predicate"

    slot_lifecycle = (plugin / "src/F4TProxyActorControllerSlotLifecycle.cpp").read_text(encoding="utf-8")
    assert re.search(
        r"if \(F4T::ProxySaveSafety::IsPlacedFallbackProxy\(\s*"
        r"IsCkPlacedProxyReference\(\*proxy, \*proxy, std::nullopt\),\s*"
        r"proxy->IsCreated\(\)\)\) \{[\s\S]*?\} else \{\s*"
        r"F4T::DebugLogging::Log[\s\S]*?"
        r"F4T::ProxySaveSafety::DestroyProxyActor\(\*proxy\);",
        slot_lifecycle,
    ), "ReleaseAllSlotProxiesInOldCell must hold only the CK-placed, non-created fallback and destroy every other proxy"

    assert re.search(
        r"void RunSaveTeardown\(\)\s*\{[\s\S]{0,600}?"
        r"CollectUnresolvedProxyCandidates\(unresolved\);[\s\S]{0,200}?"
        r"SweepCandidatePage\(unresolved, false\);",
        safety,
    ), "RunSaveTeardown must retry the unresolved proxy handles before the save is written"

    assert re.search(
        r"void AfterLoadSweep\(bool a_loadSucceeded, bool a_clearStandIns\)\s*\{\s*"
        r"DropPrivateProxyBasesWithoutFreeing\(\);\s*"
        r"g_unresolvedProxyHandles\.clear\(\);\s*"
        r"ClearProxySessionState\(\);",
        safety,
    ), "AfterLoadSweep must drop the pre-load handles instead of keeping them as unresolved"

    assert re.search(
        r"if \(generation != g_lastObservedGeneration && !IsLoadingMenuOpen\(\)\) \{",
        safety,
    ), "the generation-change teardown must be deferred while the loading menu is open"

    release_sites = sorted((plugin / "src").glob("*.cpp"))
    for path in release_sites:
        if path.name.startswith("F4TProxyActorController"):
            assert "MarkAsDeleted" not in path.read_text(encoding="utf-8"), (
                f"{path} deletes a proxy outside DestroyProxyActor"
            )

    npc_puppets = (plugin / "src/F4TNpcSyncPuppets.cpp").read_text(encoding="utf-8")
    assert npc_puppets.count("MarkAsDeleted") == 1, (
        "the NPC puppet path owns the only other release primitive, DeletePuppet"
    )
    assert re.search(
        r"bool IsRecordedCreatedPuppet\(std::uint32_t a_formId\)\s*\{\s*"
        r"return g_createdPuppetBases\.contains\(a_formId\);\s*\}",
        npc_puppets,
    ), "the sweep must also exclude puppets recorded at spawn, before they are registered"

    assert safety.count("a_ref.MarkAsDeleted();") == 1
    assert re.search(
        r"void DestroyProxyActor\(RE::TESObjectREFR& a_ref\)\s*\{\s*"
        r"if \(const auto\* actor = a_ref\.As<RE::Actor>\(\); actor && F4T::PowerArmorSync::IsRetiredFrameOwner\(\*actor\)\) \{\s*return;\s*\}\s*"
        r"(?:F4T::CrashBreadcrumbs::Record\([^;]*\);\s*)?"
        r"DisableAndDeleteRef\(a_ref\);\s*\}",
        safety,
    ), "DestroyProxyActor must preserve owners of retained frames"
    assert safety.count("F4T::ProxySaveSafety::DecideProxyRetire(") == 1, (
        "only the save teardown may apply the retire decision"
    )

    assert re.search(
        r"void RetireProxyActorForSave\(RE::TESObjectREFR& a_ref, std::size_t& a_removed\)\s*\{[\s\S]{0,400}?"
        r"F4T::ProxySaveSafety::DecideProxyRetire\(\s*"
        r"actor && IsPlacedFallbackProxyActor\(\*actor\),\s*"
        r"actor && IsCustomPlayerProxyActor\(\*actor\),\s*"
        r"IsRuntimeProxyRef\(a_ref\)\)",
        safety,
    ), "RetireProxyActorForSave must pass slot and loose-handle ownership to the retire guard"
    assert re.search(
        r"inline constexpr ProxyRetireAction DecideProxyRetire\(",
        header,
    ), "the retire guard must live in the pure predicate"
    assert re.search(
        r"bool IsRuntimeProxyRef\(const RE::TESObjectREFR& a_ref\)\s*\{\s*"
        r"const auto\* actor = a_ref\.As<RE::Actor>\(\);\s*"
        r"if \(actor && F4T::PowerArmorSync::IsRetiredFrameOwner\(\*actor\)\) \{\s*return true;\s*\}",
        spawn,
    ), "retained frame owners must remain runtime proxies for candidate filtering"
    assert re.search(
        r"void RetireProxyActorForSave\(RE::TESObjectREFR& a_ref, std::size_t& a_removed\)\s*\{[\s\S]*?"
        r"if \(actor && F4T::PowerArmorSync::IsRetiredFrameOwner\(\*actor\)\) \{\s*return;\s*\}\s*"
        r"if \(actor\) \{\s*F4T::AnimationGraphOverride::ClearProxy\(actor\);",
        safety,
    ), "save retirement must preserve retained frame owners before mutating them"
    assert "IsProxyOwnedByRuntime" not in header
    assert "IsProxyOwnedByRuntime" not in tests
    assert "TestProxyRetireOnlyTouchesCustomProxyActors();" in tests
    assert "TestDynamicPoolProxiesAreOwnedByTheRuntime" not in tests

    assert re.search(
        r"const auto resolvedOwner = frame\.ownerHandle\.get\(\);\s*"
        r"if \(!resolvedOwner \|\| resolvedOwner\.get\(\) != frame\.owner\.get\(\)\) \{\s*continue;\s*\}"
        r"\s*auto\* owner = frame\.owner\.get\(\);\s*if \(owner && !owner->IsDeleted\(\)\)",
        safety[safety.index("std::size_t RemoveProxyActorsForSave()"):safety.index("void ClearProxySessionState()")],
    ), "save cleanup must validate the retained owner before dereferencing it"

    assert re.search(
        r"std::size_t RemoveProxyActorsForSave\(\)[\s\S]*?"
        r"if \(F4T::PowerArmorSync::GetFrameSaveGate\(\)\.HasFrames\(\)\) \{\s*return removed;\s*\}\s*"
        r"for \(auto& \[playerId, slot\] : g_proxySlots\)[\s\S]*?"
        r"RetireProxyActorForSave\(\*liveProxy, removed\);",
        safety,
    ), "RemoveProxyActorsForSave must retire the slot handles directly"
    for path in sorted((plugin / "src").glob("*.cpp")) + sorted((plugin / "include").glob("*.h")):
        text = path.read_text(encoding="utf-8")
        assert "g_dynamicProxyPool" not in text and "IsDynamicPoolActor" not in text, (
            f"{path.name} must not reference the removed dynamic proxy pool"
        )

    assert re.search(
        r"const bool loadInProgress = saveLoad && saveLoad->queuedEntryToLoad != nullptr;\s*"
        r"if \(!g_saveLoadThreadWasBusy && IsSaveStartFallback\(saveLoadThreadBusy, IsLoadingMenuOpen\(\), loadInProgress\)\)",
        safety,
    ), "the save-start fallback must skip a queued load"
    assert re.search(
        r"g_lastObservedGeneration = F4T::ReplicationRuntime::CurrentGeneration\(\);\s*"
        r"\s*F4T::DebugLogging::Log\(",
        safety,
    ), "AfterLoadSweep must record the generation it already swept so Tick does not repeat it"

    for path in release_sites:
        assert path.read_text(encoding="utf-8").count("void ResetSlotForRespawn(ProxyActorSlot& a_slot)") == (
            1 if path.name == "F4TProxyActorControllerSlotResolution.cpp" else 0
        ), f"{path.name} must use the shared ResetSlotForRespawn, not a private copy"
    assert safety.count("ResetSlotForRespawn(slot);") == 1
    assert controller.count("ResetSlotForRespawn(a_slot);") == 1

    callers = {
        path.name: len(re.findall(r"F4T::ProxySaveSafety::DestroyProxyActor\(", path.read_text(encoding="utf-8")))
        for path in release_sites
    }
    callers = {name: count for name, count in callers.items() if count}
    assert callers == {
        "F4TProxyActorController.cpp": 2,
        "F4TProxyActorControllerSlotLifecycle.cpp": 2,
        "F4TProxyActorControllerSlotPlacement.cpp": 3,
        "F4TPowerArmorSync.cpp": 1,
        "F4TProxyActorControllerPipboyLight.cpp": 1,
    }, f"unexpected proxy release sites: {callers}"

    resolution = (plugin / "src/F4TProxyActorControllerSlotResolution.cpp").read_text(encoding="utf-8")
    equipment_src = (plugin / "src/F4TProxyActorControllerEquipment.cpp").read_text(encoding="utf-8")
    assert re.search(
        r"void ClearProxySessionState\(\)\s*\{\s*RemoveProxyActorsForSave\(\);\s*"
        r"for \(auto& \[_, slot\] : g_proxySlots\) \{\s*F4T::PowerArmorSync::PreservePlacedFrame\(slot\.placedPowerArmorFrame\);\s*F4T::ProxyActorController::detail::ReleaseSlotPrivateProxyBase\(slot\);\s*\}\s*g_proxySlots\.clear\(\);",
        safety,
    ), "ClearProxySessionState must preserve reserved frames before dropping slots"
    assert re.search(
        r"bool IsTrackedByLiveSlot\(const RE::Actor& a_actor\)\s*\{\s*"
        r"if \(F4T::PowerArmorSync::IsRetiredFrameOwner\(a_actor\)\) \{\s*return true;\s*\}\s*"
        r"const auto actorId = a_actor\.GetFormID\(\);",
        safety,
    ), "saved-proxy sweeps must protect retained frame owners by identity"
    assert re.search(
        r"std::size_t RemoveProxyActorsForSave\(\)[\s\S]*?ResetSlotForRespawn\(slot\);",
        safety,
    ), "RemoveProxyActorsForSave must reset each slot"
    assert re.search(
        r"void ResetSlotForRespawn\(ProxyActorSlot& a_slot\)\s*\{[\s\S]*?\n\t\}\n",
        resolution,
    ), "ResetSlotForRespawn definition must exist"
    respawn_body = resolution[resolution.index("void ResetSlotForRespawn(ProxyActorSlot& a_slot)"):]
    respawn_body = respawn_body[:respawn_body.index("\n\t}\n")]
    assert "ResetSlotEquipmentTracking(a_slot, true);" in respawn_body, "ResetSlotForRespawn must reset the equipment tracking with forget=true"
    assert re.search(
        r"void ResetSlotEquipmentTracking\(ProxyActorSlot& a_slot, bool a_forgetTrackedStacks\)\s*\{\s*F4T::PowerArmorSync::DestroyPlacedFrame\(a_slot\.placedPowerArmorFrame, a_slot\.proxyHandle\.get\(\)\.get\(\)\);",
        equipment_src,
    ), "ResetSlotEquipmentTracking must destroy the tracked placed frame"

    assert "void RecordStandIn(std::uint32_t a_formId);" in header
    assert "std::vector<std::uint32_t> StandInFormIdsForSave();" in header
    assert "void LoadStandInFormIds(std::vector<std::uint32_t> a_formIds);" in header
    assert "void ClearStandInFormIds();" in header
    assert "constexpr std::uint32_t kStandInSerializationUniqueId = 'COPS';" in main
    assert re.search(
        r"if \(const auto\* serialization = F4SE::GetSerializationInterface\(\)\) \{\s*"
        r"serialization->SetUniqueID\(kStandInSerializationUniqueId\);\s*"
        r"serialization->SetSaveCallback\(OnStandInSave\);\s*"
        r"serialization->SetLoadCallback\(OnStandInLoad\);\s*"
        r"serialization->SetRevertCallback\(OnStandInRevert\);",
        main,
    ), "F4SE_PLUGIN_LOAD must register the stand-in save, load and revert callbacks under the CO unique id"
    assert re.search(
        r"void F4SEAPI OnStandInSave\(const F4SE::SerializationInterface\* a_intfc\)\s*\{\s*"
        r"const auto formIds = F4T::ProxySaveSafety::StandInFormIdsForSave\(\);\s*"
        r"if \(F4T::ProxySaveSafety::IsSaveTeardownPending\(\)\) \{[\s\S]*?\}\s*"
        r"if \(!a_intfc->WriteRecord\(",
        main,
    ), "the save callback must write the stand-in list as one record and log a failed write"
    assert re.search(
        r"void F4SEAPI OnStandInLoad\(const F4SE::SerializationInterface\* a_intfc\)[\s\S]*?"
        r"a_intfc->ResolveFormID\(savedFormId\)[\s\S]*?"
        r"F4T::ProxySaveSafety::LoadStandInFormIds\(std::move\(formIds\)\);",
        main,
    ), "the load callback must resolve each saved id and hand the result to LoadStandInFormIds"
    assert re.search(
        r"void F4SEAPI OnStandInRevert\(const F4SE::SerializationInterface\*\)\s*\{\s*"
        r"F4T::ProxySaveSafety::ClearStandInFormIds\(\);",
        main,
    ), "the revert callback must clear the stand-in list"

    assert re.search(
        r"bool IsStandIn\(std::uint32_t a_formId\)\s*\{\s*"
        r"std::scoped_lock lock\{ g_standInMutex \};\s*"
        r"return std::ranges::contains\(g_standInFormIds, a_formId\);",
        standins,
    ), "the sweep's stand-in check must read the CO-owned list under its mutex"
    assert re.search(
        r"void RecordStandIn\(std::uint32_t a_formId\)\s*\{\s*"
        r"std::scoped_lock lock\{ g_standInMutex \};[\s\S]*?g_standInFormIds\.push_back\(a_formId\);",
        standins,
    ), "RecordStandIn must add the adopted form id under the mutex"
    assert re.search(
        r"void RecordStandIn\(std::uint32_t a_formId\)\s*\{\s*"
        r"std::scoped_lock lock\{ g_standInMutex \};[\s\S]*?"
        r"if \(std::ranges::contains\(g_standInFormIds, a_formId\)\) \{\s*return;\s*\}[\s\S]*?"
        r"if \(!F4T::ProxySaveSafety::CanRecordStandIn\(g_standInFormIds\.size\(\)\)\) \{[\s\S]*?return;\s*\}[\s\S]*?"
        r"g_standInFormIds\.push_back\(a_formId\);",
        standins,
    ), "RecordStandIn must refuse to grow the list past the record bound"
    assert re.search(
        r"void DisableAndDeleteRef\(RE::TESObjectREFR& a_ref\)\s*\{[\s\S]*?"
        r"LogDisableAndDeleteRefState\(\"after\", a_ref\);\s*"
        r"if \(a_ref\.IsDeleted\(\)\) \{\s*F4T::ProxySaveSafety::ForgetStandIn\(a_ref\.GetFormID\(\)\);\s*return;\s*\}",
        safety,
    ), "a stand-in id must be forgotten only after its ref is actually deleted"
    assert re.search(
        r"if \(a_ref\.IsDeleted\(\)\) \{\s*F4T::DebugLogging::Log\([\s\S]*?ForgetStandIn\(a_ref\.GetFormID\(\)\);\s*return;\s*\}\s*"
        r"LogDisableAndDeleteRefState\(\"before\", a_ref\);",
        safety,
    ), "the already-deleted early return must forget the stand-in id"
    assert re.search(
        r"void DisableAndDeleteRef\(RE::TESObjectREFR& a_ref\)\s*\{[\s\S]*?"
        r"if \(F4T::ProxySaveSafety::IsProtectedPlacedCodsworth\(a_ref\.IsCreated\(\), isCodsworthBase\)\) \{[\s\S]*?return;\s*\}\s*"
        r"if \(a_ref\.IsDeleted\(\)\) \{",
        safety,
    ), "a placed Codsworth base must be refused before any delete path"
    assert "const auto isCodsworthBase = npc && npc->GetFormID() == kDebugCodsworthBaseFormId;" in safety, "the placed-Codsworth guard must read the base of the ref"
    assert "void AfterLoadSweep(bool a_loadSucceeded, bool a_clearStandIns);" in header
    assert re.search(
        r"void ClearStandInFormIds\(\)\s*\{\s*std::scoped_lock lock\{ g_standInMutex \};\s*g_standInFormIds\.clear\(\);\s*g_standInFormIdsSeenThisSession\.clear\(\);",
        standins,
    ), "ClearStandInFormIds must empty both lists under the mutex"
    assert "NoteLoadEvent" not in safety, "the two-flag ordering gate must be gone"
    assert "SweepSavedProxyActorsOnGameThread" not in safety, "the cosave load must not sweep"
    assert "void ForgetStandIn(std::uint32_t a_formId);" in header
    assert "inline constexpr std::size_t kMaximumStandInFormIds = 4096;" in header
    assert "inline constexpr bool IsValidStandInRecordLength(" in header
    assert "inline constexpr bool IsSuccessfulLoadMessageData(" in header
    assert "inline constexpr bool ShouldKeepStandIn(" in header
    assert "inline constexpr bool IsUniqueStandInCandidate(" in header
    assert "inline constexpr bool CanRecordStandIn(" in header
    assert re.search(
        r"bool IsValidStandInRecordLength\(std::uint32_t a_length\) noexcept\s*\{\s*return a_length % sizeof\(std::uint32_t\) == 0 && a_length <= kMaximumStandInFormIds \* sizeof\(std::uint32_t\);",
        header,
    ), "the record length must be a whole number of ids within the bound"
    assert re.search(
        r"void LoadStandInFormIds\(std::vector<std::uint32_t> a_formIds\)\s*\{\s*std::scoped_lock lock\{ g_standInMutex \};\s*"
        r"g_standInFormIdsSeenThisSession\.insert\(a_formIds\.begin\(\), a_formIds\.end\(\)\);\s*"
        r"g_standInFormIds = std::move\(a_formIds\);\s*\}",
        standins,
    ), "LoadStandInFormIds must only store the list"
    assert re.search(
        r"void AfterLoadSweep\(bool a_loadSucceeded, bool a_clearStandIns\)\s*\{\s*DropPrivateProxyBasesWithoutFreeing\(\);\s*g_unresolvedProxyHandles\.clear\(\);\s*ClearProxySessionState\(\);\s*"
        r"if \(a_clearStandIns\) \{\s*ClearStandInFormIds\(\);\s*\}\s*"
        r"SweepSavedProxyActors\(a_loadSucceeded\);\s*"
        r"if \(a_loadSucceeded\) \{\s*PruneStandInFormIds\(\);\s*\}",
        safety,
    ), "AfterLoadSweep must clear the list on a failed load, run the legacy sweep on every path, and prune only after a successful load"
    assert re.search(
        r"std::erase_if\(g_standInFormIds, \[\]\(std::uint32_t a_formId\) \{\s*auto\* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>\(a_formId\);\s*"
        r"const bool resolved = ref != nullptr;\s*const bool created = ref && ref->IsCreated\(\);\s*const bool deleted = ref && ref->IsDeleted\(\);\s*"
        r"const bool keep = F4T::ProxySaveSafety::ShouldKeepStandIn\(resolved, created, deleted\);[\s\S]*?return !keep;",
        standins,
    ), "the prune must keep an unresolved id and drop only a resolved ref that is deleted or not created"
    assert re.search(r"ClearProxySessionState\(\);\s*SweepSavedProxyActors\(false\);", safety), (
        "the session-start sweep in Tick must not use the stand-in predicate"
    )
    assert re.search(r"case F4SE::MessagingInterface::kPreLoadGame: \{[\s\S]*?F4T::ProxySaveSafety::ClearStandInFormIds\(\);", main), "kPreLoadGame must clear the stand-in list"
    assert re.search(r"case F4SE::MessagingInterface::kNewGame: \{\s*const auto survivalLoad = F4T::SurvivalPolicy::g_clientState.BeginLoad\(\);\s*F4T::InventoryGrant::OnNewGame\(\);\s*F4T::ProxySaveSafety::ClearStandInFormIds\(\);\s*RunProxyLoadSweepOnMainThread\(true, survivalLoad\);", main), "kNewGame must close the barrier, clear the stand-in list and sweep with clear"
    assert re.search(r"case F4SE::MessagingInterface::kGameLoaded: \{[\s\S]*?RunProxyLoadSweepOnMainThread\(false\);", main) and "clearStandIns=false" in main, "kGameLoaded must sweep without clearing the stand-in list"
    assert re.search(r"const auto loadSucceeded = F4T::ProxySaveSafety::IsSuccessfulLoadMessageData\(reinterpret_cast<std::uintptr_t>\(a_msg->data\)\);", main), "kPostLoadGame must read the success flag from the message data"
    assert main.count("AfterLoadSweep(loadSucceeded, !loadSucceeded);") == 2 and "AddTask([loadSucceeded, survivalLoad]() {" in main, "the post-load sweep must carry the success flag and load token into its task"
    assert main.count("AfterLoadSweep(false, a_clearStandIns);") == 2, "the new-game and loaded-game path must run the legacy sweep only"
    assert re.search(r"case F4SE::MessagingInterface::kPreLoadGame: \{\s*F4T::SurvivalPolicy::g_clientState\.BeginTrackedLoad\(\);", main), "pre-load must close the Survival barrier before other load work"
    assert re.search(r"case F4SE::MessagingInterface::kNewGame: \{\s*const auto survivalLoad = F4T::SurvivalPolicy::g_clientState\.BeginLoad\(\);[\s\S]*?RunProxyLoadSweepOnMainThread\(true, survivalLoad\);", main), "new-game must bind the Survival barrier to its sweep"
    assert "if (survivalLoad == 0) F4T::SurvivalPolicy::g_clientState.UntrackedLoad();" in main, "unpaired post-load messages must invalidate readiness"
    assert main.count("CompleteLoad(survivalLoad, loadSucceeded);") == 2 and main.count("CompleteLoad(a_survivalLoad);") == 2, "deferred and synchronous sweep paths must complete only their captured load token"
    assert len(re.findall(r"AfterLoadSweep\(false, a_clearStandIns\);\s*if \(a_survivalLoad != 0\) F4T::SurvivalPolicy::g_clientState.CompleteLoad\(a_survivalLoad\);", main)) == 2, "both new-game sweep paths must complete after the sweep"
    assert len(re.findall(r"AfterLoadSweep\(loadSucceeded, !loadSucceeded\);\s*if \(survivalLoad != 0\) F4T::SurvivalPolicy::g_clientState.CompleteLoad\(survivalLoad, loadSucceeded\);", main)) == 2, "both post-load sweep paths must complete after the sweep"
    assert re.search(r"case F4SE::MessagingInterface::kPostLoadGame: \{\s*const auto survivalLoad = F4T::SurvivalPolicy::g_clientState\.TakePendingLoad\(\);", main), "post-load must take its pending Survival token before scheduling the sweep"
    game_loaded_case = main.split("case F4SE::MessagingInterface::kGameLoaded:", 1)[1].split("case F4SE::MessagingInterface::kPostLoadGame:", 1)[0]
    assert "BeginLoad" not in game_loaded_case and "BeginTrackedLoad" not in game_loaded_case, "game-data initialization must not start a save-load barrier"
    assert '#include "F4TDeathBagView.h"' in main and main.count("F4T::DeathBagView::SweepSavedContainers();") == 4 and len(re.findall(r"SweepSavedContainers\(\);\s*F4T::ProxySaveSafety::AfterLoadSweep\(loadSucceeded, !loadSucceeded\);", main)) == 2, "every load sweep path must delete the saved death bags, including both post-load branches"
    assert re.search(r"if \(!taskInterface\) \{\s*F4T::DeathBagView::SweepSavedContainers\(\);\s*F4T::ProxySaveSafety::AfterLoadSweep\(false, a_clearStandIns\);", main), "the synchronous load sweep must delete saved death bags before the legacy sweep"
    assert re.search(r"AddTask\(\[a_clearStandIns, a_survivalLoad\]\(\) \{\s*F4T::DeathBagView::SweepSavedContainers\(\);", main), "the deferred load sweep must delete saved death bags first"
    assert re.search(r"if \(!a_intfc->WriteRecord\(", main), "a failed WriteRecord must be logged"
    assert "IsValidStandInRecordLength(length)" in main and re.search(r"ReadRecordData\(savedFormIds\.data\(\), length\) != length", main), "the load must validate the length and check the read count"

    for name, fn in [
        ("F4TProxyActorControllerPlaceAtMeSpawn.cpp", "StartPlaceAtMeBackedProxySettle"),
        ("F4TProxyActorControllerSlotPlacement.cpp", "StartSlotPlaceAtMeSettle"),
    ]:
        text = (plugin / "src" / name).read_text(encoding="utf-8")
        assert re.search(
            rf"void {fn}\([^)]*\)\s*\{{[\s\S]{{0,300}}?IsUniqueStandInCandidate\(a_newCandidateCount\)\) \{{\s*F4T::ProxySaveSafety::RecordStandIn\(a_actor\.GetFormID\(\)\);",
            text,
        ), f"{name}:{fn} must record the PlaceAtMe actor it adopts as a CO stand-in"

    placeatme_spawn = (plugin / "src/F4TProxyActorControllerPlaceAtMeSpawn.cpp").read_text(encoding="utf-8")
    runtime_spawn = (plugin / "src/F4TProxyActorControllerRuntimeSpawn.cpp").read_text(encoding="utf-8")
    npc_puppets = (plugin / "src/F4TNpcSyncPuppets.cpp").read_text(encoding="utf-8")
    internal_header = (plugin / "include/F4TProxyActorControllerInternal.h").read_text(encoding="utf-8")
    assert "StartPlaceAtMeBackedProxySettle(*actor, newCandidateCount);" in placeatme_spawn, "the PlaceAtMe adoption must pass its candidate count"
    assert "StartSlotPlaceAtMeSettle(a_slot, *actor, newCandidateCount);" in runtime_spawn, "the slot adoption must pass its candidate count"
    assert "TryIsolatePlaceAtMeBackedProxyCandidate(a_currentCell, a_player, newCandidateCount)" in placeatme_spawn
    assert "TryIsolateSlotPlaceAtMeCandidate(a_slot, a_currentCell, a_player, newCandidateCount)" in runtime_spawn
    assert "void StartSlotPlaceAtMeSettle(ProxyActorSlot& a_slot, RE::Actor& a_actor, std::size_t a_newCandidateCount);" in internal_header
    assert "g_createdPuppetBases.erase(ref->GetFormID());" in npc_puppets and "F4T::ProxySaveSafety::ForgetStandIn(ref->GetFormID());" in npc_puppets, "the puppet delete must forget the stand-in it removes"
    assert "DecidePuppetDelete(resolved, ref && ref->IsDeleted())" in npc_puppets, "the puppet delete is guarded by resolution and the deleted flag"

    slot_placement = (plugin / "src/F4TProxyActorControllerSlotPlacement.cpp").read_text(encoding="utf-8")
    runtime_spawn = (plugin / "src/F4TProxyActorControllerRuntimeSpawn.cpp").read_text(encoding="utf-8")
    slot_lifecycle = (plugin / "src/F4TProxyActorControllerSlotLifecycle.cpp").read_text(encoding="utf-8")
    main_source = (plugin / "src/main.cpp").read_text(encoding="utf-8")
    assert "[RT-VERIFY 408-A] cosave load callback" in main_source and "[RT-VERIFY 408-A] kPostLoadGame" in main_source, "main.cpp must log the cosave and kPostLoadGame order with RT-VERIFY 408-A"
    assert "[RT-VERIFY 408-B] ResolveFormID" in main_source, "the cosave load must log each ResolveFormID result with RT-VERIFY 408-B"
    assert "[RT-VERIFY 408-C] PlaceAtMe window OPEN" in placeatme_spawn and "[RT-VERIFY 408-C] PlaceAtMe window CLOSE settle" in placeatme_spawn, "the PlaceAtMe window must log open and close with RT-VERIFY 408-C"
    assert "[RT-VERIFY 408-C] slot PlaceAtMe window OPEN" in slot_placement and "[RT-VERIFY 408-C] slot PlaceAtMe window CLOSE timeout" in runtime_spawn, "the slot PlaceAtMe window must log open and close with RT-VERIFY 408-C"
    assert "cell-change hold proxy=" in slot_lifecycle, "the cell-change hold must log its position"
    assert "[RT-VERIFY 408-D] reused id" in standins and "[RT-VERIFY 408-E/F/G] RecordStandIn" in standins,"RecordStandIn must log the thread and form id and the reuse with RT-VERIFY 408-D and 408-E/F/G"
    assert "ShouldLogPeriodic(" in header, "the periodic log predicate must be declared in the header"
    assert "kMaximumWindowCandidateLogLines" in placeatme_spawn and "kMaximumWindowCandidateLogLines" in slot_placement, "the PlaceAtMe and slot candidate logs must be capped per window"
    assert "kMaxSeenStandInFormIds" in standins,"the reuse-check set must have a ceiling"
    assert re.search(r"void BeginSlotPlaceAtMeSpawnAttempt\(", slot_placement) and slot_placement.count("BeginSlotPlaceAtMeSpawnAttempt(") == 3 and "return BeginSlotPlaceAtMeSpawnAttempt(" not in slot_placement, "BeginSlotPlaceAtMeSpawnAttempt must be void and called (not returned) from both callers"
    assert re.search(
        r"if \(a_slot\.spawnState == PlaceAtMeBackedProxySpawnState::kCustomProxyAttempting\) \{[\s\S]{0,400}?a_slot\.spawnState = PlaceAtMeBackedProxySpawnState::kNotStarted;\s*\}\s*BeginSlotPlaceAtMeSpawnAttempt\([\s\S]{0,300}?\);\s*return true;",
        slot_placement,
    ), "the custom-to-Codsworth fallback must reset the abandoned custom attempt to kNotStarted before it starts the Codsworth attempt"
    assert re.search(
        r"if \(TryBeginSlotCodsworthFallback\(a_slot, a_currentCell, a_player\)\) \{\s*return true;\s*\}\s*\}\s*MarkSlotSpawnFailed\(a_slot\);\s*return false;",
        runtime_spawn,
    ), "a failed custom-to-Codsworth start must fall through to MarkSlotSpawnFailed"
    assert "[RT-VERIFY 408-D] DisableAndDeleteRef early return" in safety and "LogDisableAndDeleteRefState(\"before\", a_ref);" in safety and "LogDisableAndDeleteRefState(\"after\", a_ref);" in safety, "DisableAndDeleteRef must log its state before and after and its early return with RT-VERIFY 408-D"
    assert "TryClaimSlotWindowCandidateLog(actor->GetFormID(), acceptedAsNew)" in slot_placement, "the slot window candidate log must be keyed on acceptedAsNew so a changed value is logged"
    assert re.search(
        r"if \(g_placeAtMeWindowCandidateLogs < F4T::ProxySaveSafety::kMaximumWindowCandidateLogLines\) \{\s*\+\+g_placeAtMeWindowCandidateLogs;\s*LogInfoWithLocalPlayerPrefix",
        placeatme_spawn,
    ), "the Stage 3.7 candidate LogInfo must sit inside the window line cap"

def _section_012_f4tproxyweapondrawcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TProxyWeaponDrawContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TProxyWeaponDrawContract.py")
    from pathlib import Path
    import re


    root = Path(__file__).resolve().parents[3]
    plugin = root / "CO-BETA/plugin"
    header = (plugin / "include/F4TProxyWeaponDraw.h").read_text(encoding="utf-8")
    impl = (plugin / "src/F4TProxyWeaponDraw.cpp").read_text(encoding="utf-8")
    actions = (plugin / "src/F4TProxyActorControllerActions.cpp").read_text(encoding="utf-8")
    animation_sync = (plugin / "src/F4TProxyAnimationSync.cpp").read_text(encoding="utf-8")


    def brace_span(text, open_brace):
        depth = 0
        for index in range(open_brace, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    return open_brace, index
        raise AssertionError("unbalanced braces")


    apply_start = impl.index("void ApplyProxyWeaponDrawDecision(")
    apply_open, apply_close = brace_span(impl, impl.index("{", apply_start))
    draw_if_start = impl.index("if (drawn) {", apply_open, apply_close)
    draw_if_open, draw_if_close = brace_span(impl, impl.index("{", draw_if_start))

    assert impl.count("a_proxy.AttachWeapon(") == 1, "only the draw path may attach a weapon"
    attach = impl.index("a_proxy.AttachWeapon(weaponInstance, equipIndex);")
    assert apply_open < attach < apply_close, "the attach must live in the draw decision application"
    assert draw_if_open < attach < draw_if_close, "only a draw may attach"
    assert attach < impl.index("a_proxy.DoReparentWeapon("), "the attach must come first, as on the transition path"

    reparent = impl.index("a_proxy.DoReparentWeapon(&a_weapon, equipIndex, drawn);")
    magic_hands = impl.index("a_proxy.DrawWeaponMagicHands(drawn);")
    assert apply_open < attach < reparent < magic_hands < apply_close, (
        "the draw decision must attach, reparent and send magic hands in order"
    )
    assert "BindToProxy(state, a_proxy.GetFormID(), now);" in impl, "the reconcile must bind its state to the proxy"
    assert "lastVisitSeconds" in header, "the state must record the last visit"
    assert re.search(
        r"a_nowSeconds - a_state\.lastVisitSeconds > a_generationGapSeconds",
        header,
    ), "a stale visit gap must start a new proxy generation"
    assert "PruneStale(g_weaponDrawStates," in impl, "stale weapon draw state must be pruned"
    assert impl.index("PruneStale(g_weaponDrawStates,") < impl.index("g_weaponDrawStates[a_remotePlayerId]"), (
        "pruning must happen before the entry is used"
    )

    assert re.search(
        r"\[\[nodiscard\]\] inline Decision Decide\(\s*bool a_remoteDrawn,\s*bool a_actualDrawn,\s*"
        r"bool a_hasWeapon,\s*bool a_graphSettling,\s*double a_lastChangeSeconds,\s*double a_nowSeconds,",
        header,
    ), "Decide must take remote drawn, actual drawn, has weapon, settling, last change and now"

    assert "inline constexpr double kStabilitySeconds = 0.150;" in header
    assert "inline constexpr double kMinChangeIntervalSeconds = 0.500;" in header
    assert re.search(r"a_nowSeconds - a_state\.pendingSinceSeconds < a_parameters\.stabilitySeconds", header), (
        "the desired value must be stable before it is acted on"
    )
    assert re.search(
        r"a_nowSeconds - a_lastChangeSeconds < a_parameters\.minChangeIntervalSeconds",
        header,
    ), "changes must be rate limited"

    assert "a_proxy.GetWeaponMagicDrawn()" in impl
    assert "a_proxy.DoReparentWeapon(&a_weapon, equipIndex, drawn);" in impl
    assert "a_proxy.DrawWeaponMagicHands(drawn);" in impl
    assert "ProxyActorController::detail::ProxyHasLoaded3D(a_proxy)" in impl
    assert "ProxyActorController::detail::LogThrottledInfo(" in impl
    assert "kEnableProxyWeaponDrawReconcile" not in impl, "the reconcile must not sit behind an enable switch"
    assert "ClearPendingKeepInterval(state);" in impl, "convergence and a missing weapon must keep the last change time for the rate limit"
    assert "Reset(state);" not in impl, "a missing weapon must not reset the change interval"
    for source_name, source_text in (
        ("src/F4TProxyWeaponDraw.cpp", impl),
        ("src/F4TProxyAnimationSync.cpp", animation_sync),
    ):
        assert not re.search(
            r"RE::BGSObjectInstanceT<RE::TESObjectWEAP>\s+weaponInstance\s*\{[^}]*\bnullptr\b",
            source_text,
        ), f"{source_name}: AttachWeapon must receive the equipped weapon's instance data, not nullptr"
    assert re.search(
        r"GetEquippedRightHandWeapon\(a_proxy, equippedItem\)", impl
    ), "the draw path must hand the whole equipped item to the attach"
    assert "ApplyProxyWeaponDrawDecision(a_proxy, *weapon, equippedItem.instanceData.get(), decision);" in impl, (
        "the draw decision must apply the equipped weapon's instance data"
    )
    assert animation_sync.count(
        "RE::BGSObjectInstanceT<RE::TESObjectWEAP> weaponInstance{ weapon, equippedWeapon->instanceData.get() };"
    ) == 2, "both transition AttachWeapon calls must pass the equipped weapon's instance data"
    assert re.search(
        r"F4T::ProxyWeaponDraw::ReconcileProxyWeaponDraw\(\s*a_remotePlayer\.playerId,\s*a_proxy,\s*"
        r"a_remotePlayer,\s*IsProxyWeaponGraphSettling\(a_slot\)\);",
        actions,
    ), "the proxy update path must reconcile the weapon draw with the slot settle flag"
    assert actions.index("ReconcileProxyWeaponDraw(") < actions.index("if (IsProxyWeaponGraphSettling(a_slot)) {"), (
        "reconciliation must observe the settle window before the update path returns"
    )


    for path in (plugin / "src").glob("F4TProxyWeaponDraw*.cpp"):
        assert len(path.read_text(encoding="utf-8").splitlines()) < 1000, f"{path} exceeds 1000 lines"

    print("F4TProxyWeaponDrawContract: all checks passed.")

def _section_013_f4ttextentrymodecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TTextEntryModeContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TTextEntryModeContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


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


    helper = read_code("src/F4TTextEntryMode.cpp")
    helper_header = read_code("include/F4TTextEntryMode.h")
    ui_client = read_code("src/F4TUIClient.cpp")
    main_cpp = read_code("src/main.cpp")

    check("F4T::TextEntryMode" in helper_header
          and "void SetChatInputFocused(bool a_focused);" in helper_header
          and "void Reset();" in helper_header,
          "F4TTextEntryMode.h must declare SetChatInputFocused(bool) and Reset()")

    check("F4T_" not in helper, "the text-entry helper must not be feature gated")

    set_focus = function_body(helper, "void SetChatInputFocused(bool a_focused)")
    check(in_order(set_focus, "if (a_focused == g_entered)", "SetTextEntryMode(a_focused);", "g_entered = a_focused;"),
          "SetChatInputFocused must return on no state change, call the engine, then record the state")
    check(helper.count("SetTextEntryMode(") == 1,
          "the helper must be the only SetTextEntryMode call so every enter is matched by one leave")

    reset = function_body(helper, "void Reset()")
    check("SetChatInputFocused(false);" in reset, "Reset must leave text-entry mode through SetChatInputFocused(false)")

    for relative in ("src", "include", "tests"):
        for path in (PLUGIN / relative).rglob("*"):
            if path.suffix in (".cpp", ".h") and path.name != "F4TTextEntryMode.cpp":
                check("SetTextEntryMode(" not in path.read_text(encoding="utf-8"),
                      f"{path.relative_to(ROOT).as_posix()} must not call SetTextEntryMode directly")

    disconnect = function_body(ui_client, "void Callback_Disconnect()")
    check("F4T::TextEntryMode::Reset();" in disconnect and "#if" not in disconnect,
          "Callback_Disconnect must reset text-entry mode ungated")

    transport = function_body(ui_client, "void NotifyTransportDisconnected(")
    check(re.search(r"AddTask\(\[reasonCopy, bumpGeneration\]\(\) \{\s*F4T::TextEntryMode::Reset\(\);", transport) is not None,
          "NotifyTransportDisconnected must reset text-entry mode first inside its game task, ungated")

    check("std::atexit(F4T::TextEntryMode::Reset);" in main_cpp,
          "main.cpp must register TextEntryMode::Reset with std::atexit")

    pinned_ids = PLUGIN / "lib/commonlibf4/include/RE/IDs.h"
    if pinned_ids.is_file():
        check("SetTextEntryMode{ 1270079, 2268339, 4491359 }" in pinned_ids.read_text(encoding="utf-8"),
              "pinned CommonLib IDs.h must map ControlMap::SetTextEntryMode to OG 1270079, NG 2268339, AE 4491359")
    else:
        print("NOTE: pinned CommonLib submodule is empty; the ControlMap id check was skipped.")

    chat_code = read_code("src/F4TChat.cpp")
    chat_action = function_body(chat_code, "void HandleAction(std::string_view a_actionJson)")
    check('op == "focus"' in chat_action and "SetChatInputFocused(" in chat_action,
          "Chat::HandleAction must route the focus op to TextEntryMode::SetChatInputFocused")
    check(re.search(r"^\s*const auto focused = action\.find\(\"focused\"\);", chat_action, re.M) is not None,
          "the focus op must read the focused field with a plain declaration (no stray qualifier)")
    check(chat_action.index('op == "focus"') < chat_action.index("IsConnectedToServer()"),
          "the focus op must be handled before the connected check so a blur always releases text entry")
    chat_js = (ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/chat/chat.js").read_text(encoding="utf-8")
    check('addEventListener("focus"' in chat_js and 'addEventListener("blur"' in chat_js and 'op: "focus"' in chat_js,
          "chat.js must post focus ops on input focus and blur")
    check('addEventListener("pagehide"' in chat_js, "chat.js must release text entry on pagehide")

    chat_view = strip_comments((ROOT / "CommonwealthOnlineUI/src/F4TChatView.cpp").read_text(encoding="utf-8"))
    chat_view_action = function_body(chat_view, "void HandleAction(const std::string& a_json)")
    check('op != "focus"' in chat_view_action,
          "ChatView::HandleAction must let the focus op through to the plugin")

    close_internal = function_body(chat_view, "void CloseInternal(std::string_view a_reason)")
    check(in_order(close_internal, "SetDesired(false, false);", "ApplyState(a_reason);", "SendAction(kReleaseFocusJson);"),
          "ChatView::CloseInternal must release chat input focus when the window is hidden")
    release_focus = function_body(chat_view, "void ReleaseInputFocus()")
    check('R"({"op":"focus","focused":false})"' in chat_view and "RunOnGameThread(" in release_focus
          and "SendAction(kReleaseFocusJson)" in release_focus,
          "ChatView::ReleaseInputFocus must send the focus-off action through SendAction on the game thread")

    cursor_write = function_body(chat_view, "void WriteWindowCursor(const char* a_reason)")
    check("IsCurrentlyVisible()" in cursor_write and "CursorPolicy::Default" in cursor_write
          and "CursorPolicy::Hidden" in cursor_write,
          "ChatView::WriteWindowCursor must show Prisma's cursor while the window is visible and hide it otherwise")
    check("SetViewCursorPolicyCached(" in cursor_write,
          "the chat window must set its own cursor policy instead of relying on Prisma's default")
    check("WriteWindowCursor(\"Open\")" in function_body(chat_view, "void OpenInternal(std::string_view a_reason)"),
          "ChatView::OpenInternal must write the cursor policy once the window is up")
    check("WriteWindowCursor(\"Close\")" in close_internal,
          "ChatView::CloseInternal must hide the pointer again so none is left over gameplay")
    cursor_tick = function_body(chat_view, "void Tick()")
    check("g_cursorWritten = false;" in cursor_tick and "WriteWindowCursor(\"FocusChanged\")" in cursor_tick,
          "ChatView::Tick must re-write the cursor policy after a focus handoff")
    check('addEventListener("mousedown", keepComposerFocus)' in chat_js
          and "function keepComposerFocus" in chat_js and "event.preventDefault()" in chat_js,
          "chat.js must cancel mousedown on the channel buttons so a click keeps the composer focused")
    check("elements.input.focus();" in function_body(chat_js, "function selectChannel(channel)"),
          "chat.js:selectChannel must return focus to the composer after a mouse channel switch")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TTextEntryModeContract: all checks passed.")

_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TProxySaveSafetyContract.py", _section_011_f4tproxysavesafetycontract),
    ("CO-BETA/plugin/tests/F4TProxyWeaponDrawContract.py", _section_012_f4tproxyweapondrawcontract),
    ("CO-BETA/plugin/tests/F4TTextEntryModeContract.py", _section_013_f4ttextentrymodecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TProxyConsoleContracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
