from pathlib import Path

root = Path(__file__).resolve().parents[3]

ui_header = (root / "CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
plugin_header = (root / "CO-BETA/plugin/include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
plugin_api = (root / "CommonwealthOnlineUI/src/PluginAPI.cpp").read_text(encoding="utf-8")
bridge_header = (root / "CommonwealthOnlineUI/include/F4TServerBrowserBridge.h").read_text(encoding="utf-8")
bridge = (root / "CommonwealthOnlineUI/src/F4TServerBrowserBridgeFacade.cpp").read_text(encoding="utf-8")
layout_test = (root / "CO-BETA/plugin/tests/F4TUIApiLayoutTests.cpp").read_text(encoding="utf-8")
budget_header = (root / "co-core/include/F4TModpackProbeBudget.h").read_text(encoding="utf-8")
budget_source = (root / "co-core/src/manifest/F4TModpackProbeBudget.cpp").read_text(encoding="utf-8")
budget_tests = (root / "co-core/tests/F4TModpackProbeBudgetTests.cpp").read_text(encoding="utf-8")
probe_header = (root / "CO-BETA/plugin/include/F4TClientModpackProbe.h").read_text(encoding="utf-8")
probe = (root / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
progress_header = (root / "CO-BETA/plugin/include/F4TUIClientModpackProgress.h").read_text(encoding="utf-8")
progress_source = (root / "CO-BETA/plugin/src/F4TUIClientModpackProgress.cpp").read_text(encoding="utf-8")
main_source = (root / "CO-BETA/plugin/src/main.cpp").read_text(encoding="utf-8")

assert ui_header == plugin_header, "the UI and plugin API headers must stay byte-identical"
for fragment in ("IVCommonwealthOnlineUI13", "PushJoinProgress", "V13 = 13", "0xE6A14D0D"):
    assert fragment in ui_header, f"the UI API must define {fragment}"
assert "Api_PushJoinProgress" in plugin_api and "g_interfaceV13" in plugin_api
assert "case InterfaceVersion::V13:" in plugin_api, "the checked API export must serve V13"
assert "CheckLayoutGate<IVCommonwealthOnlineUI13>()" in layout_test, "the V13 layout gate must be tested"
assert "PushJoinProgress" in bridge_header and "ServerBrowserBridge::PushJoinProgress" in bridge
assert '{ "type", "joinLoading" }' in bridge, "progress must reuse the join overlay's joinLoading event"

assert "enum class ProbeBudgetKind" in budget_header and "ProbeBudgetLimit" in budget_header
assert "ProbeBudgetKind::entries" in budget_source and "ProbeBudgetKind::elapsedMilliseconds" in budget_source
assert "TestBudgetLimitNamesTheFirstLimit" in budget_tests

for fragment in ("ProbeProgress", "ProbeProgressCallback", "SetProbeProgressCallback", "budgetLimit", "checkedEntries", "totalEntries"):
    assert fragment in probe_header, f"the probe header must expose {fragment}"
for fragment in ("ReportProgress", "kProgressInterval", "FormatBudgetDetail", "ProbeBudgetLimit(run.budget)", "ProbeBudgetKind::hashedBytes"):
    assert fragment in probe, f"the probe must implement {fragment}"
assert "200" in probe and "kProgressInterval = std::chrono::milliseconds" in probe, (
    "progress updates must be throttled to about five per second"
)
assert "throttled && !(a_total > 0 && a_checked >= a_total)" in probe, (
    "the final progress update (checked == total) must bypass the throttle"
)
assert "Ask the server owner for a smaller manifest" in probe, (
    "a budget overrun must say which limit was hit instead of reporting nothing"
)

assert "void Install()" in progress_header
assert "IVCommonwealthOnlineUI13" in progress_source and "SetProbeProgressCallback" in progress_source
assert "ui == nullptr || ui->PushJoinProgress == nullptr" in progress_source, (
    "an older UI DLL without V13 must return null and the progress text is skipped, never dereferenced"
)
assert "Checking your modpack" in progress_source
assert "F4T::UIClientModpackProgress::Install();" in main_source, "the plugin must install the progress forwarder"

print("Modpack probe progress contract tests passed")
