import re
from pathlib import Path


root = Path(__file__).resolve().parents[3]


def read(relative):
    return (root / relative).read_text(encoding="utf-8")


main = read("CO-BETA/plugin/src/main.cpp")
alert = read("CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp")
probe = read("CO-BETA/plugin/src/F4TClientModpackProbe.cpp")
ui_client = read("CO-BETA/plugin/src/F4TUIClient.cpp")
client = read("CO-BETA/plugin/src/F4TModpackAutoFixClient.cpp")
transport = read("CO-BETA/plugin/src/F4TGnsModpackDownload.cpp")

load = main[main.index("F4SE_PLUGIN_LOAD("):]
assert "F4T::ModpackAutoFixClient::OnPluginLoad();" in load
assert load.index("F4T::DebugLogging::Initialize();") < load.index("F4T::ModpackAutoFixClient::OnPluginLoad();") < load.index("F4T::UIClientModpackProgress::Install();")
assert re.search(r"case F4SE::MessagingInterface::kGameDataReady:(?:(?!break;).)*F4T::ModpackAutoFixClient::OnGameDataReady\(\);", main, re.S)

assert 'code == "modpack_mismatch" && F4T::ModpackAutoFixClient::CanOffer(serverKey)' in alert
assert "hasClearResume || hasModpackUrl || hasAutoFix" in alert
assert "F4T::ModpackAutoFixClient::Begin(boundServerKey);" in alert
assert "AlertAction, 4> actions" in alert

assert re.search(r"!probed\.truncated && probed\.loadOrderKnown && a_rejection\.code == \"modpack_mismatch\"\)\s*\{\s*F4T::ModpackAutoFixClient::NoteMismatch", probe)
assert 'connectionFailure->code == "modpack_mismatch"' in ui_client
assert "F4T::ModpackAutoFixClient::NoteEndpoint(serverKey, host, port, id, name, serverPassword);" in ui_client

apply_at_load = client[client.index("void OnPluginLoad()"):client.index("void OnGameDataReady()")]
assert apply_at_load.index("installer.Recover()") < apply_at_load.index("installer.ApplyPending(")
assert apply_at_load.index("IsElevated()") < apply_at_load.index("installer.ApplyPending(")
assert apply_at_load.index("DetectManager(") < apply_at_load.index("installer.ApplyPending(")
ready = client[client.index("void OnGameDataReady()"):]
assert "installer.ConfirmVerified()" in ready
assert "F4T::ServerProfileStore::LookupProfileId(result->serverKey).has_value()" in ready
assert '"modpack_install", "Install on Restart"' in client
assert "manager == ModpackInstall::ModManager::none" in client
assert "ModpackDownload::BuildConsentPlan(" in client
assert "ModpackAutoFix::HasRoomFor(" in client

assert "WipeServerPassword(text);" in transport
assert "ModpackDownload::ParseEndPacket(*packet)" in transport
assert "totalBytes" not in transport

print("F4TModpackAutoFixContract: all checks passed.")
