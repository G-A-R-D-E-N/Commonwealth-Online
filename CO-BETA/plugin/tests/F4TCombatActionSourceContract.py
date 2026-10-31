import re
from pathlib import Path

root = Path(__file__).resolve().parents[3]
main = (root / "CO-BETA/plugin/src/F4TReplicationRuntimePublish.cpp").read_text(encoding="utf-8")
capture = (root / "CO-BETA/plugin/src/F4TRangedCombatCapture.cpp").read_text(encoding="utf-8")

assert "a_nextActionSequence++" not in main
assert "event.sequence = F4T::RangedCombatCapture::AllocateNonZeroSequence(a_nextActionSequence);" in main
assert "event.sequence = AllocateNonZeroSequence(a_nextActionSequence);" in capture

initial_start = main.index("if (!hasLastTransform)")
initial_end = main.index("const auto networkMovementState", initial_start)
initial = main[initial_start:initial_end]
assert "F4T::CombatActionContract::SelectInitialActions" in initial
assert "const auto sentInitialTransform = F4T::Networking::SendTransformPacket" in initial
assert "RecordSentActions(sentInitialTransform, initialActionEvents);" in initial
assert initial.index("const auto sentInitialTransform") < initial.index("RecordSentActions(sentInitialTransform, initialActionEvents);")
assert "return;" in initial

record_start = main.index("void RecordSentActions")
record_end = main.index("void PollLocalPlayerTransform", record_start)
record = main[record_start:record_end]
assert "F4T::CombatActionContract::CommitSentActions" in record
assert "NoteLocalAttackAction(" in record
assert "actionEvent.captureTime" in record

send_start = main.index("const auto sentTransform = F4T::Networking::SendTransformPacket", initial_end)
send_end = main.index("lastDebugSampleTransform", send_start)
send = main[send_start:send_end]
assert "RecordSentActions(sentTransform, currentActionEvents);" in send
assert send.index("const auto sentTransform") < send.index("RecordSentActions(sentTransform, currentActionEvents);")

note_start = capture.index("void NoteLocalAttackAction")
note_end = capture.index("std::vector<F4T::RemotePlayerState::RemoteActionEvent> CaptureRangedFireEvents", note_start)
note = capture[note_start:note_end]
assert "std::chrono::steady_clock::time_point a_capturedAt" in note
assert "AttackActionContext{ a_sequence, a_type, a_capturedAt }" in note
assert "AttackActionContext{ a_sequence, a_type, now }" not in note
assert "F4T::CombatActionContract::FindUniqueAttackSequence" in capture
assert "F4T::CombatActionContract::IsHitDecidable" in capture
assert "rbegin" not in capture

contract_header = (root / "CO-BETA/plugin/include/F4TCombatActionContract.h").read_text(encoding="utf-8")
assert "rbegin" not in contract_header
assert "FindExactAttackSequence" in contract_header

