import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


relay_h = read("CO-SERVER/server-cpp/include/ServerLiveRelay.h")
runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
plugin_live = read("CO-BETA/plugin/src/F4TServerLive.cpp")
ui_live = read("CommonwealthOnlineUI/src/F4TUiServerLive.cpp")
settings = read("co-core/src/config/F4TServerSettingsFile.cpp")

assert "if (_inFlight) {" in relay_h and "return Unresolved();" in relay_h, "a settings change that arrives while one is in flight must be refused as unresolved"
assert "_inFlight = pending.has_value();" in relay_h and "_inFlight = false;" in relay_h, "Drain must hold the in-flight mark until its result is set"
assert '{ "unresolved", true }' in relay_h, "the relay must mark an unresolved change in its reply data"

erase_start = runtime.index("auto eraseRuntimeConnection = [&]")
erase_body = runtime[erase_start:runtime.index("auto sendWorldStateToNewHost", erase_start)]
assert "sessionAdmissions.Cancel(found->second.admissionTicket)" in erase_body, "every runtime erase must cancel the connection's admission ticket"
assert erase_body.index("sessionAdmissions.Cancel(") < erase_body.index("runtimeConnections.erase(found);"), "the admission ticket must be cancelled before the runtime entry is erased"
assert runtime.count("sessionAdmissions.Cancel(") == 1, "the admission cancel belongs in the one shared cleanup, not in each disconnect path"

assert 'failure["data"] = nlohmann::json{ { "unresolved", true } };' in plugin_live, "an unanswered settings change must reach the panel as unresolved"
assert 'failure["data"] = *data;' in plugin_live, "a refused settings reply must carry its data to the panel"
assert re.search(r'a_error = parsed\.value\("error", std::string\{\}\);\s*a_result = parsed;', ui_live), "the panel must keep the reply when the plugin callback reports failure"

assert "ReportedUnresolved(liveResult)" in settings, "an unresolved live reply must not write or revert the file"
assert "FieldErrorTexts(revertResult)" in settings and "could not restore " in settings, "a rollback that reports field errors must name the fields it could not restore"

print("F4TServerRelayContracts: all checks passed.")
