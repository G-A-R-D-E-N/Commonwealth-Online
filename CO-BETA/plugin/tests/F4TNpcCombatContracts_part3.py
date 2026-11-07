#!/usr/bin/env python3
"""F4TNpcCombatContracts_part3: consolidated contract sections.

Consolidated from 4 single-contract scripts so the group runs one process
instead of 4. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TNpcCombatContracts_part3.py
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




def _section_013_f4tnpccoopcombatcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TNpcCoopCombatContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TNpcCoopCombatContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    def read_workflow(name):
        for folder in (".github/public-ci/workflows", ".github/workflows"):
            if (ROOT / folder / name).is_file():
                return (ROOT / folder / name).read_text(encoding="utf-8")
        raise FileNotFoundError(f"workflow not found: {name}")


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


    xmake = (PLUGIN / "xmake.lua").read_text(encoding="utf-8")
    npc_sync_header = read_code("include/F4TNpcSync.h")
    guard_header = read_code("include/F4TNpcActorGuard.h")
    combat_raw = (PLUGIN / "src/F4TNpcSyncCombat.cpp").read_text(encoding="utf-8")
    combat = read_code("src/F4TNpcSyncCombat.cpp")
    capture = read_code("src/F4TNpcCombatCapture.cpp")
    coop = read_code("src/F4TNetworkingNpcCoop.cpp")
    remote = read_code("src/F4TRemotePlayerState.cpp")
    inbound = read_code("src/F4TNetworkingInbound.cpp")
    npc_sync_cpp = read_code("src/F4TNpcSync.cpp")
    proxy_cpp = read_code("src/F4TProxyActorController.cpp")
    proxy_puppet = read_code("src/F4TProxyPuppet.cpp")
    publish = read_code("src/F4TReplicationRuntimePublish.cpp")
    networking_header = read_code("include/F4TNetworking.h")
    ci_workflow = read_workflow("co-beta-ci.yml")


    check("F4T_NPC_" not in npc_sync_header and "F4T_ENABLE_NPC_SYNC" not in npc_sync_header,
          "F4TNpcSync.h must carry no NPC feature macro, the NPC combat code is unconditional")
    check("F4T_NPC_" not in xmake and "co_test_features" not in xmake,
          "xmake.lua must define no NPC feature macro and no test-features switch")

    tick = function_body(combat, "void Tick()")
    engage = function_body(combat, "void EngageNearbyProxies(")
    engage_proxy = function_body(combat, "void EngageProxy(")
    heal = function_body(combat, "void RestoreProxyHealth()")
    puppet_gate = function_body(proxy_puppet, "bool IsNoAiActorFormID(")

    check("return;" not in tick.split("RestoreProxyHealth();")[0], "Combat::Tick must not return early when the actor guard is missing, so health restore and engagement still run")
    check(in_order(tick, "RestoreProxyHealth();", "EngageNearbyProxies(now);"), "Combat::Tick must restore proxy health before it engages")
    check("kEngageScanInterval" in tick and "constexpr auto kEngageScanInterval = 500ms;" in combat,
          "engagement must be scanned on a 500 ms interval")
    check(re.search(r"if \(isHost\) \{\s*a_runGameplay\(\[&\] \{\s*Combat::Tick\(\);", npc_sync_cpp) is not None,
          "Update must run Combat::Tick only while the local client is the scope authority")
    check("constexpr auto kEngageCooldown = 5s;" in combat, "the engage cooldown must be 5 s")
    check("constexpr float kEngageRadius = 2048.0F;" in combat, "the engage radius must be 2048 units")
    check("constexpr std::size_t kMaximumEngagesPerTick = 4;" in combat, "at most 4 engagements per tick")
    check(re.search(r"if \(!npc->GetHostileToActor\(proxy\.actor\)\) \{\s*continue;", engage) is not None,
          "an NPC must engage a proxy only when GetHostileToActor is true, through the skip if (!npc->GetHostileToActor(proxy.actor))")
    check(re.search(r"if \(!npc \|\| npc->IsDead\(false\)\) \{\s*continue;", engage) is not None and re.search(r"if \(npc->IsInCombat\(\)\)\s*\{\s*continue", engage) is None,
          "engagement must not skip NPCs that are already in combat, the engaged-pair set decides")
    check(re.search(r"const auto\* playerCell = player->GetParentCell\(\);\s*if \(!playerCell\) \{\s*return;", engage) is not None
          and re.search(r"if \(proxy\.actor->IsDead\(false\) \|\| proxy\.actor->GetParentCell\(\) != playerCell\) \{\s*continue;", engage) is not None
          and "IsSameLocation" not in engage,
          "engagement must skip proxies whose parent cell is not exactly the local player's parent cell, and must stop when the player has no cell")
    check("RE::PlayerCharacter::GetSingleton()" in engage, "engagement must read the local player for the scope check")
    check("npc->IsDead(false)" in engage and "proxy.actor->IsDead(false)" in engage,
          "engagement must skip dead NPCs and dead proxies")
    check(re.search(r"!\(distance < kEngageRadius\)", engage) is not None, "engagement must stop at the 2048 unit radius")
    check("a_now - last->second < kEngageCooldown" in engage and "g_lastEngageByPair[key] = a_now;" in engage,
          "engagement must honour the 5 s cooldown per (npcId, proxy player id) pair")
    check("kEngageCooldown" in engage and "std::erase_if(g_lastEngageByPair" in engage,
          "the cooldown map must be pruned of entries older than the cooldown")
    check(re.search(r"engaged >= kMaximumEngagesPerTick", engage) is not None, "engagement must stop at the per-tick limit")
    check("constexpr std::size_t kMaximumEngagedPairs = 1024;" in combat and "std::unordered_set<std::uint64_t> g_engagedPairs;" in combat,
          "the engaged-pair set must be a file-local unordered_set capped at 1024 pairs")
    check(re.search(r"if \(npc->IsInCombat\(\) && g_engagedPairs\.contains\(key\)\) \{\s*continue;", engage) is not None,
          "engagement must skip a pair already in the engaged-pair set while its NPC is in combat")
    check(re.search(r"if \(!npc->IsInCombat\(\)\) \{\s*std::erase_if\(g_engagedPairs", engage) is not None,
          "the engaged pairs of an NPC that left combat must be erased")
    check(in_order(engage, "EngageProxy(*npc, *proxy.actor);", "g_lastEngageByPair[key] = a_now;", "g_engagedPairs.insert(key);"),
          "the engaged pair must be inserted after EngageProxy")
    check(re.search(r"g_engagedPairs\.size\(\) > kMaximumEngagedPairs\) \{\s*g_engagedPairs\.clear\(\);", engage) is not None,
          "the engaged-pair set must be cleared once it exceeds 1024 pairs")
    reset_body = function_body(combat, "void Reset()")
    check("g_lastEngageByPair.clear();" in reset_body and "g_engagedPairs.clear();" in reset_body,
          "Combat::Reset must clear the cooldown map and the engaged-pair set together")
    check("Combat::Reset();" in function_body(npc_sync_cpp, "void Reset()") and "Combat::Reset();" in function_body(npc_sync_cpp, "void Update("),
          "NpcSync::Reset and the authority change in Update must call Combat::Reset")
    check("EngageProxy(*npc, *proxy.actor);" in engage, "engagement must call EngageProxy")
    check("Detail::GetActorHandle(a_proxy)" in engage_proxy and "Detail::GetActorHandle(a_npc)" in engage_proxy,
          "EngageProxy must use the proxy as aggressor and the NPC as target")
    check("hitData.healthDamage = 0.0F;" in engage_proxy and "a_npc.DoHitMe(std::addressof(hitData));" in engage_proxy,
          "EngageProxy must call DoHitMe with zero health damage")
    check("hitData.stagger = RE::STAGGER_MAGNITUDE::kNone;" in engage_proxy and "kSmall" not in engage_proxy,
          "EngageProxy must set stagger kNone so the engage hit does not stagger the NPC")
    check("missing = maximum - current" in heal, "the proxy heal must compute the missing health as maximum minus current")
    check(re.search(r"ModActorValue\(RE::ACTOR_VALUE_MODIFIER::kDamage, \*actorValues->health, missing\);", heal) is not None,
          "the proxy heal must apply a positive kDamage delta of the missing health")
    check("-missing" not in heal, "the proxy heal must never use a negative delta")
    check("!(missing > 0.0F)" in heal and "proxy.actor->IsDead(false)" in heal,
          "the proxy heal must skip dead proxies and proxies that are not missing health")
    check("GetPermanentActorValue(*actorValues->health)" in heal and "GetBaseActorValue(" not in heal,
          "the proxy heal maximum must come from the permanent health value, not the base value")
    check(re.search(r"if \(NpcSync::IsPuppetFormId\(a_formID\) \|\| NpcSync::IsSuppressedFormId\(a_formID\)\) \{", puppet_gate) is not None,
          "IsNoAiActorFormID must run UpdateNoAI for suppressed hidden originals as well as puppets")

    for name, source in (("combat", combat), ("capture", capture), ("coop", coop)):
        check("ScopedCoWrite" not in source, f"no ScopedCoWrite may appear in the NPC combat code ({name})")
    check("SetBaseActorValue" not in combat and "SetActorValue" not in combat, "combat code must not write base health")
    check("KillImpl" not in combat and "StartCombat" not in combat, "combat code must not kill or console-start combat")
    check(re.search(r"std::vector<ActiveProxyActor> GetActiveProxyActors\(\)", proxy_cpp) is not None,
          "GetActiveProxyActors must be defined")
    check("lifecycleState != ProxyLifecycleState::kActive" in function_body(proxy_cpp, "std::vector<ActiveProxyActor> GetActiveProxyActors()"),
          "GetActiveProxyActors must list only active proxies")

    check(re.search(r"bool SendNpcDamagePacket\(", networking_header) is not None,
          "SendNpcDamagePacket must be declared")
    capture_class = capture[capture.index("class NpcDamageCapture"):]
    sink = function_body(capture_class, "RE::BSEventNotifyControl ProcessEvent(")
    check("if (!localPlayer || !a_event.usesHitData) {" in sink, "the damage sink must accept only events that carry HitData")
    check("TryGetRemotePlayerIdForProxyFormId(hitTarget->GetFormID())" in sink, "the damage sink target must resolve to a proxy")
    check("TryGetHostNpcByActorFormId(aggressor->GetFormID())" in sink, "the damage sink aggressor must resolve to a synced host NPC")
    check("!std::isfinite(damage) || !(damage > 0.0F)" in sink, "the damage sink must refuse non-finite and non-positive damage")
    check("(std::min)(damage, NpcCoop::kMaximumNpcDamage)" in sink, "the damage sink must clamp damage to kMaximumNpcDamage")
    check("NpcActorGuard::IsInstalled()" not in sink, "the damage sink must not depend on the actor guard, so host NPC damage on proxies is still reported")
    check(in_order(sink, "TryGetRemotePlayerIdForProxyFormId(", "TryGetHostNpcByActorFormId(", "GetNpcAuthorityForScope(", "QueueNpcDamage("),
          "the damage sink must check the target, the aggressor and the authority before it queues")
    check("authority->playerId != *localPlayerId" in sink, "the damage sink must queue only when the local client is the authority")
    check(re.search(r"eventSource->RegisterSink\(&g_damageSink\);", capture) is not None,
          "the damage sink must be registered")
    publish_body = function_body(publish, "void SendPendingCombatHits(")
    check(re.search(r"for \(const auto& pending : F4T::NpcCombatCapture::GetAndClearPendingNpcDamage\(\)\)", publish_body) is not None
          and "a_runtime.Admits(pending.sessionGeneration)" in publish_body
          and "pending.connectionGeneration" in publish_body and "SendNpcDamagePacket(" in publish_body,
          "SendPendingCombatHits must reject stale session damage and bind sends to the originating connection")

    send = function_body(coop, "bool SendNpcDamagePacket(")
    check(in_order(send, "GetNpcAuthorityForScope(", "SendPacketBytesForGeneration("),
          "SendNpcDamagePacket must check the authority before it sends")
    check("IsInstalled()" not in send, "SendNpcDamagePacket must not depend on the actor guard, so damage reports are still sent")
    check("authority->playerId != *localPlayerId" in send and "authority->epoch != a_identity.authorityEpoch" in send,
          "SendNpcDamagePacket must send nothing unless the local client is the authority for the same epoch")
    check('{ "type", "npcDamage" }' in send, "SendNpcDamagePacket must send type npcDamage")
    wire_fields = set(re.findall(r'\{ "(\w+)",', send))
    required = {"type", "targetPlayerId", "npcId", "cellId", "worldspaceId", "sourceFormId", "authorityEpoch", "sequence", "damage"}
    check(required <= wire_fields, f"npcDamage must carry every contract field: missing {sorted(required - wire_fields)}")
    check('packet["weaponFormId"]' in send, "npcDamage must carry the optional weapon")

    handler = function_body(coop, "void HandleNpcDamagePacket(")
    check("RE::" not in handler and "GetFormByID" not in handler and "DoHitMe" not in handler,
          "HandleNpcDamagePacket runs on the receive thread and must not touch the engine")
    check("Apply" not in handler, "HandleNpcDamagePacket must only validate and queue, never apply")
    check("ScopedCoWrite" not in handler, "HandleNpcDamagePacket must not open a ScopedCoWrite")
    check("IsValidNpcDamage(*damage)" in handler and "*targetPlayerId != *localPlayerId" in handler,
          "HandleNpcDamagePacket must check the damage range and that the target is the local player")
    check(in_order(handler, "GetNpcAuthorityForScope(", "QueueIncomingNpcDamage("),
          "HandleNpcDamagePacket must check the sender is the scope authority before it queues")
    check("authority->playerId != *authorityId" in handler, "HandleNpcDamagePacket must refuse a sender that is not the scope authority")
    check(re.search(r'\} else if \(\*packetType == "npcDamage"\) \{\s*HandleNpcDamagePacket\(packet\);', inbound) is not None,
          "the npcDamage dispatch must be present")
    check(inbound.count('"npcDamage"') == 1, "the npcDamage dispatch must appear exactly once")

    queue_fn = function_body(remote, "bool QueueIncomingNpcDamage(")
    check("g_incomingNpcDamageHits.QueueHit(" in queue_fn and "a_npcId == 0" in queue_fn,
          "QueueIncomingNpcDamage must queue into the NPC tracker and refuse npc id 0")
    check("RE::" not in queue_fn and "Apply" not in queue_fn, "QueueIncomingNpcDamage must only queue")
    check("g_incomingCombatHits" not in queue_fn, "QueueIncomingNpcDamage must not touch the PvP tracker")
    check(remote.count("F4T::PvPCombatContract::InboundHitTracker g_incomingNpcDamageHits;") == 1
          and "F4T::PvPCombatContract::InboundHitTracker g_incomingCombatHits;" in remote,
          "the NPC damage path must use a second InboundHitTracker instance")
    check("std::mutex g_incomingNpcDamageMutex;" in remote, "the NPC damage tracker must have its own mutex")
    check("g_incomingNpcDamageHits = {}" in function_body(remote, "void ClearRemotePlayers()"),
          "ClearRemotePlayers must purge the NPC damage tracker")
    check("g_incomingNpcDamageHits.PurgeSender(a_playerId)" in function_body(remote, "bool RemoveRemotePlayer("),
          "RemoveRemotePlayer must purge the NPC damage tracker")
    pending = function_body(remote, "void ApplyPendingCombatHits()")
    check("g_incomingNpcDamageHits.pendingHits" in pending and "ApplyIncomingNpcDamage(hit)" in pending,
          "ApplyPendingCombatHits must drain the NPC damage tracker")
    apply_damage = function_body(remote, "void ApplyDamageToLocalPlayer(")
    check("ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, health, -appliedDamage)" in apply_damage,
          "ApplyDamageToLocalPlayer must keep the negative kDamage delta")
    check("kDamage, health, appliedDamage" not in apply_damage and "kDamage, a_health" not in remote,
          "no kDamage write may use a positive damage delta")
    check(in_order(apply_damage, "DoHitMe(", "SetBeenAttacked(true)", "ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, health, -appliedDamage)", "DamageActorValue Health"),
          "ApplyDamageToLocalPlayer must do feedback, then the damage modifier, then the console fallback")
    check("StartCombat" not in apply_damage, "ApplyDamageToLocalPlayer must not run player.StartCombat")
    npc_path = function_body(remote, "void ApplyIncomingNpcDamage(")
    check("StartCombat" not in npc_path, "the NPC damage path must not run player.StartCombat")
    check('ApplyDamageToLocalPlayer(attacker, a_hit.damage, std::format("npc damage npcId={} authorityPlayerId={}", a_hit.npcId, a_hit.senderId))' in npc_path,
          "the NPC damage path must apply through ApplyDamageToLocalPlayer with the npc id and authority player id in its log label")
    check('"npc hit"' not in remote, "the NPC damage label must not reuse the P2 npc hit label")
    pvp_path = function_body(remote, "void ApplyIncomingHit(")
    check("BoundClaimedDamage(" in pvp_path and in_order(pvp_path, "BoundClaimedDamage(", "ApplyDamageToLocalPlayer("),
          "the PvP path must bound the claimed damage before it applies it")
    check('std::format("network hit claimed={:.1f} fromPlayerId={}", a_hit.damage, a_hit.senderId)' in pvp_path,
          "the PvP path must log the claimed damage and the sender player id")
    check("StartCombat" not in remote, "F4TRemotePlayerState.cpp must not run player.StartCombat")

    for relative in ("src/F4TNpcCombatCapture.cpp", "src/F4TNetworkingNpcCoop.cpp", "src/F4TRemotePlayerState.cpp"):
        body = (PLUGIN / relative).read_text(encoding="utf-8")
        check("F4T_NPC_" not in body and "kCoTestFeatures" not in body, f"{relative} must carry no NPC feature gate")

    for source in ("F4TNpcSyncCombat.cpp",):
        lines = [line for line in combat_raw.splitlines() if "//" in line]
        check(all("// source:" in line for line in lines), f"{source} may carry only single-line // source: citations")

    check("scripts/run-python-tests.py" in ci_workflow and "--group plugin" in ci_workflow, "co-beta-ci.yml must run the plugin contract group")

    proxy_lookup = function_body(proxy_cpp, "std::optional<std::uint32_t> TryGetRemotePlayerIdForProxyFormId(")
    check("std::scoped_lock lock(g_proxyFormIdIndexLock)" in proxy_lookup and "g_proxyFormIdIndex" in proxy_lookup,
          "TryGetRemotePlayerIdForProxyFormId must look up g_proxyFormIdIndex under g_proxyFormIdIndexLock")
    check("g_proxySlots" not in proxy_lookup, "TryGetRemotePlayerIdForProxyFormId must not touch g_proxySlots")
    check(re.search(r"PublishActiveProxyFormIds\(\);\s*RebuildProxyFormIdIndex\(\);", function_body(proxy_cpp, "void Update()")) is not None,
          "Update must call RebuildProxyFormIdIndex after PublishActiveProxyFormIds")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TNpcCoopCombatContract: all checks passed.")

def _section_014_f4tnpccoophealthcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TNpcCoopHealthContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TNpcCoopHealthContract.py")
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


    def regex_in_order(text, *patterns):
        position = 0
        for pattern in patterns:
            found = re.compile(pattern, re.S).search(text, position)
            if not found:
                return False
            position = found.end()
        return True


    capture = read_code("src/F4TNpcCombatCapture.cpp")
    ranged = read_code("src/F4TRangedCombatCapture.cpp")
    coop = read_code("src/F4TNetworkingNpcCoop.cpp")
    health = read_code("src/F4TNpcSyncHealth.cpp")
    puppets = read_code("src/F4TNpcSyncPuppets.cpp")
    registry = read_code("src/F4TNpcSyncRegistry.cpp")
    inbound = read_code("src/F4TNetworkingInbound.cpp")
    publish = read_code("src/F4TReplicationRuntimePublish.cpp")
    networking_header = read_code("include/F4TNetworking.h")
    remote_state = read_code("src/F4TRemotePlayerState.cpp")
    remote_state_header = read_code("include/F4TRemotePlayerState.h")
    npc_sync_header = read_code("include/F4TNpcSync.h")
    xmake = (PLUGIN / "xmake.lua").read_text(encoding="utf-8")
    normalizer = strip_comments((ROOT / "co-core/src/validation/F4TAuthoritativePacketValidation.cpp").read_text(encoding="utf-8"))
    server_hit = strip_comments((ROOT / "CO-SERVER/server-cpp/src/ServerNpcHitImpl.inc").read_text(encoding="utf-8"))
    coop_hit_header = read_code("include/F4TNpcCoopHit.h")
    ranged_header = read_code("include/F4TRangedCombatCapture.h")
    host_sync = read_code("src/F4TNpcSyncHost.cpp")

    check("F4T_NPC_" not in capture and "F4T_ENABLE_NPC_SYNC" not in capture, "F4TNpcCombatCapture.cpp must carry no NPC feature macro")
    check(capture.count("RegisterSink(") == 2 and capture.count("eventSource->RegisterSink(&g_damageSink);") == 1,
          "the NPC hit sink and the NPC damage sink must be registered inside the capture file")
    ranged_init = function_body(ranged, "void Initialize()")
    check("F4T::NpcCombatCapture::Initialize();" in ranged_init, "RangedCombatCapture::Initialize must register the NPC sink")
    ranged_shutdown = function_body(ranged, "void Shutdown()")
    check("F4T::NpcCombatCapture::Shutdown();" in ranged_shutdown, "RangedCombatCapture::Shutdown must unregister the NPC sink")
    check("F4T_NPC_" not in npc_sync_header and "F4T_ENABLE_NPC_SYNC" not in npc_sync_header, "F4TNpcSync.h must carry no NPC feature macro")

    sink = function_body(capture, "RE::BSEventNotifyControl ProcessEvent(")
    check("!a_event.usesHitData" in sink, "the NPC sink must only trust events that carry HitData")
    check("aggressor.get() != localPlayer" in sink, "the NPC sink must only accept hits by the local player")
    check(in_order(sink, "TryGetPuppetByActorFormId(", "g_loggedGuardMissing.exchange(true)", "GetNpcAuthorityForScope(", "QueueNpcHit("),
          "the NPC sink must resolve the puppet, log once when the guard is absent, check the authority and only then queue the hit")
    guard_log = sink[sink.index("!NpcActorGuard::IsInstalled()"):sink.index("GetAssignedPlayerId()")]
    check("g_loggedGuardMissing.exchange(true)" in guard_log and "return " not in guard_log,
          "a missing actor guard must only log once and must not stop the NPC sink from queuing local hits")
    check("authority->playerId == *localPlayerId" in sink and sink.index("authority->playerId == *localPlayerId") < sink.index("QueueNpcHit("),
          "the NPC sink must not queue a hit when the local player is the authority")
    check("authority->epoch > (std::numeric_limits<std::uint32_t>::max)()" in sink, "the NPC sink must refuse an epoch the wire cannot carry")

    send = function_body(coop, "bool SendNpcHitPacket(")
    check(in_order(send, "GetNpcAuthorityForScope(", "SendPacketBytes("),
          "SendNpcHitPacket must check the authority before it sends")
    check("NpcActorGuard::IsInstalled()" not in send, "SendNpcHitPacket must not depend on the actor guard, so local hits are still sent")
    check("authority->playerId == *localPlayerId" in send and "authority->epoch != a_identity.authorityEpoch" in send,
          "SendNpcHitPacket must send nothing when the local player is the authority or the epoch moved")
    wire_fields = set(re.findall(r'\{ "(\w+)",', send))
    normalizer_body = function_body(normalizer, "std::optional<nlohmann::json> NormalizeNpcHit(")
    known = set(re.findall(r'a_packet\.find\("(\w+)"\)', normalizer_body))
    required = known - {"weaponFormId"}
    check(len(required) >= 8 and required <= wire_fields, f"npcHit must carry every field NormalizeNpcHit reads: missing {sorted(required - wire_fields)}")
    check(wire_fields - {"type"} <= known, f"npcHit carries fields NormalizeNpcHit does not know: {sorted(wire_fields - known)}")
    check('packet["weaponFormId"]' in send, "npcHit must carry the weapon when there is one")
    check('{ "type", "npcHit" }' in send, "SendNpcHitPacket must send type npcHit")
    check(re.search(r"bool SendNpcHitPacket\(", networking_header) is not None, "SendNpcHitPacket must be declared")

    publish_body = function_body(publish, "void SendPendingCombatHits(")
    check(re.search(r"if \(hit\.kind == F4T::RangedCombatCapture::HitKind::kNpc\) \{\s*if \(F4T::Networking::SendNpcHitPacket\(.*?continue;\s*\}", publish_body, re.S) is not None,
          "SendPendingCombatHits must route NPC hits to SendNpcHitPacket and never to combatHit")

    check("namespace F4T::NpcCoop" in coop_hit_header, "F4TNpcCoopHit.h must define the F4T::NpcCoop namespace")
    check(re.search(r"\bkNpc\b", ranged_header) is not None, "HitKind::kNpc must be declared")
    check("F4T::NpcCoop::NpcHitIdentity npc;" in ranged_header and "F4T::NpcCoop::NpcHitIdentity npc;" in ranged,
          "the npc member must be declared in the header and in the capture source")
    check("void QueueNpcHit(" in ranged_header, "QueueNpcHit must be declared")
    check(re.search(r"void QueueNpcHit\(.*?\}\s*void NoteLocalAttackAction\(", ranged, re.S) is not None,
          "QueueNpcHit must be defined")
    check(re.search(r"candidate\.kind,\s*candidate\.npc", ranged) is not None,
          "a combat hit must copy the npc identity")
    check('#include "F4TNpcSync.h"' in ranged_header, "F4TRangedCombatCapture.h must include F4TNpcSync.h so the gate macro is defined")
    receive = function_body(coop, "void HandleNpcHitPacket(")
    for needle in ("ReadPlayerId(a_packet, \"playerId\")", "*senderId == *localPlayerId", "IsValidNpcHitEpoch(*epoch)", "IsValidNpcHitDamage(*damage)",
                   "IsValidNpcHitScope(*cellId, worldspaceId)", "IsValidNpcSourceFormId(*sourceText, runtimeNpc)", "ParseHexFormId("):
        check(needle in receive, f"HandleNpcHitPacket must validate: {needle}")
    check("IsValidNpcHitDamage(*damage)" in receive and "EnqueueNpcHit(" in receive and receive.index("IsValidNpcHitDamage(*damage)") < receive.index("EnqueueNpcHit("),
          "HandleNpcHitPacket must validate before it enqueues")
    check("RE::" not in receive and "GetFormByID" not in receive, "HandleNpcHitPacket runs on the receive thread and must not touch the engine")
    dispatch = function_body(inbound, "void HandlePacketLine(")
    check(len(re.findall(r'\*packetType == "npcHit"', dispatch)) == 1 and
          re.search(r'\} else if \(\*packetType == "npcHit"\) \{\s*HandleNpcHitPacket\(packet\);', dispatch) is not None,
          "the npcHit dispatch branch must exist exactly once")

    apply_body = function_body(health, "HitRoute ApplyNpcHit(")
    check(regex_in_order(
        apply_body,
        r"GetNpcAuthorityForScope\(",
        r"RouteAuthority\(",
        r"TryGetProxyActorForRemotePlayerId\(a_claim\.senderPlayerId\)",
        r"TryGetHostActorFormIdByNpcId\(identity\.npcId\)",
        r"SourceMatchesActor\(identity\.sourceFormId, \*actorFormId\)",
        r"GetFormByID<RE::Actor>",
        r"IsDead\(false\)",
        r"BoundClaimedDamage\(a_claim\.senderPlayerId, a_claim\.damage\)",
        r"DoHitMe\(",
    ), "ApplyNpcHit must re-verify authority, epoch, sender proxy, host NPC, source id, liveness and damage before it writes")
    check("IsPuppetActor(" in apply_body and "IsRemotePlayerProxyActor(" in apply_body and "IsSameLocation(" in apply_body and "!Detail::IsSameLocation(" in apply_body,
          "ApplyNpcHit must refuse puppets, proxies and NPCs outside the local location")
    check("ScopedCoWrite" not in apply_body, "ApplyNpcHit must not open a ScopedCoWrite: KillImpl does not pass the 0x110 guard and the authority-side target is never a puppet")
    check("ScopedCoKill" not in apply_body, "ApplyNpcHit must not open a ScopedCoKill: the 0x117 guard only holds puppets, and the authority-side target is never a puppet")
    check("IsDead(true)" not in apply_body, "ApplyNpcHit must refuse essential-down targets: IsDead(true) is false at kEssentialDown")
    check("hitData.healthDamage = 0.0F" in apply_body and "hitData.aggressor = Detail::GetActorHandle(*attacker)" in apply_body,
          "DoHitMe feedback must carry the attacker proxy and zero health damage")
    check("ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValues->health, -applied)" in apply_body and "NpcSyncPolicy::DamageAboveFloor(current, damage, kMinAppliedHealth)" in apply_body,
          "every hit must lower health with a negative kDamage delta, clamped to the health floor for protected NPCs")
    check("SetBaseActorValue" not in apply_body and "SetActorValue" not in apply_body, "ApplyNpcHit must not write base health")
    check(re.search(r"if \(current - damage <= 0\.0F && !protectedActor\) \{\s*actor->KillImpl\(attacker, damage, true, false\);\s*\} else if \(applied > 0\.0F\) \{\s*actor->ModActorValue\(", apply_body) is not None,
          "a lethal hit on an NPC that is neither essential nor protected must call KillImpl with the attacker proxy; other hits must lower health through ModActorValue")
    check("F4T::NpcSync::ResolveStableBase(*actor)" in apply_body and "GetNPC()" not in apply_body,
          "ApplyNpcHit must resolve the essential and protected check through ResolveStableBase")
    check("boolFlags" in apply_body and "kEssential" in apply_body and "kProtected" in apply_body,
          "ApplyNpcHit must read the runtime boolFlags kEssential and kProtected, the bits KillImpl's can-be-killed test reads")
    check("auto* attacker = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(" in apply_body,
          "the KillImpl attacker must be the sender's proxy actor")
    tick = function_body(health, "void Tick()")
    check("ApplyPendingNpcHits();" in tick, "Health::Tick must apply pending NPC hits")
    check(len(re.findall(r"ApplyPendingNpcHits\(\)\s*;", health)) == 1, "ApplyPendingNpcHits must be called exactly once, from Health::Tick")
    enqueue = function_body(health, "bool EnqueueNpcHit(")
    check("Apply" not in enqueue and "RE::" not in enqueue and "GetFormByID" not in enqueue, "EnqueueNpcHit runs on the receive thread and must only queue the claim")
    pending = function_body(health, "void ApplyPendingNpcHits()")
    check("kMaximumNpcHitAge" in pending and "applied >= kMaximumNpcHitsPerTick" in pending, "pending NPC hits must be age- and rate-bounded")
    check("claims.swap(g_pendingHits)" in pending and "g_pendingHits.size() + deferred.size() > kMaximumPendingNpcHits" in pending and "g_pendingHits.push_front(" in pending,
          "pending NPC hits must be scanned in arrival order, keep deferred hits ahead of newer ones, and stay within the queue bound")
    check(regex_in_order(pending, r"KeepPendingHit\(NpcSyncPolicy::HitRoute::kRetry, age, kMaximumNpcHitAge\)", r"REX::WARN\(", r"continue;", r"applied >= kMaximumNpcHitsPerTick", r"ApplyNpcHit\("),
          "expired NPC hits must be logged and dropped before deferral or application")
    check(re.search(r"route == NpcSyncPolicy::HitRoute::kRetry\) \{\s*deferred\.push_back", pending) is not None,
          "a fresh NPC hit that cannot be applied yet must be deferred")
    check(re.search(r"route == NpcSyncPolicy::HitRoute::kDrop\) \{\s*REX::WARN\(", apply_body) is not None, "an authority or epoch mismatch must be dropped with a visible WARN")
    check("kMaximumPendingNpcHits" in function_body(health, "bool EnqueueNpcHit("), "the NPC hit queue must be bounded")

    death = function_body(puppets, "bool KillSyncedNpc(")
    check("ScopedCoWrite" not in death and "KillImpl(" in death, "ApplyPuppetDeath must not open a ScopedCoWrite: KillImpl does not pass the 0x110 guard")
    check("ScopedCoKill" in death, "ApplyPuppetDeath must open a ScopedCoKill around KillImpl: the 0x117 guard holds the kill")
    host_capture = function_body(host_sync, "std::vector<NpcState> CaptureHostSnapshot(")
    check("FormatFormId(cell->GetFormID())" not in host_capture and "cell->worldSpace" not in host_capture,
          "CaptureHostSnapshot must not stamp the NPC's own cell: every NpcState carries the authority scope")
    check("std::string{ a_authorityCellId }" in host_capture and "std::string{ a_authorityWorldspaceId }" in host_capture,
          "CaptureHostSnapshot must stamp the authority cell and worldspace on every NpcState")
    check("CaptureHostSnapshot(a_player, a_authorityCellId, a_authorityWorldspaceId)" in function_body(host_sync, "void UpdateHost("),
          "UpdateHost must pass the authority scope into CaptureHostSnapshot")

    publish_snapshot = function_body(registry, "void PublishRegistrySnapshot()")
    check(re.match(r"\{\s*if \(g_publishedGeneration == g_registryGeneration\) \{\s*return;\s*\}", publish_snapshot) is not None,
          "PublishRegistrySnapshot must return early when no registry input changed")
    for relative, minimum in (("src/F4TNpcSyncPuppets.cpp", 4), ("src/F4TNpcSyncSuppress.cpp", 3), ("src/F4TNpcSyncHost.cpp", 2)):
        count = read_code(relative).count("MarkRegistryDirty()")
        check(count >= minimum, f"{relative} must mark the registry dirty at every change site: {count} < {minimum}")

    check("float BoundClaimedDamage(std::uint32_t a_senderId, float a_claimedDamage);" in remote_state_header, "BoundClaimedDamage must be exported")
    check(re.search(r"namespace F4T::RemotePlayerState\s*\{.*float BoundClaimedDamage\(", remote_state, re.S) is not None, "BoundClaimedDamage must be defined in F4T::RemotePlayerState")

    check(re.search(r"storedHealth && !\(\*storedHealth > 0\.0\)", server_hit) is not None, "the server must reject a hit on an NPC whose stored health is not positive")
    check(re.search(r"storedMaxHealth && !\(claimedDamage <= \*storedMaxHealth \* 4\.0\)", server_hit) is not None, "the server must reject damage above four times the stored maximum health")

    check("F4T_NPC_" not in xmake and "co_test_features" not in xmake, "xmake.lua must define no NPC feature macro and no test-features switch")

    for source in sorted((PLUGIN / "src").rglob("*.cpp")) + sorted((PLUGIN / "include").rglob("*.h")):
        code = strip_comments(source.read_text(encoding="utf-8"))
        uses = len(re.findall(r"NpcActorGuard::Install\(\)", code))
        statements = len(re.findall(r"(?:^|[;{}])\s*(?:F4T::)?NpcActorGuard::Install\(\);", code, re.M))
        check(uses == statements, f"{source.name}: nothing may depend on the value NpcActorGuard::Install returns; IsInstalled() is the API")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TNpcCoopHealthContract: all checks passed.")

def _section_015_f4tnpclootcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TNpcLootContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TNpcLootContract.py")
    from pathlib import Path
    import re
    import sys

    PLUGIN = Path(__file__).resolve().parents[1]
    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(relative):
        return (PLUGIN / relative).read_text(encoding="utf-8")


    def body(text, signature):
        start = text.find(signature)
        if start < 0:
            return ""
        start = text.find("{", start)
        depth = 1
        end = start + 1
        while end < len(text) and depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        return text[start:end]


    xmake = read("xmake.lua")
    check("F4T_NPC_" not in xmake and "co_test_features" not in xmake, "xmake.lua must define no loot switch")
    header = read("include/F4TNpcSync.h")
    check("F4T_NPC_" not in header and "F4T_CO_TEST_FEATURES" not in header, "F4TNpcSync.h must carry no loot feature macro")
    for part in ("Capture", "Mirror", "Take"):
        name = f"src/F4TNpcSyncLoot{part}.cpp"
        check(f'remove_files("{name}")' not in xmake, f"{name} must build in every target")
    for path in (PLUGIN / "src").glob("F4TNpcSyncLoot*.cpp"):
        text = path.read_text(encoding="utf-8")
        check("GetNPC()" not in text and "IsDead(false)" not in text, f"{path.name}: stable identity/death rules")

    loot = read("src/F4TNpcSyncLoot.cpp")
    capture = read("src/F4TNpcSyncLootCapture.cpp")
    mirror = read("src/F4TNpcSyncLootMirror.cpp")
    take = read("src/F4TNpcSyncLootTake.cpp")
    internal = read("include/F4TNpcSyncLootInternal.h")

    sink = body(capture, "ProcessEvent(")
    check(bool(sink), "capture: ContainerChangeSink::ProcessEvent not found")
    check("RemoveItem" not in sink and "AddItem" not in sink, "capture sink must never call RemoveItem/AddItem")
    check(sink.count("scoped_lock") == 1 and "g_pendingMutex" in sink, "capture sink must take only its own mutex")
    check("itemCount <= 0" in sink, "capture sink must skip a non-positive itemCount")
    check("overflow = true" in sink, "capture sink must raise the overflow flag instead of dropping events")
    check("contains(" in sink, "capture sink must coalesce repeated container events")
    check(body(capture, "void CaptureTick(").count("changes.overflow") == 1 and
          "g_hostNpcIdsByActorFormId" in body(capture, "void CaptureTick("),
          "CaptureTick must rescan the tracked containers when the sink overflowed")

    removal = body(loot, "std::uint32_t RemoveLootFromContainer(")
    check(bool(removal), "loot: RemoveLootFromContainer not found")
    check("plan.rbegin()" in removal and "plan.rend()" in removal, "removal must apply the plan in descending stack-index order")
    check("plan.begin()" not in removal, "removal must not apply the plan in ascending stack-index order")
    check("BSAutoReadLock" in removal.split("RemoveItem(")[0], "removal must not hold the read lock across RemoveItem")
    check(removal.count("CountLootItem(") == 2 and "before - after" in removal,
          "removal must measure the removed count from before/after totals")
    check("removed += " not in removal, "removal must not add up the planned counts")

    grant = body(take, "NpcLootSync::TakeResult GrantTake(")
    check(bool(grant), "take: GrantTake not found")
    check("a_itemId, removed, true" in grant, "GrantTake must return the measured removed count")
    check("Expect(a_container.GetFormID(), a_itemId, granted" not in grant, "GrantTake must not expect the planned count")
    check("a_itemId, removed, NowMilliseconds()" in grant, "GrantTake must record the measured count in the echo ledger")
    check(grant.find("RemoveLootFromContainer(") < grant.find("Ledger().Expect("),
          "GrantTake must measure the removal before recording the ledger expectation")

    mirror_tick = body(mirror, "void MirrorTick(")
    check(bool(mirror_tick), "mirror: MirrorTick not found")
    check("void MirrorTick(const AuthorityScope& a_scope)" in internal, "MirrorTick must receive the authority scope")
    check("Detail::MirrorTick(scope)" in loot, "Loot::Tick must pass the scope to MirrorTick")
    check("TryGetPuppetByActorFormId" in mirror_tick and "puppet->sourceFormId" in mirror_tick,
          "mirror must resolve the container actor's puppet identity")
    check("a_scope.epoch" in mirror_tick and "loot.identity.authorityEpoch" in mirror_tick,
          "mirror must reject a packet from another authority epoch")
    check("loot.authorityPlayerId" in mirror_tick and "GetNpcAuthorityForScope" in mirror_tick,
          "mirror must reject a packet from another authority")

    reset_queues = body(loot, "void ResetQueues()")
    check(bool(reset_queues), "loot: ResetQueues not found")
    check("Detail::ResetQueues()" in body(loot, "void Reset()"), "Loot::Reset must clear the transport queues")
    for queue in ("g_inboundLoot", "g_inboundTakes", "g_inboundResults", "g_outboundLoot", "g_outboundResults", "g_ledger"):
        check(f"{queue}.clear()" in reset_queues or f"{queue} = " in reset_queues, f"ResetQueues must reset {queue}")
    check("g_inboundAcks" not in reset_queues, "ResetQueues must keep queued acks so an expired ack still undoes a grant after Reset")

    def squash(text):
        return " ".join(text.split())


    net_transport = read("src/F4TNetworkingNpcLoot.cpp")
    net_inbound = read("src/F4TNetworkingInbound.cpp")
    net_internal = read("src/F4TNetworkingInternal.h")
    net_public = read("include/F4TNetworking.h")

    check('remove_files("src/F4TNetworkingNpcLoot.cpp")' not in xmake, "src/F4TNetworkingNpcLoot.cpp must build in every target")
    for declaration in ("bool SendNpcLootPacket(", "bool SendNpcLootTakePacket(", "bool SendNpcLootResultPacket(",
                        "void FlushNpcLootOutbound()"):
        check(declaration in net_public, f"include/F4TNetworking.h must declare {declaration[:-1]}")
        check(declaration in net_transport, f"src/F4TNetworkingNpcLoot.cpp must define {declaration[:-1]}")
    for handler, packet_type in (("HandleNpcLootPacket", "npcLoot"),
                                 ("HandleNpcLootTakePacket", "npcLootTake"),
                                 ("HandleNpcLootResultPacket", "npcLootResult"),
                                 ("HandleNpcLootAckPacket", "npcLootAck")):
        check(f"void {handler}(const Json& a_packet);" in net_internal,
              f"F4TNetworkingInternal.h must declare {handler}")
        check(f"void {handler}(const Json& a_packet)" in net_transport,
              f"F4TNetworkingNpcLoot.cpp must define {handler}")
        check(f'*packetType == "{packet_type}"' in net_inbound,
              f"F4TNetworkingInbound.cpp must dispatch {packet_type}")
        check(f"{handler}(packet);" in net_inbound, f"F4TNetworkingInbound.cpp must call {handler}")

    flush = body(net_transport, "void FlushNpcLootOutbound()")
    check("DrainOutboundLoot()" in flush and "SendNpcLootPacket(" in flush,
          "flush must send every queued loot packet")
    check("DrainOutboundTakeResults()" in flush and "SendNpcLootResultPacket(" in flush,
          "flush must send every queued take result")

    loot_tick = body(loot, "void Tick(")
    check("Networking::FlushNpcLootOutbound()" in loot_tick, "Loot::Tick must flush the outbound transport")
    check(loot_tick.find("Detail::CaptureTick(scope)") < loot_tick.find("Networking::FlushNpcLootOutbound()") and
          loot_tick.find("Detail::MirrorTick(scope)") < loot_tick.find("Networking::FlushNpcLootOutbound()") and
          loot_tick.find("Detail::TakeTick(scope, a_runGameplay)") < loot_tick.find("Networking::FlushNpcLootOutbound()"),
          "Loot::Tick must flush after the capture, mirror and take ticks")
    check("Detail::CaptureTick(scope); Detail::TakeTick(scope, a_runGameplay); if (!a_isHost) { Detail::MirrorTick(scope); }" in squash(loot_tick),
          "Loot::Tick must run TakeTick on both roles, outside the host branch, and MirrorTick on the non-host path only")

    enqueue = body(loot, "bool EnqueueOutboundLoot(")
    check(bool(enqueue), "loot: EnqueueOutboundLoot not found")
    check(enqueue.find("kMaximumQueuedPackets") < enqueue.find("Revisions().Next("),
          "EnqueueOutboundLoot must reject a full queue before taking a revision")
    check(enqueue.find("push_back") < enqueue.find("Revisions().Next("),
          "EnqueueOutboundLoot must take the revision only after a successful enqueue")
    check("CountAvailableLoot" not in loot, "loot must keep a single container count helper")

    capture_tick = body(capture, "void CaptureTick(")
    check(squash("if (!EnqueueOutboundLoot(std::move(loot))) { ++entry; continue; }") in squash(capture_tick),
          "CaptureTick must keep the container dirty when the enqueue fails")
    check("IsLootableContainer(" in capture_tick and "IsContainerInScope(" in capture_tick,
          "CaptureTick must publish only a dead, in-scope container")
    check("IsLootableContainer(" in mirror_tick and "IsContainerInScope(" in mirror_tick,
          "MirrorTick must apply loot only to a dead, in-scope container")
    check("std::numeric_limits<std::uint32_t>::max" in mirror_tick and "a_scope.epoch == 0" in mirror_tick,
          "MirrorTick must reject epoch 0 and an epoch above the uint32 maximum")
    check(mirror_tick.find("loots.empty()") < mirror_tick.find("GetNpcAuthorityForScope"),
          "MirrorTick must skip the authority lookup when the inbound queue is empty")

    root = PLUGIN.parents[1]
    core_sync = (root / "co-core/src/npc/F4TNpcLootSync.cpp").read_text(encoding="utf-8")
    identity = body(core_sync, "Json IdentityToJson(")
    check(bool(identity), "co-core: IdentityToJson not found")
    check("a_identity.worldspaceId == 0 ? std::string{} : FormatHex(a_identity.worldspaceId)" in squash(identity),
          "interior loot must publish an empty worldspace id, not the hex of zero")

    worldspace_helper = body(net_transport, "std::string FormatScopeWorldspaceId(")
    check(bool(worldspace_helper) and
          "a_worldspaceFormId == 0 ? std::string{} : FormatScopeFormId(a_worldspaceFormId)" in squash(worldspace_helper),
          "transport must format a zero worldspace scope id as an empty string")
    check(net_transport.count("FormatScopeWorldspaceId(identity.worldspaceId)") == 2,
          "npcLoot and npcLootTake must both use the empty-aware worldspace formatter")
    check("IsValidLootScope(cellId, worldspaceId)" in net_transport,
          "the scope checks must use the same worldspace string as the authority lookup")

    send_loot = body(net_transport, "bool SendNpcLootPacket(")
    check(bool(send_loot), "transport: SendNpcLootPacket not found")
    check(send_loot.count("*a_retryable = true;") == 2,
          "only the not-connected and send-failed paths may ask the capture for a retry")
    check("bool* a_retryable = nullptr" in net_public, "the send must report whether a failure is transient")
    check("MarkLootDirty(" in flush and "retryable" in flush,
          "flush must re-mark the container dirty when a transient send fails")
    check("void MarkLootDirty(std::uint32_t a_containerFormId);" in internal,
          "F4TNpcSyncLootInternal.h must declare MarkLootDirty")
    mark_dirty = body(capture, "void MarkLootDirty(")
    check(bool(mark_dirty) and "NpcIdForLootContainer(" in mark_dirty and "g_dirtyContainers.try_emplace(" in mark_dirty,
          "MarkLootDirty must only re-mark a container that is still tracked")

    check(squash("if (!IsLootableContainer(*container)) { ++entry; continue; }") in squash(capture_tick),
          "CaptureTick must keep a tracked container dirty while it is not lootable yet")
    check("g_publishedContainers.insert(" in capture_tick and
          "g_publishedContainers.contains(" in capture_tick,
          "CaptureTick must publish a tracked container once when it first becomes lootable")
    check("g_publishedContainers.clear()" in body(capture, "void ResetCapture()"),
          "ResetCapture must clear the published-once set")

    check("g_pendingLoot" in mirror_tick and "kPendingLootLifetimeTicks" in mirror_tick,
          "MirrorTick must retry loot that arrived before the puppet was lootable")
    check("g_pendingLoot[loot.identity.npcId] = PendingLoot{" in mirror_tick,
          "MirrorTick must keep only the latest packet per npc id")
    check("g_pendingLoot.clear()" in body(mirror, "void ResetMirror()"),
          "ResetMirror must clear the pending loot")

    check("RE::PlayerCharacter::GetSingleton()" in sink and "g_pendingChanges.takes" in sink,
          "capture sink must record takes by the local player only")
    check("SendTake(" in body(capture, "void CaptureTick(") and "changes.takes" in body(capture, "void CaptureTick("),
          "CaptureTick must send the recorded takes on the non-host path")
    check("void SendTake(" in internal, "F4TNpcSyncLootInternal.h must declare SendTake")
    send_take = body(take, "void SendTake(")
    check(bool(send_take) and "Networking::SendNpcLootTakePacket(" in send_take and "g_pendingTakes[operationId]" in send_take,
          "SendTake must send the take and remember the operation")
    roll_back = body(take, "std::uint32_t RollBackTake(")
    check(bool(roll_back) and "RemoveLootFromContainer(*player" in roll_back,
          "RollBackTake must remove the rolled-back item from the local player")
    tick_take = body(take, "void TakeTick(")
    grant_gate = body(tick_take, "a_runGameplay(")
    check("result = GrantTake(" in grant_gate and 'expired ? "expired" : "unavailable", 0)' in tick_take and "TakeRequestExpired(NowMilliseconds(), queued.receivedMs, expiresInMs)" in tick_take,
          "host mutation must run through gameplay admission with an unavailable fallback")
    check(tick_take.index("g_results.Find(") < tick_take.index("a_runGameplay(") < tick_take.index("g_results.Remember("),
          "cached results must remain available and denied results must be retained")
    resolve = body(take, "void ResolvePendingTake(")
    check(bool(resolve) and "UnmatchedTakeCount(operation, a_result)" in resolve and "a_reason, notGranted);" in resolve and "g_pendingTakes.erase(pending)" in resolve and "a_result.reason == \"expired\"" in resolve,
          "ResolvePendingTake must drop the matched take and roll back its unfilled part")
    check("ResolvePendingTake(result" in tick_take and "ResolvePendingTake(result" in body(take, "void ResetTake()"),
          "TakeTick and ResetTake must both resolve drained results through ResolvePendingTake")
    unmatched = body(take, "std::uint32_t UnmatchedTakeCount(")
    check("itemId != a_operation.itemId" in unmatched and "return a_operation.count;" in unmatched and
          "ClampToAvailable(a_operation.count, count)" in unmatched,
          "UnmatchedTakeCount must refund the full count on reject or item mismatch and the shortfall on partial accept")
    check('"unresolved", false);' in tick_take and "now - entry->second.timestampMs > kGuestUnresolvedMilliseconds" in tick_take and
          "entry = g_pendingTakes.erase(entry)" in tick_take,
          "TakeTick must roll back and drop guest takes that never resolve")
    check("kGuestUnresolvedMilliseconds = 15000;" in take,
          "guest takes must wait 15 s for a result, longer than the server 5 s take lifetime")
    roll_back = body(take, "std::uint32_t RollBackTake(")
    check(bool(roll_back) and "RemoveLootFromContainer(*player" in roll_back and "AddLootToContainer(*container" in roll_back and "a_restoreToContainer" in roll_back and
          "GetParentCell()" in roll_back,
          "RollBackTake must remove from the player, restore the puppet container, and skip when the player has no cell")
    deferred_rollback = body(take, "struct DeferredRollback")
    check("bool restoreToContainer{};" in deferred_rollback and "deferred.restoreToContainer" in body(take, "void AttemptDeferredRollbacks("),
          "DeferredRollback must carry restoreToContainer and replay it, so a deferred host-accepted take is not put back on the container")
    check("RolledBack" not in take and "ReapplyTake" not in take and "removedFromPlayer" not in take,
          "a rolled-back take must not be tracked for a late-grant reapply")
    check(send_take.count("RollBackTake(") >= 3 and '"refused", true)' in send_take and '"send failed", true)' in send_take,
          "SendTake must roll back every refusal after the puppet is confirmed")
    check("PendingTake{ a_containerFormId," in send_take, "a pending take must remember the container it came from")
    check("SessionNonce()" in send_take and 'std::format("{:08X}-{}"' in send_take,
          "operation ids must carry the per-load session nonce")
    check("static const std::uint32_t nonce = std::random_device{}();" in take,
          "the session nonce must be drawn once per plugin load")
    reset_take = body(take, "void ResetTake()")
    check("DrainTakeResults()" in reset_take and "ResolvePendingTake(result" in reset_take,
          "ResetTake must honour queued results before rolling back")
    check("RollBackTake(" not in reset_take and "g_pendingTakes.clear()" not in reset_take,
          "ResetTake must leave pending takes to resolve or expire, not roll them back or clear them")
    capture_reset = body(capture, "void ResetCapture()")
    check("changes.takes" in capture_reset and "\"capture reset\", true)" in squash(capture_reset) and "g_nonHostTick" in capture_reset,
          "ResetCapture must roll back unsent takes on a non-host tick")
    check("scoped_lock" not in capture_reset,
          "ResetCapture must not hold the sink mutex while rolling back, because engine events re-enter the sink")
    check("void AddLootToContainer(RE::TESObjectREFR& a_container, std::uint32_t a_itemId, std::uint32_t a_count);" in internal,
          "F4TNpcSyncLootInternal.h must declare AddLootToContainer")
    check("std::uint32_t RollBackTake(std::uint32_t a_containerFormId" in internal, "F4TNpcSyncLootInternal.h must declare RollBackTake")
    check("struct QueuedTake" in internal and "bool EnqueueTakeAck(NpcLootSync::TakeAck a_ack);" in internal and
          "std::vector<QueuedTake> DrainTakeRequests();" in internal and "std::vector<NpcLootSync::TakeAck> DrainTakeAcks();" in internal,
          "F4TNpcSyncLootInternal.h must declare the take request and ack queues")
    enqueue_ack = body(loot, "bool EnqueueTakeAck(")
    check(bool(enqueue_ack) and "kMaximumQueuedPackets" in enqueue_ack, "EnqueueTakeAck must reject a full queue")
    check("QueuedTake{ std::move(a_request), NowMilliseconds() }" in loot, "EnqueueTakeRequest must stamp the receive time")
    check("bool TakeRequestExpired(" in core_sync, "co-core: TakeRequestExpired not found")
    check("PublishContainerLoot(a_container, a_identity, a_authorityPlayerId)" in grant, "GrantTake must publish the container after a grant")
    publish = body(take, "std::uint32_t PublishContainerLoot(")
    check(bool(publish) and "return revision;" in publish, "PublishContainerLoot must return the revision it published")
    undo_grant = body(take, "void UndoGrant(")
    check("AddLootToContainer(" in undo_grant and "PublishContainerLoot(" in undo_grant, "UndoGrant must restore the host container and publish it")
    grant_ack = body(take, "void ResolveGrantAck(")
    check("UndoGrant(" in grant_ack and "TakeAckState::Expired" in grant_ack, "an expired grant ack must undo the host grant")
    check("DrainTakeAcks()" in tick_take and "ResolveGrantAck(ack, a_scope, a_runGameplay)" in tick_take and "g_grantsAwaitingAck" in tick_take and
          "kGrantAckTimeoutMilliseconds" in tick_take,
          "TakeTick must resolve grant acks and expire grants that never get one")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)
    print("F4TNpcLootContract: all checks passed.")

def _section_016_f4tnpcsuppressgracecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TNpcSuppressGraceContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TNpcSuppressGraceContract.py")
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

    check("inline constexpr std::chrono::milliseconds kMissingNpcGrace{ 750 };" in internal,
          "the release grace must be one shared constant in F4TNpcSyncInternal.h")
    check("constexpr auto kMissingNpcGrace" not in puppets,
          "F4TNpcSyncPuppets.cpp must not keep its own grace constant")
    check("kMissingNpcGrace" in puppets, "puppet removal must keep using the shared grace")
    check("Clock::time_point missingSince{};" in internal,
          "SuppressedActor must carry missingSince so release waits out the grace")
    check("bool HasLocalFollowerMatch(const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors);" in internal,
          "F4TNpcSyncInternal.h must declare HasLocalFollowerMatch")

    follower = function_body(suppress, "bool IsLocalPlayerFollower(")
    check("niFlags.flags & kPlayerTeammateFlag" in follower and "1U << 26" in follower
          and follower.find("kPlayerTeammateFlag") < follower.find("commandingActor"),
          "IsLocalPlayerFollower must accept the player-teammate flag (niFlags bit 26) before the commandingActor test")
    check("currentProcess->middleHigh" in follower and "commandingActor.get().get()" in follower
          and "std::addressof(a_player)" in follower,
          "IsLocalPlayerFollower must compare currentProcess->middleHigh->commandingActor with the local player")
    for signature in ("RE::Actor* FindMatchingSourceActor(", "bool HasMatchingAliveLocalNpc(", "void SuppressDuplicateLocalNpcs("):
        check("IsLocalPlayerFollower(" in function_body(suppress, signature),
              f"{signature} must exclude local followers")

    follower_match = function_body(suppress, "bool HasLocalFollowerMatch(")
    check("IsLocalPlayerFollower(" in follower_match and "StableBaseFormId(" in follower_match,
          "HasLocalFollowerMatch must match a local follower by source and base identity")
    update_puppets = function_body(puppets, "void UpdatePuppets(")
    check(0 <= update_puppets.find("HasLocalFollowerMatch(state, a_player, loadedActors)") < update_puppets.find("states.emplace("),
          "UpdatePuppets must drop local-follower states before any puppet is kept or spawned")

    suppress_body = function_body(suppress, "void SuppressDuplicateLocalNpcs(")
    check("suppressed.missingSince = Clock::now();" in suppress_body,
          "the first missing tick must start the grace timer")
    grace = suppress_body.find("Clock::now() - suppressed.missingSince < kMissingNpcGrace")
    restore = suppress_body.find("SetAlpha(suppressed.originalAlpha)")
    check(0 <= grace < restore,
          "the restore must sit behind the grace check so a one-tick gap never reveals the NPC")
    hold = suppress_body[grace:restore]
    check(0 <= hold.find("GetAlpha() != 0.0F") < hold.find("SetAlpha(0.0F)"),
          "the grace window must re-apply the hidden alpha, and only when GetAlpha differs from 0")
    check(0 <= suppress_body.find("suppressed.missingSince = Clock::now();") < grace,
          "the first missing tick must enter the grace check")
    check("suppressed.missingSince = {};" in suppress_body,
          "a matched actor must reset its grace timer")
    check("suppressed.handle.get()" in suppress_body and "suppressIfDuplicate(*actor)" in suppress_body,
          "suppressed actors must be re-matched through their handle, not only through process-list visits")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TNpcSuppressGraceContract: all checks passed.")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TNpcCoopCombatContract.py", _section_013_f4tnpccoopcombatcontract),
    ("CO-BETA/plugin/tests/F4TNpcCoopHealthContract.py", _section_014_f4tnpccoophealthcontract),
    ("CO-BETA/plugin/tests/F4TNpcLootContract.py", _section_015_f4tnpclootcontract),
    ("CO-BETA/plugin/tests/F4TNpcSuppressGraceContract.py", _section_016_f4tnpcsuppressgracecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TNpcCombatContracts_part3: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
