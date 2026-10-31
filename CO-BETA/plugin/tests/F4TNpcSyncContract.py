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


def short_name(signature):
    return signature.split("(")[0].split()[-1]


npc_sync_paths = sorted((PLUGIN / "src").glob("F4TNpcSync*.cpp")) + [PLUGIN / "src/F4TNpcActorGuard.cpp", PLUGIN / "src/F4TNpcCombatCapture.cpp", PLUGIN / "src/F4TNetworkingNpcCoop.cpp"]
npc_sync = "\n".join(strip_comments(path.read_text(encoding="utf-8")) for path in npc_sync_paths)
identity = read_code("include/F4TNpcSyncIdentity.h")
health = read_code("include/F4TNpcSyncHealth.h")
remote_state = read_code("include/F4TRemoteNpcState.h")
networking_header = read_code("include/F4TNetworking.h")
outbound = read_code("src/F4TNetworkingOutbound.cpp")
inbound = read_code("src/F4TNetworkingInbound.cpp")
npc_sync_header = read_code("include/F4TNpcSync.h")
xmake = (PLUGIN / "xmake.lua").read_text(encoding="utf-8")

npc_sources = {
    "F4TNpcSync*.cpp": npc_sync,
    "F4TNpcSyncIdentity.h": identity,
    "F4TNpcSyncHealth.h": health,
}

for name, code in npc_sources.items():
    check("KillDying" not in code, f"{name} must not call KillDying")
    check(not re.search(r"SetLifeState\s*\(", code), f"{name} must not call SetLifeState")
    check("kDying" not in code, f"{name} must not set kDying")

kill_calls = []
for path in sorted((PLUGIN / "src").rglob("*")) + sorted((PLUGIN / "include").rglob("*")):
    if path.suffix not in (".cpp", ".h"):
        continue
    code = strip_comments(path.read_text(encoding="utf-8"))
    for match in re.finditer(r"KillImpl\s*\(([^;]*)\)\s*;", code):
        kill_calls.append((path.name, match.group(1)))
check(sorted(name for name, _ in kill_calls) == ["F4TNpcSyncHealth.cpp", "F4TNpcSyncPuppets.cpp"],
      f"KillImpl must be called exactly once by the puppet death and once by the authority hit, found {[name for name, _ in kill_calls]}")
for name, arguments in kill_calls:
    parts = [part.strip() for part in arguments.split(",")]
    check(not re.search(r"[Pp]layer", arguments), f"{name}: KillImpl must not be passed the player: {arguments!r}")
    if name == "F4TNpcSyncPuppets.cpp":
        check(parts == ["nullptr", "0.0F", "false", "false"], f"{name}: puppet KillImpl must have no attacker and send no story event: {arguments!r}")
    else:
        check(parts == ["attacker", "damage", "true", "false"], f"{name}: the authority kill must credit the attacker proxy: {arguments!r}")

death_body = function_body(npc_sync, "bool ApplyPuppetDeath(")
check("std::addressof(a_puppet) == std::addressof(a_player)" in death_body, "ApplyPuppetDeath must refuse the local player")
check("g_suppressedActors.contains(" in death_body, "ApplyPuppetDeath must refuse the hidden original")
check("IsPuppetActor(" in death_body, "ApplyPuppetDeath must only act on a puppet")
check(len(re.findall(r"ApplyPuppetDeath\(", npc_sync)) == 2, "ApplyPuppetDeath must have exactly one definition and one caller")
check(re.search(r"a_puppet\.boolFlags\.reset\([^;]*kEssential[^;]*kProtected[^;]*;", death_body) is not None,
      "the essential and protected bit clear must be present")
check(re.search(r"KillImpl\(\s*nullptr\s*,\s*0\.0F\s*,\s*false\s*,\s*false\s*\)", death_body) is not None,
      "puppet KillImpl must not send the story event (sendEvent=false, ragdollInstant=false)")
check(death_body.index("boolFlags.reset(") < death_body.index("KillImpl("), "the essential and protected bits must be cleared before the kill")

