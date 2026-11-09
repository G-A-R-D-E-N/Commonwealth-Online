#!/usr/bin/env python3
"""test_ui_browser_contracts_part2: consolidated contract sections.

Consolidated from 7 single-contract scripts so the group runs one process
instead of 7. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_ui_browser_contracts_part2.py
"""
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok




def _section_007_test_gamepad_sticks_contract():
    """Contract moved verbatim from tests/audit/test_gamepad_sticks_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_gamepad_sticks_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"
    VIEWS = UI / "ui/views/CommonwealthOnline"
    SRC = UI / "src"
    INCLUDE = UI / "include"

    MODULE_SRC = VIEWS / "shared/gamepad-sticks.js"
    MODULE_TAG = '<script src="../shared/gamepad-sticks.js"></script>'

    STICK_VIEWS = {
        "admin": ("F4TAdminView.cpp", ".admin-body"),
        "faction": ("F4TFactionView.cpp", ".faction-panel"),
        "help": ("F4TInfoViews.cpp", ".info-panel"),
        "quests": ("F4TQuestView.cpp", ".quest-panel"),
        "report": ("F4TInfoViews.cpp", ".info-panel"),
        "server": ("F4TInfoViews.cpp", ".info-panel"),
        "trade": ("F4TTradeView.cpp", ".trade-lines"),
    }
    FORWARDERS = {
        "F4TAdminView.cpp": "F4T::AdminView::ForwardControllerAxes(",
        "F4TFactionView.cpp": "F4T::FactionView::ForwardControllerAxes(",
        "F4TInfoViews.cpp": "F4T::InfoViews::ForwardControllerAxes(",
        "F4TQuestView.cpp": "F4T::QuestView::ForwardControllerAxes(",
        "F4TTradeView.cpp": "F4T::TradeView::ForwardControllerAxes(",
    }
    SKIPPED_VIEWS = ("pause-menu", "interact-wheel", "character-select", "browser", "settings")


    def source(name):
        return (SRC / name).read_text(encoding="utf-8")


    def view_text(view):
        return "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted((VIEWS / view).iterdir())
            if path.suffix in (".html", ".js") and not path.name.endswith(".test.js")
        )


    def main():
        module = MODULE_SRC.read_text(encoding="utf-8")
        for api in ("window.handleControllerAxes =", "window.CO_GamepadSticks", "start:", "requestAnimationFrame", "DEAD_ZONE = 0.2", "scrollTop -="):
            assert api in module, f"gamepad-sticks.js must contain {api}"

        menu_input = source("F4TMenuInput.cpp")
        assert "kAxisEpsilon = 0.02f" in menu_input, "F4TMenuInput.cpp must forward axes with kAxisEpsilon 0.02f"
        for owner, call in FORWARDERS.items():
            assert call in menu_input, f"F4TMenuInput.cpp must forward scroll axes to {call}"

        for owner in FORWARDERS:
            code = source(owner)
            assert "void ForwardControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY)" in code, (
                f"{owner} must define ForwardControllerAxes"
            )
            assert "window.handleControllerAxes" in code, f"{owner} must call window.handleControllerAxes"

        for view, (owner, selector) in STICK_VIEWS.items():
            html = (VIEWS / view / "index.html").read_text(encoding="utf-8")
            assert MODULE_TAG in html, f"{view}/index.html must include shared/gamepad-sticks.js"
            assert f'CO_GamepadSticks.start("{selector}")' in html, (
                f"{view}/index.html must start stick scrolling on {selector}"
            )
            assert selector.lstrip(".") in view_text(view), f"{view} must render {selector}"

        for view in SKIPPED_VIEWS:
            page = VIEWS / view / "index.html"
            assert not page.exists() or MODULE_TAG not in page.read_text(encoding="utf-8"), (
                f"{view} must not include gamepad-sticks.js"
            )

        for path in list(SRC.glob("*.cpp")) + list(INCLUDE.glob("*.h")):
            assert "SetNativeGamepad" not in path.read_text(encoding="utf-8"), (
                f"{path.name} must not use SetNativeGamepad"
            )

        print("Gamepad stick contract passed")


    def test_gamepad_sticks_contract():
        main()


    if True:
        main()

def _section_008_test_menu_button_outline_contract():
    """Contract moved verbatim from tests/audit/test_menu_button_outline_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_menu_button_outline_contract.py")
    from pathlib import Path
    import re

    ROOT = Path(__file__).resolve().parents[2]
    VIEWS = ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline"
    SHEETS = {
        "main-menu/styles.css": (".menu-item", ".menu-item:hover", ".menu-item.is-selected"),
        "browser/styles.css": (".co-nav-item", ".co-nav-item:hover:not(.active)", ".co-nav-item.active"),
    }


    def rule_body(css, selector):
        match = re.search(r"(?m)^" + re.escape(selector) + r"\s*\{([^}]*)\}", css)
        assert match, f"missing rule {selector}"
        return match.group(1)


    def test_menu_buttons_use_one_uniform_outline():
        for sheet, selectors in SHEETS.items():
            css = (VIEWS / sheet).read_text(encoding="utf-8")
            for selector in selectors:
                body = rule_body(css, selector)
                assert "border-left" not in body, f"{sheet} {selector}: a thicker left border draws a broken stripe next to the rounded outline"
            for selector in selectors[:2]:
                assert not re.search(r"(?<![-\w])transform\s*:", rule_body(css, selector)), f"{sheet} {selector}: hover offsets must not move buttons under the cursor"
            assert "transition: all" not in css.split(selectors[0], 1)[1].split("}", 1)[0], f"{sheet}: transition only the outline properties"
            assert "var(--co-green)" in rule_body(css, selectors[1]), f"{sheet}: hover keeps the green outline"


    if True:
        test_menu_buttons_use_one_uniform_outline()
        print("Menu button outline contract passed")

