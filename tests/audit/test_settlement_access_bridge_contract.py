from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


client_h = read("CO-BETA/plugin/include/F4TSettlementAccessClient.h")
client = read("CO-BETA/plugin/src/F4TSettlementAccessClient.cpp")
inbound = read("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")
ui_client = read("CO-BETA/plugin/src/F4TUIClient.cpp")
faction_view = read("CommonwealthOnlineUI/src/F4TFactionView.cpp")
factions = read("CO-BETA/plugin/src/F4TFactions.cpp")
identity = read("CO-BETA/plugin/src/F4TClientIdentity.cpp")
identity_h = read("CO-BETA/plugin/include/F4TClientIdentity.h")
networking = read("CO-BETA/plugin/src/F4TNetworking.cpp")
ui_api = read("CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h")
plugin_api = read("CO-BETA/plugin/include/CommonwealthOnlineUI_API.h")
workflow = read(".github/workflows/co-audit-regressions.yml")
faction_js = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/faction.js")
build_js = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/faction.build.js")
index_html = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/index.html")
ui_workflow = read(".github/workflows/co-ui-ci.yml")

# Outbound packets must use the exact names and fields from packets.md.
assert '"settlementAccessGet"' in client, "get request must use type settlementAccessGet"
assert '"settlementAccessUpdate"' in client, "update request must use type settlementAccessUpdate"
assert '"buildZoneId"' in client, "requests must carry buildZoneId"
assert '"targetProfileId"' in client, "updates must carry targetProfileId"
assert '"grant"' in client and '"revoke"' in client, "updates must map to op grant/revoke"
assert "buildAccessGet" in client, "the bridge must accept op buildAccessGet"
assert "buildAccessGrant" in client, "the bridge must accept op buildAccessGrant"
assert "buildAccessRevoke" in client, "the bridge must accept op buildAccessRevoke"

# Inbound packets must use the exact names and fields from packets.md.
assert '"settlementAccessState"' in client, "the client must handle settlementAccessState"
assert '"settlementAccessReject"' in client, "the client must handle settlementAccessReject"
assert '"ownerProfileId"' in client, "stored state must keep ownerProfileId"
assert '"allowedProfileIds"' in client, "stored state must keep allowedProfileIds"
assert '"lastReject"' in client, "stored state must surface the last reject for the UI"
for status in ("invalidRequest", "unknownBuildZone", "notOwner", "alreadyAllowed",
               "notAllowed", "full", "rateLimited", "persistenceUnavailable"):
    assert f'"{status}"' in client, f"reject status {status} must be recognized"
for gone in ("settlementLimit", "committed"):
    assert f'"{gone}"' not in client, f"unreachable status {gone} must not be accepted"
assert 'op == "refresh"' in client and "RefreshStoredZones" in client, \
    "opening the view must re-request every stored zone with the existing Get"
assert "ownerChanged" in client, "an owner-changed push must re-request that zone"
assert "No settlements you own yet" in build_js, "the empty state must name owned settlements"
assert "knownNames" in build_js, "names must fall back to last-known session names"
for gone in ("settlementLimit", "committed"):
    assert gone not in build_js, "unreachable status " + gone + " must not render"

# The receive loop must offer server packets to the new module.
assert '#include "F4TSettlementAccessClient.h"' in inbound
assert "F4T::SettlementAccessClient::TryHandlePacket(*packetType, packet)" in inbound
assert inbound.index("SettlementAccessClient::TryHandlePacket") < inbound.index("HandleUnknownPacket(packet)"), \
    "settlement packets must be offered before the unknown-packet path"

# The UI bridge must route actions, embed stored state, and reset on disconnect.
assert "F4T::SettlementAccessClient::HandleAction(a_actionJson)" in ui_client
assert "F4T::SettlementAccessClient::WithBuildAccess(F4T::Factions::BuildStateJson())" in ui_client
assert "F4T::SettlementAccessClient::Reset();" in ui_client
assert '"buildAccessGet"' in faction_view, "the faction view must let buildAccessGet through"
assert '"buildAccessGrant"' in faction_view, "the faction view must let buildAccessGrant through"
assert '"buildAccessRevoke"' in faction_view, "the faction view must let buildAccessRevoke through"

