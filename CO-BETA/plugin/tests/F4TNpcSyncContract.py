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
check(sorted(name for name, _ in kill_calls) == ["F4TNpcSyncHealth.cpp", "F4TNpcSyncPuppets.cpp", "F4TProxyVitals.cpp"],
      f"KillImpl must be called exactly once by the puppet death, once by the authority hit and once by the proxy death sync, found {[name for name, _ in kill_calls]}")
for name, arguments in kill_calls:
    parts = [part.strip() for part in arguments.split(",")]
    check(not re.search(r"[Pp]layer", arguments), f"{name}: KillImpl must not be passed the player: {arguments!r}")
    if name in ("F4TNpcSyncPuppets.cpp", "F4TProxyVitals.cpp"):
        check(parts == ["nullptr", "0.0F", "false", "false"], f"{name}: puppet and proxy KillImpl must have no attacker and send no story event: {arguments!r}")
    else:
        check(parts == ["attacker", "damage", "true", "false"], f"{name}: the authority kill must credit the attacker proxy: {arguments!r}")

death_body = function_body(npc_sync, "bool ApplyPuppetDeath(")
check("std::addressof(a_puppet) == std::addressof(a_player)" in death_body, "ApplyPuppetDeath must refuse the local player")
check("g_suppressedActors.contains(" in death_body, "ApplyPuppetDeath must refuse the hidden original")
check("IsPuppetActor(" in death_body, "ApplyPuppetDeath must only act on a puppet")
check("KillSyncedNpc(a_puppet)" in death_body, "ApplyPuppetDeath must kill through KillSyncedNpc")
check(len(re.findall(r"ApplyPuppetDeath\(", npc_sync)) == 2, "ApplyPuppetDeath must have exactly one definition and one caller")
kill_body = function_body(npc_sync, "bool KillSyncedNpc(")
check(re.search(r"a_actor\.boolFlags\.reset\([^;]*kEssential[^;]*kProtected[^;]*;", kill_body) is not None,
      "the essential and protected bit clear must be present")
check(re.search(r"KillImpl\(\s*nullptr\s*,\s*0\.0F\s*,\s*false\s*,\s*false\s*\)", kill_body) is not None,
      "puppet KillImpl must not send the story event (sendEvent=false, ragdollInstant=false)")
check(kill_body.index("boolFlags.reset(") < kill_body.index("KillImpl("), "the essential and protected bits must be cleared before the kill")
internal_header = read_code("include/F4TNpcSyncInternal.h")
check(all(f"visit(processLists->{name});" in internal_header for name in ("highActorHandles", "middleHighActorHandles", "middleLowActorHandles", "lowActorHandles")),
      "ForEachLoadedActor must visit all four process lists so a suppressed copy in the low or middle-low list is still found")

health_source = strip_comments((PLUGIN / "src/F4TNpcSyncHealth.cpp").read_text(encoding="utf-8"))
npc_sync_outside_health = "\n".join(strip_comments(path.read_text(encoding="utf-8")) for path in npc_sync_paths if path.name not in ("F4TNpcSyncHealth.cpp", "F4TNpcSyncCombat.cpp"))
npc_sync_outside_health += "\n" + health_source.replace(function_body(health_source, "HitRoute ApplyNpcHit("), "")
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
    check("FindRuntimeClaim(" in body, f"{short_name(signature)} must claim runtime-spawned NPCs by base and position")
capture = function_body(npc_sync, "std::vector<NpcState> CaptureHostSnapshot(")
check("WireSourceFormId(actor.GetFormID())" in capture and "SyncHostNpcIds(loadedRuntimeBases)" in capture, "capture must send runtime-created refs with the zero source and sync the host ids of loaded runtime refs")
rank_body = function_body(npc_sync, "int HostRank(")
check("HostRank(a_actor, now)" in capture and "a_left.rank < a_right.rank" in capture
      and 0 <= capture.find("std::ranges::sort(candidates, [](") < capture.find("candidates.resize(kMaxSyncedNpcs)"),
      "the host snapshot must sort its candidates by HostRank and cap the sent list after that sort")