def _section_009_test_modpack_view_contract():
    """Contract moved verbatim from tests/audit/test_modpack_view_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_modpack_view_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    MAX_LINES = 1000

    CONTENT_POLICY_HEADER = "co-core/include/F4TModpackContentPolicy.h"
    CONTENT_POLICY_SOURCE = "co-core/src/manifest/F4TModpackContentPolicy.cpp"
    MANIFEST_HEADER = "co-core/include/F4TModpackManifest.h"
    MANIFEST_SOURCE = "co-core/src/manifest/F4TModpackManifest.cpp"
    MISMATCH_REPORT_SOURCE = "co-core/src/manifest/F4TModpackMismatchReport.cpp"
    NEED_SOURCE = "co-core/src/manifest/F4TModpackNeed.cpp"
    SERVE_SOURCE = "co-core/src/manifest/F4TModpackServe.cpp"
    DOWNLOAD_SOURCE = "co-core/src/manifest/F4TModpackDownload.cpp"
    INSTALL_SOURCE = "co-core/src/manifest/F4TModpackInstall.cpp"
    INSTALL_NATIVE_SOURCE = "co-core/src/manifest/F4TModpackInstallNative.cpp"
    AUTOFIX_SOURCE = "co-core/src/manifest/F4TModpackAutoFix.cpp"
    IDENTITY_HEADER = "co-core/include/F4TIdentityProtocol.h"
    IDENTITY_SOURCE = "co-core/src/session/F4TIdentityProtocol.cpp"
    CONFIG_SOURCE = "co-core/src/config/F4TServerConfiguration.cpp"
    CONFIG_MODPACK_SOURCE = "co-core/src/config/F4TServerConfigModpack.cpp"
    SERVER_CHUNKS_SOURCE = "CO-SERVER/server-cpp/src/ServerManifestChunks.cpp"
    SERVER_FILES_SOURCE = "CO-SERVER/server-cpp/src/ServerModpackFiles.cpp"
    SERVER_CONFIG_SOURCE = "CO-SERVER/server-cpp/src/ServerConfiguration.cpp"
    SERVER_RUNTIME_INC = "CO-SERVER/server-cpp/src/ServerRuntime.inc"
    PLUGIN_DOWNLOAD_SOURCE = "CO-BETA/plugin/src/F4TGnsModpackDownload.cpp"
    PLUGIN_CHUNKS_SOURCE = "CO-BETA/plugin/src/F4TGnsManifestFetch.cpp"
    PLUGIN_PROBE_SOURCE = "CO-BETA/plugin/src/F4TClientModpackProbe.cpp"
    MODPACK_DETAIL_SOURCE = "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/modpack-detail.js"
    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")


    def test_view_limits_and_install_root_are_declared():
        header = read(CONTENT_POLICY_HEADER)
        assert "kMaximumViewFileBytes = 4ull * 1024 * 1024" in header
        assert "kMaximumViewTotalBytes = 32ull * 1024 * 1024" in header
        assert 'kResourceViewRoot = "PrismaUI_F4/views/co-resources/"' in header
        assert "resourceView" in header


    def test_view_names_are_validated_before_use():
        source = read(CONTENT_POLICY_SOURCE)
        for fragment in (
            "F4T::StateBagProtocol::IsValidResourceName",
            "kResourceViewExtensions",
            "IsValidViewName",
            "IsInstallableName",
            "InstallRelativeName",
            "MaximumInstallableFileBytes",
        ):
            assert fragment in source, fragment
        for extension in (".html", ".js", ".css", ".json", ".png", ".jpg", ".jpeg", ".svg", ".webp", ".woff", ".woff2"):
            assert f'"{extension}"' in source, extension


    def test_manifest_parses_and_diffs_view_entries():
        header = read(MANIFEST_HEADER)
        source = read(MANIFEST_SOURCE)
        assert "WithoutViewEntries" in header
        for fragment in ('"view"', "EntryKind::view", "wrongView", "Wrong view:", "WithoutViewEntries"):
            assert fragment in source, fragment
        assert '"Different view"' in read(MISMATCH_REPORT_SOURCE)


    def test_identity_gives_views_only_to_state_bag_clients():
        header = read(IDENTITY_HEADER)
        source = read(IDENTITY_SOURCE)
        assert "bool ReceivesFullModpack(const nlohmann::json& a_hello, bool a_hasViewless);" in header
        assert "modpackViewlessFingerprint" in header
        assert "modpackViewlessManifestJson" in header
        assert '#include "F4TStateBagProtocol.h"' in source
        assert "F4T::StateBagProtocol::kCapability" in source


    def test_server_selects_the_viewless_manifest_for_old_clients():
        chunks = read(SERVER_CHUNKS_SOURCE)
        files = read(SERVER_FILES_SOURCE)
        config = read(SERVER_CONFIG_SOURCE)
        assert "F4T::Identity::ReceivesFullModpack(request, a_options.requiredModpackViewlessFingerprint.has_value() || a_options.requiredModpackViewsOnly)" in chunks
        assert "F4T::Identity::ReceivesFullModpack(hello, a_options.requiredModpackViewlessFingerprint.has_value() || a_options.requiredModpackViewsOnly)" in files
        assert "F4T::ModpackServe::ExpectedFingerprint(*catalog, fullManifest)" in files
        assert "F4T::ModpackServe::FindEntry(*catalog, request->name, fullManifest)" in files
        assert "requiredModpackViewlessManifestJson" in chunks
        assert ".modpackViewlessFingerprint = a_options.requiredModpackViewlessFingerprint," in config
        lines = read(SERVER_RUNTIME_INC).splitlines()
        assert len(lines) <= MAX_LINES, len(lines)


    def test_config_accepts_a_view_only_modpack():
        source = read(CONFIG_SOURCE) + read(CONFIG_MODPACK_SOURCE)
        assert "WithoutViewEntries" in source
        assert "required.viewsOnly = true;" in source
        assert "has only resource views" not in source
        assert "IsInstallableName(name)" in source


    def test_clients_advertise_state_bag_and_place_views_under_the_resource_root():
        for source in (PLUGIN_DOWNLOAD_SOURCE, PLUGIN_CHUNKS_SOURCE):
            text = read(source)
            assert "F4T::StateBagProtocol::kCapability" in text, source
            assert '#include "F4TStateBagProtocol.h"' in text, source
        probe = read(PLUGIN_PROBE_SOURCE)
        assert '#include "F4TModpackContentPolicy.h"' in probe
        assert "ModpackManifest::InstallRelativeName(entry.name)" in probe
        assert "ModpackProbeCache::FileFact{ relativePath, item.sizeBytes, item.modifiedTicks }" in probe
        assert "ResolveDataRelative(*dataDirectory, file.path)" in probe


    def test_download_install_and_serve_keep_views_on_plain_paths():
        assert "CreatePlainParents" in read(DOWNLOAD_SOURCE)
        assert "MaximumInstallableFileBytes" in read(DOWNLOAD_SOURCE)
        assert "ModpackManifest::InstallRelativeName(" in read(INSTALL_SOURCE)
        assert "MakeNativeFileSystem" in read(INSTALL_NATIVE_SOURCE)
        assert len(read(INSTALL_NATIVE_SOURCE).splitlines()) < MAX_LINES
        assert "ConsiderView(" in read(NEED_SOURCE)
        assert "IsPlainFileInside(" in read(SERVE_SOURCE)


    def test_consent_and_ui_name_views():
        assert "resource views change how your game runs" in read(AUTOFIX_SOURCE)
        assert '"Different view"' in read(MODPACK_DETAIL_SOURCE)


    _merge_ns = dict(locals())
    if True:
        for name, function in list(_merge_ns.items()):
            if name.startswith("test_") and callable(function):
                function()
        print("Modpack view contract passed")

def _section_010_test_pause_menu_info_views():
    """Contract moved verbatim from tests/audit/test_pause_menu_info_views.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_pause_menu_info_views.py")
    from pathlib import Path
    import json
    import re

    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"
    VIEWS = UI / "ui/views/CommonwealthOnline"
    MENU = UI / "src/F4TPrismaUI_Menu.cpp"
    INFO = UI / "src/F4TInfoViews.cpp"
    MAP_SRC = UI / "src/F4TPauseMenuMap.cpp"
    FOG_HEADER = UI / "include/F4TMapFogMemory.h"
    LIFECYCLE_SRC = UI / "src/F4TPrismaUI_Lifecycle.cpp"
    PLUGIN_MAIN = ROOT / "CO-BETA/plugin/src/main.cpp"
    MAP_PICTURES = VIEWS / "pause-menu/assets/maps"
    ALLOWED_API_CALLS = {"Invoke", "IsHidden", "IsValid"}


    def check_map_pictures(script):
        cpp = MAP_SRC.read_text(encoding="utf-8")
        styles = (VIEWS / "pause-menu/styles.css").read_text(encoding="utf-8")
        frames = re.findall(
            r'\{\s*"(\w+)",\s*"(\w+)",\s*([\d.]+)F,\s*([\d.]+)F,\s*([\d.]+)F,\s*([\d.]+)F\s*\}', cpp
        )
        assert [f[0] for f in frames] == ["commonwealth", "farharbor", "nukaworld"], (
            "F4TPauseMenuMap.cpp kMapFrames must list commonwealth, farharbor and nukaworld"
        )
        assert [f[1] for f in frames] == ["Commonwealth", "DLC03FarHarbor", "NukaWorld"], (
            "F4TPauseMenuMap.cpp kMapFrames must match each worldspace by editor ID"
        )
        for key, editor_id, *rect in frames:
            folder = MAP_PICTURES / key
            assert (folder / "map.png").is_file(), f"pause-menu/assets/maps/{key}/map.png must exist"
            meta = json.loads((folder / "map.json").read_text(encoding="utf-8"))
            assert meta["worldspace"] == editor_id, f"{key}/map.json worldspace must be {editor_id}"
            cal = meta["calibration"]
            assert [cal["x0"], cal["y0"], cal["x1"], cal["y1"]] == [float(v) for v in rect], (
                f"kMapFrames for {key} must match {key}/map.json calibration"
            )
        assert 'root["mapKey"]' in cpp, "F4TPauseMenuMap.cpp must send the map picture key as mapKey"
        assert "GetParentWorld(RE::TESWorldSpace::PARENT_USE_FLAG::kMap)" in cpp, (
            "F4TPauseMenuMap.cpp must resolve child worldspaces to their parent map"
        )
        assert "a_world->editorID.c_str()" in cpp, "F4TPauseMenuMap.cpp must match worldspaces by the stored editorID"
        assert "var setMapPicture = function () {};" in script, (
            "pause-menu/script.js must declare setMapPicture at IIFE scope so renderMapUpdate can reach it"
        )
        assert "payload.mapKey" in script, "pause-menu/script.js must read the map picture key from the payload"
        assert '"assets/maps/" + next + "/map.png"' in script, "pause-menu/script.js must build the picture path from the key"
        assert "assets/maps/commonwealth" not in script and "assets/maps/" not in styles, (
            "the map picture must not be hardcoded to commonwealth in script.js or styles.css"
        )


    def check_worldspace_tabs(script):
        cpp = MAP_SRC.read_text(encoding="utf-8")
        start = cpp.index("nlohmann::json BuildOthers(")
        build_others = cpp[start:cpp.index("void BuildUpdateJsonWorker()", start)]
        assert "RE::TESForm::GetFormByID<RE::TESWorldSpace>(*remoteWorld)" in build_others, (
            "BuildOthers must look up an ally's worldspace by its form id"
        )
        assert "ResolveMapBounds(" in build_others and "!bounds.known" in build_others, (
            "BuildOthers must resolve an ally in another worldspace to its own map frame and hide unknown ones"
        )
        assert '{ "worldspace", bounds.frame->key }' in build_others, (
            "BuildOthers must send each drawn ally's worldspace key"
        )
        enter = script[script.index("function enterTravelConfirmMode() {"):script.index("function exitTravelConfirmMode() {")]
        assert "!onOwnWorldspace()" in enter, "enterTravelConfirmMode must refuse fast travel off the player's own worldspace tab"
        assert "function onOwnWorldspace()" in script, "pause-menu/script.js must define onOwnWorldspace"
        assert "function worldspaceTabsActive()" not in script, "the worldspace tabs must always be visible while the map is open"
        assert "if (!mapTabPicked && payload.mapKey)" in script, "the active tab must follow payload.mapKey until the user picks one"
        assert "var markers = ownTab && Array.isArray(payload.markers)" in script, "map markers must only draw on the player's own worldspace tab"
        assert "var captures = ownTab && mapPlayers" in script, "capture circles must only draw on the player's own worldspace tab"
        assert "if (payload.player && ownTab)" in script, "the player arrow must only draw on the player's own worldspace tab"
        assert "pickWorldspaceTab(tab.dataset.worldspace)" in script, "the tab click must route through pickWorldspaceTab"


    def check_fog_of_war(html, script):
        styles = (VIEWS / "pause-menu/styles.css").read_text(encoding="utf-8")
        layer = re.search(r'<div class="map-layer">(.*?)</div>', html)
        assert layer and 'id="mapFog"' in layer.group(1), "index.html must put the mapFog canvas inside .map-layer"
        assert html.index('class="map-layer"') < html.index('id="mapMarkers"'), (
            "the fog canvas must sit in the map layer below the markers layer"
        )
        assert ".map-fog {" in styles and "--co-map-fog:" in styles, (
            "styles.css must define the .map-fog layer and the --co-map-fog token"
        )
        assert 'getPropertyValue("--co-map-fog")' in script, "script.js must fill the fog with the --co-map-fog token"
        draw = script[script.index("function drawMapFog() {"):script.index("function renderMapUpdate(payload) {")]
        assert "var ownTab = onOwnWorldspace();" in draw and "if (!ownTab) {" in draw, (
            "drawMapFog must only draw on the player's own worldspace tab"
        )
        assert "markers[m].discovered ? FOG_DISCOVERED_RADIUS_PCT : FOG_UNDISCOVERED_RADIUS_PCT" in draw, (
            "drawMapFog must reveal each marker by its discovered flag"
        )
        assert "lastMapPayload.player" in draw and "revealMapFog(ctx, playerX, playerY, FOG_PLAYER_RADIUS_PCT)" in draw, (
            "drawMapFog must reveal the player's own position"
        )
        render = script[script.index("function renderMapUpdate(payload) {"):script.index("window.onMapUpdate = function")]
        assert "    drawMapFog();" in render, "renderMapUpdate must redraw the fog"
        assert 'fogKey = "";' in script and "mapFog.width = Math.round(mapDisplayWidth" in script, (
            "the fog must redraw when the map is resized"
        )


    def check_fog_memory(script):
        cpp = MAP_SRC.read_text(encoding="utf-8")
        header = FOG_HEADER.read_text(encoding="utf-8")
        lifecycle = LIFECYCLE_SRC.read_text(encoding="utf-8")
        main_src = PLUGIN_MAIN.read_text(encoding="utf-8")
        assert FOG_HEADER.is_file(), "CommonwealthOnlineUI/include/F4TMapFogMemory.h must exist"
        assert "inline constexpr std::uint32_t kRecordType = 'FOGM';" in header, (
            "F4TMapFogMemory.h must store the explored map as the FOGM record"
        )
        assert main_src.count("SetUniqueID(") == 1, "main.cpp must register exactly one F4SE serialization unique id"
        assert '#include "F4TMapFogMemory.h"' in main_src, "main.cpp must include F4TMapFogMemory.h"
        for call in (
            "F4T::MapFogMemory::Save(a_intfc);",
            "F4T::MapFogMemory::Load(a_intfc, version, length);",
            "F4T::MapFogMemory::Clear();",
        ):
            assert call in main_src, f"main.cpp must call {call}"
        assert "if (type == F4T::MapFogMemory::kRecordType) {" in main_src, (
            "main.cpp must hand the FOGM record to the map memory while it reads the cosave records"
        )
        assert "F4T::PauseMenuMap::SampleFogOfWar();" in lifecycle, "the menu watch task must sample the explored map"
        assert 'root["fogExplored"] = F4T::MapFogMemory::ExploredBase64(bounds.frame->key);' in cpp, (
            "the map update must send the explored grid of the player's map key"
        )
        assert 'root["fogGridSize"] = F4T::MapFogMemory::kGridSize;' in cpp, "the map update must send the grid size"
        assert "GetMenuOpen<RE::LoadingMenu>()" in cpp, "the explored map must not be sampled while the loading menu is open"
        assert "function clearExploredFog(ctx, explored, cells) {" in script, "pause-menu/script.js must clear the explored cells"
        assert "atob(explored)" in script, "pause-menu/script.js must decode the explored grid"
        draw = script[script.index("function drawMapFog() {"):script.index("function renderMapUpdate(payload) {")]
        assert "clearExploredFog(ctx, explored, Number(lastMapPayload.fogGridSize) || 0);" in draw, (
            "drawMapFog must clear the explored cells before the reveals"
        )


    def check_hostile_layer(script, html):
        cpp = MAP_SRC.read_text(encoding="utf-8")
        players = (VIEWS / "pause-menu/map-players.js").read_text(encoding="utf-8")
        assert "nlohmann::json BuildHostiles(" in cpp, "F4TPauseMenuMap.cpp must define BuildHostiles"
        start = cpp.index("nlohmann::json BuildHostiles(")
        build_hostiles = cpp[start:cpp.index("void BuildUpdateJsonWorker()", start)]
        assert "GetHostileToActor(" in build_hostiles and "highActorHandles" in build_hostiles, (
            "BuildHostiles must walk the high actor list and keep only actors hostile to the player"
        )
        assert "IsInCombat()" in build_hostiles and "currentCombatTarget" in build_hostiles, (
            "BuildHostiles must keep only hostile actors that are in combat with the player"
        )
        assert re.search(r"std::(partial_)?sort\(", build_hostiles), (
            "BuildHostiles must sort the hostiles by distance before applying the cap"
        )
        assert 'root["hostiles"]' in cpp, "F4TPauseMenuMap.cpp must send the hostile list as hostiles"
        assert re.search(r"var hostiles = ownTab &&[^\n]*payload\.hostiles", script), (
            "pause-menu/script.js must read payload.hostiles only on the player's own worldspace tab"
        )
        assert re.search(r"defaultFilters\(\) \{[^}]*hostiles: true", players), (
            "map-players.js defaultFilters must turn the hostiles filter on"
        )
        assert 'data-filter="hostiles"' in html, "pause-menu/index.html must carry the Hostiles filter button"


    def check_player_name_priority(script):
        styles = (VIEWS / "pause-menu/styles.css").read_text(encoding="utf-8")
        assert 'var zoomedOut = zoomT < STRANGER_NAME_ZOOM_T;' in script and 'pauseScreen.classList.toggle("map-zoomed-out", zoomedOut);' in script, (
            "applyMapTransform must mark the map as zoomed out for stranger names"
        )
        assert re.search(r"\.map-zoomed-out \.marker--rel-other[^{]*\.marker__name \{\s*opacity: 0;", styles), (
            "styles.css must hide stranger names while zoomed out"
        )
        ranks = [
            int(re.search(r"\.marker--rel-" + rel + r" \{[^}]*z-index: (\d+);", styles).group(1))
            for rel in ("party", "faction", "friend")
        ]
        assert ranks == sorted(ranks, reverse=True) and ranks[-1] > 1, (
            "party dots must draw above faction, friend and stranger dots"
        )


    def check_player_hud_extras(script, html):
        players = (VIEWS / "pause-menu/map-players.js").read_text(encoding="utf-8")
        styles = (VIEWS / "pause-menu/styles.css").read_text(encoding="utf-8")
        assert 'data-filter="names"' in html, "pause-menu/index.html must carry the Names filter button"
        assert re.search(r"defaultFilters\(\) \{[^}]*names: true", players), (
            "map-players.js defaultFilters must turn the names filter on"
        )
        assert 'classList.toggle("map-player-names-hidden"' in script, (
            "pause-menu/script.js must toggle the map-player-names-hidden class from the names filter"
        )
        assert re.search(r"\.map-player-names-hidden \.marker--player-other:not\(\.marker--spotlight\) \.marker__name \{\s*opacity: 0;", styles), (
            "styles.css must hide other-player names while the names filter is off, keeping the spotlight name"
        )
        assert 'id="mapLegendParty"' in html, "pause-menu/index.html must carry the party legend list"
        assert "mapPlayers.partyMembers(" in script, "pause-menu/script.js must build the party list with mapPlayers.partyMembers"
        assert 'id="mapLegendFaction"' in html, "pause-menu/index.html must carry the faction legend list"
        assert "mapPlayers.factionMembers(" in script, "pause-menu/script.js must build the faction list with mapPlayers.factionMembers"
        assert 'tag.textContent = "[" + member.factionName + "]";' in script, (
            "legend entries must show the member's faction tag as text"
        )
        calls = re.findall(r"localStorage\.\w+\(", script)
        guarded = re.findall(r"try \{[^}]*localStorage\.\w+\(", script)
        assert calls and len(guarded) == len(calls), (
            "every localStorage access in pause-menu/script.js must sit inside a try block"
        )


    def check_player_travel(script):
        menu = MENU.read_text(encoding="utf-8")
        source = MAP_SRC.read_text(encoding="utf-8")
        assert 'sendCoAction({ action: "travelToPlayer", playerId: String(playerId), cost: cost });' in script, (
            "pause-menu/script.js must send the travelToPlayer action"
        )
        assert "requestTravelToPlayer(playerId, playerCost);" in script, "travel must send the displayed quote"
        assert "cost != a_quotedCost" in source and "cost != g_deferredTravelCost" in source, (
            "player travel must reject price changes both before deferral and before charging"
        )
        assert 'actionName == "travelToPlayer"' in menu, "F4TPrismaUI_Menu.cpp must handle the travelToPlayer action"
        assert "Can't travel while in combat" in script, "pause-menu/script.js must explain the in-combat travel block"
        assert re.search(r"kCapsFormId = 0x0+F;", source), "F4TPauseMenuMap.cpp must name the caps form 0x0000000F"
        for constant in ("kTravelBaseCost = 5;", "kTravelUnitsPerCost = 1500.0F;", "kTravelMaxCost = 100;"):
            assert constant in source, f"F4TPauseMenuMap.cpp must define {constant}"
        assert 'root["inCombat"] = player->IsInCombat();' in source, "the map payload must carry inCombat"
        assert source.count("if (player->IsInCombat()) {") == 2, (
            "player travel must refuse at request time and again before charging caps"
        )
        assert source.count('RemoteFlag(entry, "faction")') == 2, (
            "faction members must be travelable in both the target lookup and the payload"
        )
        assert "RE::SendHUDMessage::ShowHUDMessage(" in source and source.count("ShowTravelRefused(") >= 9, (
            "every refused player trip must show a HUD message"
        )


    def main():
        html = (VIEWS / "pause-menu/index.html").read_text(encoding="utf-8")
        script = (VIEWS / "pause-menu/script.js").read_text(encoding="utf-8")
        menu = MENU.read_text(encoding="utf-8")
        info = INFO.read_text(encoding="utf-8")
        check_map_pictures(script)
        check_worldspace_tabs(script)
        check_fog_of_war(html, script)
        check_fog_memory(script)
        check_hostile_layer(script, html)
        check_player_name_priority(script)
        check_player_hud_extras(script, html)
        check_player_travel(script)

        opened = set(re.findall(r'onOpenView\("([a-z]+)"\)', script)) - {"settings", "map"}
        assert {"server", "help"} <= opened, "pause-menu/script.js must open the server and help views"
        for name in sorted(opened):
            assert f'viewName == "{name}"' in menu, (
                f"F4TPrismaUI_Menu.cpp:openView must handle the {name} row the pause menu opens"
            )

        for action in ("server", "help"):
            row = re.search(rf'<button[^>]*data-action="{action}"[^>]*>', html)
            assert row, f"the pause menu must carry a {action} row"
            assert "hidden" not in row.group(0), f"the {action} row must be visible"

        for folder, script_name in (("server", "server.js"), ("help", "help.js")):
            page = (VIEWS / folder / "index.html").read_text(encoding="utf-8")
            assert script_name in page, f"{folder}/index.html must load {script_name}"
            assert f'CommonwealthOnline/{folder}/index.html' in info, (
                f"F4TInfoViews.cpp must create the {folder} view"
            )
            assert (VIEWS / folder / script_name).is_file()
            assert (VIEWS / folder / script_name.replace(".js", ".test.js")).is_file()

        assert "settings/settings-data.js" in (VIEWS / "help/index.html").read_text(encoding="utf-8"), (
            "help must read its key defaults from the settings data"
        )

        called = set(re.findall(r"g_api->(\w+)\(", info))
        unexpected = called - ALLOWED_API_CALLS - {"RegisterConsoleCallback", "BindUIEvent", "SetOrder"}
        assert not unexpected, f"F4TInfoViews.cpp calls Prisma methods outside the shared API: {sorted(unexpected)}"

        for token in ("Surface::Help",):
            assert token in info, f"F4TInfoViews.cpp must attach the help view to the voice bridge ({token})"

        print("Pause menu info views contract passed")


    def test_pause_menu_info_views():
        main()


    if True:
        main()

