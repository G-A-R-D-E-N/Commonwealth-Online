from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "CO-BETA" / "plugin"
PAPYRUS_SOURCE = ROOT / "CO-BETA" / "creation-kit" / "scripts" / "source" / "COCharacterSetupQuestScript.psc"
PAPYRUS_PEX = ROOT / "CO-BETA" / "creation-kit" / "scripts" / "compiled" / "COCharacterSetupQuestScript.pex"
SETUP_SOURCE = PLUGIN / "src" / "F4TSessionCharacterSetupFlow.cpp"
SESSION_SOURCES = [
    PLUGIN / "src" / name
    for name in (
        "F4TSessionLaunchSaves.cpp",
        "F4TSessionLaunchState.cpp",
        "F4TSessionLaunchWorld.cpp",
        "F4TSessionLaunchOnboarding.cpp",
        "F4TSessionLaunch.cpp",
    )
]
INTERNAL_SOURCE = PLUGIN / "src" / "F4TSessionLaunchInternal.h"
XMAKE_SOURCE = PLUGIN / "xmake.lua"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def read_session_source() -> str:
    return "".join(read(path) for path in SESSION_SOURCES)


def in_order(text: str, markers: list[str]) -> bool:
    position = -1
    for marker in markers:
        found = text.find(marker, position + 1)
        if found < 0:
            return False
        position = found
    return True


def test_papyrus_onboarding_uses_sqs_teleport_spells_and_sink_lock():
    source = read(PAPYRUS_SOURCE)
    assert "Int Property TeleportOutSpellFormId = 0x001F1022" in source
    assert "Int Property TeleportInSpellFormId = 0x001E5601" in source
    assert "Int Property FaceGenSinkRefTypeFormId = 0x00179D20" in source
    assert "RegisterForRemoteEvent(playerRef, \"OnGetUp\")" in source
    assert "RegisterForRemoteEvent(tutorial, \"OnStageSet\")" in source
    assert "Event Actor.OnGetUp(Actor akSender, ObjectReference akFurniture)" in source
    assert "akFurniture.HasRefType(sinkType)" in source
    assert "DisablePlayerControls(abMovement = False, abFighting = True, abCamSwitch = True, abLooking = False, abSneaking = True, abMenu = False, abActivate = False, abJournalTabs = False, abVATS = False, abFavorites = False, abRunning = True)" in source
    assert "Event Quest.OnStageSet(Quest akSender, int auiStageID, int auiItemID)" in source
    assert "auiStageID != 2025" in source
    assert "Utility.Wait(1.0)\n    RegisterForMenuOpenCloseEvent(\"SPECIALMenu\")\n    Game.ShowSPECIALMenu()" in source
    assert "magicDoor.PreloadTargetArea()" in source


def test_papyrus_mq101_guard_precedes_registration():
    source = read(PAPYRUS_SOURCE)
    assert in_order(
        source,
        ["mq101.GetStage() >= 900 || mq101.IsCompleted()", "RegisterForRemoteEvent(playerRef, \"OnGetUp\")"],
    )


def test_papyrus_continue_to_vault_follows_sqs_order():
    source = read(PAPYRUS_SOURCE)
    body_start = source.index("Function ContinueToVault()")
    body = source[body_start:source.index("EndFunction", body_start)]
    steps = [
        "Utility.Wait(1.0)",
        "playerRef.AddSpell(outSpell, False)",
        "Utility.Wait(3.0)",
        "cryopod.Push()",
        "magicDoor.Activate(playerRef)",
        "playerRef.AddSpell(inSpell, False)",
        "CallFunctionNoWait(\"SetStage\", stageArgs)",
        "Utility.Wait(0.5)",
        "Game.FadeOutGame(False, False, 12.0, 8.0)",
        "Dispose()",
    ]
    for step in steps:
        assert body.count(step) == 1, step
    assert in_order(body, steps)
    assert body.count("stageArgs[0] = 900") == 1
    assert "playerRef && outSpell && inSpell && cryopod && magicDoor && mq101" in body


def test_papyrus_dispose_restores_archetypes_and_side_quests_then_stage_100():
    source = read(PAPYRUS_SOURCE)
    body_start = source.index("Function Dispose()")
    body = source[body_start:source.index("EndFunction", body_start)]
    assert in_order(
        body,
        [
            "SetupInputLayer.Delete()",
            "playerRef.ChangeAnimArchetype(VanillaForm(AnimArchetypePlayerFormId) as Keyword)",
            "playerRef.ChangeAnimFaceArchetype(VanillaForm(AnimFaceArchetypePlayerFormId) as Keyword)",
            "SetVanillaQuestStage(MQ101PlayerCommentsFormId, 1000)",
            "SetVanillaQuestStage(MQ101SanctuaryHillsFormId, 1000)",
            "SetVanillaQuestStage(MQ101Vault111FormId, 1000)",
            "SetVanillaQuestStage(MQ101RadioFormId, 100)",
            "tvStation.Stop()",
            "playerHouseInt.Remove()",
            "SetStage(100)",
        ],
    )
    for guard in ("If playerRef", "If tvStation", "If playerHouseInt", "If vanillaQuest"):
        assert guard in source, guard


