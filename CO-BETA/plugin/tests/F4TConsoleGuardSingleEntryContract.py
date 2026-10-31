from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN_SRC = ROOT / "CO-BETA/plugin/src"
UI_SRC = ROOT / "CommonwealthOnlineUI/src"
GUARD = PLUGIN_SRC / "F4TConsoleGuard.cpp"
SETUP_FLOW = PLUGIN_SRC / "F4TSessionCharacterSetupFlow.cpp"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def strip_line_comments(text):
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def direct_console_calls(path):
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    return [
        (number, line)
        for number, line in enumerate(strip_line_comments(text).splitlines(), 1)
        if "RE::Console::ExecuteCommand" in line
    ]


check(GUARD.exists(), f"missing {GUARD}")

for directory in (PLUGIN_SRC, UI_SRC):
    if not directory.exists():
        check(False, f"missing source dir {directory}")
        continue
    for path in sorted(directory.glob("*.cpp")):
        if path == GUARD:
            continue
        for number, line in direct_console_calls(path):
            check(False, f"{path.name}:{number} calls RE::Console::ExecuteCommand directly; route it through F4TConsoleGuard.cpp:ExecutePluginCommand")

guard_text = strip_line_comments(GUARD.read_text(encoding="utf-8")) if GUARD.exists() else ""
allowed = [line for line in guard_text.splitlines() if "RE::Console::ExecuteCommand" in line]
check(len(allowed) == 1, f"F4TConsoleGuard.cpp must contain exactly one RE::Console::ExecuteCommand (the ExecutePluginCommand body), found {len(allowed)}")
execute_body_start = guard_text.find("void ExecutePluginCommand(")
check(execute_body_start >= 0, "F4TConsoleGuard.cpp must define ExecutePluginCommand")
if execute_body_start >= 0:
    brace = guard_text.find("{", execute_body_start)
    depth = 0
    body = None
    for index in range(brace, len(guard_text)):
        if guard_text[index] == "{":
            depth += 1
        elif guard_text[index] == "}":
            depth -= 1
            if depth == 0:
                body = guard_text[brace + 1:index]
                break
    check(body is not None and "RE::Console::ExecuteCommand(a_command);" in body,
          "the single RE::Console::ExecuteCommand must live inside ExecutePluginCommand")

flow_text = strip_line_comments(SETUP_FLOW.read_text(encoding="utf-8")) if SETUP_FLOW.exists() else ""
check('ExecutePluginCommand("cqf CO_CharacterSetupQuest Prime")' in flow_text,
      "EnsureRunning must prime the setup quest through ExecutePluginCommand")
check('ExecutePluginCommand("startquest CO_CharacterSetupQuest")' in flow_text,
      "EnsureRunning must start the setup quest through ExecutePluginCommand")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    print("F4TConsoleGuardSingleEntryContract: plugin console entry is not single.")
    sys.exit(1)

print("F4TConsoleGuardSingleEntryContract: all checks passed.")
