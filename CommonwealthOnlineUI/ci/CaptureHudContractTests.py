import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"
VIEW = UI / "ui" / "views" / "CommonwealthOnline" / "capture-hud"
HEADER = (UI / "include" / "F4TCaptureHud.h").read_text(encoding="utf-8")
BRIDGE = (UI / "src" / "F4TCaptureHud.cpp").read_text(encoding="utf-8")
LIFECYCLE = (UI / "src" / "F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")
JS = (VIEW / "capture-hud.js").read_text(encoding="utf-8")
HTML = (VIEW / "index.html").read_text(encoding="utf-8")
PREVIEW = (VIEW / "preview.html").read_text(encoding="utf-8")
CI_YML = (ROOT / ".github" / "workflows" / "co-ui-ci.yml").read_text(encoding="utf-8")
WIN_CI = (ROOT / "scripts" / "windows-compile-ci.py").read_text(encoding="utf-8")
XMAKE = (UI / "xmake.lua").read_text(encoding="utf-8")
XMAKE_CI = (UI / "xmake-ci.lua").read_text(encoding="utf-8")
SRC_TEXT = "\n".join(
    path.read_text(encoding="utf-8")
    for path in sorted((UI / "src").glob("*.cpp")) + sorted((UI / "include").glob("*.h"))
)
ENTRY = "coCaptureSetState"
VIEW_ORDER = 525
HTML_PATH = "CommonwealthOnline/capture-hud/index.html"

failures = []


def check(label, condition):
    print(f"PASS {label}" if condition else f"FAIL {label}")
    if not condition:
        failures.append(label)


check("header declares Initialize, PushStateJson and Tick",
      all(line in HEADER for line in ("void Initialize();", "void PushStateJson(const char* a_json);", "void Tick();")))
check("wrapper names the view html path that exists on disk",
      f'constexpr auto kHtmlPath = "{HTML_PATH}";' in BRIDGE and (VIEW / "index.html").is_file())
check("wrapper and script share the entry point name",
      f'constexpr auto kEntryPoint = "{ENTRY}";' in BRIDGE and f"window.{ENTRY} = function" in JS)
check("view order 525 is used only by the capture HUD wrapper",
      f"constexpr int kViewOrder = {VIEW_ORDER};" in BRIDGE and len(re.findall(rf"=\s*{VIEW_ORDER}\s*;", SRC_TEXT)) == 1)
check("state is validated as JSON before it is pushed",
      "nlohmann::json::accept(payload)" in BRIDGE)
check("state reaches the page as a JSON string literal, never raw script text",
      "nlohmann::json(g_lastJson).dump()" in BRIDGE)
check("size guard runs before the payload is copied",
      "strnlen(a_json, kMaxJsonBytes + 1) > kMaxJsonBytes" in BRIDGE)
check("lifecycle includes the wrapper header",
      '#include "F4TCaptureHud.h"' in LIFECYCLE)
check("lifecycle initializes the capture HUD once, after the voice bridge",
      LIFECYCLE.count("F4T::CaptureHud::Initialize();") == 1
      and LIFECYCLE.index("F4T::VoiceBridge::Initialize();") < LIFECYCLE.index("F4T::CaptureHud::Initialize();"))
check("lifecycle ticks the capture HUD once, after the voice bridge",
      LIFECYCLE.count("F4T::CaptureHud::Tick();") == 1
      and LIFECYCLE.index("F4T::VoiceBridge::Tick();") < LIFECYCLE.index("F4T::CaptureHud::Tick();"))
check("shipping and compile-check targets glob src/**.cpp so the wrapper is compiled",
      'add_files("src/**.cpp")' in XMAKE and 'add_files("src/**.cpp")' in XMAKE_CI)
check("wrapper and lifecycle stay under the 750-line limit",
      len((UI / "src" / "F4TCaptureHud.cpp").read_text(encoding="utf-8").splitlines()) <= 750
      and len(LIFECYCLE.splitlines()) <= 750)
check("script takes the plugin field names nearBuildZoneId and buildZoneId",
      'plugin.nearBuildZoneId' in JS and 'settlement.buildZoneId' in JS)
check("script does not use the retired workbenchRefId names",
      "workbenchRefId" not in JS and "nearWorkbenchRefId" not in JS)
check("script never assigns markup (innerHTML)", "innerHTML" not in JS)
looked = set(re.findall(r'getElementById\("(\w+)"\)', JS))
check("index.html carries every element id the script looks up",
      looked and all(f'id="{name}"' in HTML for name in looked))
check("index.html loads the script and stylesheet",
      '<script src="capture-hud.js"></script>' in HTML and 'href="styles.css"' in HTML)
check("preview.html exists and pushes through the entry point",
      f"window.{ENTRY}" in PREVIEW and '<script src="capture-hud.js"></script>' in PREVIEW)
check("co-ui-ci node --check covers capture-hud.js",
      "node --check CommonwealthOnlineUI/ui/views/CommonwealthOnline/capture-hud/capture-hud.js" in CI_YML)
check("co-ui-ci runs capture-hud.test.js",
      "node CommonwealthOnlineUI/ui/views/CommonwealthOnline/capture-hud/capture-hud.test.js" in CI_YML)
check("windows-compile-ci checks the script, runs its test and this contract",
      '"capture-hud/capture-hud.js"' in WIN_CI
      and '"capture-hud/capture-hud.test.js"' in WIN_CI
      and 'ci/CaptureHudContractTests.py' in WIN_CI)

if failures:
    print(f"capture HUD contract FAILED: {len(failures)} check(s)")
    sys.exit(1)
print("capture HUD contract passed: header, Prisma view path and entry point, JSON validation, lifecycle init and tick, view order, CI lists")
