#!/usr/bin/env python3
"""F4TNpcCombatContracts_part4: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TNpcCombatContracts_part4.py
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


def _section_017_f4tnpcsyncactorindexcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TNpcSyncActorIndexContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TNpcSyncActorIndexContract.py")
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
        start = code.find(signature)
        if start < 0:
            return ""
        brace = code.find("{", start)
        if brace < 0:
            return ""
        depth = 0
        for index in range(brace, len(code)):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    return code[brace:index + 1]
        return ""


    internal = read_code("include/F4TNpcSyncInternal.h")
    puppets = read_code("src/F4TNpcSyncPuppets.cpp")
    suppress = read_code("src/F4TNpcSyncSuppress.cpp")
    resolution = read_code("src/F4TProxyActorControllerActorResolution.cpp")

    for_each = function_body(internal, "void ForEachLoadedActor(")
    check("thread_local std::unordered_set<std::uint32_t> visited;" in for_each and "visited.clear();" in for_each,
          "ForEachLoadedActor must reuse a thread_local visited set instead of allocating one per call")

    build = function_body(suppress, "LoadedActorIndex BuildLoadedActorIndex(")
    check(build.count("ForEachLoadedActor(") == 1,
          "BuildLoadedActorIndex must walk the loaded actors through ForEachLoadedActor")

    update_puppets = function_body(puppets, "void UpdatePuppets(")
    check(update_puppets.count("BuildLoadedActorIndex()") == 1,
          "UpdatePuppets must build the loaded actor index exactly once per call")
    check(0 <= update_puppets.find("GetSnapshot()") < update_puppets.find("BuildLoadedActorIndex()") < update_puppets.find("HasLocalFollowerMatch("),
          "UpdatePuppets must build the index lazily after the eligibility check and before first use")
    check(update_puppets.count("loadedActors") >= 5,
          "UpdatePuppets must hand the shared index to every helper it calls")

    for signature in ("RE::Actor* FindMatchingSourceActor(", "bool HasMatchingAliveLocalNpc(",
                      "bool HasLocalFollowerMatch(", "void SuppressDuplicateLocalNpcs("):
        body = function_body(suppress, signature)
        check("ForEachLoadedActor(" not in body and "a_loadedActors.find(" in body,
              f"{signature} must look up the shared actor index instead of scanning every loaded actor")

    resolve = function_body(resolution, "RE::TESNPC* ResolveProxyActorBaseByPluginLocalFormId(")
    check("resolvedBases.emplace(" in resolve and 0 <= resolve.find("resolvedBases.find(") < resolve.find("LookupForm<"),
          "ResolveProxyActorBaseByPluginLocalFormId must answer a cached base before the LookupForm")
    check(resolve.count("ShouldLog(") == 2,
          "the miss-path warnings must format only after ShouldLog accepts the throttle key")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TNpcSyncActorIndexContract: all checks passed.")


def _section_019_f4tpvpcombatcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TPvPCombatContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TPvPCombatContract.py")
    from pathlib import Path
    import re


    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN_DIR = ROOT / "CO-BETA/plugin"


    capture_cpp = (PLUGIN_DIR / "src/F4TRangedCombatCapture.cpp").read_text(encoding="utf-8")
    networking_cpp = (PLUGIN_DIR / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
    remote_cpp = (PLUGIN_DIR / "src/F4TRemotePlayerState.cpp").read_text(encoding="utf-8")

    assert "F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId" in capture_cpp
    target_check = re.search(
        r"if \(!targetPlayerId\) \{.*?return RE::BSEventNotifyControl::kContinue;\s*\}",
        capture_cpp,
        re.DOTALL
    )
    assert target_check is not None, "Capture must reject hits that do not map to an active remote player proxy"

    assert "const bool isUnarmedPunch =" in capture_cpp
    assert "const bool isRangedHit = F4T::WeaponClassification::IsRangedArchetype(weaponType);" in capture_cpp
    assert "const bool isMeleeWeaponHit =" in capture_cpp
    assert "!isUnarmedPunch && F4T::WeaponClassification::UseMeleeBehavior(weaponType);" in capture_cpp

    assert '#include "F4TPvPCombatContract.h"' in networking_cpp
    assert "F4T::PvPCombatContract::ValidateHitRouting(" in networking_cpp
    assert "F4T::PvPCombatContract::InboundHitTracker g_incomingCombatHits;" in remote_cpp
    assert "g_incomingCombatHits.QueueHit(a_senderId, a_sequence, a_damage)" in remote_cpp
    assert "g_incomingCombatHits.PurgeSender(a_playerId)" in remote_cpp
    assert "g_incomingCombatHits.pendingHits" in remote_cpp

    remove_fn = re.search(
        r"bool RemoveRemotePlayer\(std::uint32_t a_playerId\)\s*\{(.*?)\n\t\}",
        remote_cpp,
        re.DOTALL
    )
    assert remove_fn is not None, "RemoveRemotePlayer definition not found"
    remove_body = remove_fn.group(1)
    assert "g_incomingCombatHits.PurgeSender(a_playerId)" in remove_body, (
        "RemoveRemotePlayer must reset shared hit state for the removed player"
    )

    contract_h = (PLUGIN_DIR / "include/F4TPvPCombatContract.h").read_text(encoding="utf-8")
    behavior_cpp = (PLUGIN_DIR / "tests/F4TPvPCombatBehaviorTests.cpp").read_text(encoding="utf-8")
    windows_compile = (ROOT / "scripts/windows-compile-ci.py").read_text(encoding="utf-8")

    assert "IsValidDamage" in contract_h
    assert "ValidateHitRouting" in contract_h
    assert "InboundHitTracker" in contract_h

    assert "VerifyRoutingValidation" in behavior_cpp
    assert "VerifySequenceDeduplicationAndStaleness" in behavior_cpp
    assert "VerifyDisconnectReset" in behavior_cpp
    assert "VerifyPendingHitsPurgedOnDisconnect" in behavior_cpp
    assert "VerifyQueueCapacityCapping" in behavior_cpp

    assert '"F4TPvPCombatBehaviorTests"' in windows_compile

    assert "s_queueEvictions" in capture_cpp and "oldest hit evicted total=" in capture_cpp
    assert "s_unboundHitDrops" in capture_cpp and "totalDrops=" in capture_cpp

    print("F4TPvPCombatContract: all checks passed.")


def _section_021_f4tsurvivalneedsclientcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TSurvivalNeedsClientContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSurvivalNeedsClientContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"
    SCRIPTS = ROOT / "CO-BETA/creation-kit/scripts/source"
    QUEST_SCRIPT = SCRIPTS / "COQuestSyncScript.psc"
    NEEDS_SCRIPT = SCRIPTS / "CommonwealthOnlineNeeds.psc"
    NEEDS_CPP = PLUGIN / "src/F4TSurvivalNeedsClient.cpp"
    NEEDS_H = PLUGIN / "include/F4TSurvivalNeedsClient.h"
    INBOUND = PLUGIN / "src/F4TNetworkingInbound.cpp"
    MAIN = PLUGIN / "src/main.cpp"
    IDENTITY = PLUGIN / "src/F4TClientIdentity.cpp"
    NETWORKING = PLUGIN / "src/F4TNetworking.cpp"
    RUNTIME = PLUGIN / "src/F4TReplicationRuntime.cpp"

    NATIVES = ["GetHunger", "GetThirst", "GetSleep", "TakeConsumeResult", "ReportConsume"]
    KEYWORD_IDS = ["0x0F4AEC", "0x0F4AED", "0x249F30", "0x10C416", "0x249F31", "0x055ECC"]

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        if not path.exists():
            failures.append(f"missing {path.relative_to(ROOT)}")
            return ""
        return path.read_text(encoding="utf-8")


    def body_after(source, anchor):
        start = source.find(anchor)
        if start < 0:
            return None
        brace = source.index("{", start + len(anchor))
        depth = 0
        for index in range(brace, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[brace : index + 1]
        return None


    quest = read(QUEST_SCRIPT)
    needs_script = read(NEEDS_SCRIPT)
    needs_cpp = read(NEEDS_CPP)
    needs_h = read(NEEDS_H)
    inbound = read(INBOUND)
    main_src = read(MAIN)

    check('RegisterForRemoteEvent(player, "OnItemEquipped")' in quest,
          "COQuestSyncScript.psc must register OnItemEquipped on the player")
    check("Event Actor.OnItemEquipped(Actor akSender, Form akBaseObject, ObjectReference akReference)" in quest,
          "COQuestSyncScript.psc must handle Actor.OnItemEquipped with the vanilla signature")
    check("CommonwealthOnlineNeeds.ReportConsume(akBaseObject, kind)" in quest,
          "COQuestSyncScript.psc must call CommonwealthOnlineNeeds.ReportConsume")
    check("akBaseObject as Potion" in quest, "COQuestSyncScript.psc must filter consumables by the Potion (ALCH) type")
    check("IsHostile()" in quest, "COQuestSyncScript.psc must skip poison through Potion.IsHostile")
    for keyword_id in KEYWORD_IDS:
        check(keyword_id in quest, f"COQuestSyncScript.psc must classify with keyword {keyword_id}")
    check("UpdateNeedsFeedback()" in quest, "COQuestSyncScript.psc must poll needs feedback from OnTimer")
    check(";" not in quest, "COQuestSyncScript.psc must not contain explanatory comments")

    check("Native Hidden" in needs_script, "CommonwealthOnlineNeeds.psc must be a Native Hidden class")
    check(";" not in needs_script, "CommonwealthOnlineNeeds.psc must not contain comments")
    for name in NATIVES:
        check(re.search(rf"\b{name}\(.*\) Global Native", needs_script) is not None,
              f"CommonwealthOnlineNeeds.psc must declare {name} as a Global Native")
        check(f'BindNativeMethod("CommonwealthOnlineNeeds", "{name}"' in needs_cpp,
              f"F4TSurvivalNeedsClient.cpp must bind CommonwealthOnlineNeeds.{name}")

    report = body_after(needs_cpp, "void PapyrusReportConsume(")
    check(report is not None, "F4TSurvivalNeedsClient.cpp must define PapyrusReportConsume")
    report = report or ""
    check("NeedsEnabled()" in report, "PapyrusReportConsume must gate on SurvivalPolicy::NeedsEnabled")
    check("IsConnectedToServer()" in report, "PapyrusReportConsume must gate on an established connection")
    check("TranslateItemKey(a_item)" in report, "PapyrusReportConsume must key the item like the inventory sync")
    check("g_pendingSends.push_back(" in report, "PapyrusReportConsume must queue the claim for the tick drain")
    check("SendPacketBytes(" not in report, "PapyrusReportConsume must not send from the Papyrus VM thread")
    check('"needsConsume"' in report, "PapyrusReportConsume must send the needsConsume packet type")
    flush = body_after(needs_cpp, "void FlushPendingSends()") or ""
    check("SendPacketBytes(" in flush, "FlushPendingSends must send the queued needsConsume packets")
    runtime_tick = body_after(read(RUNTIME), "void Tick()") or ""
    check(runtime_tick.find("SurvivalNeedsClient::FlushPendingSends();") >= 0 and
          runtime_tick.find("SurvivalNeedsClient::FlushPendingSends();") < runtime_tick.find("InventorySync::Tick("),
          "F4TReplicationRuntime.cpp Tick must drain the needs claims before the inventory snapshot tick")
    inventory_sync = read(PLUGIN / "src/F4TInventorySync.cpp")
    check(inventory_sync.find("SurvivalNeedsClient::FlushPendingSends();") >= 0 and
          inventory_sync.find("SurvivalNeedsClient::FlushPendingSends();") < inventory_sync.find("InventorySnapshotRead::ReadLocalItems()"),
          "F4TInventorySync.cpp must drain the needs claims right before the snapshot read")
    check('"duplicate"' in needs_cpp and '"result"' in needs_cpp,
          "HandleConsumeResult must settle a duplicate by its result field, not as applied")
    check(read(NETWORKING).count("F4T::SurvivalNeedsClient::Reset();") == 2,
          "F4TNetworking.cpp must reset the needs client on connect and on disconnect")
    check('metadata.capabilities.emplace_back("survival-needs-v1");' in read(IDENTITY),
          "F4TClientIdentity.cpp must advertise survival-needs-v1 in the hello capabilities")

    check('TryHandlePacket(std::string_view a_type' in needs_cpp,
          "F4TSurvivalNeedsClient.cpp must expose TryHandlePacket")
    check('"needsUpdate"' in needs_cpp and '"needsConsumeResult"' in needs_cpp,
          "TryHandlePacket must route needsUpdate and needsConsumeResult")
    check("F4T::SurvivalNeedsClient::TryHandlePacket(*packetType, packet)" in inbound,
          "F4TNetworkingInbound.cpp must dispatch packets to F4T::SurvivalNeedsClient::TryHandlePacket")
    for handler in ("void HandleWelcomePacket(", "void HandleCapabilitiesPacket("):
        check("F4T::SurvivalNeedsClient::PushHudState();" in (body_after(inbound, handler) or ""),
              f"F4TNetworkingInbound.cpp {handler[5:-1]} must push the needs HUD state after writing g_needsEnabled")
    check("F4T::SurvivalNeedsClient::RegisterPapyrus(a_vm)" in main_src,
          "main.cpp must register the needs natives through RegisterPapyrusNeeds")
    check("RegisterPapyrusNeeds" in main_src, "main.cpp must pass RegisterPapyrusNeeds to the Papyrus interface")
    check("TESEquipEvent" not in needs_cpp,
          "F4TSurvivalNeedsClient.cpp must not use a C++ equip event sink")
    for source_name, source in (("F4TSurvivalNeedsClient.cpp", needs_cpp), ("F4TSurvivalNeedsClient.h", needs_h)):
        check("//" not in source and "/*" not in source,
              f"{source_name} must not contain explanatory comments")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(
            "F4TSurvivalNeedsClientContract: the needs consume path is incomplete. The Papyrus equip hook, "
            "the native bridge, the send gate and the inbound dispatch must all match; see "
            "docs/code-notes/Commonwealth Online.md."
        )
        _merge_exit(1)

    print("F4TSurvivalNeedsClientContract: all checks passed.")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TNpcSyncActorIndexContract.py", _section_017_f4tnpcsyncactorindexcontract),
    ("CO-BETA/plugin/tests/F4TPvPCombatContract.py", _section_019_f4tpvpcombatcontract),
    ("CO-BETA/plugin/tests/F4TSurvivalNeedsClientContract.py", _section_021_f4tsurvivalneedsclientcontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TNpcCombatContracts_part4: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