core_rank_src = (ROOT / "co-core/src/npc/F4TNpcSyncPolicy.cpp").read_text(encoding="utf-8")
check("NpcSyncPolicy::HostRank(" in rank_body and "kFreshDeathWindow" in rank_body and "IsDead(true)" in rank_body
      and "? 0 : 2" in core_rank_src and "? 1 : 3" in core_rank_src and "kFreshDeathWindow" in core_rank_src,
      "HostRank must rank living runtime NPCs first, then freshly dead NPCs, then living placed NPCs, then older dead NPCs")
check("g_hostDeadSince" in rank_body and "HostNpcIdRecordFor(" not in rank_body and "g_hostNpcIdsByActorFormId" not in rank_body,
      "HostRank must keep its dead-since stamps apart from the host id records: a rank must not create a record")
check(re.search(r"g_hostDeadSince\.try_emplace\(actorFormId, a_now - kFreshDeathWindow\)", rank_body) is not None
      and re.search(r"g_hostDeadSince\[actorFormId\] = Clock::time_point\{\};", rank_body) is not None,
      "an NPC first seen dead ranks as an older death, and an NPC seen alive restarts its fresh window at its next death")
check("std::erase_if(g_hostDeadSince" in capture and "loadedFormIds.contains(" in capture,
      "the dead-since stamps of actors that are no longer loaded must be pruned in the host snapshot")
check("deadSince" not in read_code("include/F4TNpcSyncInternal.h"),
      "HostNpcIdRecord must not carry a dead-since stamp")
sync_host_ids = function_body(npc_sync, "void SyncHostNpcIds(")
check("g_hostNpcIdsByActorFormId.erase(" in sync_host_ids and "ForgetLootContainer(" in sync_host_ids and "NpcSyncPolicy::ResolveHostNpcIdRecord(" in sync_host_ids and "kMissingHostNpcIdGrace" in sync_host_ids,
      "an unloaded runtime ref keeps its host id through the grace, then drops it and forgets its loot container")
check("HostNpcIdStep::kReissue" in sync_host_ids and "AllocateNpcId()" in sync_host_ids,
      "a host id record whose base no longer matches the loaded actor must be re-issued")
check(npc_sync.count("record.missingSince != ") >= 5, "every reader of the host id map must skip a record in its grace: its actor is not loaded")
forget = function_body(npc_sync, "void ForgetLootContainer(")
check("g_dirtyContainers.erase(" in forget and "g_publishedContainers.erase(" in forget, "forgetting a loot container must clear its dirty and published marks")
check("StableBaseFormId(" in capture, "capture must send the stable base")
check("WireSourceFormId(containerFormId)" in npc_sync and "WireSourceFormId(actorFormId)" in npc_sync, "loot containers and host NPC ids must send runtime sources as the zero wire source")
send = function_body(outbound, "bool SendNpcStatePacket(")
check("IsRuntimeFormId(npc.baseFormId)" in send and "IsRuntimeFormId(npc.sourceFormId)" in send,
      "SendNpcStatePacket must drop runtime base and source ids")
check('entry["runtimeNpc"] = true' in send, "SendNpcStatePacket must flag zero-source runtime NPCs with runtimeNpc")
receive = function_body(inbound, "void HandleNpcStatePacket(")
check("IsRuntimeFormId(*baseFormId)" in receive and "IsValidNpcSourceFormId(*sourceFormIdText, runtimeNpc)" in receive and 'ReadBool(npc, "runtimeNpc")' in receive,
      "HandleNpcStatePacket must drop runtime base ids and validate runtime source ids with runtimeNpc")
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
check(re.search(r"NpcSyncPolicy::IsDeathRetryDue\(\s*std::chrono::duration_cast<std::chrono::milliseconds>\(deathNow - slot->second\.lastDeathAttempt\),\s*slot->second\.deathAttempts\)\)\s*\{.*?slot->second\.lastDeathAttempt = deathNow;", update_puppets, re.S) is not None,
      "death retries must be gated on NpcSyncPolicy::IsDeathRetryDue and must stamp lastDeathAttempt")
