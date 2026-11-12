import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"
VIEW = UI / "ui" / "views" / "CommonwealthOnline" / "capture-hud"
def read_workflow(name):
    for folder in (".github/public-ci/workflows", ".github/workflows"):
        if (ROOT / folder / name).is_file():
            return (ROOT / folder / name).read_text(encoding="utf-8")
    raise FileNotFoundError(f"workflow not found: {name}")


HEADER = (UI / "include" / "F4TCaptureHud.h").read_text(encoding="utf-8")
BRIDGE = (UI / "src" / "F4TCaptureHud.cpp").read_text(encoding="utf-8")
LIFECYCLE = (UI / "src" / "F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8")
JS = (VIEW / "capture-hud.js").read_text(encoding="utf-8")
HTML = (VIEW / "index.html").read_text(encoding="utf-8")
PREVIEW = (VIEW / "preview.html").read_text(encoding="utf-8")
CAPTURE_CPP = "\n".join(
    (ROOT / "CO-BETA/plugin/src" / name).read_text(encoding="utf-8")
    for name in ("F4TSettlementCaptureClient.cpp", "F4TSettlementCaptureState.cpp")
)
RAID_CPP = (ROOT / "CO-BETA/plugin/src/F4TSettlementRaidClient.cpp").read_text(encoding="utf-8")
CORE_PROTOCOL = (ROOT / "co-core/src/protocol/F4TWorkshopRaidProtocol.cpp").read_text(encoding="utf-8")
CI_YML = read_workflow("co-ui-ci.yml")
XMAKE = (UI.parent / "CO-BETA/plugin/xmake.lua").read_text(encoding="utf-8")
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


BARE_DUMP_ALLOWED = (
    'text = nlohmann::json{ { "version", 1 }',
    "out << root.dump(2);",
    "const auto text = root.dump(2);",
    "output << Json{",
    "a_oldValue.dump(), a_newValue.dump());",
    "a_oldValue.dump(),",
    "a_newValue.dump(),",
    "SetPreference(key, value.dump(), error)",
    "normalized.dump());",
    "const auto encoded = a_request.dump();",
)
BARE_DUMPS = [
    line.strip()
    for path in sorted((UI / "src").glob("*.cpp"))
    for line in path.read_text(encoding="utf-8").splitlines()
    if ".dump(" in line and "ViewJson::Dump" not in line
]
check("push sites use F4T::ViewJson::Dump; only file, log and native dumps remain",
      len(BARE_DUMPS) == 10 and all(any(allowed in line for allowed in BARE_DUMP_ALLOWED) for line in BARE_DUMPS))

check("header declares Initialize, PushStateJson and Tick",
      all(line in HEADER for line in ("void Initialize();", "void PushStateJson(const char* a_json);", "void Tick();")))
check("wrapper names the view html path that exists on disk",
      f'constexpr auto kHtmlPath = "{HTML_PATH}";' in BRIDGE and (VIEW / "index.html").is_file())
check("wrapper and script share the entry point name",
      f'constexpr auto kEntryPoint = "{ENTRY}";' in BRIDGE and f"window.{ENTRY} = function" in JS)
check("view order 525 is used only by the capture HUD wrapper",
      f"constexpr int kViewOrder = {VIEW_ORDER};" in BRIDGE and len(re.findall(rf"=\s*{VIEW_ORDER}\s*;", SRC_TEXT)) == 1)
check("state is validated as JSON before it is pushed",
      "nlohmann::json::parse(payload, nullptr, false)" in BRIDGE and "state.is_object()" in BRIDGE)
check("state reaches the page as a JSON string literal, never raw script text",
      "nlohmann::json::parse(g_lastJson, nullptr, false)" in BRIDGE
      and "F4T::ViewJson::Dump(nlohmann::json(payload.dump()))" in BRIDGE)
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
check("the plugin target globs the UI src/**.cpp so the wrapper is compiled",
      'add_files(path.join(ui_dir, "src/**.cpp"))' in XMAKE)
check("wrapper and lifecycle stay under the 1000-line limit",
      len((UI / "src" / "F4TCaptureHud.cpp").read_text(encoding="utf-8").splitlines()) <= 1000
      and len(LIFECYCLE.splitlines()) <= 1000)
check("script takes the plugin field names nearBuildZoneId and buildZoneId",
      'plugin.nearBuildZoneId' in JS and 'settlement.buildZoneId' in JS)
check("script does not use the retired workbenchRefId names",
      "workbenchRefId" not in JS and "nearWorkbenchRefId" not in JS)
check("script takes the workbench, raid protection and self protection field names",
      all(name in JS for name in ("workbenchHp", "workbenchMaxHp", "workbenchBroken", "raidProtection", "selfRaidProtection")))
check("plugin emits the single protection reason the script labels",
      '"settlementProtected"' in CAPTURE_CPP and "settlementProtected:" in JS
      and "ownerNewPlayer" not in CAPTURE_CPP and "ownerNewPlayer" not in JS)
check("plugin protection JSON uses the active, reason and secondsLeft names the script reads",
      all(f'"{name}"' in CAPTURE_CPP for name in ("active", "reason", "secondsLeft", "raidProtection", "selfRaidProtection"))
      and all(f"source.{name}" in JS for name in ("active", "reason", "secondsLeft")))
check("the self protection comes from the settlement shield, with no separate per-player push",
      '"playerRaidProtection"' not in CORE_PROTOCOL and '"playerRaidProtection"' not in RAID_CPP
      and "ShieldSecondsLeft" in CAPTURE_CPP and "selfShieldLeft" in CAPTURE_CPP)
check("wrapper flashes the HUD on a self protection change, shows it near a settlement, hides it over menus and re-checks each tick",
      '"selfRaidProtection"' in BRIDGE and "(g_nearSettlement || flashing) && clear" in BRIDGE
      and "g_selfProtectionFlashLeft = kSelfProtectionFlash;" in BRIDGE
      and "if (clear) {\n\t\t\t\tg_selfProtectionFlashLeft = (std::max)(g_selfProtectionFlashLeft - elapsed" in BRIDGE.replace("\r\n", "\n")
      and BRIDGE.rindex("ApplyVisibility();") > BRIDGE.index("void Tick()"))
check("flash is gated on owning a settlement: plugin emits selfOwnsSettlement, wrapper and script read it and the ended flag",
      '"selfOwnsSettlement"' in CAPTURE_CPP
      and 'state.find("selfOwnsSettlement")' in BRIDGE and "selfOwns && changed" in BRIDGE
      and "kSelfProtectionJump" in BRIDGE and '"selfProtectionEnded"' in BRIDGE
      and "plugin.selfOwnsSettlement === true" in JS and "plugin.selfProtectionEnded === true" in JS)
check("the separate new-player protection constant is gone: the settlement shield is the only protection window",
      "NewPlayerProtectionHours" not in (ROOT / "co-core" / "include" / "F4TWorkshopRaids.h").read_text(encoding="utf-8"))
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
check("co-ui-ci runs this contract through the UI contract group",
      "scripts/run-python-tests.py --group ui" in CI_YML)

if failures:
    print(f"capture HUD contract FAILED: {len(failures)} check(s)")
    sys.exit(1)
print("capture HUD contract passed: header, Prisma view path and entry point, JSON validation, lifecycle init and tick, view order, CI lists")
