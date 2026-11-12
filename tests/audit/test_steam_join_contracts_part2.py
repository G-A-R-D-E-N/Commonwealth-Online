#!/usr/bin/env python3
"""test_steam_join_contracts_part2: consolidated contract sections.

Consolidated from 2 single-contract scripts so the group runs one process
instead of 2. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_steam_join_contracts_part2.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


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




def _section_007_test_host_autostarts_server():
    """Contract moved verbatim from tests/audit/test_host_autostarts_server.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_host_autostarts_server.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def test_host_start_launches_local_server_before_steam():
        session = read("CO-BETA/plugin/src/F4TSteamSession.cpp")
        host = session[session.index("void OnHostStart"):session.index("void OnHostStop")]
        ensure = host.index("F4T::LocalServer::EnsureRunning(")
        assert ensure < host.index("F4T::Steam::Initialize()")
        assert ensure < host.index("BeginLocalJoin(")
        assert 'PushFailed("steamHostFailed", serverError)' in host


    def test_launcher_starts_packaged_server_with_its_config():
        launcher = read("CO-BETA/plugin/src/F4TLocalServerLauncher.cpp")
        assert 'L"CommonwealthOnline.Server.exe"' in launcher
        assert 'L"commonwealth-server.json"' in launcher
        assert '--config' in launcher and '--port' in launcher
        assert 'CREATE_NEW_CONSOLE' in launcher
        assert launcher.index("if (PortInUse(a_port))") < launcher.index("CreateProcessW(")
        assert "WaitForSingleObject(process.hProcess, 0)" in launcher


    def test_no_manual_server_start_instructions():
        for path in (
            "CO-BETA/plugin/src/F4TSteamSession.cpp",
            "CO-BETA/plugin/src/F4TNetworking.cpp",
            "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/steam-friends.js",
        ):
            text = read(path)
            assert "Start CommonwealthOnline.Server.exe first" not in text, path
            assert "Run CommonwealthOnline.Server.exe on this PC" not in text, path


    def test_launch_is_serialized():
        launcher = read("CO-BETA/plugin/src/F4TLocalServerLauncher.cpp")
        body = launcher[launcher.index("static std::string LaunchServer("):]
        assert body.index("std::scoped_lock lock(g_launchMutex)") < body.index("PortInUse(a_port)")


    def test_server_dies_with_the_game():
        launcher = read("CO-BETA/plugin/src/F4TLocalServerLauncher.cpp")
        assert "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE" in launcher
        assert "CREATE_NEW_CONSOLE) | CREATE_SUSPENDED" in launcher
        body = launcher[launcher.index("static std::string LaunchServer("):]
        assert body.index("AssignProcessToJobObject(") < body.index("ResumeThread(process.hThread)")
        assert "CloseHandle(job" not in launcher
        assert "if (!SetInformationJobObject(" in launcher
        assert "ResumeThread(process.hThread) == static_cast<DWORD>(-1)" in launcher


    def test_restarted_server_that_never_listens_is_terminated():
        launcher = read("CO-BETA/plugin/src/F4TLocalServerLauncher.cpp")
        body = launcher[launcher.index("static std::string LaunchServer("):]
        failure = body[body.index("if (!error.empty()) {"):]
        assert failure.index("if (a_restarts > 0) {") < failure.index("TerminateProcess(process.hProcess, 1);")
        assert failure.index("TerminateProcess(process.hProcess, 1);") < failure.index("WaitForSingleObject(process.hProcess, 5000);")
        assert failure.index("WaitForSingleObject(process.hProcess, 5000);") < failure.index("CloseHandle(process.hProcess);")


    def main() -> int:
        failures: list[str] = []
        tests = [(name, value) for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for name, func in tests:
            try:
                func()
            except AssertionError as exc:
                failures.append(f"{name}: {exc}")
            except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
                failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print(f"{Path(__file__).stem}: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_008_test_onboarding_resume_bind():
    """Contract moved verbatim from tests/audit/test_onboarding_resume_bind.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_onboarding_resume_bind.py")
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


    def test_post_save_never_binds_a_pending_first_join_outside_the_checked_path():
        source = read_session_source()

        helper_start = source.index("bool IsPastPodExit()")
        helper = source[helper_start:source.index("bool IsFirstJoinInProgress()", helper_start)]
        assert "CharGenPhase::WaitingForGameplayWorldReady" in helper
        assert "CharGenPhase::WaitingForProfileBind" in helper
        assert "VanillaIntro" not in helper, "an intro save must never bind the server profile"

        post_start = source.index("case F4SE::MessagingInterface::kPostSaveGame:")
        post = source[post_start:source.index("case F4SE::MessagingInterface::kPostLoadGame:", post_start)]
        complete_index = post.index("TryCompletePendingProfileBind(")
        pending_stop_index = post.index("if (g_pendingProfileBind) break;")
        assert complete_index < pending_stop_index, (
            "the first-join bind must run first so its own quicksave still completes the join"
        )
        assert pending_stop_index < post.index("TryUpdateActiveProfileBinding(activeServerKey,"), (
            "a pending first-join must stop before the active-profile update"
        )
        assert "TryUpdateActiveProfileBinding(g_pendingProfileBindServerKey" not in post, (
            "a pending first-join must never be bound from the post-save message by any other route"
        )
        assert post.count("ResolveBindablePostSaveName(savedName)") == 1, (
            "the bindable save is resolved once per post-save message"
        )

        saves = read_session_file("F4TSessionLaunchSaves.cpp")
        complete_start = saves.index("void TryCompletePendingProfileBind(")
        complete = saves[complete_start:]
        assert complete.index("BindableCharacterId(*a_savedName)") < complete.index(
            "ServerProfileStore::BindSaveFileClearingHold("
        ), "the first-join bind must refuse a save with no character id before it is persisted"
        assert 'value_or("")' not in complete, "an empty character id must never be bound"


    if True:
        test_post_save_never_binds_a_pending_first_join_outside_the_checked_path()
        print("onboarding resume bind contract passed")


_SECTIONS = [
    ("tests/audit/test_host_autostarts_server.py", _section_007_test_host_autostarts_server),
    ("tests/audit/test_onboarding_resume_bind.py", _section_008_test_onboarding_resume_bind),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_steam_join_contracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