def _section_011_test_pause_menu_report_window():
    """Contract moved verbatim from tests/audit/test_pause_menu_report_window.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_pause_menu_report_window.py")
    from pathlib import Path
    import re

    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"
    VIEWS = UI / "ui/views/CommonwealthOnline"
    MENU = UI / "src/F4TPrismaUI_Menu.cpp"
    INFO = UI / "src/F4TInfoViews.cpp"
    PLUGIN = ROOT / "CO-BETA/plugin"
    PLUGIN_REPORT = PLUGIN / "src/F4TReport.cpp"
    PROTOCOL_ERRORS = (
        "invalidKind",
        "invalidTarget",
        "targetUnavailable",
        "selfTarget",
        "invalidReason",
        "noteRequired",
        "noteTooLong",
        "unsafeText",
        "rateLimited",
    )


    def test_pause_menu_has_report_row_between_help_and_quit():
        html = (VIEWS / "pause-menu/index.html").read_text(encoding="utf-8")
        row = re.search(r'<button[^>]*data-action="report"[^>]*>', html)
        assert row, "the pause menu must carry a report row"
        assert "hidden" not in row.group(0), "the report row must be visible"
        assert ">REPORT<" in html, "the report row must be labelled REPORT"
        assert html.index('data-action="help"') < html.index('data-action="report"') < html.index('data-action="quit"'), (
            "the report row must sit beside the server and help rows, before quit"
        )


    def test_pause_menu_opens_report_view():
        script = (VIEWS / "pause-menu/script.js").read_text(encoding="utf-8")
        menu = MENU.read_text(encoding="utf-8")
        info = INFO.read_text(encoding="utf-8")
        assert 'onOpenView("report")' in script, "pause-menu/script.js must open the report view"
        assert 'viewName == "report"' in menu, "F4TPrismaUI_Menu.cpp:openView must handle the report row"
        assert "F4T::InfoViews::Kind::Report" in menu, "F4TPrismaUI_Menu.cpp must open the report window"
        assert "CommonwealthOnline/report/index.html" in info, "F4TInfoViews.cpp must create the report window"


    def test_report_view_files_and_bridge_names_agree():
        page = (VIEWS / "report/index.html").read_text(encoding="utf-8")
        script = (VIEWS / "report/report.js").read_text(encoding="utf-8")
        info = INFO.read_text(encoding="utf-8")
        assert "report.js" in page, "report/index.html must load report.js"
        assert "../shared/info-panel.css" in page, "the report window must reuse the shared info-panel CSS"
        assert (VIEWS / "report/report.test.js").is_file(), "report/report.test.js must exist"
        assert 'action: "submit"' in script, "report.js must send a submit action"
        assert "window.coReportSetState = " in script and "window.coReportEvent = " in script
        assert "window.coReportSetState" in info, "F4TInfoViews.cpp must push the roster to coReportSetState"
        assert "window.coReportEvent" in info, "F4TInfoViews.cpp must push results to coReportEvent"
        assert 'action == "submit"' in info, "F4TInfoViews.cpp must forward submit actions to the plugin"
        for code in PROTOCOL_ERRORS:
            assert f"{code}:" in script, f"report.js must map the {code} error to a sentence"
        assert "NOTE_MAX = 200" in script and "noteBytes(note) > NOTE_MAX" in script, "report notes must be capped at 200 bytes"


    def test_plugin_sends_and_receives_report_packets():
        report = PLUGIN_REPORT.read_text(encoding="utf-8")
        inbound = (PLUGIN / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
        client = (PLUGIN / "src/F4TUIClient.cpp").read_text(encoding="utf-8")
        assert '"reportSubmit"' in report, "F4TReport.cpp must send the reportSubmit packet"
        assert '"reportResult"' in report and '"reportReceived"' in report, (
            "F4TReport.cpp must handle reportResult and reportReceived"
        )
        assert "F4T::Report::TryHandlePacket(*packetType, packet)" in inbound, (
            "F4TNetworkingInbound.cpp must dispatch report packets to F4TReport"
        )
        assert "F4T::Report::Register();" in client, "F4TUIClient.cpp must register the report UI callbacks"
        assert "GetRemotePlayerSnapshot()" in report, "the report roster must come from the existing remote player list"
        assert "GetAssignedPlayerId()" in report, "the report roster must leave out the local player"
        assert "IVCommonwealthOnlineUI21" in report, "F4TReport.cpp must use the report UI interface"


    def test_ui_api_exposes_report_interface():
        header = (UI / "include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
        plugin_header = (PLUGIN / "include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
        exports = (UI / "src/PluginAPI.cpp").read_text(encoding="utf-8")
        assert header == plugin_header, "both copies of CommonwealthOnlineUI_API.h must match"
        assert "struct IVCommonwealthOnlineUI21" in header
        assert "case InterfaceVersion::V21:" in exports and "g_interfaceV21" in exports, (
            "PluginAPI.cpp must route interface V21"
        )


    def main():
        test_pause_menu_has_report_row_between_help_and_quit()
        test_pause_menu_opens_report_view()
        test_report_view_files_and_bridge_names_agree()
        test_plugin_sends_and_receives_report_packets()
        test_ui_api_exposes_report_interface()
        print("Pause menu report window contract passed")


    if True:
        main()

def _section_012_test_pipboy_colour_clamp_contract():
    """Contract moved verbatim from tests/audit/test_pipboy_colour_clamp_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_pipboy_colour_clamp_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    source = (ROOT / "CommonwealthOnlineUI/src/F4TSettingsApplyHandlers.cpp").read_text(encoding="utf-8")
    body = source[source.index("bool QueuePipBoyColourCells()"):source.index("bool ApplyHUDSettings(")]

    assert "*cell = std::clamp(setting->GetFloat(), 0.005F, 1.0F);" in body, "Pip-Boy colour must use the engine bounds; source:docs/code-notes/CommonwealthOnlineUI.md"
    assert "*cell = setting->GetFloat();" not in body, "the raw Pip-Boy store must not come back"

    values = (ROOT / "CommonwealthOnlineUI/src/F4TSettingsServiceValues.cpp").read_text(encoding="utf-8")
    assert "std::clamp(a_channel / 255.0f, 0.005F, 1.0F)" in values, "the stored and persisted Pip-Boy colour must use the engine bounds"
    for channel in "rgb":
        assert f"WriteFloat(*{channel}, toEngine(colour->{channel}));" in values, f"Pip-Boy {channel} store must go through the clamp"

    print("test_pipboy_colour_clamp_contract: all checks passed.")