check(re.search(r"slot->second\.deathAttempts < NpcSyncPolicy::kMaximumDeathBackoffStep", update_puppets) is not None, "death attempts must saturate at the backoff step")
check("kMaxDeathAttempts" not in npc_sync and "kDeathRetryInterval" not in npc_sync, "the puppet death retry must have no attempt cap or fixed interval")
check("void LogPuppetDelete(" in npc_sync and "LogPuppetDelete(iterator->first" in update_puppets and '"puppet delete' in npc_sync,
      "stale puppet deletes must be logged through LogPuppetDelete")
check("void RedrawPuppetIfHidden(" in npc_sync and "RedrawPuppetIfHidden(*actor" in update_puppets and '"puppet redraw' in npc_sync,
      "hidden puppets must be redrawn through RedrawPuppetIfHidden")
check("g_createdPuppetBases" in npc_sync and "PurgeStalePuppets()" in npc_sync, "created puppets must be recorded and purged")
check("PurgeStalePuppets()" in function_body(npc_sync, "void Update("), "Update must run the stale puppet sweep")
check("g_stalePuppetSweepDone = false" in function_body(npc_sync, "void Reset()"), "Reset must re-arm the stale puppet sweep")

update_body = function_body(npc_sync, "void Update(")
first_gate = function_body(update_body, "a_runGameplay(")
second_gate = function_body(update_body[update_body.index("Loot::Tick("):], "a_runGameplay(")
check("Detail::UpdatePuppets(" in first_gate and "Health::Tick()" in first_gate and
      "Combat::Tick()" in second_gate and "Detail::UpdateHost(" in second_gate,
      "NPC puppet, health, combat and host work must pass through the gameplay gate")
check(update_body[:-1].rstrip().endswith("Detail::PublishRegistrySnapshot();"), "Update must call Detail::PublishRegistrySnapshot() last")
check(update_body.count("PublishRegistrySnapshot()") == 1, "Update must publish the registry snapshot exactly once")
check(
    update_body.index("Detail::UpdatePuppets(") < update_body.index("Health::Tick()") < update_body.index("Loot::Tick(") < update_body.index("Combat::Tick()") < update_body.index("Detail::UpdateHost("),
    "Update must run the Health, Loot and Combat Tick() calls between UpdatePuppets and UpdateHost")
check("MarkRegistryDirty()" in function_body(npc_sync, "void ClearPuppets("), "ClearPuppets must mark the registry dirty")
check("PublishRegistrySnapshot()" in function_body(npc_sync, "void Reset()"), "Reset must publish the cleared registry")
check("g_puppetClearPending" not in npc_sync, "an authority flip must not clear the puppets: slot keys carry the host and epoch")
check("ClearPuppets(" not in function_body(npc_sync, "void Update("), "Update must not clear the puppets on an authority flip")
adopt = function_body(npc_sync, "void AdoptOrphanSlots(")
check(".extract(" in adopt and "MarkRegistryDirty()" in adopt, "AdoptOrphanSlots must re-key the orphan slot in place and mark the registry dirty")
check("kRuntimeAdoptDistance" in adopt and "state.baseFormId" in adopt, "AdoptOrphanSlots must also re-adopt runtime puppets (sourceFormId 0) by base form, cell and distance")
check("NpcSyncPolicy::IsOrphanAdoptable(" in adopt and "NpcSyncPolicy::AssignNearestOrphans(" in adopt and "NpcSyncPolicy::ResetAdoptedSlot(" in adopt,
      "AdoptOrphanSlots must gate on IsOrphanAdoptable, assign globally nearest through AssignNearestOrphans and reset the adopted slot")
check("AdoptOrphanSlots(states)" in update_puppets and "AdoptOrphanSlot(key" not in npc_sync and "sourceAlreadySpawned" not in npc_sync,
      "a source whose slot sits under an old key must adopt that slot instead of blocking the respawn")