health_source = strip_comments((PLUGIN / "src/F4TNpcSyncHealth.cpp").read_text(encoding="utf-8"))
npc_sync_outside_health = "\n".join(strip_comments(path.read_text(encoding="utf-8")) for path in npc_sync_paths if path.name not in ("F4TNpcSyncHealth.cpp", "F4TNpcSyncCombat.cpp"))
npc_sync_outside_health += "\n" + health_source.replace(function_body(health_source, "void ApplyNpcHit("), "")
check("IsDead(false)" not in npc_sync_outside_health, "NPC sync sources outside the hit refusal in F4TNpcSyncHealth.cpp must use IsDead(true) so kEssentialDown is not reported or matched as dead")
check(npc_sync.count("IsDead(true)") >= 2, "capture and local match must both read IsDead(true)")

check(re.search(r"IsRuntimeFormId\([^)]*\)\s*noexcept\s*\{\s*return a_formId >= 0xFF000000U;", remote_state),
      "IsRuntimeFormId must be a >= 0xFF000000 test")
check("GetNPC()" not in npc_sync, "F4TNpcSync.cpp must resolve bases through ResolveStableBase, not GetNPC()->GetFormID()")
check("IsRuntimeFormId(npc->GetFormID())" in identity, "ResolveStableBase must reject runtime base ids")
check("ExtraLeveledCreature" in identity and "originalBase" in identity, "templated NPCs must use ExtraLeveledCreature::originalBase")
check("F4T_NPC_" not in identity, "ResolveStableBase must carry no NPC feature macro")
check(identity.count("RE::ENUM_FORM_ID::kNPC_") >= 2, "ResolveStableBase must require an NPC_ form for the reference base and for originalBase")
check("IsRuntimeFormId(original->GetFormID())" in identity, "originalBase must also be rejected when it is a runtime id")
usable = function_body(npc_sync, "bool IsStateIdentityUsable(")
check("IsRuntimeFormId(a_state.sourceFormId)" in usable and "IsRuntimeFormId(a_state.baseFormId)" in usable,
      "IsStateIdentityUsable must reject runtime source and base ids")
for signature in ("RE::Actor* FindMatchingSourceActor(", "bool HasMatchingAliveLocalNpc(", "void SuppressDuplicateLocalNpcs("):
    body = function_body(npc_sync, signature)
    check("IsStateIdentityUsable(" in body, f"{short_name(signature)} must gate on IsStateIdentityUsable")
    check("StableBaseFormId(" in body, f"{short_name(signature)} must compare StableBaseFormId")
capture = function_body(npc_sync, "std::vector<NpcState> CaptureHostSnapshot(")
check("IsRuntimeFormId(a_actor.GetFormID())" in capture, "capture must skip runtime-created refs")
check("StableBaseFormId(" in capture, "capture must send the stable base")
send = function_body(outbound, "bool SendNpcStatePacket(")
check("IsRuntimeFormId(npc.baseFormId)" in send and "IsRuntimeFormId(npc.sourceFormId)" in send,
      "SendNpcStatePacket must drop runtime base and source ids")
receive = function_body(inbound, "void HandleNpcStatePacket(")
check("IsRuntimeFormId(*baseFormId)" in receive and "IsRuntimeFormId(*sourceFormId)" in receive,
      "HandleNpcStatePacket must drop runtime base and source ids")
check("g_hostNpcIdsByActorFormId" in npc_sync, "the placed ref FormID stays the NPC id key")

check(re.search(r"SendNpcStatePacket\(\s*std::span<const F4T::RemoteNpcState::NpcState> a_npcs,\s*std::uint64_t a_authorityEpoch,\s*std::string_view a_authorityCellId,\s*std::string_view a_authorityWorldspaceId\)", networking_header),
      "SendNpcStatePacket must take the authority epoch, cell and worldspace")
for field in ("authorityEpoch", "authorityCellId", "authorityWorldspaceId"):
    check(f'{{ "{field}"' in send, f"npcState packet must carry {field}")
check("a_authorityEpoch == 0" in send, "SendNpcStatePacket must refuse a zero epoch")
update_host = function_body(npc_sync, "void UpdateHost(")
check("a_authorityEpoch" in update_host and "a_authorityCellId" in update_host and "a_authorityWorldspaceId" in update_host,
      "UpdateHost must forward the authority scope")
check("UpdateHost(*player, authorityEpoch, cellId, worldspaceId)" in npc_sync,
      "Update must pass the authority epoch, cell and worldspace to UpdateHost")