hook = (root / "CO-BETA/plugin/src/F4TProjectileLaunchHook.cpp").read_text(encoding="utf-8")
install = hook[hook.index("void Install()"):]
assert "kEnableProjectileLaunchHook" not in hook and "kCoTestFeatures" not in hook
version_check = install.index("version != kVerifiedVersion")
resolve = install.index("kLaunchDispatch.address()")
assert version_check < resolve < install.index("F4T::InlineHook::Install(request, LogLine, s_launchHook)")
# source:RE-Work/subsystems/commonwealth-online-re/ranged-hit-identity-co56/README.md (section 10, #219 answer)
og_block = hook[hook.index("#if defined(F4T_TARGET_OG)"):hook.index("#elif defined(F4T_TARGET_NG) || defined(F4T_TARGET_AE)")]
ngae_block = hook[hook.index("#elif defined(F4T_TARGET_NG) || defined(F4T_TARGET_AE)"):hook.index("#error")]
ng_block = ngae_block[ngae_block.index("#if defined(F4T_TARGET_NG)"):ngae_block.index("#else")]
ae_block = ngae_block[ngae_block.index("#else"):ngae_block.index("#endif")]
assert "kLaunchDispatch{ 1452334 }" in og_block and "kVerifiedVersion{ 1, 10, 163 }" in og_block
assert "std::array<std::uint8_t, 20> kExpectedPrologue" in og_block
assert re.sub(r"\s+", " ", "0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x4B, 0x08, 0x53, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00") in re.sub(r"\s+", " ", og_block)
assert "kLaunchDispatch{ 2236958 }" in ngae_block
assert "kVerifiedVersion{ 1, 10, 984 }" in ng_block and "kVerifiedVersion{ 1, 11, 240 }" in ae_block
assert "std::array<std::uint8_t, 15> kExpectedPrologue" in ngae_block
assert "0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57" in ngae_block
detour = hook[hook.index("LaunchDispatchDetour("):hook.index("void Install()")]
assert detour.index("original(a_handleOut, a_launch)") < detour.index("*a_handleOut")
assert "a_launch->shooter != localPlayer" in detour
assert "RE::PlayerCharacter::GetSingleton()" in detour
assert "!a_launch->shooter" in detour
assert detour.index("!a_launch->shooter") < detour.index("a_launch->shooter != localPlayer")
assert "static " not in detour and "thread_local" not in detour and "s_installAttempted" not in detour
assert detour.index("NoteLocalProjectileLaunch") > detour.index("!a_launch->shooter")

capture_init = capture[capture.index("void Initialize()"):capture.index("void Shutdown()")]
assert "F4T::ProjectileLaunchHook::Install();" in capture_init
launch_note = capture[capture.index("void NoteLocalProjectileLaunch"):capture.index("std::vector<F4T::RemotePlayerState::RemoteActionEvent> CaptureRangedFireEvents", capture.index("void NoteLocalProjectileLaunch"))]
assert "F4T::CombatActionContract::FindUniqueAttackSequence(3U, a_now, s_recentAttackActions)" in launch_note
binding = capture[capture.index("std::vector<CombatHit> GetAndClearPendingCombatHits()"):]
assert "candidate.usesHitData" in binding
assert binding.index("F4T::CombatActionContract::FindExactAttackSequence") < binding.index("F4T::CombatActionContract::FindUniqueAttackSequence")

