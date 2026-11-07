from pathlib import Path
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"
SERVER = ROOT / "CO-SERVER/server-cpp"
CORE = ROOT / "co-core"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(path):
    if not path.exists():
        failures.append(f"missing {path.relative_to(ROOT).as_posix()}")
        return ""
    return path.read_text(encoding="utf-8")


SOURCES = (
    PLUGIN / "include/F4TExplosionSync.h",
    PLUGIN / "src/F4TExplosionSync.cpp",
    PLUGIN / "src/F4TNetworkingInbound.cpp",
    PLUGIN / "src/F4TNetworkingOutbound.cpp",
    PLUGIN / "src/F4TReplicationRuntime.cpp",
    PLUGIN / "src/F4TClientIdentity.cpp",
    PLUGIN / "include/F4TNetworking.h",
    CORE / "include/F4TShotReplay.h",
    CORE / "include/F4TExplosionSyncPolicy.h",
    CORE / "src/validation/F4TExplosionSyncPolicy.cpp",
    CORE / "tests/F4TExplosionSyncPolicyTests.cpp",
    CORE / "src/validation/F4TShotReplay.cpp",
    SERVER / "src/ServerShotReplayRelay.cpp",
    SERVER / "src/ServerShotReplayRelay.h",
    SERVER / "src/ServerWelcome.cpp",
    SERVER / "src/ServerProtocolImpl.inc",
)
for path in SOURCES:
    check(len(read(path).splitlines()) <= 1000, f"{path.name} is over 1000 lines")

explosion_header = read(PLUGIN / "include/F4TExplosionSync.h")
explosion = read(PLUGIN / "src/F4TExplosionSync.cpp")
check(
    "namespace F4T::ExplosionSync" in explosion_header
    and "void NoteWelcome(const nlohmann::json& a_welcome);" in explosion_header
    and "void Tick(bool a_connected);" in explosion_header
    and "void FlushOutbound();" in explosion_header
    and "void DiscardOutbound();" in explosion_header
    and "void HandleRelayedPacket(const nlohmann::json& a_packet);" in explosion_header,
    "explosion sync header must expose the five entry points",
)
check(
    "F4T::ShotReplay::kExplosionCapabilityName" in explosion and "ParseExplosionSpawn(" in explosion,
    "explosion sync must read the capability name and parse through co-core",
)
check(
    "ParseRelayedPlayerId(" in explosion and "F4T::Networking::SendExplosionSpawnPacket(" in explosion,
    "explosion sync must parse relayed sender ids and send through the networking sender",
)
check(
    "F4T::ReplicationRuntime::CurrentGeneration()" in explosion
    and "F4T::Networking::GetConnectionGeneration()" in explosion
    and "F4T::SurvivalPolicy::g_clientState.Revision(spawn.connection) == spawn.revision" in explosion,
    "outbound explosion spawns must recheck session, connection and survival revision before sending",
)
check(
    "NEW_REFR_DATA" in explosion and "CreateReferenceAtLocation(" in explosion,
    "replay must place the explosion through NEW_REFR_DATA",
)
check(
    "Policy::IsNewRef(" in explosion and "std::erase_if(g_seen" not in explosion and "kSeenKeep" not in explosion and "g_seen.clear();" in explosion,
    "seen blasts must be rebuilt each scan and cleared on disconnect",
)
check(
    "#include \"F4TExplosionSyncPolicy.h\"" in explosion and "Policy::Decide(" in explosion
    and "Policy::InReplayRange(" in explosion and "Policy::SameSpace(" in explosion
    and "Policy::ProxyBlastMatches(" in explosion,
    "explosion sync must take its decisions from the co-core policy",
)
check(
    "WithinRadius(" not in explosion and "kProxyHold" not in explosion and "kProxyMatchRadius" not in explosion,
    "the plugin must not carry its own decision constants",
)
check(
    "IsExplosionRef(" in explosion and "explosionTable.address()" in explosion and "chainTable.address()" in explosion,
    "the explosion cast must be guarded by the Explosion and ChainExplosion type check",
)
check(
    "g_proxyBlasts.erase(blast);" in explosion,
    "a matched proxy blast must be consumed",
)
resolve_at = explosion.find("F4T::QuestFormKey::ToFormId(")
check(
    resolve_at > explosion.find("void ProcessPending(") and resolve_at < explosion.find("void NoteWelcome("),
    "form key resolution must run on the game thread inside ProcessPending",
)
check(
    "ToFormId" not in explosion[explosion.find("void HandleRelayedPacket("):],
    "the network thread must only parse and queue the raw spawn",
)