# Constraint: no new UI capability, the existing faction bridge carries build access.
assert "V17" not in ui_api and "V17" not in plugin_api, "no interface beyond the interact wheel V16"
capture_slot = ui_api.split("struct IVCommonwealthOnlineUI15", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI15" in ui_api else ""
assert "V15" not in ui_api or ("PushCaptureHud" in capture_slot and "BuildAccess" not in capture_slot), "V15 may only be the capture HUD slot, never build access"
assert ui_api == plugin_api, "the UI API header copies must stay byte-identical"

# Capped files must not grow; the new module must stay under the limit.
def lines(text):
    return text.count("\n") + (0 if text.endswith("\n") else 1)

assert lines(client) < 750, "the new client module must stay under 750 lines"
assert lines(factions) <= 750, "F4TFactions.cpp must not grow past 750 lines"
assert lines(ui_client) < 750, "F4TUIClient.cpp must stay under 750 lines"
assert lines(inbound) < 750, "F4TNetworkingInbound.cpp must stay under 750 lines"
assert lines(faction_view) < 750, "F4TFactionView.cpp must stay under 750 lines"

# The new module must ride the existing client sources glob, not a new .inc.
assert "src/F4TSettlementAccessClient.cpp" not in read("CO-BETA/plugin/xmake.lua"), \
    "the client glob already covers src/**.cpp; do not list the file explicitly"

# The Get refresh must stay inside the server bucket (1 per second, burst 4) and the
# state must stay inside the 32 KiB faction buffer.
assert "kMaximumRefreshGets" in client and "kMinimumGetInterval" in client, \
    "the refresh must pace and cap its settlementAccessGet requests"
assert "g_nextGetAllowed = now + kMinimumGetInterval * static_cast<std::chrono::seconds::rep>(count)" in client, \
    "a multi-zone refresh must reserve one second per request it sends"
assert "QueueGetRequest" in client and client.count("g_nextGetAllowed") >= 4, \
    "single get paths must share one reservation window with the refresh"
assert "kRejectDisplayInterval" in client, "a stale reject banner must expire"
assert client.count("QueuePacket(Json{ { \"type\", \"settlementAccessGet\" }") >= 2, \
    "the refresh and the single-zone path must both re-request through settlementAccessGet"
assert "kStateOutputBudgetBytes" in client, "build access must respect the faction state buffer budget"
assert 'state["selfProfileId"] = F4T::ClientIdentity::ConnectedProfileId()' in client, \
    "the plugin must report the local profile id so an owner outside a faction can manage"
assert "ClientIdentity::ClearConnectedProfileId()" in client, "the profile id must reset on disconnect"
assert "ConnectedProfileId" in identity_h and "LookupProfileId(a_serverKey)" in identity, \
    "the connected profile id must come from the accepted connection's profile store entry"
assert "a_identity.profileId" in identity, \
    "the accepted ready identity carries the authoritative profile id"
assert "ClearConnectedProfileId()" in identity.split("void ClearConnectionIdentity()")[1][:400], \
    "the identity module must clear its own cached profile id"
assert networking.count("F4T::SettlementAccessClient::Reset();") == 2, \
    "both local teardown paths must reset the build access store"
assert "F4T::SettlementAccessClient::Reset();" in networking.split("bool ConnectToServerInternal")[1][:3000], \
    "the connect teardown must reset the previous server's build access state"
assert "build_access_send_offline" in client, "a packet dropped while offline must be logged"
assert "build_access_state_dump" in client, "a failed state merge must be logged"

assert "run: python3 tests/audit/test_settlement_access_bridge_contract.py" in workflow, \
    "CI must run this contract"

# The Social view must send the exact bridge ops and render real server state.
for op in ('"buildAccessGet"', '"buildAccessGrant"', '"buildAccessRevoke"'):
    assert op in faction_js, "faction.js actionFor must build " + op
    assert op in build_js, "faction.build.js must render and send " + op
assert "coBuildAccessTarget" in build_js and "coBuildAccessTarget" in faction_js
assert "coFactionBuild.buildSection" in faction_js, "renderState must include the Build access section"
assert '<script src="faction.build.js"></script>' in index_html
assert "faction.build.test.js" in ui_workflow, "CI must run the Build access panel tests"
assert "Owner:" in build_js and "You cannot build here" in build_js, "read-only non-owned line must render"
assert "rateLimited" in build_js, "rate-limited replies must render visible text"
assert "selfProfileId" in build_js, "the view must prefer the plugin-reported profile id"
assert "truncated" in build_js, "a state truncated by the buffer budget must be disclosed"
assert "{8,128}" in build_js and "A-Za-z0-9._:@-" in build_js, \
    "the profile id field must mirror the server's ValidProfileId range and charset"
assert "buildAccessTargets" in faction_js, "typed profile ids must stay per settlement"
assert "resetCache" in build_js and "coFactionBuild.resetCache()" in faction_js, \
    "the name cache must be dropped when the connection state resets"
assert "Number.isSafeInteger" in build_js, "a build zone id must be a safe JS integer before it is sent"
for name in ("faction.js", "faction.build.js", "faction.build.test.js", "faction.harness.js"):
    path = "CommonwealthOnlineUI/ui/views/CommonwealthOnline/faction/" + name
    assert lines(read(path)) < 750, name + " must stay under 750 lines"

print("test_settlement_access_bridge_contract: all checks passed.")
