#!/usr/bin/env python3
"""F4TQuestStoryContracts_part3: consolidated contract sections.

Consolidated from 1 single-contract scripts so the group runs one process
instead of 1. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TQuestStoryContracts_part3.py
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




def _section_007_f4tjoinflyovercontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TJoinFlyOverContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TJoinFlyOverContract.py")
    import re
    import sys
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[3]
    JOIN_FLOW = ROOT / "CO-BETA/plugin/src/F4TJoinFlow.cpp"
    JOIN_FLOW_HEADER = ROOT / "CO-BETA/plugin/include/F4TJoinFlow.h"
    JOIN_STATE_HEADER = ROOT / "CO-BETA/plugin/include/F4TJoinState.h"
    JOIN_STATE_TESTS = ROOT / "CO-BETA/plugin/tests/F4TJoinStateTests.cpp"
    FLY_OVER = ROOT / "CO-BETA/plugin/src/F4TFlyOver.cpp"
    FLY_OVER_HEADER = ROOT / "CO-BETA/plugin/include/F4TFlyOver.h"
    FLY_OVER_PATH_HEADER = ROOT / "CO-BETA/plugin/include/F4TFlyOverPath.h"
    NETWORKING_GNS = ROOT / "CO-BETA/plugin/src/F4TNetworkingGns.cpp"
    SESSION_LAUNCH_STATE = ROOT / "CO-BETA/plugin/src/F4TSessionLaunchState.cpp"
    SESSION_LAUNCH_ONBOARDING = ROOT / "CO-BETA/plugin/src/F4TSessionLaunchOnboarding.cpp"
    CONNECTION_POLICY = ROOT / "co-core/include/F4TConnectionPolicy.h"
    SESSION_SAVE_GUARD = ROOT / "CO-BETA/plugin/src/F4TSessionSaveGuard.cpp"
    OVERLAY = ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/join-overlay/overlay.js"
    OVERLAY_TEST = ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/join-overlay/overlay.test.js"

    FAILURES = []

    def read(path):
        return path.read_text(encoding="utf-8").replace("\r\n", "\n")

    def check(name, ok):
        if not ok:
            FAILURES.append(name)

    def check_pattern(name, text, pattern):
        check(name, re.search(pattern, text) is not None)

    def check_order(name, text, first, second):
        check(name, first in text and second in text and text.index(first) < text.index(second))

    def function_body(text, signature):
        match = re.search(signature, text)
        if not match:
            return None
        brace = text.find("{", match.end())
        if brace < 0:
            return None
        depth = 0
        for index in range(brace, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    return text[brace + 1 : index]
        return None

    def main():
        join = read(JOIN_FLOW)
        header = read(JOIN_FLOW_HEADER)
        join_state = read(JOIN_STATE_HEADER)
        join_state_tests = read(JOIN_STATE_TESTS)
        fly = read(FLY_OVER)
        fly_header = read(FLY_OVER_HEADER)
        networking_gns = read(NETWORKING_GNS)
        session_state = read(SESSION_LAUNCH_STATE)
        onboarding = read(SESSION_LAUNCH_ONBOARDING)
        save_guard = read(SESSION_SAVE_GUARD)
        overlay = read(OVERLAY)
        overlay_test = read(OVERLAY_TEST)

        removed_tokens = (
            "g_firstTime",
            "a_firstTime",
            "InVaultStartCell",
            "kVaultStartCellEditorId",
            "kLoadingWorldTimeoutSeconds",
            "kMinimumFlyOverSeconds",
            "CanApplyServerCorrection",
        )
        check(
            "the first-time flag, vault-cell detection, first-join 700 s budget and 3 s fly-over minimum are gone",
            all(token not in join for token in removed_tokens),
        )
        check_pattern(
            "NotifyAuthenticated takes no first-time parameter",
            header,
            r"void NotifyAuthenticated\(\);",
        )
        check_pattern(
            "NotifyAuthenticated is defined without a first-time parameter",
            join,
            r"void NotifyAuthenticated\(\)\s*\{",
        )
        check_pattern(
            "the handshake notifies authentication without a first-time flag",
            networking_gns,
            r"F4T::JoinFlow::NotifyAuthenticated\(\);",
        )
        check(
            "the handshake no longer passes a first-time flag to NotifyAuthenticated",
            "NotifyAuthenticated(false)" not in networking_gns,
        )

        loading_start = join.index("if (g_machine.Current() == JoinState::State::LoadingWorld)")
        fly_over_branch = join.index(
            "if (g_machine.Current() == JoinState::State::FlyOver)", loading_start
        )
        loading = join[loading_start:fly_over_branch]
        check(
            "LoadingWorld no longer evaluates fly-over eligibility",
            all(
                token not in loading
                for token in ("eligible", "WorldIsExterior", "HasPathForCurrentWorld", "FlyOver::Start")
            ),
        )
        check_pattern(
            "LoadingWorld advances with a false fly-over flag and enters Spawning",
            join,
            r"g_machine\.Advance\(JoinState::Event::WorldLoaded, false\);\s*EnterSpawning\(\);",
        )
        check(
            "the world-loaded transition is sent from exactly one place",
            join.count("JoinState::Event::WorldLoaded") == 1,
        )

        start_count = join.count("FlyOver::Start()")
        check(
            "the fly-over is started from exactly one call site in the join flow",
            start_count == 1,
        )
        vault_helper = function_body(join, r"bool TryStartVaultExitFlyOver\(\)")
        check(
            "the single fly-over start lives in the armed vault-exit helper",
            vault_helper is not None and "FlyOver::Start()" in vault_helper,
        )
        check(
            "the armed vault-exit helper is defined once and called once",
            join.count("TryStartVaultExitFlyOver") == 2,
        )
        tick = function_body(join, r"void Tick\(\)")
        check(
            "Tick handles the armed vault exit before its inactive early return",
            tick is not None
            and "TryStartVaultExitFlyOver()" in tick
            and tick.index("TryStartVaultExitFlyOver()") < tick.index("if (!g_active)"),
        )
        check(
            "the armed branch never runs from the LoadingWorld path",
            "FlyOver::Start()" not in loading,
        )

        if vault_helper is not None:
            check(
                "the armed pan returns before it starts while a join flow is already active",
                re.match(r"\s*if \(g_active\) \{\s*return false;\s*\}", vault_helper) is not None,
            )
            check_pattern(
                "the vault-exit decision is a pure function in F4TJoinState.h",
                join_state,
                r"constexpr FlyOverDecision DecideFlyOver\(const FlyOverInput& a_input\)",
            )
            check(
                "the pure decision takes every armed input and returns wait, start or disarm",
                all(
                    token in join_state
                    for token in (
                        "connected",
                        "loaded",
                        "exterior",
                        "hasPath",
                        "cameraRefused",
                        "retryExpired",
                        "FlyOverDecision::Wait",
                        "FlyOverDecision::Start",
                        "FlyOverDecision::Disarm",
                    )
                ),
            )
            check(
                "the join state tests cover every decision",
                all(
                    token in join_state_tests
                    for token in (
                        "DecideFlyOver(",
                        "FlyOverDecision::Wait",
                        "FlyOverDecision::Start",
                        "FlyOverDecision::Disarm",
                        "cameraRefused",
                        "retryExpired",
                    )
                ),
            )
            check(
                "the armed pan decides through the pure function",
                "JoinState::DecideFlyOver(" in vault_helper
                and "JoinState::FlyOverInput{" in vault_helper,
            )
            check_order(
                "the armed pan reads the server connection before it decides",
                vault_helper,
                "F4T::Networking::IsConnectedToServer()",
                "JoinState::DecideFlyOver(",
            )
            check_order(
                "a pending session exit counts as not connected before the armed pan decides",
                vault_helper,
                "F4T::SessionSaveGuard::IsSessionExitPending()",
                "JoinState::DecideFlyOver(",
            )
            check(
                "the armed pan needs a loaded exterior world with a path before the start",
                "PlayerWorldLoaded(" in vault_helper
                and "WorldIsExterior(" in vault_helper
                and "FlyOver::HasPathForCurrentWorld()" in vault_helper,
            )
            check_order(
                "the fly-over path is checked before the camera takeover",
                vault_helper,
                "FlyOver::HasPathForCurrentWorld()",
                "FlyOver::Start()",
            )
            check(
                "a loaded exterior with no path disarms instead of staying armed",
                re.search(
                    r"decision == JoinState::FlyOverDecision::Disarm[\s\S]{0,500}?"
                    r"exterior && !hasPath[\s\S]{0,250}?DisarmVaultExitFlyOver\(",
                    vault_helper,
                )
                is not None,
            )
            check(
                "the retired one-shot logging state is gone",
                "g_vaultExitNoPathLogged" not in join and "g_vaultExitCheckedWorld" not in join,
            )
            check(
                "a refused camera keeps the arm armed and starts the retry timer",
                "FlyOver::CanTakeOverCamera()" in vault_helper
                and "NoteVaultExitCameraRefusal(" in vault_helper,
            )
            check_pattern(
                "the retry window is the named 10 s constant",
                join,
                r"constexpr double kVaultExitFlyOverRetrySeconds = 10\.0;",
            )
            check(
                "the retry expiry disarms and logs the world",
                re.search(
                    r"retryExpired\)[\s\S]{0,800}?LogInfo\([\s\S]{0,350}?DisarmVaultExitFlyOver\(",
                    vault_helper,
                )
                is not None,
            )
            check(
                "a non-camera start failure disarms at once",
                re.search(
                    r"if \(!FlyOver::Start\(\)\)\s*\{[\s\S]*?DisarmVaultExitFlyOver\(",
                    vault_helper,
                )
                is not None,
            )
            check_order(
                "the arm does not activate the flow before the camera takeover",
                vault_helper,
                "FlyOver::Start()",
                "g_active = true",
            )
            check_order(
                "the arm does not mark the vault pan before the camera takeover",
                vault_helper,
                "FlyOver::Start()",
                "g_vaultExitPan = true",
            )
            check_order(
                "the arm does not push the overlay before the camera takeover",
                vault_helper,
                "FlyOver::Start()",
                "PushOverlay(nullptr)",
            )
            check(
                "a successful start still enters the FlyOver state and marks the vault pan",
                "JoinState::State::FlyOver" in vault_helper
                and "g_active = true" in vault_helper
                and "g_vaultExitPan = true" in vault_helper,
            )
            disarm_body = function_body(join, r"void DisarmVaultExitFlyOver\(std::string_view a_reason\)")
            check(
                "every disarm clears the retry timer",
                disarm_body is not None
                and "g_vaultExitRetryStarted = false" in disarm_body
                and "g_vaultExitRetryStartSeconds = 0.0" in disarm_body,
            )
            check(
                "the arm and the flow end reset the retry timer as well",
                join.count("g_vaultExitRetryStarted = false;") >= 4,
            )

        start_body = function_body(fly, r"bool Start\(\)")
        install_body = function_body(fly, r"bool InstallTakeover\(\)")
        check(
            "a refused start never marks the restore guard",
            start_body is not None
            and "MarkTakeoverInstalled" not in start_body
            and "AbandonAfterWorldReset" not in start_body,
        )
        check(
            "the restore guard is marked only after the camera state is validated",
            install_body is not None
            and "MarkTakeoverInstalled" in install_body
            and "previousEnabled" in install_body
            and install_body.index("MarkTakeoverInstalled") > install_body.index("previousEnabled"),
        )
        check_pattern(
            "the camera readiness probe is exposed to the join flow",
            fly_header,
            r"\[\[nodiscard\]\] bool CanTakeOverCamera\(\);",
        )
        camera_probe = function_body(fly, r"bool CanTakeOverCamera\(\)")
        check(
            "the camera readiness probe checks the same camera states the takeover rejects",
            "StateIsSafeToTakeOver" in fly
            and camera_probe is not None
            and "GetCameraCurrentState()" in camera_probe,
        )

        begin_body = function_body(join, r"void BeginConnectingOnGameThread\(\)")
        check(
            "a new join disarms the vault-exit pan",
            begin_body is not None and "DisarmVaultExitFlyOver(" in begin_body,
        )
        reset_body = function_body(join, r"void OnWorldReset\(bool a_newWorld\)")
        check(
            "OnWorldReset restores the camera before it disarms and ends the flow",
            reset_body is not None
            and re.search(r"FlyOver::Restore\(reason\);[\s\S]*?DisarmVaultExitFlyOver\(", reset_body)
            is not None
            and re.search(
                r"if \(g_active && \(state == JoinState::State::FlyOver \|\| "
                r"state == JoinState::State::Spawning\)\)",
                reset_body,
            )
            is not None
            and reset_body.index("FlyOver::Restore(reason)") < reset_body.index("FinishFlow("),
        )

        check_pattern(
            "the join flow exposes EndForFirstTimeJoin",
            header,
            r"void EndForFirstTimeJoin\(\);",
        )
        end_first_time = function_body(join, r"void EndForFirstTimeJoin\(\)")
        check(
            "EndForFirstTimeJoin hides the overlay and stays inactive with no failure event",
            end_first_time is not None
            and "HideOverlay();" in end_first_time
            and "g_active = false" in end_first_time
            and "FinishFlow(" not in end_first_time
            and "Event::Failed" not in end_first_time,
        )
        first_time_join = function_body(onboarding, r"LaunchOutcome StartFirstTimeJoin\(")
        check(
            "StartFirstTimeJoin releases the join overlay right after queueing the first-time join",
            first_time_join is not None
            and re.search(
                r"QueueFirstTimeJoin\([\s\S]*?F4T::JoinFlow::EndForFirstTimeJoin\(\);",
                first_time_join,
            )
            is not None,
        )

        check_pattern(
            "the join flow exposes ArmVaultExitFlyOver",
            header,
            r"void ArmVaultExitFlyOver\(\);",
        )
        complete = function_body(
            session_state,
            r"void CompletePendingJoin\(bool a_success, std::string_view a_reason\)",
        )
        check(
            "CompletePendingJoin reads the pending completion",
            complete is not None,
        )
        was_first_join = None
        if complete is not None:
            capture = re.search(r"(?:const )?(?:bool|auto)\s+(\w+)\s*=\s*IsFirstJoinInProgress\(\);", complete)
            check(
                "CompletePendingJoin captures the first-join phase before it changes phase",
                capture is not None
                and capture.start() < complete.index("SetPhase("),
            )
            if capture is not None:
                was_first_join = capture.group(1)
                check(
                    "CompletePendingJoin arms the vault-exit pan only on a successful first join",
                    re.search(
                        r"if \(a_success && " + re.escape(was_first_join) + r"\)\s*\{[\s\S]{0,400}?"
                        r"F4T::JoinFlow::ArmVaultExitFlyOver\(\);",
                        complete,
                    )
                    is not None,
                )
            check(
                "the join completion arms the vault-exit pan from exactly one place",
                complete.count("ArmVaultExitFlyOver()") == 1,
            )
            arm = complete.find("ArmVaultExitFlyOver()")
            check(
                "the vault-exit pan is armed only after the terminal phase is set",
                arm > complete.index("SetPhase(")
                and (was_first_join is None or arm > complete.index("a_success && " + was_first_join)),
            )

        arm_on_game_thread = function_body(join, r"void ArmVaultExitFlyOverOnGameThread\(\)")
        arm_public = function_body(join, r"void ArmVaultExitFlyOver\(\)")
        check(
            "the arm body is an anonymous-namespace function defined before the public entry",
            arm_on_game_thread is not None
            and join.index("void ArmVaultExitFlyOverOnGameThread()") < join.index("\t}\n\n\tvoid BeginConnecting()"),
        )
        check(
            "the game-thread arm sets the armed flag and resets the retry and path state with its log line",
            arm_on_game_thread is not None
            and all(
                token in arm_on_game_thread
                for token in (
                    "g_vaultExitArmed = true;",
                    "g_vaultExitPan = false;",
                    "g_vaultExitRetryStarted = false;",
                    "g_vaultExitRetryStartSeconds = 0.0;",
                    "g_vaultExitPathWorld = 0;",
                    "g_vaultExitPathAvailable = false;",
                    'LogInfo("Vault-exit fly-over armed; waiting for the first exterior world with a path.");',
                )
            ),
        )
        check(
            "the armed flag is written from exactly one place, the game-thread arm",
            join.count("g_vaultExitArmed = true;") == 1
            and arm_on_game_thread is not None
            and "g_vaultExitArmed = true;" in arm_on_game_thread,
        )
        check(
            "the public arm queues the game-thread body through the F4SE task interface",
            arm_public is not None
            and re.search(
                r"F4SE::GetTaskInterface\(\)[\s\S]*?tasks->AddTask\(\[epoch\]\(\) \{\s*"
                r"if \(JoinState::ShouldArmVaultExit\(epoch, g_vaultExitDisarmEpoch\.load\(\)\)\) \{\s*"
                r"ArmVaultExitFlyOverOnGameThread\(\);\s*\}\s*\}\);\s*return;\s*\}\s*"
                r"ArmVaultExitFlyOverOnGameThread\(\);\s*\Z",
                arm_public,
            )
            is not None,
        )
        check(
            "the public arm reads the disarm epoch on the calling thread before it queues",
            arm_public is not None
            and re.search(
                r"const auto epoch = g_vaultExitDisarmEpoch\.load\(\);\s*"
                r"if \(auto\* tasks = F4SE::GetTaskInterface\(\)\) \{\s*tasks->AddTask\(\[epoch\]\(\) \{",
                arm_public,
            )
            is not None,
        )
        check_pattern(
            "the vault-exit disarm epoch is a plain atomic 64-bit counter",
            join,
            r"std::atomic<std::uint64_t> g_vaultExitDisarmEpoch\{ 0 \};",
        )
        check_pattern(
            "the queued arm decision is the pure ShouldArmVaultExit helper",
            join_state,
            r"constexpr bool ShouldArmVaultExit\(std::uint64_t a_capturedEpoch, std::uint64_t a_currentEpoch\) noexcept",
        )
        check(
            "the join state tests cover ShouldArmVaultExit",
            "ShouldArmVaultExit(" in join_state_tests,
        )
        disarm_epoch_body = function_body(join, r"void DisarmVaultExitFlyOver\(std::string_view a_reason\)")
        check(
            "every vault-exit disarm bumps the epoch before its early return",
            disarm_epoch_body is not None
            and "g_vaultExitDisarmEpoch.fetch_add(1)" in disarm_epoch_body
            and disarm_epoch_body.index("g_vaultExitDisarmEpoch.fetch_add(1)")
            < disarm_epoch_body.index("if (!g_vaultExitArmed)"),
        )
        finish_epoch_body = function_body(
            join,
            r"void FinishFlow\(JoinState::Event a_event, std::string_view a_reason, FlyOver::ExitReason a_reasonKind\)",
        )
        check(
            "the flow end bumps the disarm epoch before it clears the armed flag",
            finish_epoch_body is not None
            and "g_vaultExitDisarmEpoch.fetch_add(1)" in finish_epoch_body
            and finish_epoch_body.index("g_vaultExitDisarmEpoch.fetch_add(1)")
            < finish_epoch_body.index("g_vaultExitArmed = false;"),
        )
        check(
            "the world reset reaches the epoch-bumping disarm without an early return",
            reset_body is not None
            and "return" not in reset_body
            and re.search(r'DisarmVaultExitFlyOver\("world reset"\);', reset_body) is not None,
        )
        check(
            "the arm body is called from the queued task and the no-task fallback only",
            join.count("ArmVaultExitFlyOverOnGameThread()") == 3,
        )
        check_pattern(
            "the end epoch is a plain atomic 64-bit counter for the worker-thread read",
            join,
            r"std::atomic<std::uint64_t> g_joinFlowEndEpoch\{ 0 \};",
        )
        check_order(
            "EndForFirstTimeJoin advances the end epoch before it hides the overlay",
            end_first_time or "",
            "g_joinFlowEndEpoch.fetch_add(1)",
            "HideOverlay();",
        )
        check_pattern(
            "the queued begin decision is the pure ShouldBeginConnecting helper",
            join_state,
            r"constexpr bool ShouldBeginConnecting\(std::uint64_t a_capturedEpoch, std::uint64_t a_currentEpoch\) noexcept",
        )
        check(
            "the join state tests cover ShouldBeginConnecting",
            "ShouldBeginConnecting(" in join_state_tests,
        )
        begin_public = function_body(join, r"void BeginConnecting\(\)")
        check(
            "BeginConnecting reads the end epoch on the calling thread before it queues",
            begin_public is not None
            and "g_joinFlowEndEpoch.load()" in begin_public
            and "AddTask(" in begin_public
            and begin_public.index("g_joinFlowEndEpoch.load()") < begin_public.index("AddTask("),
        )
        check(
            "the queued begin compares the captured epoch before it begins connecting",
            begin_public is not None
            and re.search(
                r"AddTask\(\[epoch\]\(\) \{\s*if \(JoinState::ShouldBeginConnecting\(epoch, "
                r"g_joinFlowEndEpoch\.load\(\)\)\) \{\s*BeginConnectingOnGameThread\(\);\s*\}\s*\}\);",
                begin_public,
            )
            is not None,
        )
        check(
            "the no-task fallback begins connecting directly",
            begin_public is not None
            and re.search(r"return;\s*\}\s*BeginConnectingOnGameThread\(\);\s*\Z", begin_public) is not None,
        )

        check_pattern(
            "the vault-exit fly-over duration is 26 s",
            join,
            r"constexpr double kVaultExitFlyOverSeconds = 26\.0;",
        )
        check_pattern(
            "the fly-over exit condition uses the vault-exit duration",
            join,
            r"FlyOver::ElapsedSeconds\(\) >= kVaultExitFlyOverSeconds\b",
        )
        check(
            "the vault-exit duration is defined once and used once",
            join.count("kVaultExitFlyOverSeconds") == 2,
        )
        check(
            "the fly-over writes pitch to rotation.x and yaw to rotation.y",
            "freeState->rotation.x = a_sample.pitch;" in fly
            and "freeState->rotation.y = a_sample.yaw;" in fly,
        )
        glide_seconds = re.search(r"kBostonGlideSeconds = (\d+)\.0F;", read(FLY_OVER_PATH_HEADER))
        check(
            "the Boston glide outlasts the vault-exit duration so the camera never flies back",
            glide_seconds is not None and float(glide_seconds.group(1)) > 26.0,
        )

        publish = function_body(join, r"bool CanPublishGameplayTransform\(\)")
        check(
            "gameplay transforms keep publishing during the vault-exit pan",
            publish is not None
            and re.search(
                r"return !g_active \|\|[\s\S]{0,120}?[Vv]ault[\s\S]{0,120}?AllowsGameplayPublish\(",
                publish,
            )
            is not None,
        )

        notify = function_body(join, r"void NotifyAuthenticatedOnGameThread\(\)")
        check(
            "authentication no longer re-activates an inactive join flow",
            notify is not None and "BeginConnectingOnGameThread()" not in notify,
        )
        check(
            "authentication pushes the overlay only while the flow is active",
            notify is not None
            and re.search(r"if \(!g_active\)\s*\{\s*return;\s*\}\s*PushOverlay\(nullptr\);", notify)
            is not None,
        )

        check_pattern(
            "the connect budget is 60 s",
            join,
            r"constexpr double kConnectingTimeoutSeconds = 60\.0;",
        )
        check_pattern(
            "the returning-join loading budget is 60 s",
            join,
            r"constexpr double kReturningLoadingTimeoutSeconds = 60\.0;",
        )
        check_pattern(
            "a connect or authentication that stalls past the connect budget ends the join flow",
            join,
            r"if \(NowSeconds\(\) - g_loadingStartSeconds >= kConnectingTimeoutSeconds\) \{\s*"
            r"FinishFlow\(JoinState::Event::Failed, \"timed out connecting\"",
        )
        check_pattern(
            "the returning-join world gate still requires a loaded gameplay world",
            join,
            r"bool PlayerWorldLoaded\(const RE::PlayerCharacter& a_player\)\s*\{\s*"
            r"if \(!a_player\.GetParentCell\(\)\) \{\s*return false;\s*\}\s*"
            r"if \(F4T::SessionWorldState::IsLoadingScreenActive\(\) \|\| F4T::SessionWorldState::IsMainMenuOpen\(\)\) \{",
        )
        check_pattern(
            "FinishFlow restores the camera, controls and HUD on every failure, cancel and disconnect",
            join,
            r"void FinishFlow\(.*?\)\s*\{\s*FlyOver::Restore\(a_reasonKind\);",
        )
        check_pattern(
            "EndSessionJoin marshals to the game thread before it finishes the flow",
            join,
            r"void EndSessionJoin\(bool a_success, std::string_view a_reason\)\s*\{\s*if \(a_success\) \{\s*return;\s*\}\s*"
            r"if \(auto\* tasks = F4SE::GetTaskInterface\(\)\) \{\s*"
            r"tasks->AddTask\(\[reason = std::string\{ a_reason \}\]\(\) \{\s*EndSessionJoinOnGameThread\(reason\);",
        )
        check_pattern(
            "the marshalled end of the join flow acts only while a join is active",
            join,
            r"void EndSessionJoinOnGameThread\(std::string_view a_reason\)\s*\{\s*if \(!g_active\) \{\s*return;\s*\}\s*"
            r"FinishFlow\(JoinState::Event::Failed, a_reason, FlyOver::ExitReason::Failed\);",
        )
        check_pattern(
            "a failed connect ends the join flow once, after the fingerprint and duplicate-identity retries",
            networking_gns,
            r"fingerprint, a_joinId, true, cancelled, a_backgroundValidation, a_generationOut\);\s*\}[\s\S]*?if \(!connected\) \{\s*F4T::JoinFlow::EndSessionJoin\(false, a_error\);\s*\}",
        )
        check_pattern(
            "the retry loop asks the pure policy on every pass with the current failure and the fingerprint just sent",
            networking_gns,
            r"const auto nextStep = \[&\]\(\) \{\s*return F4T::ConnectionPolicy::DecideNextJoinRetry\(\s*"
            r"failure \? std::string_view\{ failure->code \} : std::string_view\{\},\s*"
            r"fingerprint \? std::string_view\{ \*fingerprint \} : std::string_view\{\},\s*"
            r"failure \? std::string_view\{ failure->matchingModpackFingerprint \} : std::string_view\{\},\s*"
            r"retriesUsed,\s*elapsedSeconds\(\)\);",
        )
        check_pattern(
            "the single fingerprint retry before the loop is limited to modpack_mismatch",
            networking_gns,
            r"if \(!connected && failure && failure->code == \"modpack_mismatch\" &&\s*"
            r"!failure->matchingModpackFingerprint\.empty\(\)\) \{",
        )
        check_pattern(
            "the retry loop gives up when the user cancels, before each wait and attempt",
            networking_gns,
            r"const auto cancelled = \[joinGeneration\] \{\s*return F4T::UIClient::IsVoluntaryExitRequested\(\) \|\| F4T::UIClient::JoinGeneration\(\) != joinGeneration;\s*\};[\s\S]*?const auto& userCancelled = cancelled;[\s\S]*?"
            r"while \(!userCancelled\(\)\) \{\s*const auto step = nextStep\(\);[\s\S]*?"
            r"if \(userCancelled\(\)\) \{\s*break;",
        )
        check_pattern(
            "the retry loop stops on a stop step or when the attempt would not fit the budget",
            networking_gns,
            r"if \(step == F4T::ConnectionPolicy::JoinRetryStep::stop \|\|\s*"
            r"!retryFits\(step == F4T::ConnectionPolicy::JoinRetryStep::retryAfterInterval\)\) \{\s*break;",
        )
        check_pattern(
            "every retry attempt counts toward the cap before it starts",
            networking_gns,
            r"\+\+retriesUsed;\s*failure\.reset\(\);",
        )
        check_pattern(
            "the pure policy bounds every retry by the attempt cap and the budget",
            read(CONNECTION_POLICY),
            r"a_failureCode == \"modpack_mismatch\" && !a_matchingFingerprint\.empty\(\) &&\s*"
            r"a_matchingFingerprint != a_sentFingerprint && a_attemptsUsed < a_maximumAttempts &&\s*"
            r"std::isfinite\(a_elapsedSeconds\) && a_elapsedSeconds < a_budgetSeconds\)\s*\{\s*"
            r"return JoinRetryStep::retryImmediately;",
        )
        check_pattern(
            "the quit save restores the fly-over before it queues the quick save",
            save_guard,
            r"void StartQuitSave\(std::string_view a_reason\)\s*\{\s*"
            r"F4T::FlyOver::Restore\(F4T::FlyOver::ExitReason::Disconnect\);[\s\S]*?"
            r"StartQueuedSave\(ArmOwner::kQuit, a_reason\)",
        )
        check_pattern(
            "the watchdog and teardown paths restore the takeover",
            fly,
            r"Restore\(ExitReason::SessionTeardown\);\s*return true;[\s\S]*?Restore\(ExitReason::WatchdogTimeout\);",
        )
        check_pattern(
            "the plugin shutdown path ends the takeover",
            join,
            r"void Shutdown\(\)\s*\{\s*FlyOver::Shutdown\(\);",
        )
        check_pattern(
            "the single restore path releases player input and the input context",
            fly,
            r"void Restore\(ExitReason a_reason\)[\s\S]*?PopInputContext\([\s\S]*?ReleasePlayerState\(\);",
        )
        check_pattern(
            "the single restore path puts back the blockPlayerInput byte it captured",
            fly,
            r"controls->blockPlayerInput = g_takeover\.savedBlockPlayerInput;",
        )
        lock_call = "SetVaultExitControlsDisabled(true"
        check(
            "the control lock is taken from exactly one site and only after the takeover starts",
            join.count(lock_call) == 1
            and join.index(lock_call) > join.index("FlyOver::Start()"),
        )
        refusal_body = function_body(join, r"void NoteVaultExitCameraRefusal\(std::string_view a_worldName\)")
        check(
            "the camera retry leaves player controls alone",
            refusal_body is not None and "SetVaultExitControlsDisabled" not in refusal_body,
        )
        shutdown_body = function_body(join, r"void Shutdown\(\)")
        check(
            "plugin shutdown does not run a console command",
            shutdown_body is not None and "SetVaultExitControlsDisabled" not in shutdown_body,
        )
        finish_body = function_body(
            join,
            r"void FinishFlow\(JoinState::Event a_event, std::string_view a_reason, FlyOver::ExitReason a_reasonKind\)",
        )
        check(
            "FinishFlow releases the control lock",
            finish_body is not None and "SetVaultExitControlsDisabled(false" in finish_body,
        )
        reset_lock_body = function_body(join, r"void OnWorldReset\(bool a_newWorld\)")
        check(
            "OnWorldReset releases the control lock",
            reset_lock_body is not None and "SetVaultExitControlsDisabled(false" in reset_lock_body,
        )
        tick_body = function_body(join, r"void Tick\(\)")
        check(
            "both fly-over-ended paths release the control lock",
            tick_body is not None
            and tick_body.count('SetVaultExitControlsDisabled(false, "fly-over ended")') == 2,
        )
        check(
            "a session exit pending at the fly-over end skips the Entering overlay and still advances the flow",
            tick_body is not None
            and re.search(
                r"if \(FlyOver::Tick\(\)\) \{[\s\S]*?g_machine\.Advance\(JoinState::Event::FlyOverTimeout, false\);\s*"
                r"if \(!F4T::SessionSaveGuard::IsSessionExitPending\(\)\) \{\s*EnterSpawning\(\);\s*\}",
                tick_body,
            )
            is not None,
        )
        for name, text in (("F4TJoinFlow.cpp", join), ("F4TFlyOver.cpp", fly)):
            check(
                f"{name} must not touch the first-join save path",
                re.search(r"QueueSaveLoadTask|ArmInternalSave|BGSSaveLoadManager|kQuickSave|SaveGame\(", text) is None,
            )
        check(
            "the pure decision carries the vanilla-scene input as its last field",
            re.search(r"bool retryExpired\{ false \};\s*bool vanillaSceneHolding\{ false \};\s*\}", join_state) is not None,
        )
        decide_body = function_body(join_state, r"constexpr FlyOverDecision DecideFlyOver\(const FlyOverInput& a_input\)") or ""
        check_order(
            "the vanilla scene holds the pan after the path check and before the camera check",
            decide_body,
            "!a_input.hasPath",
            "a_input.vanillaSceneHolding",
        )
        check_order(
            "the vanilla scene holds the pan before the camera refusal is considered",
            decide_body,
            "a_input.vanillaSceneHolding",
            "a_input.cameraRefused",
        )
        check(
            "the armed pan reads MQ102 and waits for its controls-restored stage",
            "kMq102FormId = 0x0001CC2A" in join
            and "kMq102ControlsRestoredStage = 10" in join
            and "GetFormByID<RE::TESQuest>(kMq102FormId)" in join
            and "vanillaSceneHolding," in vault_helper,
        )
        check_order(
            "the vanilla-scene hold is computed before the pure decision",
            vault_helper,
            "vanillaSceneHolding = true",
            "JoinState::DecideFlyOver(",
        )
        check_pattern(
            "an expired vanilla scene wait disarms and returns without taking the camera or controls",
            vault_helper,
            r"if \(NowSeconds\(\) - g_vaultExitSceneWaitStartSeconds >= kVaultExitSceneWaitSeconds\) \{\s*"
            r'DisarmVaultExitFlyOver\("vanilla controls were not restored before the wait expired"\);\s*'
            r'ReleaseVaultExitControls\("vanilla pod-exit wait expired"\);\s*'
            r"return false;\s*\}\s*vanillaSceneHolding = true;",
        )
        for helper_name, helper_signature in (
            ("FinishFlow", r"void FinishFlow\("),
            ("DisarmVaultExitFlyOver", r"void DisarmVaultExitFlyOver\("),
            ("ArmVaultExitFlyOverOnGameThread", r"void ArmVaultExitFlyOverOnGameThread\("),
        ):
            body = function_body(join, helper_signature) or ""
            check(f"{helper_name} resets the vanilla-scene wait", "ResetVaultExitSceneWait();" in body)
        check(
            "the start resets the vanilla-scene wait",
            "ResetVaultExitSceneWait();" in vault_helper,
        )
        check_pattern(
            "FlyOver status copy is the welcome line",
            overlay,
            r'FlyOver: "Welcome to Commonwealth Online",',
        )
        check_pattern(
            "overlay test pins the welcome copy",
            overlay_test,
            r'assert\.equal\(model\.STATUS\.FlyOver, "Welcome to Commonwealth Online"\);',
        )

        if FAILURES:
            for name in FAILURES:
                print(f"FAIL: {name}")
            print(f"{len(FAILURES)} contract check(s) failed")
            return 1
        print("F4TJoinFlyOverContract: all checks passed")
        return 0

    if True:
        _merge_exit(main())


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TJoinFlyOverContract.py", _section_007_f4tjoinflyovercontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TQuestStoryContracts_part3: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
