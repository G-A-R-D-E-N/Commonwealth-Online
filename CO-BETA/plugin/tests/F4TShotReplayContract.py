from pathlib import Path
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


for path in (
    CORE / "include/F4TShotReplay.h",
    CORE / "src/validation/F4TShotReplay.cpp",
    CORE / "tests/F4TShotReplayTests.cpp",
    SERVER / "src/ServerShotReplayRelay.h",
    SERVER / "src/ServerShotReplayRelay.cpp",
    SERVER / "tests/ServerShotReplayRelayTests.cpp",
    PLUGIN / "include/F4TPlayerAnimEventSync.h",
    PLUGIN / "src/F4TPlayerAnimEventSync.cpp",
    PLUGIN / "include/F4TProjectileAim.h",
    PLUGIN / "src/F4TProjectileAim.cpp",
    PLUGIN / "src/F4TProjectileLaunchHook.cpp",
):
    check(len(read(path).splitlines()) <= 1000, f"{path.name} is over 1000 lines")

core_header = read(CORE / "include/F4TShotReplay.h")
core_source = read(CORE / "src/validation/F4TShotReplay.cpp")
core_xmake = read(CORE / "xmake.lua")
check('kCapabilityName = "shot-replay-v1"' in core_header, "shot-replay-v1 capability name changed")
check("constexpr std::size_t kMaximumEventNameLength = 64;" in core_source, "event name bound must stay at 64")
check(
    '"attack", "meleeattack", "powerattack", "block", "bash", "riflesighted", "reload", "throw", "grenade", "weap"'
    in core_source,
    "replayable prefix list changed",
)
check(
    'if (EqualsIgnoreAsciiCase(a_eventName, "attackStart") || EqualsIgnoreAsciiCase(a_eventName, "meleeattackStart")) {'
    in core_source,
    "attackStart and meleeattackStart must stay refused",
)
check(
    "src/validation/F4TShotReplay.cpp" in core_xmake and "include/F4TShotReplay.h" in core_xmake,
    "co-core xmake must build the validator",
)

relay_header = read(SERVER / "src/ServerShotReplayRelay.h")
relay = read(SERVER / "src/ServerShotReplayRelay.cpp")
session_types = read(SERVER / "src/ServerSessionTypes.h")
welcome = read(SERVER / "src/ServerWelcome.cpp")
check("HandleShotReplay(" in relay_header, "relay header must declare HandleShotReplay")
check("inline constexpr double kShotReplayRefillPerSecond = 20.0;" in session_types, "refill rate changed")
check("inline constexpr double kShotReplayBurst = 40.0;" in session_types, "burst size changed")
check("F4T::PacketRateLimit::TokenBucket shotReplays;" in session_types, "projectile aim needs its own bucket")
check("F4T::PacketRateLimit::TokenBucket animEventReplays;" in session_types, "anim events need their own bucket")
check(
    "TakeToken(a_sender.shotReplays, a_monotonicSeconds, kShotReplayRefillPerSecond, kShotReplayBurst)" in relay,
    "projectile aim must draw from its own bucket",
)
check(
    "TakeToken(a_sender.animEventReplays, a_monotonicSeconds, kShotReplayRefillPerSecond, kShotReplayBurst)" in relay,
    "anim events must draw from their own bucket",
)
check("ParseProjectileAim(a_packet)" in relay and "ParsePlayerAnimEvent(a_packet)" in relay, "relay must validate through co-core")
check("compatibilityCapabilities.Has(F4T::ShotReplay::kCapabilityName)" in relay, "relay must check the sender's capability")
check("std::string{ F4T::ShotReplay::kCapabilityName }" in welcome, "welcome must advertise shot-replay-v1")

protocol_inc = read(SERVER / "src/ServerProtocolImpl.inc")
protocol_hdr = read(SERVER / "src/ServerProtocolImpl.h")
shot_replay_dispatch = 'if (*type == "projectileAim" || *type == "playerAnimEvent") return CommonwealthOnline::Server::ShotReplayRelay::HandleShotReplay(sessions, a_connectionId, session, *packet, a_monotonicSeconds, stats);'
npc_dispatch = 'DispatchNpcCoopPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)'
check(
    shot_replay_dispatch in protocol_inc
    and npc_dispatch in protocol_inc
    and protocol_inc.find(shot_replay_dispatch) < protocol_inc.find(npc_dispatch),
    "server dispatch must route projectileAim/playerAnimEvent to ShotReplayRelay before npc coop",
)
check(
    '#include "ServerShotReplayRelay.h"' in protocol_hdr,
    "server protocol header must include the shot replay relay",
)