check(0 <= update_puppets.find("std::erase_if(g_runtimeClaims") < update_puppets.find("AdoptOrphanSlots(states)") < update_puppets.find("SuppressDuplicateLocalNpcs(states"),
      "orphan adoption must run after the runtime claims are pruned and before duplicate suppression")
redraw = function_body(npc_sync, "void RedrawPuppetIfHidden(")
check("NpcSyncPolicy::IsRedrawEpisodeSettled(" in redraw and "lastNeeded" in redraw,
      "the redraw episode must reset only after the puppet stayed clean for the settle period, measured from lastNeeded")
dedupe = function_body(npc_sync, "void DropDuplicateRuntimeStates(")
check("NpcSyncPolicy::FindRuntimeDuplicateLosers(" in dedupe and "DropDuplicateRuntimeStates(states, localCellId)" in update_puppets
      and update_puppets.find("DropDuplicateRuntimeStates(states") < update_puppets.find("std::erase_if(g_runtimeClaims, [&](const auto& a_entry) { return !states.contains"),
      "runtime NPC states from two hosts must be matched one-to-one nearest-first and only the loser of each pair dropped, before the claims are pruned")
check("NpcSyncPolicy::DecidePuppetRedraw(" in redraw and "kMaximumPuppetRedraws" in redraw and "PuppetNeedsRedraw(" in redraw,
      "RedrawPuppetIfHidden must be throttled and capped through DecidePuppetRedraw")
check("GetAppCulled()" in function_body(npc_sync, "bool PuppetNeedsRedraw(") and "IsVisible()" not in function_body(npc_sync, "bool PuppetNeedsRedraw("),
      "a puppet needs a redraw only for alpha, disabled or an app-culled root in the local location")
check("NpcSyncPolicy::AcceptRemoteNpcState(scoped.hostPlayerId, localPlayerId, LocalHostSendsState(key, state, a_player, loadedActors))" in update_puppets,
      "overlapping remote copies must resolve to the lower player id, runtime copies included")
check(update_puppets.index("BuildLoadedActorIndex()") < update_puppets.index("AcceptRemoteNpcState("),
      "the loaded actor index must be built before the policy reads the runtime claims")
check("g_runtimeClaims.erase(key)" in update_puppets and 0 <= update_puppets.find("std::erase_if(g_runtimeClaims") < update_puppets.find("SuppressDuplicateLocalNpcs(states"),
      "a rejected key must release its runtime claim, and the claims of keys that left the state set must be pruned before duplicate suppression reads them")
check(update_puppets.count("g_runtimeClaims.erase(key);") >= 6,
      "every key that UpdatePuppets rejects must release its runtime claim, not only the policy reject")
check(re.search(r"IsStateInLocalLocation\(state, a_player\)\)\s*\{\s*g_runtimeClaims\.erase\(key\);\s*continue;", update_puppets) is not None,
      "a key outside the local location or with an unusable identity must release its runtime claim before it is skipped")
check("g_runtimeClaims.clear()" in function_body(npc_sync, "void ClearPuppets("),
      "clearing the puppets must drop every runtime claim")
local_sends = function_body(npc_sync, "bool LocalHostSendsState(")
check("FindRuntimeClaim(" in local_sends and "g_lastHostSentRuntimeNpcIds.contains(" in local_sends,
      "a runtime copy counts as sent only when its claimed local actor is in the last host snapshot")
check("g_lastHostSentRuntimeNpcIds.insert(state.npcId)" in function_body(npc_sync, "void UpdateHost(") and "g_lastHostSentRuntimeNpcIds.clear()" in function_body(npc_sync, "void ClearHostNpcIds("),
      "the host must record the npcIds of the runtime NPCs it sent and forget them when it stops hosting")
