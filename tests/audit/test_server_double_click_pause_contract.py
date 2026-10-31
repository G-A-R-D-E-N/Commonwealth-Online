from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

REQUIRED_CONDITIONS = (
    "GetConsoleProcessList(attachedProcesses, 2) == 1",
    "GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &consoleMode)",
    "GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR",
)


def main() -> int:
    failures: list[str] = []
    source = (ROOT / "CO-SERVER/server-cpp/src/ServerMain.cpp").read_text(encoding="utf-8")
    guard = re.search(r"if \(([^{]*?)\) \{\s*std::cout << \"Press Enter to close", source)
    if guard is None:
        failures.append("DOUBLE-CLICK-PAUSE: the Press Enter prompt must be guarded by its own if block")
    else:
        for condition in REQUIRED_CONDITIONS:
            if condition not in guard.group(1):
                failures.append(f"DOUBLE-CLICK-PAUSE: the prompt guard must include: {condition}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("Server double-click pause contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
