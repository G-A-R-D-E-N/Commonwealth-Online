import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"
PLUGIN = ROOT / "CO-BETA" / "plugin"
VIEW = UI / "ui" / "views" / "CommonwealthOnline" / "structure-hud"


def read_workflow(name):
    for folder in (".github/public-ci/workflows", ".github/workflows"):
        if (ROOT / folder / name).is_file():
            return (ROOT / folder / name).read_text(encoding="utf-8")
    raise FileNotFoundError(f"workflow not found: {name}")


HEADER = (UI / "include" / "F4TStructureHud.h").read_text(encoding="utf-8")
BRIDGE = (UI / "src" / "F4TStructureHud.cpp").read_text(encoding="utf-8")
LIFECYCLE = (UI / "src" / "F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")
PLUGIN_API = (UI / "src" / "PluginAPI.cpp").read_text(encoding="utf-8")
UI_API = (UI / "include" / "CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
JS = (VIEW / "structure-hud.js").read_text(encoding="utf-8")
HTML = (VIEW / "index.html").read_text(encoding="utf-8")
PREVIEW = (VIEW / "preview.html").read_text(encoding="utf-8")
CI_YML = read_workflow("co-ui-ci.yml")
XMAKE = (PLUGIN / "xmake.lua").read_text(encoding="utf-8")
PLUGIN_HUD = (PLUGIN / "src" / "F4TStructureDamageHud.cpp").read_text(encoding="utf-8")
REPLICATION = (PLUGIN / "src" / "F4TWorkshopReplicationClient.cpp").read_text(encoding="utf-8")
SRC_TEXT = "\n".join(
    path.read_text(encoding="utf-8")
    for path in sorted((UI / "src").glob("*.cpp")) + sorted((UI / "include").glob("*.h"))
)
ENTRY = "coStructureSetState"
VIEW_ORDER = 526
HTML_PATH = "CommonwealthOnline/structure-hud/index.html"

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
check("view order 526 is used only by the structure HUD wrapper",
      f"constexpr int kViewOrder = {VIEW_ORDER};" in BRIDGE and len(re.findall(rf"=\s*{VIEW_ORDER}\s*;", SRC_TEXT)) == 1)
check("state is capped at 1 KiB and the guard runs before the payload is copied",
      "constexpr std::size_t kMaxJsonBytes = 1024;" in BRIDGE
      and "strnlen(a_json, kMaxJsonBytes + 1) > kMaxJsonBytes" in BRIDGE)
check("state is validated as JSON before it is pushed",
      "nlohmann::json::accept(payload)" in BRIDGE)
check("state reaches the page as a JSON string literal, never raw script text",
      "F4T::ViewJson::Dump(nlohmann::json(g_lastJson))" in BRIDGE)
check("lifecycle includes the wrapper header",
      '#include "F4TStructureHud.h"' in LIFECYCLE)
check("lifecycle initializes the structure HUD once, after the capture HUD",
      LIFECYCLE.count("F4T::StructureHud::Initialize();") == 1
      and LIFECYCLE.index("F4T::CaptureHud::Initialize();") < LIFECYCLE.index("F4T::StructureHud::Initialize();"))
check("lifecycle ticks the structure HUD once, after the capture HUD",
      LIFECYCLE.count("F4T::StructureHud::Tick();") == 1
      and LIFECYCLE.index("F4T::CaptureHud::Tick();") < LIFECYCLE.index("F4T::StructureHud::Tick();"))
check("UI API declares V23 with its own struct, map and layout",
      "V23 = 23" in UI_API
      and "struct IVCommonwealthOnlineUI23" in UI_API
      and "void (*PushStructureHud)(const char* a_stateJson){ nullptr };" in UI_API
      and "static constexpr std::uint32_t    layout = 0x4B8E3D17;" in UI_API)
check("PluginAPI routes V23 to the structure HUD push",
      "void Api_PushStructureHud(const char* a_stateJson)" in PLUGIN_API
      and "F4T::StructureHud::PushStateJson(a_stateJson);" in PLUGIN_API
      and "IVCommonwealthOnlineUI23 g_interfaceV23{ Api_PushStructureHud };" in PLUGIN_API
      and "case InterfaceVersion::V23:\n\t\t\treturn a_visit(g_interfaceV23);" in PLUGIN_API)
check("the plugin target globs the UI src/**.cpp so the wrapper is compiled",
      'add_files(path.join(ui_dir, "src/**.cpp"))' in XMAKE)
check("wrapper and lifecycle stay under the 1000-line limit",
      len((UI / "src" / "F4TStructureHud.cpp").read_text(encoding="utf-8").splitlines()) < 1000
      and len(LIFECYCLE.splitlines()) < 1000)
check("script paints the health payload through describeHealth",
      "describeHealth" in JS and "window.coStructureHudModel" in JS)
check("script never assigns markup (innerHTML)", "innerHTML" not in JS)
looked = set(re.findall(r'getElementById\("(\w+)"\)', JS))
check("index.html carries every element id the script looks up",
      looked and all(f'id="{name}"' in HTML for name in looked))
check("index.html loads the script and stylesheet",
      '<script src="structure-hud.js"></script>' in HTML and 'href="styles.css"' in HTML)
check("preview.html exists and pushes through the entry point",
      f"window.{ENTRY}" in PREVIEW and '<script src="structure-hud.js"></script>' in PREVIEW)
check("co-ui-ci node --check covers structure-hud.js",
      "node --check CommonwealthOnlineUI/ui/views/CommonwealthOnline/structure-hud/structure-hud.js" in CI_YML)
check("co-ui-ci runs structure-hud.test.js",
      "node CommonwealthOnlineUI/ui/views/CommonwealthOnline/structure-hud/structure-hud.test.js" in CI_YML)
check("co-ui-ci runs this contract through the UI contract group",
      "scripts/run-python-tests.py --group ui" in CI_YML)
check("plugin HUD push uses the V23 interface and the task queue",
      "RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI23>()" in PLUGIN_HUD
      and "F4SE::GetTaskInterface()" in PLUGIN_HUD
      and "api->PushStructureHud(text.c_str());" in PLUGIN_HUD)
handler = REPLICATION.split("bool HandleStructureDamage(const Json& a_packet)", 1)[1].split("\n\t\t}\n", 1)[0]
check("HandleStructureDamage pushes the HUD before it takes the replication mutex",
      "PushStructureDamageHud(a_packet);" in handler
      and handler.index("PushStructureDamageHud(a_packet);") < handler.index("std::scoped_lock lock(g_mutex);"))
check("workshop replication client stays under the 1000-line limit",
      len(REPLICATION.splitlines()) < 1000)

check("structure HUD hides while the Pip-Boy is open and shows again when it closes",
      "const auto wanted = !F4T::PrismaUI::IsPipboyOpen();" in BRIDGE
      and 'g_hudState.ApplyState(wanted ? "HudShow" : "HudHide");' in BRIDGE)

if failures:
    print(f"structure HUD contract FAILED: {len(failures)} check(s)")
    sys.exit(1)
print("structure HUD contract passed: header, Prisma view path and entry point, 1 KiB guard, lifecycle init and tick, V23 routing, plugin push ordering, CI lists")
