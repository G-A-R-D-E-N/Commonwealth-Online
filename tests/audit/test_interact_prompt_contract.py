from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"
PLUGIN = ROOT / "CO-BETA" / "plugin"
VIEW = UI / "ui" / "views" / "CommonwealthOnline" / "interact-prompt"
CI = next((p for p in (ROOT / ".github" / "workflows" / "co-ui-ci.yml", ROOT / ".github" / "public-ci" / "workflows" / "co-ui-ci.yml") if p.is_file()), ROOT / ".github" / "workflows" / "co-ui-ci.yml")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_v29_interface_is_declared_and_exported():
    api = read(UI / "include" / "CommonwealthOnlineUI_API.h")
    plugin_api = read(PLUGIN / "include" / "CommonwealthOnlineUI_API.h")
    exports = read(UI / "src" / "PluginAPI.cpp")
    assert "V29 = 29" in api, "the UI API must declare interface version 29"
    assert "struct IVCommonwealthOnlineUI29" in api, "the UI API must declare the V29 struct"
    assert "void (*PushInteractPrompt)(const char* a_json){ nullptr };" in api, "V29 must carry PushInteractPrompt"
    assert "InterfaceVersion::V29;" in api and "InterfaceVersionMap<IVCommonwealthOnlineUI29>" in api, (
        "V29 needs its own version map entry"
    )
    assert "void (*PushInteractPrompt)(const char* a_json){ nullptr };" in plugin_api, "the plugin API copy must match V29"
    assert "IVCommonwealthOnlineUI29 g_interfaceV29{ Api_PushInteractPrompt };" in exports, "PluginAPI must export V29"
    assert "case InterfaceVersion::V29:" in exports and "return a_visit(g_interfaceV29);" in exports, (
        "VisitInterface must route V29"
    )
    assert "F4T::InteractPromptHud::PushStateJson(a_json);" in exports, "V29 must reach the interact prompt host"


def test_wheel_pushes_the_prompt_through_uiclient():
    wheel = read(PLUGIN / "src" / "F4TInteractWheel.cpp")
    wheel_ui = read(PLUGIN / "src" / "F4TInteractWheelUi.cpp")
    client = read(PLUGIN / "include" / "F4TUIClient.h")
    assert "UIClient::PushInteractPrompt(g_lastPromptJson.c_str());" in wheel, (
        "the wheel must push the prompt through UIClient"
    )
    assert "PushInteractPrompt(const char* a_json);" in client, "UIClient must declare PushInteractPrompt"
    assert "IVCommonwealthOnlineUI29" in wheel_ui and "g_uiPrompt->PushInteractPrompt(a_json);" in wheel_ui, (
        "UIClient must call the V29 interface"
    )


def test_widget_files_exist_and_ci_lists_them():
    for name in ("index.html", "styles.css", "interact-prompt.js", "preview.html", "interact-prompt.test.js"):
        assert (VIEW / name).is_file(), f"interact-prompt/{name} must exist"
    ci = read(CI)
    assert "node --check CommonwealthOnlineUI/ui/views/CommonwealthOnline/interact-prompt/interact-prompt.js" in ci, (
        "co-ui-ci must syntax-check interact-prompt.js"
    )
    assert "node CommonwealthOnlineUI/ui/views/CommonwealthOnline/interact-prompt/interact-prompt.test.js" in ci, (
        "co-ui-ci must run interact-prompt.test.js"
    )


if __name__ == "__main__":
    test_v29_interface_is_declared_and_exported()
    test_wheel_pushes_the_prompt_through_uiclient()
    test_widget_files_exist_and_ci_lists_them()
    print("Interact prompt V29 push route, widget files and CI lists passed")
