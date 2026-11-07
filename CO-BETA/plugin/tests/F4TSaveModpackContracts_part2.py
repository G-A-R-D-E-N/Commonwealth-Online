#!/usr/bin/env python3
"""F4TSaveModpackContracts_part2: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TSaveModpackContracts_part2.py
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




def _section_010_f4tsavelineagetimingcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TSaveLineageTimingContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSaveLineageTimingContract.py")
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


    lineage_raw = (PLUGIN / "src/F4TSaveLineageSession.cpp").read_text(encoding="utf-8")
    lineage = strip_comments(lineage_raw)
    lineage_header = (PLUGIN / "include/F4TSaveLineageSession.h").read_text(encoding="utf-8")
    session = strip_comments((PLUGIN / "src/F4TSessionLaunch.cpp").read_text(encoding="utf-8"))

    check("#include <Windows.h>" in lineage_raw,
          "F4TSaveLineageSession.cpp must include <Windows.h> for HANDLE and CreateFileW")

    is_settled = function_body(lineage, "bool IsSaveFileSettled(")
    check(re.search(r'tmpFilePath \+= "\.tmp";', is_settled) is not None
          and re.search(r"exists\(tmpFilePath, ec\) \|\| ec\) \{\s*return false;", is_settled) is not None,
          "IsSaveFileSettled must return false while <path>.tmp exists")
    check(re.search(r"CreateFileW\(\s*a_saveFilePath\.c_str\(\),\s*GENERIC_READ,\s*0,", is_settled) is not None,
          "IsSaveFileSettled must open the save with CreateFileW and share mode 0 (exclusive)")
    check(is_settled.index("exists(tmpFilePath") < is_settled.index("CreateFileW("),
          "IsSaveFileSettled must check the .tmp file before opening the save")

    read_name = function_body(lineage, "std::string ReadPostSaveGameName(")
    check("kSaveNameFieldBytes = 0x104" in read_name and "strnlen(name, kSaveNameFieldBytes)" in read_name,
          "ReadPostSaveGameName must bound the post-save name to the 0x104-byte save name field")
    check("std::string ReadPostSaveGameName(" not in session and "kSaveNameFieldBytes" not in session,
          "ReadPostSaveGameName must live in F4TSaveLineageSession.cpp, not F4TSessionLaunch.cpp")
    check("F4T::SaveLineageSession::ReadPostSaveGameName(*a_msg)" in session,
          "SessionLaunch must read the post-save name through F4TSaveLineageSession")

    update = function_body(session, "void Update()")
    check("TryCompletePendingRecord" not in update,
          "SessionLaunch::Update must not call TryCompletePendingRecord; the record settles on the F4SE task queue")

    post_save = function_body(session, "void OnF4SEMessage(")
    check(re.search(r"TryCompletePendingProfileBind\(onboardingSave\);\s*if \(g_pendingProfileBind\) break;\s*F4T::SaveLineageSession::TryUpdateActiveProfileBinding\(g_activeProfileServerKey", post_save) is not None,
          "a pending first-join bind must not fall through to active-profile save binding")

    begin_calls = [match.start() for match in re.finditer(r"(?<!void )BeginPendingRecord\(", lineage)]
    check(len(begin_calls) == 1,
          "BeginPendingRecord must have exactly one call site")
    check(begin_calls and lineage[:begin_calls[0]].rstrip().endswith("if (!a_savedName.empty()) {"),
          "BeginPendingRecord must be reached for every post-save name")
    check("kEnableSaveLineage" not in lineage and "kEnableSaveLineage" not in lineage_header,
          "the save lineage path must not be gated")

    check(re.search(r"constexpr auto kPendingRecordTimeout = std::chrono::seconds\{ 30 \};", lineage) is not None,
          "kPendingRecordTimeout must be a 30 s bound")
    check(re.search(r"steady_clock::now\(\) - g_pendingRecord->startedAt < kPendingRecordTimeout\) \{\s*return;",
                    lineage) is not None,
          "PollPendingRecord must wait for the next frame while under the timeout")
    check("AddTask" not in lineage and "QueuePendingPoll" not in lineage,
          "the pending record must not re-queue itself on the F4SE task queue; the drain runs re-added tasks in the same frame")
    replication = strip_comments((PLUGIN / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8"))
    check("F4T::SaveLineageSession::TickPendingRecord();" in function_body(replication, "void Tick()"),
          "ReplicationRuntime::Tick must poll the pending save record once per frame")
    check(re.search(r"did not settle within \{\} s[\s\S]*?g_pendingRecord\.reset\(\);\s*return;", lineage) is not None,
          "PollPendingRecord must drop the pending record once the timeout passes")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TSaveLineageTimingContract: all checks passed.")

def _section_011_f4tsavepluginpreflightcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TSavePluginPreflightContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSavePluginPreflightContract.py")
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]
    plugin = root / "CO-BETA/plugin"
    flow = (plugin / "src/F4TSessionSaveFlow.cpp").read_text(encoding="utf-8")
    preflight = (plugin / "src/F4TSavePluginPreflight.cpp").read_text(encoding="utf-8")
    parser = (plugin / "src/F4TSaveFilePlugins.cpp").read_text(encoding="utf-8")
    parser_header = (plugin / "include/F4TSaveFilePlugins.h").read_text(encoding="utf-8")
    preflight_header = (plugin / "include/F4TSavePluginPreflight.h").read_text(encoding="utf-8")
    xmake = (plugin / "xmake.lua").read_text(encoding="utf-8")


    def between(text, start, end):
        assert start in text, f"missing start marker: {start}"
        begin = text.index(start)
        finish = text.index(end, begin)
        return text[begin:finish]


    def check(condition, message):
        assert condition, message


    main_menu = between(flow, "bool LoadSaveFromMainMenu(", "bool LoadSaveInGame(")
    in_game = between(flow, "bool LoadSaveInGame(", "bool IsMissingAssociatedSaveError(")

    check("return LoadSaveInGame(a_saveFile, a_error);" in main_menu,
          "the main-menu load must share LoadSaveInGame so one preflight covers both join loads")
    check("DoLoadGame" not in flow,
          "no join load may reach the engine without passing LoadSaveInGame's preflight")

    check("SavePluginPreflight::AllowSaveLoad(a_saveFile, a_error)" in in_game,
          "the in-game load must preflight the save's plugins")
    check(in_game.index("SavePluginPreflight::AllowSaveLoad") < in_game.index("QueueSaveLoadTask"),
          "the plugin preflight must run before QueueSaveLoadTask")
    check(in_game.index("SavePluginPreflight::AllowSaveLoad") < in_game.index("QUEUED_TASK::kLoadGame"),
          "the plugin preflight must run before QUEUED_TASK::kLoadGame is queued")
    check("return false;" in in_game[in_game.index("SavePluginPreflight::AllowSaveLoad"):],
          "a blocked preflight must fail the in-game load")

    check("AllowSaveLoad(std::string_view a_saveFile, std::string& a_error)" in preflight_header,
          "the preflight must expose AllowSaveLoad with the player-facing error out-parameter")

    check("sLocalSavePath:General" in preflight,
          "the save folder must come from the engine's sLocalSavePath setting")
    check('"Saves"' not in preflight,
          "the preflight must not hard-code the Saves folder")
    check("GetSaveFolderDirectory()" in preflight,
          "the preflight must resolve the save folder through the existing profile store path code")

    check("ParseSavePlugins(*headerBytes)" in preflight,
          "the preflight must parse the .fos header before loading")
    check("BuildMissingPluginsMessage(missing)" in preflight,
          "the preflight must build the player-facing missing-plugin message")
    check("a_error = SaveFilePlugins::BuildMissingPluginsMessage(missing);" in preflight,
          "a blocked load must set a_error")
    check("return false;" in preflight[preflight.index("a_error ="):],
          "a blocked load must return the existing failure outcome")

    parse_failure_at = preflight.index("if (!savedPlugins) {")
    parse_failure = preflight[parse_failure_at:parse_failure_at + 400]
    check("return true;" in parse_failure,
          "an unparseable save must fall back to loading instead of blocking")

    check('kSaveMagic = "FO4_SAVEGAME"' in parser_header,
          "the parser must key off the FO4_SAVEGAME magic")
    check("kMinimumSaveVersion = 11" in parser_header and "kMaximumSaveVersion = 15" in parser_header,
          "the parser must only accept the documented FO4 save versions")
    check("kMaximumNamedPlugins = 5" in parser_header,
          "the player message must cap the named plugins at five")
    check("headerSize" in parser and "Position() - kFixedHeaderBytes" in parser,
          "the parser must validate the header size")
    check("pluginInfoSize" in parser, "the parser must validate the plugin info size")
    check("ReadU16(lightCount)" in parser and "ReadPluginNames(reader, lightCount" in parser,
          "the parser must read the light (ESL) plugin list")
    check("FindMissingPlugins(const PluginList& a_saved, const PluginList& a_loaded)" in parser,
          "the parser module must compare the saved and loaded plugin lists")
    check("and {} more" in parser,
          "the player message must count the plugins it did not name")
    check("This save needs plugins that are not loaded:" in parser,
          "the player message must list the missing plugins")
    check("Install the server modpack or pick another save." in parser,
          "the player message must tell the player what to do")
    check("ParseSavePlugins(std::span<const std::uint8_t> a_headerBytes)" in parser_header,
          "the parser must be a pure bytes-in, plugin-names-out function")
    check("#if" not in parser and "#ifdef" not in parser,
          "the .fos parser must be always compiled")
    check("#if" not in preflight and "#ifdef" not in preflight,
          "the plugin preflight must be always on, with no compile-time switch")

    check("RE::TESDataHandler::GetSingleton()" in (plugin / "src/F4TLoadOrderSync.cpp").read_text(encoding="utf-8"),
          "the loaded plugin list must come from the same TESDataHandler API as LoadOrderSync")
    check("F4T::LoadOrderSync::GatherLocalPlugins()" in preflight,
          "the preflight must use the shared loaded-plugin gatherer")
    check("CapturedPlugins()" in (plugin / "src/F4TLoadOrderSync.cpp").read_text(encoding="utf-8").split("GatherLocalPlugins()")[1][:200],
          "the shared gatherer must prefer the plugin list captured at kGameDataReady")
    empty_guard = preflight[preflight.index("LoadedPlugins()", preflight.index("AllowSaveLoad(std::string_view")):]
    check("loaded.full.empty() && loaded.light.empty()" in empty_guard and "return true;" in empty_guard.split("FindMissingPlugins")[0],
          "an empty loaded-plugin list must skip the preflight instead of blocking every plugin")

    print("Save plugin preflight contract tests passed")

def _section_012_f4tsessionsaveguardcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TSessionSaveGuardContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSessionSaveGuardContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"
    UI = ROOT / "CommonwealthOnlineUI"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    policy = (PLUGIN / "include/F4TSessionSaveGuardPolicy.h").read_text(encoding="utf-8")
    guard = (PLUGIN / "src/F4TSessionSaveGuard.cpp").read_text(encoding="utf-8")
    SESSION_SOURCES = [
        PLUGIN / "src/F4TSessionLaunchSaves.cpp",
        PLUGIN / "src/F4TSessionLaunchState.cpp",
        PLUGIN / "src/F4TSessionLaunchWorld.cpp",
        PLUGIN / "src/F4TSessionLaunchOnboarding.cpp",
        PLUGIN / "src/F4TSessionLaunch.cpp",
    ]
    launch = "".join(path.read_text(encoding="utf-8") for path in SESSION_SOURCES)
    main_cpp = (PLUGIN / "src/main.cpp").read_text(encoding="utf-8")
    menu = (UI / "src/F4TPrismaUI_Menu.cpp").read_text(encoding="utf-8")
    queue = (PLUGIN / "src/F4TSessionSaveQueue.cpp").read_text(encoding="utf-8")
    queue_header = (PLUGIN / "include/F4TSessionSaveQueue.h").read_text(encoding="utf-8")

    check("DecidePlayerSave" in policy, "policy must expose DecidePlayerSave")
    check("kSaveBlockedNotice" not in policy and "Saving is disabled in multiplayer" not in policy,
          "blocked saves must not have a player-facing notice")

    check("if (!a_sessionActive || a_internalSaveAllowed)" in policy,
          "DecidePlayerSave must allow outside a session or for an armed internal save")
    check("return Decision::kBlock" in policy, "DecidePlayerSave must block player saves in a session")

    check("0xced1d0" in guard, "SaveGameImpl OG offset must be verified before install")
    check("0xb77a30" in guard, "SaveGameImpl NG offset must be verified before install")
    check("0xbf9330" in guard, "SaveGameImpl AE offset must be verified before install")
    check("0xcee3d0" in guard, "IsSavingAllowed OG offset must be verified before install")
    check("0xb78f10" in guard, "IsSavingAllowed NG offset must be verified before install")
    check("0xbfb3a0" in guard, "IsSavingAllowed AE offset must be verified before install")

    check("SaveGameImplDetour" in guard, "SaveGameImpl must be hooked")
    check("IsSavingAllowedDetour" in guard, "IsSavingAllowed must be hooked")
    check("ArmInternalSave" in guard, "CO must be able to arm an internal save")
    check("DisarmInternalSave" in guard, "CO must be able to disarm an internal save")
    check("[RT-VERIFY 410-A]" in guard, "SaveGameImpl must log the 410-A pause-menu save values")
    check("[RT-VERIFY 410-C]" in guard, "SaveGameImpl must log the 410-C file-name fact")
    check("ShowHUDMessage" not in guard and "SendHUDMessage" not in guard, "blocked saves must never show a HUD message")
    check('LogBlockedSave("IsSavingAllowed")' in guard and 'LogBlockedSave("SaveGameImpl")' in guard,
          "both save hooks must log a blocked save")
    check("it is not tied to the queued save" in guard, "a timed-out quit save must log that the next save in the orphan window is refused once, not tied to the queued save")
    check("AtVerifiedOffset" in guard, "hooks must refuse install when CommonLib address != verified entry")
    check("if (a_hook) {" in guard, "a second Install must skip a hook that is already installed")
    check("FirstRefusalFromCaller(callerRva)" in guard and "first refusal from this caller" in guard,
          "IsSavingAllowed must log the first refusal from each distinct caller")
    save_impl = guard[guard.index("bool SaveGameImplDetour("):guard.index("bool IsSavingAllowedDetour(")]
    check("exchange(true" not in save_impl and "g_armGeneration" not in guard,
          "a refused consumed arm must not be restored: a restored arm would let the next manual save through")
    check("save refused by engine: SaveGameImpl returned false" in queue,
          "a refused queued save must log the false SaveGameImpl result as the refusal signal in the queue")
    check("[RT-VERIFY 410-B2 caller]" in guard, "the IsSavingAllowed refusal log must carry the 410-B2 caller tag")
    check("g_sessionExitLanded.store(true" in save_impl and "owner == ArmOwner::kQuit && result" in save_impl,
          "the landed store must happen only for a consumed arm owned by the quit path whose SaveGameImpl returned true")
    orphan_at = save_impl.find("ConsumeOrphanQuitSaveRefusal(session, fileName)")
    arm_at = save_impl.find("g_internalSaveOwner.exchange")
    check(orphan_at != -1 and arm_at != -1 and orphan_at < arm_at,
          "the orphan quit save refusal must run before the arm is consumed")
    check(orphan_at != -1 and arm_at != -1 and "orphan queued save refused, file" in save_impl[orphan_at:arm_at]
          and "LogBlockedSave" not in save_impl[orphan_at:arm_at],
          "an orphan queued save must be refused with a 410-C log and no HUD notice")
    check('#include "F4TWorldPolicyEnforcer.h"' in guard, "the save guard must include the world policy enforcer for the save release")
    block_at = save_impl.find("DecidePlayerSave(session, consumed) == Decision::kBlock")
    release_at = save_impl.find("F4T::WorldPolicyEnforcer::ReleaseForSave();")
    original_at = save_impl.find("original(a_self, a_fileName")
    reapply_at = save_impl.find("F4T::WorldPolicyEnforcer::ReapplyAfterSave();")
    check(-1 not in (block_at, release_at, original_at) and block_at < release_at < original_at,
          "the detour must release hidden quests after the block decision and before the original save writes")
    check(-1 not in (original_at, reapply_at) and original_at < reapply_at and "owner != ArmOwner::kQuit" in save_impl[original_at:reapply_at],
          "the detour must reapply the policy after the original save only when the session continues, never on quit")
    check("GetConnectionGeneration()" in guard, "the quit save must capture and compare the connection generation")
    check("continuation skipped" in guard, "a stale quit continuation must be skipped and logged")
    ui_client_src = (PLUGIN / "src/F4TUIClient.cpp").read_text(encoding="utf-8")
    check("IsSessionExitPending()" in ui_client_src and ui_client_src.count("IsSessionExitPending()") >= 2,
          "UIClient must refuse new local and remote joins while a session exit save is pending")
    check("[RT-VERIFY 410-D in-world]" in ui_client_src, "a disconnect during a join must log the 410-D in-world tag")
    check("NotePostSaveGame" in main_cpp and "F4T::SessionSaveGuard::NotePostSaveGame();" in main_cpp,
          "main.cpp must forward kPostSaveGame to the save guard")

    header = (PLUGIN / "include/F4TSessionSaveGuard.h").read_text(encoding="utf-8")
    check("RunOrDeferSessionExit(std::string_view a_reason, std::function<void()> a_proceed)" in header,
          "the save guard must expose RunOrDeferSessionExit")
    run_or_defer = guard[guard.index("void RunOrDeferSessionExit"):guard.index("void Install()")]
    saving_allowed = guard[guard.index("bool IsSavingAllowedDetour"):]
    check("InternalSaveAllowed() || t_armedSaveInProgress" in saving_allowed[:saving_allowed.index("DecidePlayerSave")],
          "the engine IsSavingAllowed check inside an armed SaveGameImpl must see the consumed arm through t_armedSaveInProgress")
    check("bDisplayMissingContentDialogue:SaveGame" in main_cpp and "SetBinary(false)" in main_cpp,
          "kGameDataReady must turn off the vanilla missing-content box so a CO join load is not stopped behind the overlay")
    start_quit = guard[guard.index("void StartQuitSave"):guard.index("bool SaveGameImplDetour(")]
    start_queued = queue[queue.index("bool StartQueuedSave"):queue.index("void StopQueue")]
    check("F4T::SessionSaveQueue::StartQueuedSave(ArmOwner::kQuit, a_reason)" in start_quit and "ArmInternalSave(a_reason, a_owner)" in start_queued
          and "QueueSaveLoadTask" not in start_queued and "QueueSaveLoadTask" not in start_quit,
          "a quit save must arm the internal save through StartQueuedSave, owned by the quit path, and must not queue the save itself; the poll gate queues it")
    check("g_saveQueueOutstanding" not in start_queued and "g_saveQueueOutstanding" not in start_quit,
          "starting a quit save must not clear the outstanding marker of a queued save that is still in flight")
    check("StartQuitSave(a_reason)" in run_or_defer, "RunOrDeferSessionExit must start the quit save through StartQuitSave")
    check("SessionWorldState::IsMainMenuOpen()" in run_or_defer
          and run_or_defer.find("SessionWorldState::IsMainMenuOpen()") < run_or_defer.find("g_sessionExitProceed = std::move(a_proceed)"),
          "a disconnect with the main menu open has no world to save and must proceed before the quit save is set up")
    defer_at = run_or_defer.find("if (deferArm) {")
    check(defer_at != -1 and defer_at < run_or_defer.find("StartQuitSave(a_reason)")
          and "return;" in run_or_defer[defer_at:run_or_defer.find("StartQuitSave(a_reason)")],
          "a quit that finds an internal save already armed must return before arming, instead of overwriting the arm")
    check("g_sessionExitAwaitingArm = deferArm" in run_or_defer and "TakeDeferredQuitArm()" in guard[guard.index("void Tick()"):],
          "a deferred quit must record that it waits for the arm, and Tick must arm it once the arm clears")
    check("AcceptsSessionExitRequest(static_cast<bool>(g_sessionExitProceed))" in run_or_defer
          and "bool AcceptsSessionExitRequest(bool a_exitAlreadyPending) noexcept" in policy,
          "a second quit request while one is pending must be refused by the policy function")
    check("ArmOwner::kFirstJoin" in launch, "the first-join arm must carry the first-join owner tag")
    check("OwnerName(owner)" in save_impl and "ArmInternalSave: reason '{}', owner {}" in guard,
          "arm and consume traces must log the arm owner")
    tick = guard[guard.index("void Tick()"):]
    check('outcome = "landed"' in tick and 'outcome = "timeout"' in tick,
          "the quit save must settle on landed or timeout")
    check(tick.index('outcome = "timeout"') < tick.index("proceed();") and tick.index('outcome = "landed"') < tick.index("proceed();"),
          "quit must proceed only after the save landed, timed out, or the session ended")
    check("quit save: timeout after" in guard and "quit save: landed and kPostSaveGame grace" in guard,
          "quit save settle paths must log their outcome")
    check("the queued save is not cancelled" in tick and "the next queued-name save within the orphan window is refused once" in tick,
          "the timeout trace must say the next save in the window is refused once, not tied to the queued save")
    check("ArmOrphanQuitSaveRefusal(capturedGeneration);" in tick and "orphanPossible = !landed && !g_sessionExitAwaitingArm" in tick
          and tick.find("ArmOrphanQuitSaveRefusal(capturedGeneration);") < tick.find("proceed();"),
          "a timed-out or ended quit save with a queued save must arm the orphan refusal before proceed")
    check("g_refuseOrphanQuitSave.store(true" in guard and "orphan refusal armed" in guard,
          "arming the orphan refusal must set the atomic flag and log it")
    consume_orphan = guard[guard.index("bool ConsumeOrphanQuitSaveRefusal"):guard.index("bool TakeDeferredQuitArm")]
    check("GetConnectionGeneration() != g_orphanQuitSaveGeneration" in consume_orphan
          and "std::chrono::steady_clock::now() - g_orphanQuitSaveSetAt >= kOrphanRefusalWindow" in consume_orphan
          and "new session connected" in consume_orphan and "window elapsed" in consume_orphan,
          "the orphan refusal must clear with a log on a new session or after the 30 s window")
    check("graceRestarted = true" in save_impl and "g_sessionExitPostSaveAt = std::chrono::steady_clock::now();" in save_impl
          and "kPostSaveGame arrived before landing" in save_impl,
          "landing must restart the post-save grace when kPostSaveGame came first, under the session-exit mutex, with a log")
    check("if (!original)" not in save_impl and "original not available" not in guard,
          "the SaveGameImpl null-original branch must be removed: InstallHook stores the original before enabling")
    check("allowed && original && original(" not in guard and "allowed && original(a_self)" in guard,
          "the IsSavingAllowed null-original guard must be removed")
    ui_client = (PLUGIN / "src/F4TUIClient.cpp").read_text(encoding="utf-8")
    check('RunOrDeferSessionExit("disconnect requested", DisconnectNow)' in ui_client,
          "the UI disconnect callback must defer through the save guard")

    check("F4T::SessionSaveQueue::StartQueuedSave(F4T::SessionSaveGuard::ArmOwner::kFirstJoin" in launch, "first-join profile save must arm the internal save through the queue; the pump queues it after the poll gate")
    check("SessionSaveGuard::DisarmInternalSave" in launch, "first-join clear must disarm the internal save")
    check("SessionSaveQueue::StopQueue(F4T::SessionSaveGuard::ArmOwner::kFirstJoin)" in launch
          and launch.find("SessionSaveQueue::StopQueue(F4T::SessionSaveGuard::ArmOwner::kFirstJoin)", launch.index("DisarmInternalSave(a_reason)")) > launch.index("DisarmInternalSave(a_reason)"),
          "first-join clear must stop the first-join queue, so a stale wait cannot queue a save in a later session")
    check("SessionSaveGuard::Install" in main_cpp, "save guard must install with the other game hooks")
    runtime_cpp = (PLUGIN / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8")
    check(
    	"SessionSaveGuard::Tick" in runtime_cpp,
    	"save guard must tick from ReplicationRuntime, which the permanent task still delegates to",
    )

    check("the CO exit save is queued and the quit waits for it" in menu,
          "quit-to-desktop in a session must wait for the CO exit save")
    check("!inSession && saveMgr && saveMgr->savingAllowed" in menu,
          "exit-save queue must require offline session and savingAllowed")
    begin_quit = menu.index("void BeginPauseMenuQuit")
    end_quit = menu.index("void NotifyExitSavePosted")
    desktop = menu[begin_quit:end_quit]
    check("kSaveAndQuitToDesktop" in desktop, "offline desktop quit must still queue exit save")
    check("Session::IsConnected()" in desktop, "desktop quit must check session before exit-save queue")
    check(desktop.index("IsConnected()") < desktop.index("kSaveAndQuitToDesktop"),
          "session check must precede the exit-save queue")

    check("ShowBlockedNotice" not in guard and "ShowDebouncedBlockedNotice" not in guard
          and "g_isSavingAllowedNoticeShownAt" not in guard and "kIsSavingAllowedNoticeDebounce" not in guard,
          "the blocked-save notice and its debounce must be gone")

    arm_fn = guard[guard.index("void ArmInternalSave"):guard.index("void DisarmInternalSave")]
    check("previous == ArmOwner::kQuit && a_owner != ArmOwner::kQuit" in arm_fn
          and "[RT-VERIFY 410-C] ArmInternalSave refused" in arm_fn and "OwnerName(previous)" in arm_fn,
          "ArmInternalSave must refuse to overwrite a live quit arm, logging both owners")
    queue_fn = launch[launch.index("std::optional<bool> QueueProfileSave"):launch.index("std::optional<std::string> BindableCharacterId")]
    check("F4T::SessionSaveGuard::IsSessionExitPending() ||" in queue_fn
          and queue_fn.find("StartQueuedSave") != -1 and queue_fn.find("IsSessionExitPending()") < queue_fn.find("StartQueuedSave"),
          "QueueProfileSave must refuse while a session exit save is pending, before arming the first-join save")
    check("g_profileSaveRequeued" not in launch and "re-queuing once" not in launch,
          "first-join timeout must not replace an attempt whose queued save may still run")
    note_queue = queue[queue.index("void NoteEntered"):queue.index("bool TakeOutstanding")]
    check("F4T::SessionSaveQueue::NoteEntered(owner, result);" in save_impl and "owner != ArmOwner::kNone" in save_impl,
          "every consumed arm, first-join or quit, must report its SaveGameImpl result to the queue: a false result is the refusal signal")
    check(note_queue.find("if (a_result) {") != -1 and note_queue.find("if (a_result) {") < note_queue.find("++g_firstJoinEntry.count;")
          and note_queue.find("++g_firstJoinEntry.count;") < note_queue.find("SessionSaveGuard::IsSessionExitPending()"),
          "the first-join save counts as accepted only when SaveGameImpl returns true: the engine ISA check runs inside SaveGameImpl, so a refusal still enters it")
    check("TryRunForConnectionGeneration(connection" in queue_fn and "SurvivalEnforcer::Tick(connection, session, loaded" in queue_fn and queue_fn.index("SurvivalEnforcer::Tick") < queue_fn.index("StartQueuedSave("),
          "the first-join arm must be created inside the connection and Survival leases")
    check("first-join save refused by engine" in note_queue and "a_owner == ArmOwner::kQuit && !SessionSaveGuard::IsSessionExitPending()" in note_queue,
          "a refused first-join save must re-arm the first-join owner and re-queue, and a refused quit save must do nothing once the quit is no longer pending")
    clear_fn = guard[guard.index("void ClearInternalSaveIfOwner"):guard.index("void StartQuitSave")]
    check("compare_exchange_strong(expected, ArmOwner::kNone" in clear_fn and "arm left in place" in clear_fn,
          "Tick disarm must be a compare-and-clear that logs a mismatch")
    check(tick.count("ClearInternalSaveIfOwner(observedOwner") == 4 and "DisarmInternalSave(" not in tick,
          "every Tick disarm must compare-and-clear the owner Tick observed")

    check("[RT-VERIFY 410-E] join refused: already connected" in ui_client_src, "a join while connected must be refused and log the 410-E tag")
    check("join started while already connected" not in ui_client_src, "the dead join-while-connected log must be gone")
    join_fn = ui_client_src[ui_client_src.index("bool Callback_JoinSession("):ui_client_src.index("bool Callback_ConnectLocal(")]
    check("Disconnect from the current server before joining another." in join_fn and join_fn.index("IsConnectedToServer()") < join_fn.index("g_joinInProgress.exchange(true)"),
          "a join while connected must be refused with a player message before the join flag is set")

    check("a_label, a_target)" not in guard, "install logs must not print the absolute hook address")

    check("a_label, ModuleRva(a_target));" in guard, "install logs must print a module-relative address")

    disconnect_fn = ui_client_src[ui_client_src.index("void Callback_Disconnect()"):ui_client_src.index("void DisconnectNow()\n")]
    check("if (!taskInterface)" in disconnect_fn and disconnect_fn.index("AddTask") < disconnect_fn.index("RunOrDeferSessionExit"),
          "the UI disconnect callback must marshal its body to the game task before the save guard")
    check("[RT-VERIFY 410-D] disconnect marshalled to game task" in ui_client_src, "the UI disconnect marshal must log the 410-D tag")

    exchange_lines = [line for line in save_impl.splitlines() if "g_internalSaveOwner.exchange(ArmOwner::kNone" in line]
    check(len(exchange_lines) == 1 and "session && queued ?" in exchange_lines[0],
          "the arm exchange must happen only for a session save while our queued save is outstanding")
    check("bool IsQueuedSaveName(std::string_view a_fileName)" in guard and "IsQueuedSaveName(fileName)" in save_impl,
          "the queued-name predicate must be defined in the guard and used in SaveGameImplDetour")
    check("[RT-VERIFY 410-C] armed internal save not consumed: file" in save_impl,
          "a live arm not consumed by a queued save call must log the 410-C not-consumed line with the arm owner")
    check("IsQueuedSaveName(a_fileName)" in consume_orphan and "[RT-VERIFY 410-C] save inside the orphan window let through: file" in consume_orphan,
          "the orphan refusal must check the file name and log a save that is not queued-named and passes through")
    disarm_fn = guard[guard.index("void DisarmInternalSave"):guard.index("void RunOrDeferSessionExit")]
    check("ClearInternalSaveIfOwner(ArmOwner::kFirstJoin, a_reason);" in disarm_fn and "ArmOwner::kQuit" not in disarm_fn,
          "disarming must clear only a first-join owner, so a live quit arm survives first-join cleanup")
    check("a_observed == ArmOwner::kFirstJoin" in clear_fn and "ArmOrphanQuitSaveRefusal(F4T::Networking::GetConnectionGeneration());" in clear_fn,
          "clearing a first-join arm (Tick or DisarmInternalSave) must arm the orphan refusal so its late quicksave is refused")

    check("std::atomic_bool g_queuedQuitSaveRan{ false };" in guard, "the queued-quit-save flag must be defined in the guard")
    check("g_refuseOrphanQuitSave.exchange(false" in start_quit and "orphan refusal cleared before arming the quit" in start_quit
          and start_quit.find("orphan refusal cleared before arming the quit") < start_quit.find("StartQueuedSave(ArmOwner::kQuit"),
          "a quit save must clear a pending orphan refusal before arming, with a 410-C log")
    check("g_sessionExitDeadline = std::chrono::steady_clock::now() + kSessionExitSaveTimeout;" in start_quit,
          "a quit save must reset the 15 s deadline when it starts, so a deferred quit gets its full budget")
    check("g_queuedQuitSaveRan.store(false" in start_quit, "a quit save must reset the queued-quit-save flag before its queued save runs")
    check("g_queuedQuitSaveRan.store(true" in save_impl and "owner == ArmOwner::kQuit && g_sessionExitPending" in save_impl
          and "queued quit save executed" in save_impl and save_impl.index("g_queuedQuitSaveRan.store(true") > save_impl.index("ConsumeOrphanQuitSaveRefusal"),
          "a consumed quit arm must mark the queued quit save as executed, after the orphan check")
    check("!g_queuedQuitSaveRan.load(std::memory_order_acquire)" in tick and "orphanPossible = !landed && !g_sessionExitAwaitingArm && !g_queuedQuitSaveRan" in tick,
          "Tick must not arm the orphan refusal once the queued quit quicksave already ran")
    check("void ArmOrphanQuitSaveRefusal(std::uint64_t a_generation)" in guard and "g_orphanQuitSaveGeneration = a_generation;" in guard
          and "ArmOrphanQuitSaveRefusal(capturedGeneration);" in tick,
          "the orphan refusal must take the connection generation from its caller, Tick passing the captured one")
    check("TODO-verify(#407) source:docs/code-notes/CO-BETA-plugin.md" in guard and "E:/" not in guard,
          "the save guard source must cite the request through the code-notes, not a private E: path")

    notice = (PLUGIN / "src/F4TSessionExitSaveNotice.cpp").read_text(encoding="utf-8")
    lineage_header = (PLUGIN / "include/F4TSaveLineageSession.h").read_text(encoding="utf-8")
    lineage = (PLUGIN / "src/F4TSaveLineageSession.cpp").read_text(encoding="utf-8")
    check("bool HasPendingRecord();" in lineage_header and "return g_pendingRecord.has_value();" in lineage,
          "the save lineage must expose HasPendingRecord over the pending server record")
    check('kAlertId{ "co-session-exit-save" }' in notice and 'request.category = "Session";' in notice,
          "the exit save notice must use the single alert id co-session-exit-save in the Session category")
    check(run_or_defer.find("ShowSaving();") > run_or_defer.find("g_sessionExitPending.store(true") > -1,
          "RunOrDeferSessionExit must show the saving notice after a request is accepted")
    check("g_sessionExitRetried = false;" in run_or_defer,
          "a newly accepted quit request must reset the single-retry flag")
    check("F4T::SaveLineageSession::HasPendingRecord()" in tick,
          "the landed outcome must wait for the pending server record of the save")
    check('"quit save: retrying after timeout' in tick and 'StartQuitSave("quit save retry")' in tick,
          "the first timeout must log the retry and re-arm through StartQuitSave")
    check('retry = outcome == "timeout" && !landed &&' in tick,
          "a save that already landed must not be retried")
    check(tick.find("ShowRetrying();") > -1 and tick.find("ShowRetrying();") < tick.find("proceed();"),
          "the retry must show the retrying notice without proceeding")
    check(tick.find("g_sessionExitRetried = true;") > -1 and tick.find("g_sessionExitRetried = true;") < tick.find("proceed = std::move(g_sessionExitProceed);"),
          "the first timeout must set the retry flag and keep the continuation stored")
    check("F4T::SessionExitSaveNotice::ShowSaved();" in tick and "F4T::SessionExitSaveNotice::ShowFailed(" in tick,
          "the landed and failed settle paths must show the saved or failed notice")
    check("The save did not finish in time. Progress since your last save may be lost." in tick
          and "The connection closed before your session was saved." in tick,
          "the second timeout and the session-ended outcome must show their failure messages")
    check(re.search(r"g_sessionExitRetried = true;\s*g_sessionExitPostSaveAt\.reset\(\);", tick) is not None,
          "the retry must drop a kPostSaveGame time seen before it so the grace restarts for the retried save")
    check("ShowFailed(landed ? \"Your session was saved, but the server did not confirm it in time.\"" in tick,
          "a timeout after the save landed must say the save happened")
    check("F4T::SessionExitSaveNotice::Dismiss();" in tick,
          "a skipped continuation after a connection change must dismiss the saving notice")

    watchdog = re.search(r"now - g_pauseMenuQuitStartedAt >= (\d+)", menu)
    guard_window = re.search(r"kSessionExitSaveTimeout = std::chrono::milliseconds\((\d+)\)", guard)
    check(watchdog is not None and guard_window is not None and int(watchdog.group(1)) > 3 * int(guard_window.group(1)),
          "the pause-menu quit watchdog must outlast three save-guard windows: a quit can wait for an earlier arm, then the quit save and its one retry")
    check("no result after 75s" in menu, "the pause-menu quit watchdog log must state its 75 s limit")
    pump = queue[queue.index("void PumpQueue"):queue.index("void NoteEntered")]
    trace_at = pump.find("[RT-VERIFY 410-F]")
    queue_at = pump.find("QueueSaveLoadTask(kQueuedSaveTask)")
    check(trace_at != -1 and queue_at != -1 and trace_at < queue_at and 'IsMenuOpen("PauseMenu")' in pump
          and "saveLoadManager->savingAllowed" in pump,
          "the save must log savingAllowed and the PauseMenu state before it queues")
    check("QueueReady(state.stablePolls, state.attempts)" in pump and "NextStablePollCount(state.stablePolls, allowed)" in pump
          and "const auto allowed = SessionSaveGuard::OriginalIsSavingAllowed(saveLoadManager);" in pump,
          "the queue must pass the poll gate: consecutive original IsSavingAllowed polls, then QueueReady")
    first_lock = pump.find("std::scoped_lock lock{ g_queueMutex };")
    second_lock = pump.find("std::scoped_lock lock{ g_queueMutex };", first_lock + 1)
    poll_at = pump.find("OriginalIsSavingAllowed(")
    check(first_lock != -1 and first_lock < poll_at < second_lock,
          "the original IsSavingAllowed poll must run outside the queue mutex")
    check("QueueStopped(a_owner)" in pump and "return SessionSaveGuard::IsSessionExitLanded();" in queue
          and "return SessionSaveGuard::IsSessionExitPending();" in queue,
          "the pump must stop once the quit has landed, and stop the first-join queue while a quit is pending")
    check(queue.count("QueueSaveLoadTask(") == 1 and "QUEUED_TASK::kQuickSave" not in guard
          and "QUEUED_TASK::kQuickSave" not in launch and "QUEUED_TASK::kQuickSave" not in queue,
          "the quit and first-join saves must queue the single regular task, never kQuickSave")
    check("constexpr auto kQueuedSaveTask = RE::BGSSaveLoadManager::QUEUED_TASK::kForceSave;" in queue,
          "the queued task must be one named constant set to kForceSave")
    check("constexpr auto kQueueAcceptWindow = std::chrono::milliseconds(1500);" in queue,
          "the queue accept window must be 1500 ms per the RE answer")
    check(re.search(r"if \(state\.queuedAt && now - \*state\.queuedAt >= kQueueAcceptWindow\) \{[\s\S]*?state\.wanted = RetryQueuedSave\(a_owner, state\.attempts\);\s*if \(!state\.wanted && !g_quitQueue\.wanted && !g_quitQueue\.queuedAt &&\s*!g_firstJoinQueue\.wanted && !g_firstJoinQueue\.queuedAt\) \{\s*F4T::PowerArmorSync::GetFrameSaveGate\(\)\.CancelPendingSave\(\);", pump),
          "an exhausted queue timeout must release the frame save gate when no other save is queued")
    check("state.wanted = RetryQueuedSave(a_owner, state.attempts);" in note_queue and
          "if (a_owner == ArmOwner::kFirstJoin) return;" in note_queue and
          "return a_owner == ArmOwner::kQuit && a_attemptsMade < kMaxQueueAttempts;" in policy,
          "first-join queue failure must not rearm, while quit saves retain bounded retries")
    check("TODO-verify(#407) source:docs/code-notes/CO-BETA-plugin.md" in queue and "E:/" not in queue,
          "the queue source must cite the request through the code-notes, not a private E: path")
    check("bool StartQueuedSave(SessionSaveGuard::ArmOwner a_owner, std::string_view a_reason, std::uint64_t a_attemptId = 0);" in queue_header
          and "void PumpQueue(SessionSaveGuard::ArmOwner a_owner);" in queue_header
          and "void NoteEntered(SessionSaveGuard::ArmOwner a_owner, bool a_result);" in queue_header
          and "FirstJoinAttempt::Entry FirstJoinEntry();" in queue_header and "bool TakeOutstanding();" in queue_header,
          "the queue header must expose the owner-keyed start, pump, note and outstanding API")
    check("QueueArmedSave" not in header and "FirstJoinSaveAccepted" not in header and "bool IsSessionExitLanded();" in header,
          "the guard header must drop the moved queue functions and expose the landed flag")
    check("return name.empty() || (name.size() >= kQueuedSaveNamePrefix.size()" in guard,
          "an empty (null) save name must count as a queued save name")
    check(note_queue.find("if (a_result) {") < note_queue.find("++g_firstJoinEntry.count;") < note_queue.find("state.wanted = false;"),
          "an accepted save must count the first-join entry and clear the queue's wanted flag only on true")
    check("const auto queuedSignature = fileName.empty() && a_deviceID == -1 && a_outputStats == 0 && a_compress;" in save_impl
          and "queuedSignature && F4T::SessionSaveQueue::TakeOutstanding()" in save_impl
          and save_impl.find("queuedSignature && F4T::SessionSaveQueue::TakeOutstanding()") < save_impl.find("g_internalSaveOwner.exchange(ArmOwner::kNone"),
          "the arm must be consumed only by a kForceSave-signature call while our queue is outstanding: a named autosave must not take the arm or set the first-join accepted flag")
    check("g_saveQueueOutstanding" not in guard and "g_firstJoinSaveAccepted" not in guard and "g_quitQueuedAt" not in guard,
          "the queue state must live in F4TSessionSaveQueue, not in the guard")
    check(tick.find("F4T::SessionSaveQueue::PumpQueue(ArmOwner::kFirstJoin)") != -1
          and tick.find("F4T::SessionSaveQueue::PumpQueue(ArmOwner::kFirstJoin)") < tick.find("if (!g_sessionExitPending.load"),
          "Tick must pump the first-join queue even when no quit is pending, so a refused first-join save is re-queued")
    check("F4T::SessionSaveQueue::PumpQueue(ArmOwner::kQuit)" in tick and tick.find("if (!proceed)") < tick.find("F4T::SessionSaveQueue::PumpQueue(ArmOwner::kQuit)"),
          "Tick must pump the quit queue only while the quit waits for its outcome")
    check(tick.find("g_sessionExitPending.store(false") > -1 and tick.find("F4T::SessionSaveQueue::StopQueue(ArmOwner::kQuit)") > tick.find("g_sessionExitPending.store(false"),
          "a settled quit must reset the quit queue state in Tick")
    check("inline constexpr std::uint32_t kStablePollsBeforeQueue{ 3 };" in policy and "inline constexpr std::uint32_t kMaxQueueAttempts{ 3 };" in policy,
          "the poll gate must need 3 consecutive polls and at most 3 queue attempts")
    check("return a_allowed ? a_previous + 1 : 0;" in policy and "return a_stablePolls >= kStablePollsBeforeQueue && a_attemptsMade < kMaxQueueAttempts;" in policy,
          "the poll counter and queue readiness must be pure functions in the policy header")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        _merge_exit(1)

    print("F4TSessionSaveGuardContract.py: ok")

_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TSaveLineageTimingContract.py", _section_010_f4tsavelineagetimingcontract),
    ("CO-BETA/plugin/tests/F4TSavePluginPreflightContract.py", _section_011_f4tsavepluginpreflightcontract),
    ("CO-BETA/plugin/tests/F4TSessionSaveGuardContract.py", _section_012_f4tsessionsaveguardcontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TSaveModpackContracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
