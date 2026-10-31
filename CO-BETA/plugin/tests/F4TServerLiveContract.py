from pathlib import Path
import re
import sys

PLUGIN = Path(__file__).resolve().parents[1]
REPO = PLUGIN.parents[1]
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(path):
    return re.sub(r"//[^\n]*", "", (PLUGIN / path).read_text(encoding="utf-8"))


def read_root(path):
    return (REPO / path).read_text(encoding="utf-8")


source = read("src/F4TServerLive.cpp")
header = read("include/F4TServerLive.h")
session = read("src/F4TSteamSession.cpp")
api = read("include/CommonwealthOnlineUI_API.h")
host_link = read_root("co-core/include/F4TSteamHostLink.h")
server_link = read_root("CO-SERVER/server-cpp/src/SteamIdentityLink.cpp")
runtime = read_root("CO-SERVER/server-cpp/src/ServerRuntime.inc")
live_settings = read_root("CO-SERVER/server-cpp/src/ServerLiveSettings.cpp")
ui_bridge = read_root("CommonwealthOnlineUI/src/F4TServerSettingsBridge.cpp")
ui_settings = read_root("co-core/src/config/F4TServerSettingsFile.cpp")
ui_live = read_root("CommonwealthOnlineUI/src/F4TServerLive.cpp")
ui_api = read_root("CommonwealthOnlineUI/src/PluginAPI.cpp")
ui_panel = read_root("CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/server-settings.js")
inbound = read_root("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")

check('kApplySettingsCommand = "apply_settings"' in host_link and
      'kServerConfigCommand = "server_config"' in host_link,
      "the live settings and running config command names must live in the shared co-core header")
check("kApplySettingsCommand" in source and "kApplySettingsCommand" in server_link,
      "the plugin and the server must use the shared live settings command name")
check("kServerConfigCommand" in source and "kServerConfigCommand" in server_link,
      "the plugin and the server must use the shared running config command name")

check("SteamHostLink::Directory()" in source and "SteamHostLink::ReadEndpoint" in source,
      "the live path must read the published Steam host link endpoint")
check("SteamHostLink::Send" in source,
      "the live path must send through the existing host link client")
check("admin-token" not in source and "adminPort" not in source and "USERPROFILE" not in source,
      "the live path must not read the admin token or any server key path")
check("kHostLinkGamePort = 7777" in source,
      "the live path must address the same game port the Steam host link is published for")

check("RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI12>" in source,
      "the plugin must request the V12 interface that carries the live callbacks")
check("SetServerLiveCallbacks(&kCallbacks)" in source and
      "ServerLiveCallbacks kCallbacks{ ApplyServerSettings }" in source,
      "the live callback table must carry the whole-settings applier")
check("F4T::ServerLive::Attach();" in session,
      "the Steam session attach path must install the live callbacks")
check('#include "F4TServerLive.h"' in session, "the Steam session must include the live header")
check("Attach()" in header, "the live header must declare Attach")

check("V12 = 12" in api, "the UI API must append interface version 12")
check(re.search(r"struct IVCommonwealthOnlineUI12\s*\{\s*void \(\*SetServerLiveCallbacks\)", api) is not None,
      "V12 must only add SetServerLiveCallbacks; earlier versions must keep their members")
check("bool (*ApplyServerSettings)(const char* a_requestJson, char* a_resultBuffer, std::size_t a_resultBufferSize)" in api,
      "the live callback table must expose one whole-settings applier")
check("0x7A4B5C0C" in api, "the V12 layout tag must be unique to this interface")

check("Api_SetServerLiveCallbacks" in ui_api and "g_interfaceV12{ Api_SetServerLiveCallbacks }" in ui_api,
      "the UI must implement V12 with the live callback registration")
check("case InterfaceVersion::V12:" in ui_api, "the UI must serve V12 through VisitInterface")
check("F4T::ServerLive::SetCallbacks" in ui_api and "F4T::ServerLive::ApplySettings" in ui_bridge,
      "the UI bridge must apply the saved settings through the registered live callbacks")
check("g_registered" in ui_live and "callbacks->ApplyServerSettings(" in ui_live,
      "the UI live module must only call a registered settings applier")
check("QueryRunningConfig" in ui_live and "QueryRunningConfig" in ui_bridge,
      "the UI must be able to read the running server's own config path")

check("LiveSettingsApplier" in ui_settings and "RevertLiveSettings(a_liveSettings" in ui_settings,
      "the save sequence owns the live failure and the put-back through the whole-settings applier")
check("a_runningDocument" in ui_settings and "previousDocument" in ui_settings,
      "the put-back baseline is passed in from the running server, with the file as the fallback")
refusal_path = ui_settings[ui_settings.index("case LiveApplyResult::Refused"):ui_settings.index("case LiveApplyResult::Unavailable")]
check("RevertLiveSettings(a_liveSettings, baseline" in refusal_path and "a_runningDocument" in refusal_path,
      "a refusal after a partial apply must put the running server's own settings back through the same applier")
check("const auto& baseline" in refusal_path,
      "the put-back must prefer the running document over the file it may have drifted from")
check("had already applied" in refusal_path,
      "a refusal that could not be reverted must name the fields the server had already applied")
server_admin = read_root("CO-SERVER/server-cpp/src/AdminControlServer.cpp")
check('response["data"] = std::move(a_data);' in server_admin and "&a_request, result.data" in server_link,
      "the server failure reply must carry the applied fields to the plugin")
