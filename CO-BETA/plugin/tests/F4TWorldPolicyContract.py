from pathlib import Path
import re
import sys

PLUGIN = Path(__file__).resolve().parents[1]
REPO = PLUGIN.parents[1]
failures = []
pending = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(path):
    return re.sub(r"//[^\n]*", "", (PLUGIN / path).read_text(encoding="utf-8"))


def read_root(path):
    return (REPO / path).read_text(encoding="utf-8")


def function_body(text, signature):
    start = text.index("{", text.index(signature))
    depth = 0
    for position in range(start, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[start:position + 1]
    return text[start:]


enforcer = read("src/F4TWorldPolicyEnforcer.cpp")
enforcer_header = read("include/F4TWorldPolicyEnforcer.h")
client = read("src/F4TWorldPolicyClient.cpp")
client_header = read("include/F4TWorldPolicyClient.h")
shim = read("src/F4TGnsWinsockShim.cpp")
sync = read("src/F4TObjectSync.cpp")
sync_header = read("include/F4TObjectSync.h")
inbound = read("src/F4TNetworkingInbound.cpp")
identity = read("src/F4TClientIdentity.cpp")
ui_client = read("src/F4TUIClient.cpp")
plugin_sources = "".join(read(source.relative_to(PLUGIN).as_posix()) for source in (PLUGIN / "src").glob("*.cpp"))
notes = read_root("docs/code-notes/CO-BETA-plugin.md")
co_core_header = read_root("co-core/include/F4TWorldPolicy.h")
adapter = read_root("co-core/src/protocol/F4TGnsProtocolAdapter.cpp")

check(not (PLUGIN / "include/F4TWorldPolicy.h").exists(),
      "the plugin must not shadow the co-core F4TWorldPolicy.h")
check(not (PLUGIN / "include/F4TWorldPolicyWire.h").exists(),
      "the plugin must not keep a second world policy wire parser")
check(not (PLUGIN / "src/F4TWorldPolicyWire.cpp").exists(),
      "the plugin must not keep a second world policy wire parser")
check("Reassembler" not in enforcer and "g_reassembler" not in enforcer,
      "the enforcer must not reassemble; the protocol adapter's co-core reassembler is the single copy")
check("worldPolicyReassembler.Apply(packet)" in adapter,
      "the protocol adapter must reassemble worldPolicy and worldPolicyPart through the co-core reassembler")
check("kMaximumTotalDisableEntries" in enforcer,
      "the disabled-reference tracking cap must be the server's total disable cap")
check("kMaximumTrackedDisabledRefs = F4T::WorldPolicy::kMaximumTotalDisableEntries" in enforcer,
      "the tracking cap must equal the 131072-entry policy list limit")

check("HandleWorldPolicyPacket" not in enforcer and "HandleWorldPolicyPacket" not in enforcer_header
      and "HandleWorldPolicyPacket" not in plugin_sources,
      "the plugin-side packet handler must be gone; no plugin source may dispatch worldPolicy packets")
check("void AcceptPolicy(const F4T::WorldPolicy::Policy& a_policy);" in enforcer_header,
      "the enforcer must expose AcceptPolicy for the adapter's completed policy")
check("TryHandlePacket" not in enforcer_header, "the enforcer must not claim the faction handler name")
check("TryHandlePacket" not in enforcer, "the enforcer must not define a second TryHandlePacket")
check(inbound.count("Factions::TryHandlePacket(") == 1, "the factions inbound dispatch must stay the only inbound factions TryHandlePacket")
check("worldPolicy" not in inbound, "the legacy inbound dispatch must not handle worldPolicy; the adapter consumes it")

shim_wiring = re.search(
    r"const std::scoped_lock protocolLock\(state->protocolMutex\);\s*adapted = state->protocol\.AdaptInbound\([\s\S]*?"
    r"worldPolicy = state->protocol\.GetWorldPolicy\(\);\s*\}\s*if \(worldPolicy\) \{\s*"
    r"F4T::WorldPolicyEnforcer::AcceptPolicy\(\*worldPolicy\);\s*\}",
    shim)
check(shim_wiring is not None,
      "the shim must read the adapted policy under protocolMutex right after AdaptInbound and pass it to AcceptPolicy after the lock is released")
check('#include "F4TWorldPolicyEnforcer.h"' in shim, "the shim must include the enforcer header")

check("kWorldPolicyPartType" not in enforcer and "kWorldPolicyType" not in enforcer,
      "the enforcer must not claim the worldPolicy packet types any more")

check("MarkAsDeleted" not in enforcer and "MarkAsDeleted" not in client,
      "world policy must disable references, never delete them")
check("DeleteOnly" not in client_header,
      "the ref action set must only offer Skip and Disable")
check(re.search(r"RefAction DecideRefAction\(RefState a_state\)", client_header) is not None,
      "the ref decision must depend on the reference state alone")
check(re.search(r"void ReleaseDisabledRefs\(\)[\s\S]{0,200}?ReleaseRef\(refId\)", enforcer) is not None,
      "every disabled reference must be released through ReleaseRef")

check("g_stoppedQuestFlags" in enforcer and re.search(r"void RestoreQuestFlags\(\)", enforcer) is not None,
      "cleared quest flags must be recorded and restored")
check(re.search(r"StopQuest\(RE::TESQuest& a_quest\)\s*\{\s*g_stoppedQuestFlags\.try_emplace", enforcer) is not None,
      "a stopped quest must record its original flags before the sweep clears them")
check("g_allowQuestIds" not in enforcer and "allowQuests" not in client_header,
      "the unused quest whitelist must be gone")

check(re.search(r"void ApplyAdoptedPolicy\(std::uint64_t a_generation\)[\s\S]{0,400}?SweepAttachedCells\(\)", enforcer) is not None,
      "an adopted policy must sweep the loaded cells immediately")
check(re.search(r"void ApplyAdoptedPolicy\(std::uint64_t a_generation\)[\s\S]{0,400}?SweepQueuedCells\(\)", enforcer) is not None,
      "an adopted policy must drain the cell queue immediately")
accept_body = function_body(enforcer, "void AcceptPolicy(")
check("AddTask" not in accept_body and "GetTaskInterface" not in accept_body,
      "the accept path runs under the receive thread's socket lock and must not queue F4SE tasks")
check(re.search(r"void Tick\(std::uint64_t a_nowMs, std::uint64_t a_generation\)[\s\S]{0,600}?ApplyAdoptedPolicy\(a_generation\)", enforcer) is not None,
      "the game-thread Tick must adopt the stored policy")
check("F4T::ReplicationRuntime::CurrentGeneration()" in accept_body,
      "the accept path must stamp the stored policy with the generation it was received in")
check(re.search(r"if \(g_delivered && \*g_delivered == a_policy\) \{\s*return;\s*\}", accept_body) is not None,
      "the same policy must not be re-applied on every inbound packet")
check(re.search(r"g_delivered = a_policy;[\s\S]{0,200}?stored = g_inbound\.Store\(a_policy, generation\);", accept_body) is not None,
      "the delivered policy and the inbound store must be updated under the same lock")
check("g_policyAdopted" in enforcer and "bool HasAdoptedPolicy() noexcept" in enforcer_header,
      "the join flow needs a policy-adopted flag to wait on")
check(re.search(r"if \(a_generation != g_lastGeneration\)[\s\S]{0,200}?ClearSessionPolicy\(a_generation\)", enforcer) is not None,
      "a new session must clear the applied policy")
clear_body = function_body(enforcer, "void ClearSessionPolicy(")
check("ReleaseDisabledRefs()" in clear_body and "RestoreQuestFlags()" in clear_body,
      "leaving a session must re-enable the disabled references and restore the quest flags")
check(re.search(r"g_inbound\.OnSessionGeneration\(a_generation\)", clear_body) is not None,
      "leaving a session must reset the inbound policy state so a stale policy is never applied to the next one")
check(re.search(r"g_delivered\.reset\(\)", clear_body) is not None,
      "leaving a session must forget the delivered policy so the same policy is applied again on the next session")
check("InboundPolicyState" in client_header and
      re.search(r"bool InboundPolicyState::TakeAdoptable\(std::uint64_t a_generation, Policy& a_out\)", client) is not None,
      "the inbound generation, dirty flag and adoption must live in the pure state machine")
check(re.search(r"bool InboundPolicyState::Store\(const Policy& a_policy, std::uint64_t a_generation\)[\s\S]{0,220}?a_policy\.version <= policy_\.version", client) is not None,
      "an older policy version must not overwrite a newer one")
check("void ReleaseForSave()" in enforcer_header and "void ReapplyAfterSave()" in enforcer_header,
      "the save flow must be able to release the enforcement and re-apply it")
check(re.search(r"kPreSaveGame:\s*F4T::WorldPolicyEnforcer::ReleaseForSave\(\);", read("src/main.cpp")) is not None,
      "disabled references and cleared quest flags must be released before the save is written")
check(re.search(r"kPostSaveGame:\s*F4T::WorldPolicyEnforcer::ReapplyAfterSave\(\);", read("src/main.cpp")) is not None,
      "the enforcement must be re-applied once the save is written")
check(re.search(r"F4T::ObjectSync::RefreshTrackedLocks\(\);\s*F4T::WorldPolicyEnforcer::ReapplyAfterSave\(\);", read("src/main.cpp")) is not None,
      "a loaded save must have the enforcement re-applied to it")

check("kMaximumDisableEntriesPerMessage" in co_core_header and
      "kMaximumTotalDisableEntries = 2 * kMaximumClassEntries" in co_core_header,
      "the wire chunk and total caps must come from the co-core header")
check(re.search(r"inline constexpr std::string_view kCapabilityName = \"world-policy-v1\";", co_core_header) is not None,
      "the co-core capability name must be world-policy-v1")
check("metadata.capabilities.emplace_back(std::string{ F4T::WorldPolicy::kCapabilityName });" in identity,
      "the client must claim the world-policy-v1 capability in its hello metadata")
check(re.search(r"if \(expected\.kind == Kind::Door && !g_sharedDoors\.load\(\)\) \{\s*return;\s*\}\s*g_revisions\[\*key\] = revision;", sync) is not None,
      "a door state received while the policy keeps doors vanilla must not consume its revision")

check("SetSharedDoors" in sync_header and re.search(r"void SetSharedDoors\(bool a_sharedDoors\)", sync) is not None,
      "object sync must expose the shared-door policy")
check(re.search(r"const bool door = expected\.kind == Kind::Door;\s*if \(door && !g_sharedDoors\.load\(\)\) \{\s*continue;\s*\}", sync) is not None,
      "door state must not be applied while the policy keeps doors vanilla")
check(re.search(r"const bool published = state\.kind != Kind::Door \|\| \(kEnableDoorOpenState && g_sharedDoors\.load\(\)\);", sync) is not None,
      "door state must not be published while the policy keeps doors vanilla")
check(re.search(r"if \(tracked\.observed\.kind == Kind::Door && !g_sharedDoors\.load\(\)\) \{\s*continue;\s*\}", sync) is not None,
      "a door tracked under a shared policy must be dropped when the policy turns vanilla")
check(re.search(r"void SyncDoorReplication\(\)\s*\{\s*const bool shared = g_applied\.doors == F4T::WorldPolicy::Doors::Shared;", enforcer) is not None,
      "the enforcer must push the applied door policy into object sync")
check(re.search(r"if \(g_sharedDoorsPushed && shared == g_sharedDoorsValue\) \{\s*return;\s*\}", enforcer) is not None,
      "the door mode must only be pushed into object sync when it changes")
check(re.search(r"if \(!g_disableIds\.empty\(\) \|\| !g_disabledByUs\.empty\(\)\)", enforcer) is not None,
      "the world sweep must be skipped while no reference is targeted or disabled")
check(re.search(r"if \(g_policyAdopted\.load\(\)\) \{\s*PushModeToUi\(\);\s*\}", enforcer) is not None,
      "the world mode must not reach the UI before a real policy has been adopted")
check("SetSharedDoors(true)" in sync or "g_sharedDoors{ true }" in sync,
      "doors must default to shared, which is the behaviour without a policy")

check("kWorldSweepIntervalMs = 30000" in enforcer,
      "the full attached-cell sweep must be slow; the cell-loaded sink covers new cells")
check(re.search(r"if \(a_nowMs >= g_nextWorldSweepMs\)", enforcer) is not None,
      "the slow sweep must stay interval-driven")

ui_api = read("include/CommonwealthOnlineUI_API.h")
check(ui_api.count("SetWorldMode") == 1 and re.search(r"struct IVCommonwealthOnlineUI5\s*\{\s*void \(\*SetWorldMode\)", ui_api) is not None,
      "SetWorldMode must appear once, as the first member of IVCommonwealthOnlineUI5; earlier interface versions must not change")
check("g_uiWorld->SetWorldMode" in ui_client,
      "F4TUIClient::PushWorldMode must deliver the mode to the UI through SetWorldMode")
check("COUI::WorldMode::Set(" in read_root("CommonwealthOnlineUI/src/PluginAPI.cpp"),
      "the UI side of SetWorldMode must route into COUI::WorldMode::Set")

for item in pending:
    print(f"PENDING: {item}")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("world policy contract ok")
