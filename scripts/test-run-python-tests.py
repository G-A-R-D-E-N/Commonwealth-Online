import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
runner = root / "scripts/run-python-tests.py"
excluded = (
    "F4TJoinCancellationTests.py",
    "F4TConnectionLifecycleTests.py",
    "F4TReceiveNotificationTests.py",
    "F4TReceiveThreadOwnershipTests.py",
    "F4TSendCleanupRaceTests.py",
    "F4TReconnectWindowTests.py",
)
args = [sys.executable, str(runner), "--group", "plugin", "--compile-only", "--list"]
for name in excluded:
    args.extend(("--exclude", f"CO-BETA/plugin/tests/{name}"))
assert subprocess.run(args, cwd=root, capture_output=True, text=True).returncode == 0
result = subprocess.run(args + ["--exclude", "CO-BETA/plugin/tests/missing.py"], cwd=root, capture_output=True, text=True)
assert result.returncode != 0 and "matched no test file" in result.stdout
print("Python test selection: PASS")

client_tests = [
    root / "CO-BETA/plugin/tests" / name for name in excluded
] + [
    root / "CommonwealthOnlineUI/ci/MenuInputAxisTests.py",
    root / "CommonwealthOnlineUI/ci/MenuStartupTests.py",
    root / "CommonwealthOnlineUI/ci/ForegroundFocusTests.py",
    *sorted((root / "tests/audit").glob("test_browser_controller_*.py")),
]
for path in client_tests:
    result = subprocess.run(
        [sys.executable, "-c", "import runpy, sys; sys.platform = 'linux'; runpy.run_path(sys.argv[1], run_name='__main__')", str(path)],
        cwd=root, capture_output=True, text=True,
    )
    assert result.returncode != 0 and "Game client tests can only run on a Windows host" in result.stderr, path
print("Client test host guards: PASS")

result = subprocess.run([sys.executable, str(runner), "--group", "ui", "--compile-only", "--list"],
                        cwd=root, capture_output=True, text=True, check=True)
assert "CommonwealthOnlineUI/ci/ForegroundFocusTests.py" in result.stdout