anim_header = read(PLUGIN / "include/F4TPlayerAnimEventSync.h")
anim = read(PLUGIN / "src/F4TPlayerAnimEventSync.cpp")
check(
    "void Tick(bool a_flushed);" in anim_header and "void HandleRelayedPacket(const nlohmann::json& a_packet);" in anim_header,
    "anim sync header API changed",
)
check("constexpr std::size_t kNotifySlot = 0x01;" in anim, "the replay hook must stay on notify slot 1")
check("constexpr std::size_t kMaximumQueued = 64;" in anim, "queues must stay bounded")
check("RE::PlayerCharacter::GetSingleton()" in anim, "the hook must read the live local player")
check("static_cast<Holder*>(player)" in anim, "the hook must use the player's holder subobject")
check("VtableGuard::VerifySlot(request) != VtableGuard::SlotVerdict::kOk" in anim, "the slot must be verified before the write")
check("VtableGuard::WriteSucceeded(" in anim, "the write must be confirmed")
check("ok && name" in anim and "InstallState::holder.load(std::memory_order_acquire)" in anim, "only accepted events from the local holder are captured")
check("g_localHolder" not in anim and "g_localHolder" not in anim_header, "install state has a single owner")
check("F4T::ShotReplay::IsReplayableAnimEvent(name)" in anim, "captured events must pass the allow-list")
check("F4T::HookThread::Id::PlayerAnimEventCapture" in anim, "the capture must note its hook id")
check(
    "F4T::Networking::IsConnectedToServer()" in anim and "F4T::Networking::SendPlayerAnimEventPacket(event.name);" in anim,
    "outbound events must be sent only while connected",
)
check("F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(playerId)" in anim, "inbound events must resolve the proxy")
check("F4T::AnimationGraphOverride::SendGraphEvent(a_proxy, a_name.c_str())" in anim, "inbound events must go through the graph override")
check("F4T::ShotReplay::IsRetryableAnimEvent(name)" in anim and "DropPendingReplays(playerId);" in anim, "a newer retryable event must replace the pending retry of its sender")
check("a_proxy->DrawWeaponMagicHands(true);" in anim and "RetryPendingReplays();" in anim, "a refused start event must draw the weapon and retry")
check("kRetryWindow{ 600 }" in anim and "kRetryWindowWhileDrawing{ 1500 }" in anim, "retry windows must be 600 ms, 1500 ms while drawing")
check("ParseRelayedPlayerId(a_packet)" in anim and "ParsePlayerAnimEvent(a_packet)" in anim, "relayed packets must be validated through co-core")


def masked_code(text):
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i + 1 < n:
                out[i] = out[i + 1] = " "
                i += 2
        elif ch in "\"'":
            quote = ch
            out[i] = " "
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < n:
                        if text[i] != "\n":
                            out[i] = " "
                        i += 1
                else:
                    if text[i] != "\n":
                        out[i] = " "
                    i += 1
            if i < n:
                out[i] = " "
                i += 1
        else:
            i += 1
    return "".join(out)


def brace_depth_before(masked, index):
    return masked[:index].count("{") - masked[:index].count("}")


anim_code = masked_code(anim)
check(anim_code.count("{") == anim_code.count("}"), "F4TPlayerAnimEventSync.cpp braces must balance")
check(anim_code.rstrip().endswith("}"), "F4TPlayerAnimEventSync.cpp must end on its namespace closing brace")
check(anim_code.count("namespace F4T::PlayerAnimEventSync") == 1, "anim sync must open exactly one named namespace")
for symbol, depth, message in (
    ("namespace F4T::PlayerAnimEventSync", 0, "the anim sync namespace must open at file scope"),
    ("bool NotifyHook", 2, "NotifyHook must sit in the anonymous namespace"),
    ("void InstallOnLocalPlayer", 2, "InstallOnLocalPlayer must sit in the anonymous namespace"),
    ("void TryInstallOnTick", 2, "TryInstallOnTick must sit in the anonymous namespace"),
    ("void Tick(bool a_flushed)", 1, "Tick must sit directly in F4T::PlayerAnimEventSync"),
    ("void HandleRelayedPacket(", 1, "HandleRelayedPacket must sit directly in F4T::PlayerAnimEventSync"),
):
    position = anim_code.find(symbol)
    check(position != -1, f"anim sync source is missing {symbol}")
    check(brace_depth_before(anim_code, position) == depth, message)