claim_body = function_body(npc_sync, "RE::Actor* FindRuntimeClaim(")
check("a_state.isDead);" in claim_body, "the runtime claim must prefer a candidate with the same dead state as the NPC state")
check("NpcSyncPolicy::ResolveRuntimeClaim(g_runtimeClaims, a_key" in claim_body and "candidates.push_back(" in claim_body and "IsRuntimeClaimCandidate(actor, a_state, a_player)" in claim_body,
      "FindRuntimeClaim must hand the eligible loaded runtime actors to the policy, which keeps one claim per key and skips any actor another key already holds")
check("ClaimedByAnotherKey" not in npc_sync,
      "the claim exclusivity rule lives in the co-core policy, not in a plugin helper")
core_policy = (ROOT / "co-core/include/F4TNpcSyncPolicy.h").read_text(encoding="utf-8")
check("claimKey != a_key && claimedFormId == a_actorFormId" in core_policy and "a_now - missingSince < a_grace" in core_policy,
      "the co-core policy must hold the claim exclusivity and the host id grace rules")
core_tests = (ROOT / "co-core/tests/F4TNpcSyncPolicyTests.cpp").read_text(encoding="utf-8")
check("kNotLoaded, 5999ms, 5s)" in core_tests and "kNotLoaded, 6000ms, 5s)" in core_tests and "ResolveRuntimeClaim(far, 1" in core_tests,
      "the co-core tests must pin the 5 s grace boundary and the claim distance boundary")
claim_candidate = function_body(npc_sync, "bool IsRuntimeClaimCandidate(")
check("CachedStableBaseFormId(a_actor) == a_state.baseFormId" in claim_candidate and "F4T::NpcSync::StableBaseFormId(" not in claim_candidate,
      "a runtime claim candidate must compare the stable base from the per-tick cache, not resolve it again")
cached_base = function_body(npc_sync, "std::uint32_t CachedStableBaseFormId(")
check("g_stableBaseCache.emplace(" in cached_base and "F4T::NpcSync::StableBaseFormId(" in cached_base,
      "CachedStableBaseFormId must fill the per-tick cache from the stable base on a miss")
check("g_stableBaseCache.clear()" in function_body(npc_sync, "LoadedActorIndex BuildLoadedActorIndex("),
      "the per-tick stable base cache must be cleared when the loaded actor index is rebuilt")
check(re.search(r"state\.sourceFormId != 0\)\s*\{\s*g_lastHostSentSources\.insert\(state\.sourceFormId\);", npc_sync) is not None,
      "only nonzero sources may enter g_lastHostSentSources: a runtime NPC has no source to echo")
check("g_suppressedActors.contains(" in capture, "the host must not capture a copy it hid for a remote puppet")
check("g_puppetSlots" in function_body(npc_sync, "void SuppressDuplicateLocalNpcs("), "a local copy may be hidden only while a live puppet slot holds its source")
begin_spawn = function_body(npc_sync, "bool BeginSpawn(")
check("GetFormByID<RE::TESNPC>(a_state.baseFormId)" in begin_spawn and "a_player.GetParentCell()" in begin_spawn and "if (!sourceActor)" not in begin_spawn,
      "BeginSpawn must fall back to the base NPC in the player's cell when the local source copy is not loaded")
check("ResolvePendingSpawn()" in function_body(npc_sync, "bool IsPuppetActor("), "a pending spawn must count as a puppet: its actor has no slot yet and must not be matched as a local source")
check("!FindMatchingSourceActor(key, state, a_player, loadedActors)" not in update_puppets and "GetFormByID<RE::TESNPC>(state.baseFormId)" in update_puppets,
      "a remote NPC must spawn from its base without a loaded local source copy")
check("LogMirrorPolicySkip" not in npc_sync, "an aliased NPC must not be skipped: a live local alias is the mirror and otherwise a mirror copy is spawned from the base")
check(re.search(r"IsUniqueOrAliasedPlacedRef\(state\.sourceFormId\)\)\s*\{\s*auto\* alias = FindMatchingSourceActor\(key, state, a_player, loadedActors\);\s*if \(alias && !alias->IsDead\(true\)\)", update_puppets) is not None,
      "a live local alias must be the mirror for an aliased NPC and must not be duplicated")
