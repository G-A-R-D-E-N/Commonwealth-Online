from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SESSION_SOURCES = [
    ROOT / "CO-BETA" / "plugin" / "src" / name
    for name in (
        "F4TSessionLaunchSaves.cpp",
        "F4TSessionLaunchState.cpp",
        "F4TSessionLaunchWorld.cpp",
        "F4TSessionLaunchOnboarding.cpp",
        "F4TSessionLaunch.cpp",
    )
]


def read_session_file(name: str) -> str:
    return (ROOT / "CO-BETA" / "plugin" / "src" / name).read_text(encoding="utf-8")


def read_session_source() -> str:
    return "".join(path.read_text(encoding="utf-8") for path in SESSION_SOURCES)


def test_onboarding_save_after_pod_exit_binds_the_pending_server_profile():
    source = read_session_source()

    helper_start = source.index("bool IsPastPodExit()")
    helper = source[helper_start:source.index("bool IsFirstJoinInProgress()", helper_start)]
    assert "CharGenPhase::WaitingForGameplayWorldReady" in helper
    assert "CharGenPhase::WaitingForProfileBind" in helper
    assert "VanillaIntro" not in helper, "an intro save must never bind the server profile"

    post_start = source.index("case F4SE::MessagingInterface::kPostSaveGame:")
    post = source[post_start:source.index("case F4SE::MessagingInterface::kPostLoadGame:", post_start)]
    onboarding_index = post.index("onboardingSave && g_pendingProfileBind && IsPastPodExit()")
    assert post.index("TryCompletePendingProfileBind(") < onboarding_index, (
        "the first-join bind must run first so its own quicksave still completes the join"
    )
    onboarding = post[onboarding_index:post.index("case F4SE::MessagingInterface::kNewGame:", onboarding_index)]
    assert "const auto onboardingCharacterId = BindableCharacterId(*onboardingSave);" in onboarding, (
        "the onboarding bind must resolve the character id it is about to bind"
    )
    assert onboarding.index("if (!onboardingCharacterId)") < onboarding.index(
        "TryUpdateActiveProfileBinding(g_pendingProfileBindServerKey,"
    ), "a save with no character id must be refused instead of bound with an empty id"
    assert "TryUpdateActiveProfileBinding(g_pendingProfileBindServerKey," in onboarding, (
        "a save made after the vault exit (the quit save) must bind and attest through the "
        "connected lineage path so a rejoin resumes it instead of restarting the intro"
    )
    assert ", false, onboardingSave, savedName);" in onboarding
    assert post.count("ResolveBindablePostSaveName(savedName)") == 1, (
        "the bindable save is resolved once per post-save message"
    )

    saves = read_session_file("F4TSessionLaunchSaves.cpp")
    complete_start = saves.index("void TryCompletePendingProfileBind(")
    complete = saves[complete_start:]
    assert complete.index("BindableCharacterId(*a_savedName)") < complete.index(
        "ServerProfileStore::BindSaveFile("
    ), "the first-join bind must refuse a save with no character id before it is persisted"
    assert 'value_or("")' not in complete, "an empty character id must never be bound"


if __name__ == "__main__":
    test_onboarding_save_after_pod_exit_binds_the_pending_server_profile()
    print("onboarding resume bind contract passed")