sender = read(PLUGIN / "src/F4TNetworkingOutbound.cpp")
check(
    "bool SendExplosionSpawnPacket(std::string_view a_formKey, double a_x, double a_y, double a_z)" in sender
    and '{ "type", "explosionSpawn" }' in sender,
    "outbound explosionSpawn packet missing",
)
networking_header = read(PLUGIN / "include/F4TNetworking.h")
check(
    "bool SendExplosionSpawnPacket(std::string_view a_formKey, double a_x, double a_y, double a_z);" in networking_header,
    "networking header must declare the explosion sender",
)

inbound = read(PLUGIN / "src/F4TNetworkingInbound.cpp")
check('#include "F4TExplosionSync.h"' in inbound, "inbound must include the explosion sync header")
check(
    "F4T::ExplosionSync::TryHandlePacket(*packetType, packet)" in inbound,
    "inbound must hand packets to the explosion sync handler",
)
check(
    'a_type == "welcome" || a_type == "capabilities"' in explosion and "NoteWelcome(a_packet);" in explosion
    and 'a_type != "explosionSpawn"' in explosion and "HandleRelayedPacket(a_packet);" in explosion,
    "the explosion handler must note welcome and capabilities and dispatch explosionSpawn",
)
check(len(inbound.splitlines()) < 775, "F4TNetworkingInbound.cpp must stay under 775 lines")

identity = read(PLUGIN / "src/F4TClientIdentity.cpp")
check(
    "metadata.capabilities.emplace_back(std::string{ F4T::ShotReplay::kExplosionCapabilityName });" in identity,
    "client must advertise explosion-sync-v1 unconditionally",
)

runtime = read(PLUGIN / "src/F4TReplicationRuntime.cpp")
check('#include "F4TExplosionSync.h"' in runtime, "replication runtime must include the explosion sync header")
check("F4T::ExplosionSync::Tick(connected && IsSurvivalWorldReady());" in runtime, "Tick must drive explosion sync with the connection state and the survival world gate")
gate_start = runtime.find("F4T::SurvivalEnforcer::Tick(connection, g_runtime.CurrentGeneration(), loaded, [&] {")
gate_end = runtime.find("\n\t\t\t\t});", gate_start)
check(
    gate_start >= 0 and gate_end > gate_start and "F4T::ExplosionSync::FlushOutbound();" in runtime[gate_start:gate_end],
    "outbound explosion flush must run inside the Survival gate",
)
check(
    "if (!explosionFlushed) F4T::ExplosionSync::DiscardOutbound();" in runtime,
    "explosion outbound must be discarded when the gate is not admitted",
)

core_header = read(CORE / "include/F4TShotReplay.h")
core_source = read(CORE / "src/validation/F4TShotReplay.cpp")
check('kExplosionCapabilityName = "explosion-sync-v1"' in core_header, "explosion-sync-v1 capability name changed")
check("kMaximumExplosionCoordinate = 1000000.0" in core_header, "explosion coordinate bound changed")
check("ParseExplosionSpawn(const nlohmann::json& a_packet)" in core_source, "co-core must parse explosion spawns")

relay = read(SERVER / "src/ServerShotReplayRelay.cpp")
check(
    "HandleExplosionSpawn(" in relay
    and "a_sender.compatibilityCapabilities.Has(F4T::ShotReplay::kExplosionCapabilityName)" in relay
    and "a_sender.explosionSpawns" in relay
    and "F4T::ShotReplay::kExplosionCapabilityName, *scope" in relay,
    "server relay must gate explosion spawns on the sender and the recipients",
)
welcome = read(SERVER / "src/ServerWelcome.cpp")
check("std::string{ F4T::ShotReplay::kExplosionCapabilityName }" in welcome, "welcome must advertise explosion-sync-v1")
dispatch = read(SERVER / "src/ServerProtocolImpl.inc")
check(
    '*type == "explosionSpawn") return CommonwealthOnline::Server::ShotReplayRelay::HandleExplosionSpawn(' in dispatch,
    "server dispatch must route explosionSpawn to ShotReplayRelay",
)

