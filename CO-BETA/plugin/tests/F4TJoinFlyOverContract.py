import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
JOIN_FLOW = ROOT / "CO-BETA/plugin/src/F4TJoinFlow.cpp"
FLY_OVER = ROOT / "CO-BETA/plugin/src/F4TFlyOver.cpp"
NETWORKING_GNS = ROOT / "CO-BETA/plugin/src/F4TNetworkingGns.cpp"
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


def main():
    join = read(JOIN_FLOW)
    fly = read(FLY_OVER)
    networking_gns = read(NETWORKING_GNS)
    save_guard = read(SESSION_SAVE_GUARD)
    overlay = read(OVERLAY)
    overlay_test = read(OVERLAY_TEST)

    check_pattern(
        "first-time world ready waits for the first-join setup (quicksave and profile bind)",
        join,
        r"const auto ready = g_firstTime \?\s*\(F4T::SessionWorldState::IsGameplayWorldReady\(\*player\) &&\s*"
        r"F4T::SessionLaunch::CanApplyServerCorrection\(\)\) :\s*PlayerWorldLoaded\(\*player\);",
    )
    check_pattern(
        "vault detection restarts the first-join budget and hides the join overlay",
        join,
        r"InVaultStartCell\(\*player\) && !g_firstTime\) \{\s*g_firstTime = true;\s*"
        r"g_loadingStartSeconds = NowSeconds\(\);\s*HideOverlay\(\);",
    )
    check_pattern(
        "the first-time LoadingWorld budget is 700 s",
        join,
        r"constexpr double kLoadingWorldTimeoutSeconds = 700\.0;",
    )
    check_pattern(
        "the connect budget is 60 s",
        join,
        r"constexpr double kConnectingTimeoutSeconds = 60\.0;",
    )
    check_pattern(
        "gameplay publish opens only in Ready or with no active join flow",
        join,
        r"bool CanPublishGameplayTransform\(\)\s*\{\s*return !g_active \|\| "
        r"JoinState::AllowsGameplayPublish\(g_machine\.Current\(\)\);\s*\}",
    )
    check_pattern(
        "fly-over start is gated on a loaded world, never on the loading menu",
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
        "OnWorldReset always restores and ends the flow only in FlyOver or Spawning",
        join,
        r"void OnWorldReset\(bool a_newWorld\)\s*\{\s*"
        r"const auto reason = a_newWorld \? FlyOver::ExitReason::NewGame : FlyOver::ExitReason::LoadGame;\s*"
        r"FlyOver::Restore\(reason\);\s*const auto state = g_machine\.Current\(\);\s*"
        r"if \(g_active && \(state == JoinState::State::FlyOver \|\| state == JoinState::State::Spawning\)\) \{\s*"
        r"FinishFlow\(JoinState::Event::Cancelled, \"world reset\", reason\);",
    )
    check_pattern(
        "a connect or authentication that stalls past the connect budget ends the join flow",
        join,
        r"if \(NowSeconds\(\) - g_loadingStartSeconds >= kConnectingTimeoutSeconds\) \{\s*"
        r"FinishFlow\(JoinState::Event::Failed, \"timed out connecting\"",
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
        r"matching, a_joinId\);\s*\}[\s\S]*?if \(!connected\) \{\s*F4T::JoinFlow::EndSessionJoin\(false, a_error\);\s*\}",
    )
    check_pattern(
        "the duplicate-identity retry is bounded and gives up when the join is superseded",
        networking_gns,
        r'failure->code == "duplicate_identity"\)[\s\S]*?kRetryBudget[\s\S]*?'
        r"GetConnectionGeneration\(\) != generation\) \{\s*break;",
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
    for name, text in (("F4TJoinFlow.cpp", join), ("F4TFlyOver.cpp", fly)):
        check(
            f"{name} must not touch the first-join save path",
            re.search(r"QueueSaveLoadTask|ArmInternalSave|BGSSaveLoadManager|kQuickSave|SaveGame\(", text) is None,
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


if __name__ == "__main__":
    sys.exit(main())