check(re.search(r"bool hasHealth\{ false \};\s*float health\{\};\s*float maxHealth\{\};", remote_state), "NpcState must carry health and maxHealth")
check('entry["health"]' in send and 'entry["maxHealth"]' in send, "SendNpcStatePacket must send health and maxHealth")
check('"health"' in receive and '"maxHealth"' in receive, "HandleNpcStatePacket must parse health and maxHealth")
apply_health = function_body(health, "inline void ApplyAuthorityHealth(")
check("a_state.health / a_state.maxHealth" in apply_health and "GetBaseActorValue(" in apply_health,
      "authority health must be applied as a fraction of the puppet's own maximum")
check(re.search(r"!\(a_state\.maxHealth > 0\.0F\)", apply_health) is not None and re.search(r"!\(ownMaximum > 0\.0F\)", apply_health) is not None,
      "ApplyAuthorityHealth must guard a non-positive maxHealth and own maximum")
check("kMinAppliedHealth" in apply_health, "ApplyAuthorityHealth must keep the minimum applied health")
check("ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage" in health, "authority health is applied through ModActorValue(kDamage)")
check("CheckClampDamageModifier" not in npc_sync + health + identity, "the tier-2 damage block override is a later package")
check("SetBaseActorValue" not in npc_sync + health, "health must not be applied through SetBaseActorValue")

check("DeletePendingSpawn()" in function_body(npc_sync, "void ClearPuppets("), "ClearPuppets must delete the pending spawn ref")
pending = function_body(npc_sync, "void UpdatePendingSpawn(")
check(pending.count("DeletePendingSpawn()") >= 2, "the pending spawn timeout and cancel paths must delete the created ref")
update_puppets = function_body(npc_sync, "void UpdatePuppets(")
check(re.search(r"deathNow - slot->second\.lastDeathAttempt >= kDeathRetryInterval\)\s*\{\s*\+\+slot->second\.deathAttempts;\s*slot->second\.lastDeathAttempt = deathNow;", update_puppets) is not None,
      "death retries must be gated on and must stamp lastDeathAttempt with kDeathRetryInterval")
check(re.search(r"constexpr auto kDeathRetryInterval = [1-9]\d*ms;", npc_sync) is not None, "kDeathRetryInterval must be a positive millisecond interval")
check("g_createdPuppetBases" in npc_sync and "PurgeStalePuppets()" in npc_sync, "created puppets must be recorded and purged")
check("PurgeStalePuppets()" in function_body(npc_sync, "void Update()"), "Update must run the stale puppet sweep")
check("g_stalePuppetSweepDone = false" in function_body(npc_sync, "void Reset()"), "Reset must re-arm the stale puppet sweep")

update_body = function_body(npc_sync, "void Update()")
check(update_body[:-1].rstrip().endswith("Detail::PublishRegistrySnapshot();"), "Update must call Detail::PublishRegistrySnapshot() last")
check(update_body.count("PublishRegistrySnapshot()") == 1, "Update must publish the registry snapshot exactly once")
check(
    update_body.index("Detail::UpdatePuppets(") < update_body.index("Health::Tick()") < update_body.index("Loot::Tick(") < update_body.index("Combat::Tick()") < update_body.index("Detail::UpdateHost("),
    "Update must run the Health, Loot and Combat Tick() calls between UpdatePuppets and UpdateHost")
check("MarkRegistryDirty()" in function_body(npc_sync, "void ClearPuppets("), "ClearPuppets must mark the registry dirty")
check("PublishRegistrySnapshot()" in function_body(npc_sync, "void Reset()"), "Reset must publish the cleared registry")

check("F4T_NPC_" not in npc_sync_header and "F4T_ENABLE_NPC_SYNC" not in npc_sync_header, "F4TNpcSync.h must carry no NPC feature macro")
check("F4T_NPC_" not in xmake and "co_test_features" not in xmake, "xmake.lua must define no NPC feature macro and no test-features switch")
for check_name in ("CommonwealthOnlineNpcSyncCompileCheck",):
    compile_check = re.search(rf'target\("{check_name}"\)(.*?)(?=\ntarget\(|\Z)', xmake, re.S)
    check(compile_check is not None, f"{check_name} target is missing")
    if compile_check:
        block = compile_check.group(1)
        check('set_kind("object")' in block, f"{check_name} must be compile-only")
        check("set_default(false)" in block, f"{check_name} must not build by default")
        check(all(f'"src/{path.name}"' in block for path in npc_sync_paths), f"{check_name} must build every NPC sync source")

