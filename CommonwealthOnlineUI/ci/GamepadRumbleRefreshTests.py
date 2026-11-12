import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"

SETTINGS_APPLY = (UI / "src" / "F4TSettingsApplyHandlers.cpp").read_text(encoding="utf-8")

QUEUE_BODY = re.compile(r"bool QueueGamepadEnableRefresh\(bool a_enable\)\s*\{(.*?)\n\t\t\}", re.S)
SETTER_CALL = re.compile(r"\bsetter\(enable && rumbleOn\);")
MANAGER_NULL = re.compile(r"if \(!\*manager\) \{")
EARLY_RETURN = re.compile(r"\breturn;")

failures = []


def check(label, condition):
    print(f"PASS {label}" if condition else f"FAIL {label}")
    if not condition:
        failures.append(label)


body = QUEUE_BODY.search(SETTINGS_APPLY)
text = body.group(1) if body else ""
setter = SETTER_CALL.search(text)
manager = MANAGER_NULL.search(text)
check("QueueGamepadEnableRefresh exists", body is not None)
check("setter is called with enable && rumbleOn", setter is not None)
check("setter(enable && rumbleOn) precedes the manager null check",
      setter is not None and manager is not None and setter.start() < manager.start())
check("no return; precedes the setter call",
      setter is not None and EARLY_RETURN.search(text[:setter.start()]) is None)

if failures:
    print(f"gamepad rumble refresh contract FAILED: {len(failures)} check(s)")
    sys.exit(1)
print("gamepad rumble refresh contract passed: setter(enable && rumbleOn) runs before the manager null check with no early return")
