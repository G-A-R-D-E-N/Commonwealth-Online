import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"

REGISTRY = (UI / "src" / "F4TSettingsRegistry.cpp").read_text(encoding="utf-8")
SERVICE = (UI / "src" / "F4TSettingsService.cpp").read_text(encoding="utf-8")
BRIDGE = (UI / "src" / "F4TSettingsBridge.cpp").read_text(encoding="utf-8")
MODEL = (UI / "ui" / "views" / "CommonwealthOnline" / "settings" / "settings-model.js").read_text(encoding="utf-8")

DIFFICULTY_BLOCK = re.compile(r'\.id = "gameplay\.difficulty",(.*?)\}\);', re.S)
REFRESH_BODY = re.compile(r"void SettingsService::RefreshFromEngine\(\)\s*\{(.*?)\n\t\}", re.S)
SET_BODY = re.compile(r"SettingResult SettingsService::SetSetting\(.*?\n\t\}", re.S)
UNAVAILABLE_GUARD = re.compile(
    r'if \(!IsSettingWritable\(s_states, definition->id\)\) \{\s*'
    r'return Fail\("SETTING_UNAVAILABLE"'
)
HIDDEN_SKIP = re.compile(r"if \(!definition\.showInUI\) \{\s*continue;\s*\}")
MODEL_SKIP = re.compile(r"if \(setting\.showInUI === false\) \{\s*return;\s*\}")

failures = []


def check(label, condition):
    print(f"PASS {label}" if condition else f"FAIL {label}")
    if not condition:
        failures.append(label)


difficulty = DIFFICULTY_BLOCK.search(REGISTRY)
block = difficulty.group(1) if difficulty else ""
check("registry defines gameplay.difficulty", difficulty is not None)
check("gameplay.difficulty sets .showInUI = false", block.count(".showInUI = false,") == 1)
check("gameplay.difficulty does not set showInUI true", ".showInUI = true" not in block)
check("gameplay.difficulty keeps the 0-4 range without Survival option",
      ".maximum = 4," in block and 'Opt(4, "Very Hard")' in block and 'Opt(5,' not in block)

refresh = REFRESH_BODY.search(SERVICE)
check("RefreshFromEngine exists", refresh is not None)
check("RefreshFromEngine skips showInUI false definitions (no s_states entry)",
      refresh is not None and HIDDEN_SKIP.search(refresh.group(1)) is not None)

set_body = SET_BODY.search(SERVICE)
check("SetSetting exists", set_body is not None)
check("SetSetting refuses a definition with no s_states entry as SETTING_UNAVAILABLE",
      set_body is not None and UNAVAILABLE_GUARD.search(set_body.group(0)) is not None)
check("SETTING_UNAVAILABLE is returned only from the s_states guard",
      SERVICE.count('"SETTING_UNAVAILABLE"') == 1)
check("SettingsService serialises showInUI into the DTO",
      '{ "showInUI", a_definition.showInUI }' in SERVICE)

check("settings bridge skips showInUI false definitions", HIDDEN_SKIP.search(BRIDGE) is not None)
check("settings-model.js drops showInUI false rows before rendering",
      MODEL_SKIP.search(MODEL) is not None)

if failures:
    print(f"settings difficulty hidden contract FAILED: {len(failures)} check(s)")
    sys.exit(1)
print("settings difficulty hidden contract passed: registry showInUI=false, s_states guard, SETTING_UNAVAILABLE branch, UI and bridge skips")