remote_action = (root / "CO-BETA/plugin/src/F4RemoteActionComponent.cpp").read_text(encoding="utf-8")
actions = (root / "CO-BETA/plugin/src/F4TProxyActorControllerActions.cpp").read_text(encoding="utf-8")
remote_state = (root / "CO-BETA/plugin/src/F4TRemotePlayerState.cpp").read_text(encoding="utf-8")
assert "kEnableProxyAttackAnimationReplay" not in contract_header and "kEnableProxyAttackAnimationReplay" not in remote_action
hit_guard = (root / "CO-BETA/plugin/src/F4TProxyAttackHitGuard.cpp").read_text(encoding="utf-8")
main_cpp = (root / "CO-BETA/plugin/src/main.cpp").read_text(encoding="utf-8")
assert main_cpp.index("F4T::NpcActorGuard::Install();") < main_cpp.index("F4T::ProxyAttackHitGuard::Install();")
guard_install = hit_guard[hit_guard.index("void Install()"):hit_guard.index("bool IsInstalled()")]
hit_hook = hit_guard[hit_guard.index("bool HitFrameHandlerHook("):hit_guard.index("bool WeaponFireHandlerHook(")]
weapon_hook = hit_guard[hit_guard.index("bool WeaponFireHandlerHook("):hit_guard.index("std::uintptr_t ReadExecuteHandlerSlot(")]
assert "RE::VTABLE::HitFrameHandler[0]" in guard_install and "RE::VTABLE::WeaponFireHandler[0]" in guard_install
assert guard_install.count("// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md") == 2
assert hit_hook.index("IsProxyActor(a_actor)") < hit_hook.index("return false;") < hit_hook.index("g_originalHitFrameHandler(")
assert weapon_hook.index("IsProxyActor(a_actor)") < weapon_hook.index("return true;") < weapon_hook.index("g_originalWeaponFireHandler(")
assert guard_install.index("if (g_hookInstalled)") < guard_install.index("VtableGuard::VerifySlot(hitFrameRequest)")
assert guard_install.index("VtableGuard::VerifySlot(hitFrameRequest)") < guard_install.index("VtableGuard::VerifySlot(weaponFireRequest)") < guard_install.index("hitFrameVtable.write_vfunc")
assert guard_install.index("g_originalHitFrameHandler = ") < guard_install.index("hitFrameVtable.write_vfunc")
assert guard_install.index("g_originalWeaponFireHandler = ") < guard_install.index("weaponFireVtable.write_vfunc")
assert "g_hookInstalled = hitFrameInstalled && weaponFireInstalled;" in guard_install
assert guard_install.index("VtableGuard::WriteSucceeded(ReadExecuteHandlerSlot(weaponFireRequest.vtableAddress)") < guard_install.index("g_hookInstalled = hitFrameInstalled && weaponFireInstalled;") < guard_install.index("RestoreSlotIfReplaced(hitFrameVtable") < guard_install.index("RestoreSlotIfReplaced(weaponFireVtable")
restore = hit_guard[hit_guard.index("void RestoreSlotIfReplaced("):hit_guard.index("void LogRefusal(")]
assert restore.index("ReadExecuteHandlerSlot(a_request.vtableAddress) == a_request.replacement") < restore.index("a_vtable.write_vfunc(kExecuteHandlerVtableIndex, a_original)")
assert hit_guard.count("g_originalHitFrameHandler = nullptr") == 1 and hit_guard.count("g_originalWeaponFireHandler = nullptr") == 1
assert guard_install.index("ExpectedRvasForRuntime()") < guard_install.index("VtableGuard::VerifySlot(hitFrameRequest)")
assert guard_install.index("SlotMatchesExpectedRva(hitFrameRequest.slotValue, moduleBase, expected->hitFrame)") < guard_install.index("hitFrameVtable.write_vfunc")
assert guard_install.index("SlotMatchesExpectedRva(weaponFireRequest.slotValue, moduleBase, expected->weaponFire)") < guard_install.index("hitFrameVtable.write_vfunc")
assert "kOgRvas{ 0xF6B920, 0xF6E370 }" in hit_guard and "kNgRvas{ 0xD76D40, 0xD79B90 }" in hit_guard and "kAeRvas{ 0xDFCD90, 0xDFFBE0 }" in hit_guard
assert "bool IsInstalled()" in hit_guard and "return g_hookInstalled;" in hit_guard[hit_guard.index("bool IsInstalled()"):]
trigger = remote_action[remote_action.index("bool RemoteActionQueue::TriggerActionAnimation"):]
trigger_gate = trigger.index("if (!F4T::ProxyAttackHitGuard::IsInstalled())")
ranged_start = trigger.index("if (a_action.actionType == 3U)")
assert trigger_gate < ranged_start
assert "return false;" in trigger[trigger_gate:ranged_start]
assert trigger_gate < trigger.index("EnsureProxyReadyToFire(a_proxy, m_readyAlertSentForFront)")
ranged_end = trigger.index("const auto graphAccepted = F4T::ProxyAnimationSync::TrySendProxyGraphEvent(a_proxy, a_action.eventName);", ranged_start)
ranged = trigger[ranged_start:ranged_end]
assert ranged.index("EnsureProxyReadyToFire(a_proxy, m_readyAlertSentForFront)") < ranged.index('TrySendProxyGraphEvent(a_proxy, "attackStart")')
assert ranged.count("TrySendProxyGraphEvent(") == 1 and ranged.count("EnsureProxyReadyToFire(") == 1
assert ranged.count('"attackStart"') == 1
assert "a_action.eventName" not in ranged
assert "attackStartAuto" not in remote_action
assert remote_action.count('TrySendProxyGraphEvent(a_proxy, "attackStart")') == 1
assert "iSyncReadyAlertRelaxed" not in remote_action and "iSyncIdleLocomotion" not in remote_action
assert "GetGraphVariableImplInt" not in remote_action and "RangedFireEventFor" not in remote_action
assert "co309-npc6-ranged-transition-conditions" in remote_action
assert not re.search(r'TrySendProxyGraphEvent\(a_proxy, "attackStart"\)\s*\|\|', remote_action)
replay = remote_action[remote_action.index("bool RemoteActionQueue::ReplayNextAction"):remote_action.index("bool RemoteActionQueue::ApplyActionToProxy")]
assert '#include "F4TProxyAttackHitGuard.h"' in remote_action
assert "F4T::CombatActionContract::IsPermanentReplayFailure( action.eventName.empty(), F4T::ProxyAttackHitGuard::IsInstalled())" in re.sub(r"\s+", " ", replay)
packet_readers = (root / "CO-BETA/plugin/src/F4TNetworkingPacketReaders.cpp").read_text(encoding="utf-8")
assert "!F4T::NetworkValidation::IsAllowedRemoteAction(*type, *eventName)" in packet_readers, "inbound actions must stay allowlisted before they reach replay"
assert "constexpr" not in replay
assert "setalert" not in trigger and "RE::Console::ExecuteCommand" not in trigger
assert "Cannot fire ranged action" not in remote_action
assert trigger.count("if (graphAccepted) {") == 2 and trigger.index("if (graphAccepted) {") < trigger.index("Replayed visual-only ranged fire")
assert "bool m_readyAlertSentForFront{ false };" in (root / "CO-BETA/plugin/include/F4RemoteActionComponent.h").read_text(encoding="utf-8")
ensure = remote_action[remote_action.index("void EnsureProxyReadyToFire("):remote_action.index("void RemoteActionQueue::Enqueue")]
assert ensure.index("boolFlags.reset(RE::Actor::BOOL_FLAGS::kAttackingDisabled)") < ensure.index("if (a_alertSent)") < ensure.index("F4T::ConsoleGuard::ExecutePluginCommand")
enqueue = re.sub(r"\s+", " ", remote_action[remote_action.index("void RemoteActionQueue::Enqueue("):remote_action.index("void RemoteActionQueue::Clear()")])
clear_fn = re.sub(r"\s+", " ", remote_action[remote_action.index("void RemoteActionQueue::Clear()"):remote_action.index("bool RemoteActionQueue::ReplayNextAction")])
assert "m_actionQueue.erase(m_actionQueue.begin()); m_readyAlertSentForFront = false;" in enqueue
assert "if (m_actionQueue.empty()) { m_readyAlertSentForFront = false; } m_actionQueue.push_back(a_action);" in enqueue
assert "m_actionQueue.clear(); m_readyAlertSentForFront = false;" in clear_fn
assert "m_actionQueue.erase(m_actionQueue.begin()); m_readyAlertSentForFront = false; if (applied)" in re.sub(r"\s+", " ", replay)
assert "kEnableProxyWeaponAlertExperiment" not in actions
assert "if constexpr" not in actions, "the proxy weapon alert path must not be gated"
apply_damage = remote_state[remote_state.index("void ApplyDamageToLocalPlayer("):remote_state.index("void ApplyIncomingHit(")]
npc_damage = remote_state[remote_state.index("void ApplyIncomingNpcDamage("):remote_state.index("void ApplyIncomingNpcDamage(") + 400]
assert apply_damage.index("player->DoHitMe(") < apply_damage.index("player->SetBeenAttacked(true);") < apply_damage.index("player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, health, -appliedDamage);")
assert "player.StartCombat" not in remote_state
assert "StartCombat" not in apply_damage and "StartCombat" not in npc_damage
assert "ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage" in remote_state
assert "ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, health, -appliedDamage);" in remote_state

publish_capture = main[main.index("std::vector<F4T::RemotePlayerState::RemoteActionEvent> CaptureMeleeAttackEvents("):main.index("void RecordSentActions")]
assert 'event.eventName = "meleeattackStart";' in publish_capture
assert "kEnableProxyAttackAnimationReplay" not in main and "kEnableProxyAttackAnimationReplay" not in capture