def _section_013_test_quest_menu_availability():
    """Contract moved verbatim from tests/audit/test_quest_menu_availability.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_quest_menu_availability.py")
    from pathlib import Path
    import re

    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"
    PAUSE = UI / "ui/views/CommonwealthOnline/pause-menu"
    QUEST = UI / "ui/views/CommonwealthOnline/quests"
    VIEW = UI / "src/F4TQuestView.cpp"
    REMOVED_GATE = ("HostAvailable", "PushMenuAvailability")


    def slice_between(source: str, start_anchor: str, end_anchor: str) -> str:
        start = source.find(start_anchor)
        assert start >= 0, f"{start_anchor.strip()} is missing"
        end = source.find(end_anchor, start)
        assert end > start, f"{end_anchor.strip()} must follow {start_anchor.strip()}"
        return source[start:end]


    def main():
        html = (PAUSE / "index.html").read_text(encoding="utf-8")
        script = (PAUSE / "script.js").read_text(encoding="utf-8")
        view = VIEW.read_text(encoding="utf-8")
        window = (QUEST / "quests.js").read_text(encoding="utf-8")

        row = re.search(r"<button[^>]*data-action=\"quests\"[^>]*>", html)
        assert row, "the pause menu must carry a QUESTS row"
        assert "hidden" not in row.group(0), (
            "the QUESTS row must be visible before the plugin registers quest callbacks"
        )

        menu = slice_between(html, '<nav class="menu" id="menu"', "</nav>")
        ordinals = [int(value) for value in re.findall(r'data-index="(\d+)"', menu)]
        assert ordinals == list(range(len(ordinals))), (
            "the pause menu rows must carry data-index equal to their display order, "
            f"got {ordinals}"
        )

        for name, text in (
            ("pause-menu/index.html", html),
            ("pause-menu/script.js", script),
            ("F4TQuestView.cpp", view),
        ):
            assert "setQuestMenuAvailable" not in text, (
                f"{name} must not toggle the QUESTS row; the window reports availability"
            )

        unavailable = slice_between(window, "if (s.enabled !== true) {", "if (trimmed(s.mode)")
        assert "Quests are not available on this server" in unavailable
        assert 'data-action="close"' in unavailable, (
            "the not-available panel must be dismissable by pointer, not only by Escape"
        )

        for name in REMOVED_GATE:
            assert name not in view, f"F4TQuestView.cpp:{name} was the removed row gate"
            for base in ("src", "include"):
                for path in sorted((UI / base).rglob("*")):
                    if path.suffix not in (".cpp", ".h", ".hpp"):
                        continue
                    assert name not in path.read_text(encoding="utf-8"), (
                        f"{path.relative_to(ROOT).as_posix()} still references {name}"
                    )

        notify = slice_between(view, "void NotifyStateChanged()", "bool IsVisible()")
        assert "PushState()" in notify, (
            "F4TQuestView.cpp:NotifyStateChanged must refresh an open window when the plugin reports a change"
        )

        print("Quest menu availability contract passed")


    def test_quest_menu_availability():
        main()


    if True:
        main()


_SECTIONS = [
    ("tests/audit/test_gamepad_sticks_contract.py", _section_007_test_gamepad_sticks_contract),
    ("tests/audit/test_menu_button_outline_contract.py", _section_008_test_menu_button_outline_contract),
    ("tests/audit/test_modpack_view_contract.py", _section_009_test_modpack_view_contract),
    ("tests/audit/test_pause_menu_info_views.py", _section_010_test_pause_menu_info_views),
    ("tests/audit/test_pause_menu_report_window.py", _section_011_test_pause_menu_report_window),
    ("tests/audit/test_pipboy_colour_clamp_contract.py", _section_012_test_pipboy_colour_clamp_contract),
    ("tests/audit/test_quest_menu_availability.py", _section_013_test_quest_menu_availability),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_ui_browser_contracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
