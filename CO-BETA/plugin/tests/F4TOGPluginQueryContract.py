from pathlib import Path

root = Path(__file__).resolve().parents[3]
templates = {
    "plugin": root / "CO-BETA/plugin/res/commonwealthonline-plugin.cpp.in",
    "ui": root / "CommonwealthOnlineUI/res/commonwealthonlineui-plugin.cpp.in",
}

for label, path in templates.items():
    text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    assert "F4SE_PLUGIN_VERSION" in text, f"{label}: NG/AE F4SE needs F4SEPlugin_Version"
    assert "F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)" in text, \
        f"{label}: OG F4SE 0.6.x skips plugins without F4SEPlugin_Query"
    assert "a_info->infoVersion = F4SE::PluginInfo::kVersion;" in text, f"{label}: Query must fill PluginInfo"
    assert "a_f4se->RuntimeVersion() == F4SE::RUNTIME_1_10_163" in text, \
        f"{label}: Query must accept only the OG runtime"

plugin = templates["plugin"].read_text(encoding="utf-8").replace("\r\n", "\n")
query = plugin.index("F4SE_PLUGIN_QUERY")
assert plugin.rfind("#if defined(F4T_TARGET_OG)", 0, query) > plugin.rfind("#endif", 0, query), \
    "plugin: the Query export belongs to the OG target only"

print("OG plugin query contract passed")