def test_papyrus_abort_never_advances_mq_stages():
    source = read(PAPYRUS_SOURCE)
    body_start = source.index("Function AbortOnboarding()")
    body = source[body_start:source.index("EndFunction", body_start)]
    assert "UnregisterForRemoteEvent(playerRef, \"OnGetUp\")" in body
    assert "UnregisterForRemoteEvent(tutorial, \"OnStageSet\")" in body
    assert "SetupInputLayer.Delete()" in body
    assert "ChangeAnimArchetype" in body
    assert "SetStage" not in body


def test_papyrus_has_no_legacy_setup_or_coc_paths():
    source = read(PAPYRUS_SOURCE).lower()
    for legacy in ("startmarker", "genderchoicemessage", "sexchangepending", "facecameraidle",
                   "bodycameraidle", "showracemenu", "beginsetup", "coc vault111cryo", "setstage(5000)"):
        assert legacy not in source, legacy


def test_papyrus_compiled_pex_is_present():
    assert PAPYRUS_PEX.is_file()


def test_plugin_flow_uses_brief_phases_and_vanilla_new_game():
    session = read_session_source()
    assert in_order(
        session,
        [
            "CharGenPhase::StartingNewGame,",
            "CharGenPhase::VanillaIntro,",
            "CharGenPhase::WaitingForContinue,",
            "CharGenPhase::WaitingForPodExit,",
            "CharGenPhase::WaitingForGameplayWorldReady,",
            "CharGenPhase::WaitingForProfileBind,",
        ],
    )
    assert "F4T::SessionCharacterSetupFlow::StartVanillaNewGame()" in session
    assert "F4T::SessionCharacterSetupFlow::BeginOnboarding()" in session
    assert "F4T::SessionCharacterSetupFlow::ShowContinueAlert()" in session
    assert "F4T::SessionCharacterSetupFlow::ContinueToVault()" in session
    assert "F4T::SessionCharacterSetupFlow::Abort()" in session
    assert "g_newGameMessageSeen = true;" in session


def test_plugin_failed_start_is_clean_and_returns_clear_error():
    session = read_session_source()
    start = session.index("LaunchOutcome StartFirstTimeJoin(")
    body = session[start:session.index("\n\t}", start)]
    assert in_order(body, ["StartVanillaNewGame()", "a_error = ", "return F4T::SessionLaunch::LaunchOutcome::Failed;"])
    assert "g_pendingJoinCompletion = std::move(a_onComplete);" in body[body.index("StartVanillaNewGame()"):]


def test_begin_onboarding_is_gated_on_loading_player_and_quest():
    session = read_session_source()
    gate_start = session.index("if (g_charGenPhase == CharGenPhase::StartingNewGame) {")
    gate = session[gate_start:session.index("ArmWaitDeadline(kVanillaIntroBudget);", gate_start)]
    assert "!g_newGameMessageSeen || !player || IsLoadingScreenActive()" in gate
    assert "F4T::SessionCharacterSetupFlow::BeginOnboarding()" in gate
    setup = read(SETUP_SOURCE)
    begin = setup[setup.index("bool BeginOnboarding()"):setup.index("g_onboardingIssued = true;")]
    assert "GetFormByEditorID<RE::TESQuest>" in begin
    assert "return false;" in begin


def test_continue_wait_refreshes_alert_and_has_a_deadline():
    session = read_session_source()
    wait_start = session.index("if (g_charGenPhase == CharGenPhase::WaitingForContinue) {")
    wait = session[wait_start:session.index("if (g_charGenPhase == CharGenPhase::WaitingForPodExit) {", wait_start)]
    assert "kContinueAlertRefreshInterval" in wait
    assert "ShowContinueAlert()" in wait
    assert "WaitDeadlinePassed()" in wait
    assert "FailFirstTimeJoin(\"Timed out waiting for the Continue prompt" in wait
    assert "ArmWaitDeadline(kContinueWaitBudget);" in session