check(
    anim_code.find("void TryInstallOnTick") < anim_code.find("void Tick(bool a_flushed)") < anim_code.find("void HandleRelayedPacket("),
    "namespace boundary: TryInstallOnTick must precede Tick and HandleRelayedPacket",
)

hook_ids = read(PLUGIN / "include/F4THookThreadTable.h")
check("LiveFireGiveBack = 20," in hook_ids and "PlayerAnimEventCapture = 21," in hook_ids, "hook id 21 must follow LiveFireGiveBack")
check("AimRedirect = 22," in hook_ids, "hook id 22 is the aim redirect")
networking_header = read(PLUGIN / "include/F4TNetworking.h")
check("bool SendPlayerAnimEventPacket(std::string_view a_eventName);" in networking_header, "networking header must declare the sender")
outbound = read(PLUGIN / "src/F4TNetworkingOutbound.cpp")
check(
    "bool SendPlayerAnimEventPacket(std::string_view a_eventName)" in outbound and '{ "type", "playerAnimEvent" }' in outbound,
    "outbound playerAnimEvent packet missing",
)
inbound = read(PLUGIN / "src/F4TNetworkingInbound.cpp")
check(
    '#include "F4TPlayerAnimEventSync.h"' in inbound
    and '*packetType == "playerAnimEvent"' in inbound
    and "F4T::PlayerAnimEventSync::HandleRelayedPacket(packet);" in inbound,
    "inbound playerAnimEvent dispatch missing",
)
identity = read(PLUGIN / "src/F4TClientIdentity.cpp")
check(
    '#include "F4TShotReplay.h"' in identity
    and "metadata.capabilities.emplace_back(std::string{ F4T::ShotReplay::kCapabilityName });" in identity,
    "client must advertise shot-replay-v1",
)
check(
    '#include "F4TPlayerAnimEventSync.h"' in identity
    and "if (F4T::PlayerAnimEventSync::IsInstalled()) {" in identity,
    "shot-replay-v1 must be advertised only when the hook installed",
)
check("bool IsInstalled()" in anim_header, "anim sync header must expose IsInstalled")
check(
    "kMaximumInstallAttempts" in anim and "TryInstallOnTick" in anim and "kInstallRetryInterval" in anim,
    "install must retry bounded on the game thread",
)
check("g_installRefused" not in anim, "fail-closed install flag must be gone")
check("bool SendProjectileAimPacket(double a_x, double a_y, double a_z);" in networking_header, "networking header must declare the aim sender")
check(
    "bool SendProjectileAimPacket(double a_x, double a_y, double a_z)" in outbound and '{ "type", "projectileAim" }' in outbound,
    "outbound projectileAim packet missing",
)
aim_header = read(PLUGIN / "include/F4TProjectileAim.h")
aim = read(PLUGIN / "src/F4TProjectileAim.cpp")
launch = read(PLUGIN / "src/F4TProjectileLaunchHook.cpp")
combat = read(PLUGIN / "src/F4TRangedCombatCapture.cpp")
check(
    "void Install();" in aim_header
    and "void FlushOutbound();" in aim_header and "void DiscardOutbound();" in aim_header
    and "void QueueAimForSend(" in aim_header
    and "void HandleRelayedAimPacket(" in aim_header
    and "bool IsInstalled();" in aim_header,
    "aim header must expose the aim path",
)
check(
    "QueueAimForSend" not in anim
    and "HandleRelayedAimPacket" not in anim
    and "g_remoteAim" not in anim
    and "ProjectileAim" not in anim,
    "anim sync must not carry aim state",
)
check(
    '*packetType == "projectileAim"' in inbound
    and "F4T::ProjectileAim::HandleRelayedAimPacket(packet);" in inbound,
    "inbound projectileAim dispatch missing",
)
check(
    "HandleRelayedAimPacket" in aim and "ParseProjectileAim(a_packet)" in aim and "RemoteAimQueue" in aim,
    "relayed aim must be validated through co-core and stored in the aim queue",
)
check(
    "kRemoteAimExpiry" in core_header and "kRemoteAimPerPlayerCap" in core_header and "class RemoteAimQueue" in core_header,
    "the aim queue caps and expiry must live in co-core",
)
check(
    "a_launch->zAngle" in launch and "a_launch->xAngle" in launch and "DirectionFromLaunchAngles(" in launch,
    "producer must read the launch angles and convert with co-core",
)
check(
    "offsetof(RE::ProjectileLaunchData, zAngle) == 0x4C" in launch
    and "offsetof(RE::ProjectileLaunchData, xAngle) == 0x50" in launch,
    "producer offsets must match the answer file",
)
check(
    "Direction{ cosX * std::sin(a_zAngle), cosX * std::cos(a_zAngle), -std::sin(a_xAngle) }" in core_source,
    "direction formula changed",
)
check("constexpr std::size_t kAddInitialVelocitySlot = 0xE7;" in aim, "redirect must hook slot 0xE7")
for vtable in (
    "RE::VTABLE::Projectile[0]",
    "RE::VTABLE::MissileProjectile[0]",
    "RE::VTABLE::BeamProjectile[0]",
    "RE::VTABLE::ConeProjectile[0]",
    "RE::VTABLE::FlameProjectile[0]",
    "RE::VTABLE::GrenadeProjectile[0]",
):
    check(vtable in aim, f"redirect must hook {vtable}")
