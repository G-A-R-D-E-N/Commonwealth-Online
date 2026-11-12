import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"
VIEW = UI / "ui" / "views" / "CommonwealthOnline" / "interact-prompt"


def read_workflow(name):
    for folder in (".github/public-ci/workflows", ".github/workflows"):
        if (ROOT / folder / name).is_file():
            return (ROOT / folder / name).read_text(encoding="utf-8")
    raise FileNotFoundError(f"workflow not found: {name}")


HEADER = (UI / "include" / "F4TInteractPromptHud.h").read_text(encoding="utf-8")
BRIDGE = (UI / "src" / "F4TInteractPromptHud.cpp").read_text(encoding="utf-8")
LIFECYCLE = (UI / "src" / "F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")
JS = (VIEW / "interact-prompt.js").read_text(encoding="utf-8")
HTML = (VIEW / "index.html").read_text(encoding="utf-8")
PREVIEW = (VIEW / "preview.html").read_text(encoding="utf-8")
CI_YML = read_workflow("co-ui-ci.yml")
XMAKE = (UI.parent / "CO-BETA/plugin/xmake.lua").read_text(encoding="utf-8")
SRC_TEXT = "\n".join(
    path.read_text(encoding="utf-8")
    for path in sorted((UI / "src").glob("*.cpp")) + sorted((UI / "include").glob("*.h"))
)
ENTRY = "coInteractPromptSetState"
VIEW_ORDER = 645
HTML_PATH = "CommonwealthOnline/interact-prompt/index.html"

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
check("view order 645 is used only by the interact prompt wrapper",
      f"constexpr int kViewOrder = {VIEW_ORDER};" in BRIDGE and len(re.findall(rf"=\s*{VIEW_ORDER}\s*;", SRC_TEXT)) == 1)
check("state is validated as JSON before it is pushed",
      "nlohmann::json::parse(payload, nullptr, false)" in BRIDGE and "state.is_object()" in BRIDGE)
check("state reaches the page as a JSON string literal, never raw script text",
      "F4T::ViewJson::Dump(nlohmann::json(g_lastJson))" in BRIDGE)
check("size guard runs before the payload is copied",
      "strnlen(a_json, kMaxJsonBytes + 1) > kMaxJsonBytes" in BRIDGE)
check("prompt hides while the wheel view or the trade window is visible",
      "F4T::InteractWheelView::IsCurrentlyVisible()" in BRIDGE and "F4T::TradeView::IsCurrentlyVisible()" in BRIDGE)
check("lifecycle includes the wrapper header",
      '#include "F4TInteractPromptHud.h"' in LIFECYCLE)
check("lifecycle initializes the interact prompt once, after the capture HUD",
      LIFECYCLE.count("F4T::InteractPromptHud::Initialize();") == 1
      and LIFECYCLE.index("F4T::CaptureHud::Initialize();") < LIFECYCLE.index("F4T::InteractPromptHud::Initialize();"))
check("lifecycle ticks the interact prompt once, after the capture HUD",
      LIFECYCLE.count("F4T::InteractPromptHud::Tick();") == 1
      and LIFECYCLE.index("F4T::CaptureHud::Tick();") < LIFECYCLE.index("F4T::InteractPromptHud::Tick();"))
check("the plugin target globs the UI src/**.cpp so the wrapper is compiled",
      'add_files(path.join(ui_dir, "src/**.cpp"))' in XMAKE)
check("wrapper and lifecycle stay under the 1000-line limit",
      len((UI / "src" / "F4TInteractPromptHud.cpp").read_text(encoding="utf-8").splitlines()) <= 1000
      and len(LIFECYCLE.splitlines()) <= 1000)
check("script reads the plugin field names visible and name",
      "state.visible === true" in JS and 'typeof state.name === "string"' in JS)
check("script never assigns markup (innerHTML)", "innerHTML" not in JS)
looked = set(re.findall(r'getElementById\("(\w+)"\)', JS))
check("index.html carries every element id the script looks up",
      looked and all(f'id="{name}"' in HTML for name in looked))
check("index.html carries the ACTIVATE key cap",
      'id="coInteractKey">ACTIVATE</span>' in HTML)
check("index.html loads the theme, the stylesheet and the script",
      'href="../pipboy-theme.css"' in HTML and 'href="styles.css"' in HTML and '<script src="interact-prompt.js"></script>' in HTML)
check("prompt hides while the Pip-Boy is open so it cannot cover the page",
      "!F4T::PrismaUI::IsPipboyOpen()" in BRIDGE and "bool IsPipboyOpen();" in (UI / "include" / "F4TPrismaUI.h").read_text(encoding="utf-8"))
check("preview.html exists and pushes through the entry point",
      f"window.{ENTRY}" in PREVIEW and '<script src="interact-prompt.js"></script>' in PREVIEW)
check("co-ui-ci node --check covers interact-prompt.js",
      "node --check CommonwealthOnlineUI/ui/views/CommonwealthOnline/interact-prompt/interact-prompt.js" in CI_YML)
check("co-ui-ci runs interact-prompt.test.js",
      "node CommonwealthOnlineUI/ui/views/CommonwealthOnline/interact-prompt/interact-prompt.test.js" in CI_YML)

if failures:
    print(f"interact prompt contract FAILED: {len(failures)} check(s)")
    sys.exit(1)
print("interact prompt contract passed: header, Prisma view path and entry point, JSON validation, lifecycle init and tick, view order, CI lists")
