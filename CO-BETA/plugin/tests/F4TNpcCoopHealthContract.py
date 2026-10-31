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
check(in_order(sink, "TryGetPuppetByActorFormId(", "IsInstalled()", "GetNpcAuthorityForScope(", "QueueNpcHit("),
      "the NPC sink must resolve the puppet, check the guard, check the authority and only then queue the hit")
if "IsInstalled()" in sink and "GetNpcAuthorityForScope(" in sink:
    mirror = sink[sink.index("IsInstalled()"):sink.index("GetNpcAuthorityForScope(")]
    check("!NpcActorGuard::IsInstalled()" in sink and "return RE::BSEventNotifyControl::kContinue;" in mirror and "QueueNpcHit(" not in mirror,
          "without the actor guard the NPC sink must queue nothing (mirror-only)")
    check("g_loggedMirrorOnly.exchange(true)" in mirror, "the mirror-only degrade must log once")
check("authority->playerId == *localPlayerId" in sink and sink.index("authority->playerId == *localPlayerId") < sink.index("QueueNpcHit("),
      "the NPC sink must not queue a hit when the local player is the authority")
check("authority->epoch > (std::numeric_limits<std::uint32_t>::max)()" in sink, "the NPC sink must refuse an epoch the wire cannot carry")

send = function_body(coop, "bool SendNpcHitPacket(")
check(in_order(send, "!F4T::NpcActorGuard::IsInstalled()", "GetNpcAuthorityForScope(", "SendPacketBytes("),
      "SendNpcHitPacket must check the guard and the authority before it sends")
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
               "IsValidNpcHitScope(*cellId, worldspaceId)", "IsRuntimeFormId(*sourceFormId)", "ParseHexFormId("):
    check(needle in receive, f"HandleNpcHitPacket must validate: {needle}")
check("IsValidNpcHitDamage(*damage)" in receive and "EnqueueNpcHit(" in receive and receive.index("IsValidNpcHitDamage(*damage)") < receive.index("EnqueueNpcHit("),
      "HandleNpcHitPacket must validate before it enqueues")
check("RE::" not in receive and "GetFormByID" not in receive, "HandleNpcHitPacket runs on the receive thread and must not touch the engine")
dispatch = function_body(inbound, "void HandlePacketLine(")
check(len(re.findall(r'\*packetType == "npcHit"', dispatch)) == 1 and
      re.search(r'\} else if \(\*packetType == "npcHit"\) \{\s*HandleNpcHitPacket\(packet\);', dispatch) is not None,
      "the npcHit dispatch branch must exist exactly once")

apply_body = function_body(health, "void ApplyNpcHit(")
check(regex_in_order(
    apply_body,
    r"GetNpcAuthorityForScope\(",
    r"authority->playerId != \*localPlayerId",
    r"authority->epoch != identity\.authorityEpoch",
    r"TryGetProxyActorForRemotePlayerId\(a_claim\.senderPlayerId\)",
    r"TryGetHostActorFormIdByNpcId\(identity\.npcId\)",
    r"\*actorFormId != identity\.sourceFormId",
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
check("ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValues->health, -damage)" in apply_body,
      "the non-lethal hit must lower health with a negative kDamage delta")
check("SetBaseActorValue" not in apply_body and "SetActorValue" not in apply_body, "ApplyNpcHit must not write base health")
check(re.search(r"const auto protectedActor = actor->boolFlags\.any\(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kProtected\) \|\|\s*!base \|\| base->IsEssential\(\) \|\| base->IsProtected\(\);\s*if \(current - damage > 0\.0F \|\| protectedActor\) \{\s*actor->ModActorValue\(.*?\} else \{\s*actor->KillImpl\(attacker, damage, true, false\);\s*\}", apply_body, re.S) is not None,
      "a lethal hit on a non-essential, non-protected NPC must call KillImpl with the attacker proxy; essential or protected NPCs must take ModActorValue")
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
check("kMaximumNpcHitAge" in pending and "claims.size() < kMaximumNpcHitsPerTick" in pending, "pending NPC hits must be age- and rate-bounded")
check("kMaximumPendingNpcHits" in function_body(health, "bool EnqueueNpcHit("), "the NPC hit queue must be bounded")

death = function_body(puppets, "bool ApplyPuppetDeath(")
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
    sys.exit(1)

print("F4TNpcCoopHealthContract: all checks passed.")