check(
    "NearSender(a_sender.lastMovement, *spawn)" in relay and "kExplosionSenderRadius = 6500.0" in relay,
    "server must reject explosion spawns farther than 6500 units from the sender",
)
server_tests = read(SERVER / "tests/ServerShotReplayRelayTests.cpp")
check(
    "TestExplosionFarFromSenderIsDropped();" in server_tests and "TestExplosionNearSenderIsRelayed();" in server_tests,
    "server tests must cover far and near explosion spawns",
)
core_xmake = read(CORE / "xmake.lua")
core_tests = read(CORE / "xmake-tests.lua")
check(
    "src/validation/F4TExplosionSyncPolicy.cpp" in core_xmake and "include/F4TExplosionSyncPolicy.h" in core_xmake,
    "co-core xmake must build the explosion policy",
)
check(
    'core_test("CoreExplosionSyncPolicyTests", "CommonwealthOnlineCoreExplosionSyncPolicyTests", "F4TExplosionSyncPolicyTests.cpp")' in core_tests,
    "co-core tests must register the explosion policy suite",
)

npc_capture = read(PLUGIN / "src/F4TNpcCombatCapture.cpp")
ranged_capture = read(PLUGIN / "src/F4TRangedCombatCapture.cpp")
give_back = read(PLUGIN / "src/F4TProxyLiveFireGuard.cpp")
check(
    npc_capture.count("F4T::ExplosionSync::IsReplayedExplosion(a_event.hitData.sourceRef)") == 2
    and "F4T::ExplosionSync::IsReplayedExplosion(a_event.hitData.sourceRef)" in ranged_capture,
    "NPC and ranged hit sinks must not report hits from replayed explosions",
)
check(
    "IsReplayedExplosion(a_event.hitData.sourceRef)" in give_back
    and "F4T::ExplosionSyncPolicy::GiveBackAllowed(" in give_back
    and give_back.find("F4T::ExplosionSyncPolicy::GiveBackAllowed(") < give_back.find("localPlayer->ModActorValue(")
    and "F4T::ProxyLiveFire::RegisterGiveBackSink();" in explosion
    and "g_replayed.push_back(ReplayedRef{ created, a_now });" in explosion
    and "g_replayed.clear();" in explosion
    and "hasProxy" not in explosion[explosion.find("void Replay("):explosion.find("void ProcessPending(")],
    "replayed explosions must be registered, cleared on disconnect, and given back with or without a proxy",
)

replay_body = explosion[explosion.find("void Replay("):explosion.find("void ProcessPending(")]
check(
    replay_body.find("CreateReferenceAtLocation(") < replay_body.find("g_replayedMutex")
    and explosion.count("std::lock_guard lock{ g_replayedMutex };") >= 4,
    "g_replayed must be guarded by g_replayedMutex in every access, never held across the create call",
)
check(
    "g_seen.clear();" in explosion and "std::vector<Policy::SeenRef> g_seen;" in explosion,
    "seen refs must be keyed by Policy::SeenRef",
)

check(
    "constexpr std::uintptr_t kChainChildHandleOffset = 0x634;" in explosion
    and "ChainChildHandle(a_ref)" in explosion
    and "Policy::RecordChainChild(g_chainChildren" in explosion
    and "Policy::IsChainChild(g_chainChildren, blast.nativeHandle)" in explosion
    and "Policy::PruneChainChildren(g_chainChildren, a_now, kReplayedKeep);" in explosion
    and "g_chainChildren.clear();" in explosion
    and "a_ref->GetHandle().get_handle()" in explosion
    and "TracksChainChildren()" in explosion
    and "1, 10, 163" in explosion and "1, 10, 984" in explosion and "1, 11, 240" in explosion,
    "chain children must be recorded from the +0x634 handle of live ChainExplosion refs, skipped for relay and proxy registration, pruned and cleared on disconnect, gated to OG/NG/AE",
)
check(
    "kProxyHold{ 500 }" in read(CORE / "include/F4TExplosionSyncPolicy.h")
    and "kRemoteAimExpiry{ 500 }" in read(CORE / "include/F4TShotReplay.h")
    and "Decide(DecisionInput{ true, true, true, false, milliseconds{ 500 } }) == Decision::Replay" in read(CORE / "tests/F4TExplosionSyncPolicyTests.cpp"),
    "proxy hold must be 500 ms, matching the remote aim expiry",
)
check(
    "ChainChildRecord" in read(CORE / "include/F4TExplosionSyncPolicy.h")
    and "bool IsChainChild(const std::vector<ChainChildRecord>& a_records, std::uint32_t a_handle) noexcept;" in read(CORE / "include/F4TExplosionSyncPolicy.h")
    and "std::erase_if(a_records, [&](const ChainChildRecord& a_record) { return a_now - a_record.seenAt > a_keep; });" in read(CORE / "src/validation/F4TExplosionSyncPolicy.cpp")
    and "a_record.handle == a_handle" in read(CORE / "src/validation/F4TExplosionSyncPolicy.cpp")
    and "IsChainChild(records, 0x0100AB)" in read(CORE / "tests/F4TExplosionSyncPolicyTests.cpp"),
    "co-core must own the chain child record, lookup and prune policy, with a test",
)
policy_source = read(CORE / "src/validation/F4TExplosionSyncPolicy.cpp")
check(
    "a_blastPlayerId == a_spawnPlayerId" in policy_source
    and "a_entry.address == a_ref.address" in policy_source
    and "a_entry.localId == a_ref.localId" in policy_source,
    "proxy blast match must require the same player, and duplicate check must key on address and form id",
)
check(
    "GiveBackAllowed(double a_healthAfterHit) noexcept" in read(CORE / "include/F4TExplosionSyncPolicy.h")
    and "return a_healthAfterHit > 0.0;" in policy_source,
    "give-back must be refused when the hit leaves the local player at zero health",
)