guard_header = read_code("include/F4TNpcActorGuard.h")
guard = read_code("src/F4TNpcActorGuard.cpp")
guard_raw = (PLUGIN / "src/F4TNpcActorGuard.cpp").read_text(encoding="utf-8")
check("// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md" in guard_raw,
      "F4TNpcActorGuard.cpp must cite the RE export with // source:")
check("F4T_NPC_" not in guard_header and "kEnableNpcActorGuard" not in guard_header, "F4TNpcActorGuard.h must carry no gate constant")
check("kEnableNpcClampBlock" not in guard_header, "kEnableNpcClampBlock must stay absent")
install = function_body(guard, "bool Install(")
check("VerifySlot(" in install and install.index("VerifySlot(") < install.index("write_vfunc("),
      "the actor guard Install must verify the vtable slot before writing it")
check("IsCoWriteActive()" in function_body(guard, "void HandleHealthDamageHook("), "the actor guard hook must check IsCoWriteActive")
check(re.search(r"kHandleHealthDamageVtableIndex = 0x110;", guard) is not None, "the actor guard slot constant must be 0x110")
check("kEnableNpcActorGuard" not in install, "the actor guard Install must carry no gate")
check("g_originalHandleHealthDamage = " in install and install.index("g_originalHandleHealthDamage = ") < install.index("write_vfunc("),
      "the actor guard Install must assign the original pointer before writing the slot")
check(re.search(r"kKillImplVtableIndex = 0x117;", guard) is not None, "the actor guard KillImpl slot constant must be 0x117")
verify_positions = [match.start() for match in re.finditer(r"VerifySlot\(", install)]
write_positions = [match.start() for match in re.finditer(r"write_vfunc\(", install)]
check(len(verify_positions) == 2 and len(write_positions) == 2 and max(verify_positions) < min(write_positions),
      "both actor guard slots (0x110 and 0x117) must be verified before either write_vfunc")
check(len(write_positions) == 2 and "g_originalKillFn = " in install and install.index("g_originalKillFn = ") < write_positions[1],
      "the actor guard Install must assign the KillImpl original before writing that slot")
check("g_hookInstalled = handleHealthInstalled && killInstalled;" in install,
      "the actor guard Install must set g_hookInstalled only when both slots were written")
kill_hook = function_body(guard, "void KillHook(")
check("IsGuardedFormId(" in kill_hook and "IsCoWriteActive()" in kill_hook and "IsCoKillActive()" in kill_hook,
      "the KillImpl hook must check the guard, IsCoWriteActive() and IsCoKillActive()")
check("bool IsCoKillActive();" in guard_header and "class ScopedCoKill" in guard_header,
      "F4TNpcActorGuard.h must declare IsCoKillActive and ScopedCoKill")
check("bool IsInstalled();" in guard_header, "F4TNpcActorGuard.h must declare IsInstalled")
check("catch (...)" in function_body(guard, "bool IsGuardedFormId("), "IsGuardedFormId must fail open so a throw never leaves the guard detour")
check("catch (...)" in function_body(read_code("src/F4TProxyPuppet.cpp"), "void UpdateHook("), "the proxy UpdateHook proxy branch must contain exceptions")
installed = function_body(guard, "bool IsInstalled(")
check(re.match(r"\{\s*return g_hookInstalled;\s*\}", installed) is not None,
      "IsInstalled must report g_hookInstalled")
main_cpp = read_code("src/main.cpp")
check(re.search(r"ProxyPuppet::Install\(\);\s*F4T::NpcActorGuard::Install\(\);", main_cpp) is not None,
      "main.cpp must call NpcActorGuard::Install() after ProxyPuppet::Install()")
no_ai = function_body(read_code("src/F4TProxyPuppet.cpp"), "bool IsNoAiActorFormID(")
check("NpcSync::IsPuppetFormId(a_formID)" in no_ai, "F4TProxyPuppet.cpp IsNoAiActorFormID must cover NPC puppets")

for relative in ("src/F4TNpcSyncPuppets.cpp", "src/F4TNpcSyncHost.cpp", "src/F4TNpcSyncHealth.cpp", "include/F4TNpcSyncIdentity.h", "include/F4TNpcSyncHealth.h"):
    text = (PLUGIN / relative).read_text(encoding="utf-8")
    check("// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md" in text,
          f"{relative} must cite the RE export with // source:")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TNpcSyncContract: all checks passed.")