def test_return_to_menu_prompt_is_shown_on_failed_start():
    session = read_session_source()
    start = session.index("LaunchOutcome StartFirstTimeJoin(")
    body = session[start:session.index("\n\t}", start)]
    assert "F4T::SessionCharacterSetupFlow::ShowReturnToMenuAlert();" in body
    setup = read(SETUP_SOURCE)
    assert "request.title = \"Could not start the new game\";" in setup
    assert "Return to the main menu to join this server for the first time." in setup


def test_plugin_launch_file_stays_within_limit():
    for path in (*SESSION_SOURCES, INTERNAL_SOURCE):
        assert len(read(path).splitlines()) <= 750, path.name


def test_legacy_modules_and_xmake_target_are_removed():
    for removed in (
        PLUGIN / "src" / "F4TSessionIntroPrime.cpp",
        PLUGIN / "include" / "F4TSessionIntroPrime.h",
        PLUGIN / "src" / "F4TCharacterSetupController.cpp",
        PLUGIN / "include" / "F4TCharacterSetupController.h",
        PLUGIN / "tests" / "F4TCharacterSetupControllerTests.cpp",
    ):
        assert not removed.exists(), removed
    xmake = read(XMAKE_SOURCE)
    assert "CommonwealthOnlineCharacterSetupControllerTests" not in xmake
    assert "F4TCharacterSetupControllerTests" not in xmake


def test_continue_alert_is_bound_to_t_key():
    setup = read(SETUP_SOURCE)
    assert 'request.title = "Character ready"' in setup
    assert '{ "continue", "Skip to the vault (T)", "T" }' in setup
    alert = read(ROOT / "CommonwealthOnlineUI" / "src" / "F4TAlertSystem.cpp")
    assert "static bool wasT = false;" in alert
    assert "{ 'T', &wasT, \"T\" }" in alert


def main() -> int:
    failures: list[str] = []
    for name, func in list(globals().items()):
        if name.startswith("test_") and callable(func):
            try:
                func()
            except AssertionError as error:
                failures.append(f"{name}: {error}")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("test_vanilla_start_onboarding: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


def test_main_menu_close_releases_browser_menu_input_block() -> None:
    source = read(ROOT / "CommonwealthOnlineUI" / "src" / "F4TPrismaUI_Lifecycle.cpp")
    body = source[source.index("void OnFalloutMainMenuClosed()"):]
    body = body[: body.index("void DispatchMainMenuControllerInput")]
    assert in_order(body, [
        "if (g_browserDesiredVisible || g_browserCurrentlyVisible) {",
        "DeactivateEmbeddedBrowser(\"FalloutMainMenuClosed\");",
        "ReconcileMainMenuOverlayState(\"FalloutMainMenuClosed\");",
    ])
    browser = read(ROOT / "CommonwealthOnlineUI" / "src" / "F4TPrismaUI_Browser.cpp")
    deactivate = browser[browser.index("void DeactivateEmbeddedBrowser("):]
    assert "F4T::MenuInput::SetBrowserActive(false);" in deactivate[: deactivate.index("void ", 10)]


def test_continue_dismisses_continue_alert() -> None:
    source = read(ROOT / "CO-BETA" / "plugin" / "src" / "F4TSessionCharacterSetupFlow.cpp")
    body = source[source.index("void ContinueToVault()"):]
    body = body[: body.index("void Abort()")]
    assert in_order(body, [
        "g_continueIssued = true;",
        "DismissContinueAlert();",
        "ContinueToVault\");",
    ])


def test_overlay_release_runs_when_another_path_hid_the_menu() -> None:
    body = read(ROOT / "CommonwealthOnlineUI" / "src" / "F4TPrismaUI_Browser.cpp")
    body = body[body.index("bool ApplyMainMenuState("):]
    body = body[: body.index("return result.applied;")]
    assert in_order(body, [
        "static bool overlayEngaged = false;",
        "if (result.showRequested) {",
        "overlayEngaged = true;",
        "if (result.hideRequested || (overlayEngaged && !g_mainMenuState.IsCurrentlyVisible())) {",
        "overlayEngaged = false;",
        "F4T::MenuInput::SetMainMenuActive(false);",
    ])


def test_vault_walk_after_pod_exit_gets_player_paced_budget() -> None:
    session = read_session_source()
    assert "constexpr auto kVaultExitBudget = std::chrono::minutes{ 30 };" in read(INTERNAL_SOURCE)
    pod = session.index("if (g_charGenPhase == CharGenPhase::WaitingForPodExit) {")
    pod = session[pod:session.index("if (g_charGenPhase == CharGenPhase::WaitingForGameplayWorldReady) {", pod)]
    assert in_order(pod, [
        "IsPodExitSignal()",
        "ArmWaitDeadline(kVaultExitBudget);",
        "CharGenPhase::WaitingForGameplayWorldReady,",
    ])