relay_body = relay[relay.find("HandleExplosionSpawn("):]
check(
    '#include "ServerExplosionFormAllowlist.h"' in relay
    and "IsKnownExplosionForm(spawn->formKey.plugin, spawn->formKey.localId)" in relay
    and "a_stats.explosionFormsRejected++;" in relay
    and "ExplosionFormAllowlist::kEntries" in relay,
    "server relay must reject explosion forms outside the verified allowlist and count them",
)
check(
    "bool FriendlyFireAllows(const Session& a_sender, const Session& a_recipient, const ExplosionFriendlyFire& a_rules)" in relay
    and "if (!a_rules.friendlyFire) {" in relay
    and "a_rules.factionFriendlyFire || !a_rules.sameFaction" in relay
    and "!a_rules.sameFaction(a_sender.profileId, a_recipient.profileId)" in relay
    and "a_friendlyFire && !FriendlyFireAllows(a_sender, recipient, *a_friendlyFire)" in relay
    and "*scope, &a_friendlyFire);" in relay_body,
    "explosion relay must gate each recipient on friendlyFire and factionFriendlyFire the way combatHit does",
)
relay_impl = read(SERVER / "src/ServerProtocolImpl.inc")
check(
    "HandleExplosionSpawn(sessions, a_connectionId, session, *packet, a_monotonicSeconds, stats, { friendlyFire, factionFriendlyFire, [this]" in relay_impl
    and "return factionService.SameFaction(a_viewer, a_other);" in relay_impl,
    "explosion dispatch must pass the live friendlyFire and factionFriendlyFire flags with the faction lookup",
)
allowlist = read(SERVER / "src/ServerExplosionFormAllowlist.h")
allowlist_entries = re.findall(r'\{ "([a-z0-9]+\.esm)", 0x([0-9A-F]{6}) \}', allowlist)  # source:E:/RE-work/explosion-allowlist/expl-all.json
allowlist_keys = {(plugin, int(local_id, 16)) for plugin, local_id in allowlist_entries}
verified_text = read(PLUGIN / "tests/data/explosion-expl-all.json")
verified_rows = json.loads(verified_text) if verified_text else []
live_by_install = {
    install: {(row["plugin"], row["localId"]) for row in verified_rows if row["install"] == install and not row["deleted"]}
    for install in ("OG", "NG", "AE")
}
verified_keys = live_by_install["OG"]
check(
    len(verified_keys) == 342 and live_by_install["NG"] == verified_keys and live_by_install["AE"] == verified_keys,
    "verified EXPL json must hold the same 342 live records for OG, NG and AE",
)
check(
    len(allowlist_entries) == len(allowlist_keys) == 342 and allowlist_keys == verified_keys,
    "explosion allowlist must equal the full verified EXPL key set, no more and no fewer",
)
check(
    '{ "fallout4.esm", 0x1A7FF2 }' in allowlist and '{ "fallout4.esm", 0x0D83EC }' in allowlist,  # source:E:/RE-work/explosion-allowlist/expl-all.json
    "explosion allowlist must keep FatManExplosion and the other verified entries",
)
check(
    "TestExplosionWithUnknownFormIsRejected();" in server_tests,
    "server tests must cover unknown explosion forms",
)

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TExplosionSyncContract: all checks passed.")