check("Arrow" not in aim, "arrow vtables are not verified and must stay unhooked")
check(
    "a_projectile->velocity.x = dx * speed" in aim and "a_projectile->movementDirection.x = dx" in aim,
    "redirect must overwrite velocity at the same speed and the movement direction",
)
check("redirected beam shot for player" in aim, "beam redirect must log once per shot")
check("RollbackWrites(" in aim, "failed install must roll back written slots")
aim_runtime = read(PLUGIN / "src/F4TReplicationRuntime.cpp")
check(
    '#include "F4TProjectileAim.h"' in aim_runtime and "F4T::ProjectileAim::DiscardOutbound();" in aim_runtime,
    "Tick must discard aim when gameplay is not admitted",
)
check(
    '#include "F4TProjectileAim.h"' in combat and "F4T::ProjectileAim::Install();" in combat,
    "combat init must install the aim unit",
)
runtime = read(PLUGIN / "src/F4TReplicationRuntime.cpp")
check(
    '#include "F4TPlayerAnimEventSync.h"' in runtime and "F4T::PlayerAnimEventSync::Tick(animFlushed);" in runtime,
    "Tick must drive the anim sync",
)
gate_start = runtime.find("F4T::SurvivalEnforcer::Tick(connection, g_runtime.CurrentGeneration(), loaded, [&] {")
gate_end = runtime.find("\n\t\t\t\t});", gate_start)
check(gate_start >= 0 and gate_end > gate_start and "F4T::PlayerAnimEventSync::FlushOutbound();" in runtime[gate_start:gate_end], "outbound animation flush must run inside the Survival gate")
check(gate_start >= 0 and gate_end > gate_start and "F4T::ProjectileAim::FlushOutbound();" in runtime[gate_start:gate_end], "outbound aim flush must run inside the Survival gate")
check("if (!a_flushed) {" in anim and "g_outbound.clear();" in anim[anim.find("if (!a_flushed) {"):anim.find("std::deque<std::pair<std::uint32_t, std::string>> inbound;")], "denied ticks must clear unsent animation events")
check("ReadyRevision(connection, session)" in anim and "g_clientState.Revision(event.connection) == event.revision" in anim, "animation events must be stamped and rechecked against policy")
for text in (anim, anim_header, aim, aim_header, launch, combat, outbound, inbound, identity, runtime, networking_header):
    check("PlayerEventReplay" not in text and "ProxyProjectileAim" not in text, "stale phase 1 names must not return")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TShotReplayContract: all checks passed.")