check('did not answer the settings link in time' in source and '{ "ok", false }, { "error", error }' in source,
      "a link failure must carry its own error text so it is a refusal, not an unavailable server")
check("a_result = parsed;" in ui_live,
      "the UI live call must keep the refusal reply so the save sequence can see applied fields")
check('a_result.find("data")' in ui_settings,
      "the save sequence must read the applied fields out of the server reply data")
check("liveApplied" in ui_bridge and "liveResult" in ui_bridge,
      "the save reply must report what the running server applied")

live_link = read_root("CO-SERVER/server-cpp/src/ServerLiveSettingsLink.inc")
check("ServerLiveRelay" in runtime and "serverLiveRelay.Submit(" in live_link and
      "serverLiveRelay.Drain(applyLiveSettings);" in runtime,
      "the host link must post the settings change to the main loop instead of applying it on the link thread")
check('"document"' in live_link and "ReadEditable" in live_link,
      "the config reply must carry the running document a refused change is put back to")
check((REPO / "CO-SERVER/server-cpp/include/ServerLiveRelay.h").is_file(),
      "the main-loop live settings relay must exist")
check(runtime.index("serverLiveRelay.FailPending(") < runtime.index("admin.Stop();"),
      "a waiting settings change must be failed before the admin server stops")
check("steamIdentityLink.SetApplySettingsHandler(" in live_link and "steamIdentityLink.SetServerConfigProvider(" in live_link and
      'ServerLiveSettingsLink.inc' in runtime,
      "the runtime must install the live settings handler and its own config path")
check("SetApplySettingsHandler" in server_link and "Live server settings are unavailable." in server_link,
      "the host link must answer the live settings command and say when no handler is installed")
check("server_config" not in server_link.replace("kServerConfigCommand", ""),
      "the host link must not hard-code the running config command name twice")

check("SetChatEnabled" in live_settings and "SetFactionsEnabled" in live_settings and
      "SetMaximumPlayers" in live_settings and "SetSavingEnabled" in live_settings,
      "the server must apply chat, factions, capacity and persistence live")
check("ServerInfoPacket()" in live_settings and "SetObjectStateEnabled" in live_settings,
      "the server must re-send capabilities after a live settings change")
check("ServerInfoPacket()" in live_settings and "broadcastPacket" in live_settings and
      "BroadcastWorldPacket(server, runtimeConnections, a_packet)" in runtime,
      "the server must broadcast capabilities after applying settings")
check("ApplyAdminWorldMode" in runtime and "setWorldMode = applyWorldMode" in runtime,
      "the admin world mode command must share the live world mode path")
check("SetObjectStateEnabled" in read_root("CO-SERVER/server-cpp/src/ServerWorldPolicy.inc"),
      "object state must be switchable after a session has activated")
check("SetSavingEnabled" in read_root("CO-SERVER/server-cpp/src/PersistenceGate.cpp"),
      "persistence must be switchable at runtime")

check("LiveApply::Refused" in ui_bridge and "LiveApply::Unavailable" in ui_bridge,
      "the bridge must separate a refusal from a server that could not be asked")
check("liveApplied" in ui_panel and "Applied to the running server" in ui_panel,
      "the panel must tell the host what the running server applied")
check("Restart the server to apply it" not in ui_panel,
      "the panel must not tell the host to restart for a saved setting")
check("applies on its next start" not in ui_panel,
      "the panel must not describe saved settings as next-start settings")

check('"capabilities"' in inbound and "F4T::Chat::NoteWelcome" in inbound and
      "F4T::Factions::NoteWelcome" in inbound and "F4T::ObjectSync::NoteCapabilities" in inbound,
      "the plugin must react to the server's capabilities change")
check("NoteCapabilities" in read("include/F4TObjectSync.h"),
      "the object sync capability update must be a separate entry point")

world_policy = read_root("CO-SERVER/server-cpp/src/ServerWorldPolicy.inc")
tunnel_service = read("src/F4TSteamTunnelService.cpp")
tunnel_core = read_root("co-core/src/session/F4TSteamTunnel.cpp") + read_root("co-core/src/session/F4TSteamTunnelHost.cpp")
tunnel_header = read_root("co-core/include/F4TSteamTunnel.h")
check('"maxPlayers"' in world_policy and "SetAdvertisedMaximumPlayers" in live_settings,
      "a live max players change must be advertised on the capability packet")
check("SetHostMemberLimit" in inbound and "SetHostMemberLimit" in tunnel_service,
      "the plugin must route the live max players change into the Steam tunnel host")
check("SetMaximumConnections" in tunnel_service and "SetMaximumConnections" in tunnel_core,
      "the Steam tunnel host must accept a live member limit")
check("ClampHostMaximumConnections" in tunnel_header and "ClampHostMaximumConnections" in tunnel_core,
      "the live member limit must clamp to the members already connected")

launcher = read("src/F4TLocalServerLauncher.cpp")
check((PLUGIN / "src/F4TLocalServerLauncher.cpp").exists() and (PLUGIN / "include/F4TLocalServerLauncher.h").exists(),
      "the local server launcher must stay in the plugin")
check('#include "F4TLocalServerLauncher.h"' in session, "the Steam session must include the local server launcher")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("server live settings contract ok")
