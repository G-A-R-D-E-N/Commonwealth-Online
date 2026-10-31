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
    body = launcher[launcher.index("std::string EnsureRunning("):]
    assert body.index("std::scoped_lock lock(g_launchMutex)") < body.index("PortInUse(a_port)")


def test_server_dies_with_the_game():
    launcher = read("CO-BETA/plugin/src/F4TLocalServerLauncher.cpp")
    assert "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE" in launcher
    assert "CREATE_NEW_CONSOLE) | CREATE_SUSPENDED" in launcher
    body = launcher[launcher.index("std::string EnsureRunning("):]
    assert body.index("AssignProcessToJobObject(") < body.index("ResumeThread(process.hThread)")
    assert "CloseHandle(job" not in launcher
    assert "if (!SetInformationJobObject(" in launcher
    assert "ResumeThread(process.hThread) == static_cast<DWORD>(-1)" in launcher


def main() -> int:
    failures: list[str] = []
    tests = [(name, value) for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
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


if __name__ == "__main__":
    raise SystemExit(main())