check("kAliasCorrectionDistance" in update_puppets and "SetPosition(target, true)" in update_puppets and "!state.isDead" in update_puppets,
      "a local alias may be teleported only when the remote NPC is alive, in the same cell and more than the correction distance away")
check("IsCellInStateLocation(alias->GetParentCell(), state)" in update_puppets and "aliasCell" not in update_puppets and "IsBusyWithScriptedAction(*alias)" in update_puppets,
      "a local alias must be corrected within the remote NPC's cell or worldspace and never while it is in dialogue or in a scene")
check("a_cell->worldSpace" in function_body(npc_sync, "bool IsCellInStateLocation("),
      "an exterior alias must match the remote NPC's worldspace, not only its exact cell")
busy_body = function_body(npc_sync, "bool IsBusyWithScriptedAction(")
check(all(token in busy_body for token in ("kScenePackage", "kInRandomScene", "dialoguewithPlayer", "startedDialogue")) and "GetPackageThatIsRunning" not in busy_body,
      "IsBusyWithScriptedAction must cover the scene flags and the player dialogue flags, and never a bare running package: nearly every AI NPC has one")
kill_start = update_puppets.index("if (state.isDead && !F4T::NpcSync::IsUniqueOrAliasedPlacedRef(state.sourceFormId))")
kill_block = update_puppets[kill_start:update_puppets.index("if (F4T::NpcSync::IsUniqueOrAliasedPlacedRef(state.sourceFormId)) {", kill_start)]
check("KillSyncedNpc(*local)" in kill_block and "IsBusyWithScriptedAction(*local)" in kill_block and "NpcSyncPolicy::IsDeathRetryDue(" in kill_block and "g_localKillRetries" in kill_block,
      "a dead remote NPC must kill its live local copy before the alias and puppet paths run, skip a copy busy with a scripted action, and retry under the death backoff")
check(kill_block.count("IsUniqueOrAliasedPlacedRef(") == 1,
      "a unique or aliased placed ref is never killed from a remote state: it stays visible and is never duplicated")
check(re.search(r"IsBusyWithScriptedAction\(\*local\)\)\s*\{\s*continue;", kill_block) is not None,
      "a remote death whose live local copy is busy with a scripted action must skip that copy whole: no kill, no spawn, no hide")
check("IsAliasedRefScanReady()" in update_puppets and update_puppets.index("IsAliasedRefScanReady()") < kill_start,
      "a dead remote NPC must wait for the aliased-ref scan before any kill, spawn or hide")
check("AliasedRefScanComplete() = true" in read_code("include/F4TNpcSyncIdentity.h"),
      "the aliased-ref scan may mark itself complete only after a full pass")
check("std::erase_if(g_localKillRetries" in update_puppets and "IsBusyWithScriptedAction(*local->second)" in update_puppets,
      "a kill retry entry must be dropped once its local copy is dead, busy, gone or not loaded")
check("state.sourceFormId != F4T::NpcSync::kCodsworthPlacedRefFormId" in update_puppets,
      "Codsworth is never moved from a remote state: his local placed ref stays put and is never duplicated")
check("keepAlive" not in npc_sync, "a mirror copy has no keepAlive exemption: remote death kills it with its state")
check(re.search(r"state\.isDead && !slot->second\.deathApplied &&\s*NpcSyncPolicy::IsDeathRetryDue\(", update_puppets) is not None,
      "remote death must kill a mirror copy: the death condition must not exclude it")

check("F4T_NPC_" not in npc_sync_header and "F4T_ENABLE_NPC_SYNC" not in npc_sync_header, "F4TNpcSync.h must carry no NPC feature macro")
check("F4T_NPC_" not in xmake and "co_test_features" not in xmake, "xmake.lua must define no NPC feature macro and no test-features switch")

guard_header = read_code("include/F4TNpcActorGuard.h")
guard = read_code("src/F4TNpcActorGuard.cpp")
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

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TNpcSyncContract: all checks passed.")
