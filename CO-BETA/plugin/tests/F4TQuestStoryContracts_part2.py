#!/usr/bin/env python3
"""F4TQuestStoryContracts_part2: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TQuestStoryContracts_part2.py
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




def _section_004_f4tfirstjoinattemptcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TFirstJoinAttemptContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TFirstJoinAttemptContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


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
        raise AssertionError(signature)


    def ordered(text, markers):
        positions = [text.find(marker) for marker in markers]
        return all(position != -1 for position in positions) and all(a < b for a, b in zip(positions, positions[1:]))


    header = (PLUGIN / "include/F4TFirstJoinAttempt.h").read_text(encoding="utf-8")
    internal = (PLUGIN / "src/F4TSessionLaunchInternal.h").read_text(encoding="utf-8")
    saves = (PLUGIN / "src/F4TSessionLaunchSaves.cpp").read_text(encoding="utf-8")
    state = (PLUGIN / "src/F4TSessionLaunchState.cpp").read_text(encoding="utf-8")
    onboarding = (PLUGIN / "src/F4TSessionLaunchOnboarding.cpp").read_text(encoding="utf-8")
    launch = (PLUGIN / "src/F4TSessionLaunch.cpp").read_text(encoding="utf-8")
    queue = (PLUGIN / "src/F4TSessionSaveQueue.cpp").read_text(encoding="utf-8")
    queue_header = (PLUGIN / "include/F4TSessionSaveQueue.h").read_text(encoding="utf-8")
    lineage = (PLUGIN / "src/F4TSaveLineageSession.cpp").read_text(encoding="utf-8")
    lineage_header = (PLUGIN / "include/F4TSaveLineageSession.h").read_text(encoding="utf-8")
    profile = (PLUGIN / "src/F4TServerProfileStore.cpp").read_text(encoding="utf-8")
    client_header = (PLUGIN / "include/F4TSurvivalPolicyClient.h").read_text(encoding="utf-8")
    inbound = (PLUGIN / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
    replication = (PLUGIN / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8")
    recovery = (PLUGIN / "src/F4TServerProfileStoreRecovery.cpp").read_text(encoding="utf-8")

    for signature in ("inline bool CallbackAccepted(", "inline bool EntryAccepted(", "inline bool EntryPending(",
                      "inline bool SettleExpired(", "inline std::uint64_t NextId()"):
        check(signature in header, f"the attempt header must define {signature}")

    check("extern std::optional<F4T::FirstJoinAttempt::Token> g_firstJoinAttempt;" in internal
          and "void TryCompleteFirstJoinSettle();" in internal,
          "the launch internals must declare the attempt token and the settle poll")
    check("std::uint64_t LoadEpoch()" in client_header,
          "the survival client must expose the load epoch that the attempt scope records")

    queue_fn = saves[saves.index("std::optional<bool> QueueProfileSave()"):saves.index("std::optional<std::string> BindableCharacterId")]
    check("FirstJoinAttempt::NextId()" in queue_fn
          and 'StartQueuedSave(F4T::SessionSaveGuard::ArmOwner::kFirstJoin, "queued first-join save", token.id)' in queue_fn
          and "g_firstJoinAttempt = std::move(token);" in queue_fn,
          "QueueProfileSave must mint the attempt token and pass its id into the queued save")

    callback = function_body(saves, "void TryCompletePendingProfileBind(")
    check("BindSaveFile" not in callback and "CompletePendingJoin" not in callback,
          "the gameplay save callback must not bind the server profile or complete the join")
    check("FirstJoinAttempt::CallbackAccepted(" in callback and "g_firstJoinSettle = FirstJoinSettle{" in callback,
          "the gameplay save callback must validate the attempt and only then start the settle")

    settle = function_body(saves, "void TryCompleteFirstJoinSettle()")
    check(ordered(settle, [
              "FirstJoinAttempt::SameScope(",
              "FirstJoinAttempt::SettleExpired(",
              "FirstJoinAttempt::EntryPending(",
              "FirstJoinAttempt::EntryAccepted(",
              "F4T::SaveLineageSession::ExpectFirstJoinAttestation(",
              "F4T::SaveLineageSession::AttestFirstJoinSave(",
              "F4T::SaveLineageSession::FirstJoinAttestationOutcome()",
              "ServerProfileStore::BindSaveFileClearingHold(",
              "CompletePendingJoin(true",
          ]),
          "the settle poll must bind and complete only after the server accepts the first save attestation")
    check("BindSaveFile(" not in settle and "CompletePendingJoin(true" in settle,
          "the settle poll must clear the hold in the same bind that completes the join")

    fail = function_body(saves, "void FailFirstJoinAttempt(")
    check("ServerProfileStore::RecordFirstJoinHold(" in fail and "FailFirstTimeJoin(" in fail
          and 'SetActiveServerKey("");' in fail and "BindSaveFile" not in fail,
          "a failed first-join attempt must record the hold, clear the active profile key, and never bind")
    check("keep your saves, and contact the server administrator before rejoining" in fail,
          "an ambiguous first save must direct the player to recovery before rejoining")

    check(ordered(launch, ["TryCompleteFirstJoinSettle();", "if (++g_spawnPollFrameCounter % kSpawnPollIntervalFrames != 0) {"]),
          "the first-join settle must be polled every frame before the spawn gate")
    check(re.search(r"TryCompletePendingProfileBind\(onboardingSave\);\s*if \(g_pendingProfileBind\) break;", launch) is not None,
          "the post-save message must stop before the lineage bind while a first-join settle is pending")

    check(re.search(r'if \(!g_firstJoinSettle\.has_value\(\) && WaitDeadlinePassed\(\)\)\s*\{\s*FailFirstJoinAttempt\(', onboarding) is not None,
          "the first-join deadline must fail for recovery rather than replacing the attempt")
    check('FailFirstJoinAttempt("Timed out waiting for the first gameplay save to bind the server profile.")' in onboarding,
          "a first-join timeout must fail the attempt")
    check("if (!g_firstJoinSettle.has_value() && WaitDeadlinePassed()) {" in onboarding,
          "the spawn-wait deadline must not fail a first-join attempt while the settle deadline governs it")

    clear_body = function_body(state, "void ClearFirstJoinWorkingState(")
    check("g_firstJoinAttempt.reset();" in clear_body and "g_firstJoinSettle.reset();" in clear_body,
          "clearing the first-join working state must drop the attempt token and the settle")
    check("ClearFirstJoinWorkingState(a_reason);" in function_body(state, "void CompletePendingJoin("),
          "every completed or failed join must clear the first-join working state")

    check("g_firstJoinEntry = FirstJoinAttempt::Entry{ a_attemptId, false, 0 };" in queue and "return g_firstJoinEntry;" in queue,
          "the queue must record one first-join entry per attempt and hand out a copy")
    note = function_body(queue, "void NoteEntered(")
    check(ordered(note, ["if (a_result) {", "++g_firstJoinEntry.count;", "state.wanted = false;"]),
          "the first-join entry counts only a save the engine accepted, and the accepted save clears the wanted flag")
    check("FirstJoinAttempt::Entry FirstJoinEntry();" in queue_header and "std::uint64_t a_attemptId = 0);" in queue_header,
          "the queue header must expose the attempt id and the first-join entry")

    record = function_body(lineage, "bool RecordActiveSave(")
    check("return false;" in function_body(record, "if (!persisted)") and
          ordered(record, ["if (!persisted)", "return false;", "AnnounceSaveAttestation(a_serverKey, save"]),
          "an attestation must not be announced unless local persistence succeeded")
    announce = function_body(lineage, "bool AnnounceSaveAttestation(")
    check("return false;" in announce and "return true;" in announce,
          "the attestation announce must report failure to its caller")

    check('entry.erase("firstJoinHold");' not in function_body(profile, "bool BindSaveFile("),
          "a save binding must not clear a first-join hold")
    check("BindSaveFileWithHold(a_serverKey, a_saveFile, true)" in function_body(profile, "bool BindSaveFileClearingHold(")
          and 'entry.erase("firstJoinHold");' in function_body(profile, "bool BindSaveFileWithHold("),
          "only the clearing bind may erase the first-join hold, in the same atomic profile write")
    check("if (!TryLoadProfilesFile(candidate, persisted) || hasHold(persisted, true)) return true;" in profile
          and "if (std::filesystem::exists(pending, pendingError) || pendingError) return true;" in profile,
          "an unreadable or held profile store, or a pending write, must keep the first-join hold fail-closed")
    check(ordered(function_body(profile, "bool RefreshBackupFromPrimary("), ["attempt < 3", "copy_file(", "return true;", "return false;"]),
          "refreshing the backup after a clear must retry a bounded number of times before failing")
    launch_session = function_body(launch, "LaunchOutcome LaunchValidatedSession(")
    check(ordered(launch_session, ["ServerProfileStore::RepairFirstJoinBackup(profileKey);", "!resumeOnboarding && ServerProfileStore::HasFirstJoinHold(profileKey)", "auto saveFile = ServerProfileStore::LookupSaveFile(profileKey)", "StartFirstTimeJoin(profileKey"]),
          "a held profile must repair a lost backup, then be rejected before onboarding unless the vault resume gate passes")
    check(ordered(function_body(recovery, "void RepairFirstJoinBackup("), ["g_storeMutex", "TryLoadProfilesFile(path, primary)", "RefreshBackupFromPrimary()"]),
          "the backup repair must hold the store lock and refresh the backup only from a readable primary")
    hold = function_body(profile, "bool RecordFirstJoinHold(")
    check("g_profiles = previousProfiles;" in hold and "SaveProfiles()" in hold,
          "recording the hold must roll the profile store back when the save fails")
    start = function_body(onboarding, "F4T::SessionLaunch::LaunchOutcome StartFirstTimeJoin(")
    check(ordered(start, ["ServerProfileStore::RecordFirstJoinHold(", "LaunchOutcome::Failed;", "StartVanillaNewGame()"]),
          "first join must persist a recovery hold before starting a new game, and refuse to start when persistence fails")

    offer_fn = function_body(lineage, "bool HandleFirstJoinRecoveryOffer(")
    check("SendFirstJoinRecoveryConsume(" in offer_fn and "ShowFirstJoinManualReconciliation(" in offer_fn,
          "a staged recovery offer must send the consume, and a refused offer must show manual reconciliation")

    consumed_fn = function_body(lineage, "bool HandleFirstJoinRecoveryConsumed(")
    check(ordered(consumed_fn, ["LookupFirstJoinRecovery(", "HashSaveForRecovery(", "FirstJoinRecoveryApplies(", "TryRunForConnectionGeneration(", "ApplyFirstJoinRecovery("])
          and consumed_fn.count("g_recoveryMutex") == 1 and consumed_fn.rfind("ResetRecoveryRequest(") > consumed_fn.index("ApplyFirstJoinRecovery("),
          "a consumed reply must hash the save outside the recovery lock, check the approval and profile, then apply once under the connection generation")

    rejected_fn = function_body(lineage, "bool HandleFirstJoinRecoveryRejected(")
    check("ApplyFirstJoinRecovery(" not in rejected_fn and "ShowFirstJoinManualReconciliation(" in rejected_fn,
          "a rejected reply must change nothing and show manual reconciliation")

    manual_fn = function_body(lineage, "void ShowFirstJoinManualReconciliation(")
    check("save-recovery-resolve" in manual_fn and "a_saveFile" in manual_fn and "a_serverSha256" in manual_fn,
          "the manual reconciliation message must name the save, the admin command, and the server hash")

    send_fn = function_body(lineage, "bool SendFirstJoinRecoveryConsume(")
    check('"saveRecoveryConsume"' in send_fn and "consumeSentAt" in send_fn,
          "the consume must be sent as saveRecoveryConsume and record when it was sent")

    tick_fn = function_body(lineage, "void TickFirstJoinRecovery(")
    check("kRecoveryConsumeTimeout" in tick_fn and "std::chrono::seconds{ 15 }" in lineage and "ApplyFirstJoinRecovery" not in tick_fn,
          "the consume timeout must be 15 seconds and must not touch the profile store")

    check('"saveRecoveryConsumed"' in inbound and "HandleFirstJoinRecoveryConsumed(" in inbound
          and '"saveRecoveryRejected"' in inbound and "HandleFirstJoinRecoveryRejected(" in inbound,
          "the inbound dispatcher must route saveRecoveryConsumed and saveRecoveryRejected")
    check("TickFirstJoinRecovery();" in replication, "the replication tick must drive the recovery timeout")

    hash_fn = function_body(lineage, "std::optional<std::string> HashSaveForRecovery(")
    check("SaveFilePath(a_saveFile, true)" in hash_fn and "EngineSaveDirectory(folder)" not in hash_fn,
          "the recovery hash must locate the save through SaveFilePath, not the engine directory alone")
    check("HashSaveForRecovery(" not in function_body(lineage, "void ShowFirstJoinManualReconciliation("),
          "the manual reconciliation message must take its local hash from the caller, outside the recovery lock")
    offer_check = function_body(lineage, "bool HandleFirstJoinRecoveryOffer(")
    check(offer_check.index("g_recoveryMutex") < offer_check.index("StageApprovedFirstJoin(") < offer_check.rfind("g_recoveryMutex"),
          "a recovery offer must stage outside the recovery lock, then re-lock to send the consume")
    check(ordered(function_body(lineage, "void TickFirstJoinRecovery("), ["g_recoveryMutex", "LookupFirstJoinRecovery(", "ShowFirstJoinConnectionLost("]),
          "a consume timeout must show the connection-lost text after releasing the recovery lock")
    note_fn = function_body(lineage, "void NoteFirstJoinAttestationResult(")
    check(ordered(note_fn, ["if (!a_acknowledged)", "a_saveFile != g_firstJoinAttestation->saveFile"]),
          "an unacknowledged first-join attestation must reject before any save-name match")
    check(ordered(settle, ["FirstJoinAttestationOutcome()", "F4T::Networking::IsConnectedToServer()", "ServerProfileStore::BindSaveFileClearingHold("]),
          "the settle poll must recheck the live connection immediately before the clearing bind")

    check(ordered(launch, ["ShouldResumeOnboarding(", "!resumeOnboarding && ServerProfileStore::HasFirstJoinHold(", "if (resumeOnboarding)", "ResumeFirstTimeJoin("]),
          "a saved vault character must bypass only the first-join hold refusal, and only through the resume branch")
    check(ordered(launch, ["ShouldRecordOnboardingResume(", "!ServerProfileStore::RecordOnboardingResume(", "TryCompletePendingProfileBind(onboardingSave)"]),
          "the vault save must be recorded before the pending bind is evaluated")
    resume_fn = function_body(onboarding, "ResumeFirstTimeJoin(")
    check("LoadSaveFromMainMenu(" in resume_fn and "StartVanillaNewGame" not in resume_fn,
          "resuming must load the saved vault character and never start a new game")
    check("WaitingForGameplayWorldReady" in function_body(onboarding, "void QueueFirstTimeJoin("),
          "a resumed join must re-enter the vault-exit wait")
    store = (PLUGIN / "src/F4TServerProfileStore.cpp").read_text(encoding="utf-8")
    bind_fn = function_body(store, "bool BindSaveFileWithHold(")
    check(ordered(bind_fn, ["erase(\"firstJoinHold\")", "EraseOnboardingResume(entry)"]),
          "the resume record may only be dropped together with the hold, after an attested bind")
    key_source = (PLUGIN / "src/F4TServerKey.cpp").read_text(encoding="utf-8")
    check("StripPortSuffix(" in function_body(key_source, "std::string NormalizeHost("),
          "host normalisation must drop a trailing port so a host:port string cannot create a phantom profile key")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        _merge_exit(1)

    print("F4TFirstJoinAttemptContract.py: ok")

def _section_005_f4tgnsvoicecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TGnsVoiceContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TGnsVoiceContract.py")
    from pathlib import Path
    import re
    import sys


    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    VOICE_FILES = (
        "include/F4TVoiceControl.h",
        "src/F4TVoiceControl.cpp",
        "include/F4TGnsVoice.h",
        "src/F4TGnsVoice.cpp",
    )

    FORBIDDEN = (r"enable", r"feature", r"gate", r"flag", r"#if")

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def strip_comments(text):
        return re.sub(r"//[^\n]*", "", text)


    def read_code(relative):
        return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


    def brace_body(text, start):
        brace = text.index("{", start)
        depth = 0
        for index in range(brace, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    return text[brace:index + 1]
        raise AssertionError("unterminated body")


    def function_body(code, signature):
        return brace_body(code, code.index(signature))


    def regex_in_order(text, *patterns):
        position = 0
        for pattern in patterns:
            found = re.compile(pattern, re.S).search(text, position)
            if not found:
                return False
            position = found.end()
        return True


    shim = read_code("src/F4TGnsWinsockShim.cpp")
    identity = read_code("src/F4TClientIdentity.cpp")
    replication = read_code("src/F4TReplicationRuntime.cpp")
    glue = read_code("src/F4TGnsVoice.cpp")
    control = read_code("src/F4TVoiceControl.cpp")

    recv = function_body(shim, "int Recv(SOCKET a_socket")
    check(regex_in_order(recv,
                         r"payload\.size\(\)\) \{\n\t+continue;\n\t+\}\n\n\t+if \(F4T::GnsVoice::TryHandleInbound\(state,[^\n]*\)\) \{ continue; \}\n",
                         r"F4T::ComponentReceive::ClassifyReceive\(\*state,"),
          "the shim Recv must hand each message to the voice inbound hook as a statement that continues, directly after the message filter and before ClassifyReceive")
    check('#include "F4TGnsVoice.h"' in shim, "the shim must include the voice glue header")

    check(regex_in_order(identity,
                         r'metadata\.capabilities\.emplace_back\("session-lifecycle-v1"\);',
                         r'\n\t\tmetadata\.capabilities\.emplace_back\("voice-v1"\);'),
          "the hello capabilities must add voice-v1 unconditionally next to session-lifecycle-v1")
    check(identity.count('"voice-v1"') == 1, "voice-v1 must be advertised in exactly one place in the client identity")

    tick = function_body(replication, "void Tick()")
    check(regex_in_order(tick, r"F4T::ObjectSync::Tick\(nowMs, runGameplay\);", r"if \(connected\) \{\s*F4T::GnsVoice::PumpControl\("),
          "the replication tick must pump the voice control sync after the object sync tick, only while connected")
    check('#include "F4TGnsVoice.h"' in replication, "the replication runtime must include the voice glue header")

    inbound = function_body(glue, "bool TryHandleInbound(")
    check(regex_in_order(inbound,
                         r"if \(!F4T::Voice::HasVoicePrefix\(bytes\)\) \{\s*return false;\s*\}",
                         r"receivable = g_control\.CanReceiveVoice\(\);",
                         r"if \(!receivable\) \{\s*\+\+g_dropped;\s*return true;\s*\}",
                         r"DecodeVoicePacket\(bytes, F4T::Voice::VoiceDirection::ServerToClient, packet\) != F4T::Voice::VoiceDecodeResult::Ok\) \{\s*\+\+g_dropped;\s*return true;\s*\}"),
          "the inbound hook must return false for non-COV1 datagrams, count and consume unnegotiated or undecodable COV1 datagrams, and decode in the server direction")
    check("SendPacketBytes" not in inbound and "RE::" not in inbound,
          "the inbound voice hook runs on the receive thread and must not send JSON or touch the engine")
    check(regex_in_order(inbound,
                         r"\{\s*const std::scoped_lock lock\(g_sinkMutex\);\s*sink = g_sink;\s*\}",
                         r"if \(!sink\) \{\s*\+\+g_dropped;\s*return true;\s*\}",
                         r"sink\(packet\);\s*\+\+g_sunk;"),
          "the inbound voice hook must copy the sink under g_sinkMutex and call it after the lock is released")
    check(regex_in_order(inbound,
                         r"const std::scoped_lock controlLock\(g_controlMutex\);\s*const std::scoped_lock socketLock\(g_socketMutex\);\s*if \(g_socket\.lock\(\) != a_state\) \{\s*g_socket = a_state;\s*g_control\.Reset\(\);\s*\}"),
          "a datagram on a new socket must rebind and reset the control sync inside one control-mutex section, so a voice send cannot pass the gate with the previous session's ack on the new socket")

    send = function_body(glue, "bool SendVoicePacket(")
    check(regex_in_order(send,
                         r"const std::scoped_lock controlLock\(g_controlMutex\);\s*if \(!g_control\.CanSendVoice\(\)\) \{\s*\+\+g_sendFailures;\s*return false;\s*\}",
                         r"std::scoped_lock bridgeLock\(state->bridgeMutex\)",
                         r"api->send\(",
                         r"F4T::GnsProtocolAdapter::Delivery::UnreliableSequenced"),
          "the voice send must check the control gate under the control mutex, then send unreliable under the bridge mutex")
    check("state->closed.load" in send and "state->connected.load" in send,
          "the voice send must check connected and closed before sending")
    check("ReliableOrdered" not in glue, "voice must never be sent on the reliable lane")
    adapter = strip_comments((ROOT / "co-core/include/F4TGnsProtocolAdapter.h").read_text(encoding="utf-8"))
    check("UnreliableSequenced = 0" in adapter, "the unreliable delivery lane must stay wire value 0")
    check("kSendDropped" not in send and "kSendBackpressure" not in send,
          "a voice packet the bridge did not send must not count as sent")
    check(regex_in_order(send, r"result != F4T::GnsWinsockShimState::kSendSent", r"\+\+g_sent"),
          "only kSendSent may count a voice packet as sent, checked before the sent counter")

    check('constexpr double kSendIntervalSeconds = 1.0;' in control,
          "the control sync must pace snapshots at one per second")
    check(regex_in_order(function_body(control, "std::optional<std::string> ControlSync::Poll("),
                         r"m_ackedRevision == m_revision", r"kSendIntervalSeconds"),
          "the control sync poll must stop on acknowledged state before applying the pace")

    pump = function_body(glue, "void PumpControl(")
    check(regex_in_order(pump,
                         r"\A\{\s*const auto state = BoundSocket\(\);\s*if \(!state\) \{\s*return;\s*\}\s*bool protocolReady = false;\s*\{\s*const std::scoped_lock protocolLock\(state->protocolMutex\);\s*protocolReady = state->protocol\.IsReady\(\);\s*\}\s*if \(!protocolReady\) \{\s*return;\s*\}\s*std::optional<std::string> snapshot;\s*\{\s*const std::scoped_lock lock\(g_controlMutex\);\s*snapshot = g_control\.Poll\(a_now\);"),
          "PumpControl must read protocol.IsReady() under state->protocolMutex and return before g_control.Poll, so no snapshot is sent before sessionReady")

    main_cpp = read_code("src/main.cpp")
    check(regex_in_order(main_cpp,
                         r"case F4SE::MessagingInterface::kGameDataReady:",
                         r"F4T::UIClient::OnGameDataReady\(\);\s*F4T::VoiceRuntime::Start\(\);"),
          "main.cpp must start the voice runtime in the kGameDataReady case directly after UIClient::OnGameDataReady")
    check(regex_in_order(main_cpp,
                         r"case F4SE::MessagingInterface::kGameDataReady:",
                         r"F4T::UIClient::OnGameDataReady\(\);(?:(?!break;).)*?F4T::RangedCombatCapture::Initialize\(\);(?:\s*F4T::FrameLimiter::Install\(\);)?\s*break;"),
          "main.cpp must initialize ranged combat capture in the kGameDataReady case after UIClient::OnGameDataReady")

    inbound_cpp = read_code("src/F4TNetworkingInbound.cpp")
    check('#include "F4TGnsVoice.h"' in inbound_cpp, "F4TNetworkingInbound.cpp must include the voice glue header")
    check(regex_in_order(function_body(inbound_cpp, "void HandleWelcomePacket("),
                         r"F4T::ObjectSync::NoteWelcome\(a_packet\);",
                         r"F4T::GnsVoice::NoteWelcome\(a_packet\);"),
          "HandleWelcomePacket must hand the welcome to the voice glue directly after the object sync")
    check(regex_in_order(inbound_cpp,
                         r"if \(F4T::Chat::TryHandleChatPacket\(\*packetType, packet\)\) \{\s*return;\s*\}",
                         r"if \(F4T::GnsVoice::TryHandleControlPacket\(\*packetType, packet\)\) \{\s*return;\s*\}"),
          "the voice control packets must be dispatched directly after the chat packets and return")

    check(regex_in_order(function_body(glue, "bool TryHandleControlPacket("),
                         r'a_type == "voiceStateAck"\)\s*\{\s*OnStateAck\(a_packet\);\s*return true;',
                         r'a_type == "voiceTalk"\)\s*\{\s*OnTalk\(a_packet\);\s*return true;',
                         r'a_type == "voiceNotice"\)\s*\{\s*OnNotice\(a_packet\);\s*return true;',
                         r"return false;\s*\}\s*\Z"),
          "TryHandleControlPacket must map voiceStateAck, voiceTalk and voiceNotice to OnStateAck, OnTalk and OnNotice and return false otherwise")

    runtime_files = [path.relative_to(PLUGIN).as_posix() for path in sorted(PLUGIN.glob("src/F4TVoiceRuntime*.cpp"))]
    runtime_files += [path.relative_to(PLUGIN).as_posix() for path in sorted(PLUGIN.glob("include/F4TVoiceRuntime*.h"))]
    runtime_files += ["tests/F4TVoiceRuntimeLogicTests.cpp"]
    runtime_cpp = read_code("src/F4TVoiceRuntime.cpp")
    runtime = "\n".join(read_code(relative) for relative in runtime_files if relative.endswith(".cpp"))
    check(regex_in_order(runtime,
                         r"F4T::GnsVoice::RegisterVoiceSink\(",
                         r"F4T::GnsVoice::RegisterControlEventHandler\(",
                         r"F4T::VoiceUi::SetActionHandler\(",
                         r"F4T::F4SECapabilities::AddPermanentTask\(Tick\);"),
          "the voice runtime must register the voice sink, the control handler and the UI action handler, then add its permanent task")
    check("SetCanSend(view.canSend);" in runtime, "the voice runtime must pass the control view canSend to the transmitter")
    check(regex_in_order(runtime,
                         r"GetWindowThreadProcessId\(GetForegroundWindow\(\)",
                         r"const bool inputAllowed = [^;]*GetCurrentProcessId\(\)[^;]*IsCustomMenuInputCaptured\(\)[^;]*IsChatInputFocused\(\)[^;]*!F4T::SessionWorldState::IsMenuOpen\(RE::Console::MENU_NAME\.data\(\)\)[^;]*;",
                         r"SetInputAllowed\(inputAllowed\);"),
          "the voice input guard must combine the foreground process, the custom menu capture, the chat focus and the open console menu before SetInputAllowed")
    check(re.search(r"SetDesiredControl\(\s*(F4T::Voice::)?DesiredControl\(", runtime) is not None,
          "the voice runtime must publish SetDesiredControl(DesiredControl(...)) every tick")
    check("CoInitializeEx(" in function_body(runtime_cpp, "void WatchDevices(") and runtime.count("CoInitializeEx(") == 1,
          "CoInitializeEx must appear only inside the device watcher thread function")
    check(re.search(r"bool IsChatInputFocused\(\)\s*\{", read_code("src/F4TTextEntryMode.cpp")) is not None,
          "F4TTextEntryMode.cpp must define IsChatInputFocused")
    for relative in runtime_files:
        text = (PLUGIN / relative).read_text(encoding="utf-8")
        check("//" not in text and "/*" not in text, f"{relative} must not contain comments")
        for token in ("kEnable", "enable", "feature", "flag", "#if"):
            check(not re.search(token, text, re.IGNORECASE), f"{relative} must not contain the token {token}")
    for relative in ("src/main.cpp", "src/F4TNetworkingInbound.cpp"):
        text = (PLUGIN / relative).read_text(encoding="utf-8")
        for line in text.splitlines():
            if "voice" not in line.lower():
                continue
            for token in FORBIDDEN:
                check(not re.search(token, line, re.IGNORECASE), f"{relative} must not gate voice: {line.strip()}")

    for relative in VOICE_FILES:
        text = (PLUGIN / relative).read_text(encoding="utf-8")
        check("//" not in text and "/*" not in text, f"{relative} must not contain comments")
        for token in FORBIDDEN:
            check(not re.search(token, text, re.IGNORECASE), f"{relative} must not contain the token {token}")
    for relative in ("src/F4TGnsWinsockShim.cpp", "src/F4TClientIdentity.cpp", "src/F4TReplicationRuntime.cpp", "src/main.cpp", "src/F4TNetworkingInbound.cpp"):
        text = (PLUGIN / relative).read_text(encoding="utf-8")
        for line in text.splitlines():
            if "voice" not in line.lower():
                continue
            for token in FORBIDDEN:
                check(not re.search(token, line, re.IGNORECASE), f"{relative} must not gate voice: {line.strip()}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TGnsVoiceContract: all checks passed.")

def _section_006_f4tintroquestprobecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TIntroQuestProbeContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TIntroQuestProbeContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    probe_h = (PLUGIN / "include/F4TIntroQuestProbe.h").read_text(encoding="utf-8")
    probe_cpp = (PLUGIN / "src/F4TIntroQuestProbe.cpp").read_text(encoding="utf-8")
    SESSION_SOURCES = [
        PLUGIN / "src/F4TSessionLaunchSaves.cpp",
        PLUGIN / "src/F4TSessionLaunchState.cpp",
        PLUGIN / "src/F4TSessionLaunchWorld.cpp",
        PLUGIN / "src/F4TSessionLaunchOnboarding.cpp",
        PLUGIN / "src/F4TSessionLaunch.cpp",
    ]
    launch = "".join(path.read_text(encoding="utf-8") for path in SESSION_SOURCES)
    setup = (PLUGIN / "src/F4TSessionCharacterSetupFlow.cpp").read_text(encoding="utf-8")
    setup_h = (PLUGIN / "include/F4TSessionCharacterSetupFlow.h").read_text(encoding="utf-8")
    suppress = (PLUGIN / "src/F4TNpcSyncSuppress.cpp").read_text(encoding="utf-8")
    puppets = (PLUGIN / "src/F4TNpcSyncPuppets.cpp").read_text(encoding="utf-8")
    gns = (PLUGIN / "src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")

    check("BeginFirstJoinTrace" in probe_h, "probe header must expose BeginFirstJoinTrace")
    check("EndFirstJoinTrace" in probe_h, "probe header must expose EndFirstJoinTrace")
    check("Tick" in probe_h, "probe header must expose Tick")

    for form_id in ("0x0001ED86", "0x0001CC2A", "0x0001ED62", "0x000179F3", "0x000229E5"):
        check(form_id in probe_cpp, f"probe must watch quest form id {form_id}")
    for quest in ("Tutorial", "MQ03", "MQ102", "DialogueConcordArea", "MQ103"):
        check(f'"{quest}"' in probe_cpp, f"probe must watch quest editor id {quest}")

    check("GetFormByID<RE::TESQuest>" in probe_cpp, "probe must look quests up by form id")
    check("GetEventSource" not in probe_cpp, "probe must not register event sources; polling is the only probe mechanism")
    check("TESQuestStageEvent" not in probe_cpp, "probe must not sink TESQuestStageEvent")
    check("TESQuestStartStopEvent" not in probe_cpp, "probe must not sink TESQuestStartStopEvent")
    check("RegisterSink" not in probe_cpp, "probe must not register event sinks")
    check("IntroQuestProbe" in probe_cpp, "probe logs must use IntroQuestProbe component")

    check('#include "F4TIntroQuestProbe.h"' in launch, "SessionLaunch must include IntroQuestProbe")
    check("IntroQuestProbe::BeginFirstJoinTrace" in launch, "first-join queue must begin the quest trace")
    check("IntroQuestProbe::EndFirstJoinTrace" in launch, "first-join clear must end the quest trace")
    check("IntroQuestProbe::Tick" in launch, "first-join poll must tick the quest probe")

    for phase in ("StartingNewGame", "VanillaIntro", "WaitingForContinue", "WaitingForPodExit",
                  "WaitingForGameplayWorldReady", "WaitingForProfileBind"):
        check(f"CharGenPhase::{phase}" in launch, f"first-join must use the {phase} phase")
    check("WaitingForIntroPrime" not in launch and "WaitingForQuestComplete" not in launch,
          "legacy IntroPrime / LooksMenu phases must be gone")
    check("case F4SE::MessagingInterface::kNewGame:" in launch, "OnF4SEMessage must observe kNewGame")
    check("coc " not in launch and "coc Vault111Cryo" not in launch, "the coc Vault111Cryo path must be removed")
    check("SessionIntroPrime" not in launch, "SessionLaunch must not reference the deleted IntroPrime module")
    check("CharacterSetupController" not in launch, "SessionLaunch must not reference the deleted controller")

    check("BGSCodeObj" in setup and "StartNewGame" in setup, "new game must start through BGSCodeObj.StartNewGame")
    check('"cqf CO_CharacterSetupQuest BeginVanillaOnboarding"' in setup, "onboarding must begin via BeginVanillaOnboarding")
    check('"cqf CO_CharacterSetupQuest ContinueToVault"' in setup, "Continue must run ContinueToVault")
    check('"cqf CO_CharacterSetupQuest AbortOnboarding"' in setup, "failed joins must run AbortOnboarding")
    check("ShowAlert(" in setup and "durationMs = 0" in setup, "Continue prompt must be a persistent CO alert")
    check('"Continue to Vault 111 to join the server."' in setup, "Continue prompt message must match the brief")
    check("kMq102FormId = 0x0001CC2A" in setup, "pod-exit signal must look up MQ102 by 0x0001CC2A")
    check("kMq102PodExitStage = 2" in setup, "pod-exit signal must be MQ102 stage 2")
    check("ShowContinueAlert" in setup_h and "ContinueToVault" in setup_h and "IsPodExitSignal" in setup_h,
          "setup flow header must expose the vanilla-start entry points")
    onboarding = (PLUGIN / "src/F4TSessionLaunchOnboarding.cpp").read_text(encoding="utf-8")
    special_flow = onboarding.split('if (g_charGenPhase == CharGenPhase::VanillaIntro)', 1)[1].split('if (g_charGenPhase == CharGenPhase::WaitingForContinue)', 1)[0]
    check(special_flow.find('IsMenuOpen("MessageBoxMenu")') < special_flow.find('ShowContinueAlert()')
          and 'g_specialClosedSince = {};' in special_flow
          and 'now - g_specialClosedSince < std::chrono::seconds{ 1 }' in special_flow,
          "Continue must wait for the vanilla confirmation to close and the SPECIAL menu to remain closed")
    waiting = onboarding.split('if (g_charGenPhase == CharGenPhase::WaitingForContinue)', 1)[1].split('if (g_charGenPhase == CharGenPhase::WaitingForPodExit)', 1)[0]
    check('IsMenuOpen("MessageBoxMenu") || IsMenuOpen("SPECIALMenu")' in waiting
          and waiting.find('IsMenuOpen("MessageBoxMenu")') < waiting.find('PollContinueAlert()')
          and waiting.find('IsMenuOpen("MessageBoxMenu")') < waiting.find('ShowContinueAlert()'),
          "Continue actions and refreshes must wait while vanilla confirmation or SPECIAL is open")
    chat = (ROOT / "CommonwealthOnlineUI/src/F4TChatView.cpp").read_text(encoding="utf-8")
    chat_blocked = chat.split("bool VanillaMenuOwnsScreen()", 1)[1].split("bool ScreenWantsChat()", 1)[0]
    check('"MessageBoxMenu"' in chat_blocked, "chat must not take focus while a vanilla confirmation is open")
    check("coc " not in setup, "setup flow must not use coc")
    check(re.search(r"SetStage\s*\(\s*5000\s*\)", launch + setup) is None, "must never call SetStage(5000)")
    check("setstage MQ101 5000" not in (launch + setup).lower(), "must never console-setstage MQ101 5000")
    check('"Alias_PrestonGarveyAlias"' in probe_cpp, "probe must resolve Preston through the DialogueConcordArea alias name")
    check("GetAliasedRef" in probe_cpp, "probe must resolve Preston through GetAliasedRef")
    check("RE::TESNPC" not in probe_cpp, "probe must not look Preston up by a guessed NPC form id")
    check("%H:%M:%S" in probe_cpp, "watched quest stage changes must be logged with a timestamp")
    check('"stage-change"' in probe_cpp and '"cell-change"' in probe_cpp,
          "probe must log Preston on stage change and on cell change")
    check("server positionCorrection received" in probe_cpp, "probe must log every received server positionCorrection")
    check("LogServerPositionCorrection(text)" in gns, "server positionCorrection receipt must log through the probe")
    check("SanitizeLogText(a_payload, 512)" in probe_cpp, "server positionCorrection payload must not truncate at the 160-character default")
    check('if (!g_pendingSpawn && BeginSpawn(key, state, a_player, loadedActors)) {' in puppets,
          "puppet-spawn decision must be guarded by BeginSpawn returning true")
    check(puppets.index('LogNpcDecision("puppet-spawn"') > puppets.index("BeginSpawn(key, state, a_player, loadedActors)"),
          "puppet-spawn decision must log after BeginSpawn, not before it")
    check('LogNpcDecision("suppress"' in suppress and 'LogNpcDecision("unsuppress"' in suppress,
          "suppress and unsuppress decisions must log through the probe")
    check('LogNpcDecision("puppet-spawn"' in puppets and "puppet-death-applied" in puppets,
          "puppet spawn and death decisions must log through the probe")
    for name, text in (("F4TNpcSyncSuppress.cpp", suppress), ("F4TNpcSyncPuppets.cpp", puppets), ("F4TNetworkingGns.cpp", gns)):
        check('#include "F4TIntroQuestProbe.h"' in text, f"{name} must include IntroQuestProbe")
    for name, text in (("F4TIntroQuestProbe.cpp", probe_cpp), ("F4TNpcSyncSuppress.cpp", suppress),
                       ("F4TNpcSyncPuppets.cpp", puppets), ("F4TNetworkingGns.cpp", gns)):
        check(len(text.splitlines()) < 1000, f"{name} must stay under 1000 lines")


    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        _merge_exit(1)

    print("F4TIntroQuestProbeContract.py: ok")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TFirstJoinAttemptContract.py", _section_004_f4tfirstjoinattemptcontract),
    ("CO-BETA/plugin/tests/F4TGnsVoiceContract.py", _section_005_f4tgnsvoicecontract),
    ("CO-BETA/plugin/tests/F4TIntroQuestProbeContract.py", _section_006_f4tintroquestprobecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TQuestStoryContracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
