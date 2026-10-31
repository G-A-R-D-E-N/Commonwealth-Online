from pathlib import Path


root = Path(__file__).resolve().parents[3]

lifecycle_h = (root / "co-core/include/F4TSessionLifecycle.h").read_text(encoding="utf-8")
lifecycle_cpp = (root / "co-core/src/session/F4TSessionLifecycle.cpp").read_text(encoding="utf-8")
admission_h = (root / "co-core/include/F4TAdmission.h").read_text(encoding="utf-8")
admission_cpp = (root / "co-core/src/session/F4TAdmission.cpp").read_text(encoding="utf-8")
protocol_cpp = (root / "CO-SERVER/server-cpp/src/ServerProtocol.cpp").read_text(encoding="utf-8")
protocol_impl = (root / "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc").read_text(encoding="utf-8")
protocol_handlers = (root / "CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp").read_text(encoding="utf-8")
world_policy_inc = (root / "CO-SERVER/server-cpp/src/ServerWorldPolicy.inc").read_text(encoding="utf-8")
welcome_cpp = (root / "CO-SERVER/server-cpp/src/ServerWelcome.cpp").read_text(encoding="utf-8")
runtime = (root / "CO-SERVER/server-cpp/src/ServerRuntime.inc").read_text(encoding="utf-8")
maintenance = (root / "CO-SERVER/server-cpp/src/ServerMaintenance.inc").read_text(encoding="utf-8")
adapter = (root / "co-core/src/protocol/F4TGnsProtocolAdapter.cpp").read_text(encoding="utf-8")
inbound = (root / "CO-BETA/plugin/src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
message_types = (root / "CO-BETA/docs/protocol/message-types.md").read_text(encoding="utf-8")
packets = (root / "CO-BETA/docs/protocol/packets.md").read_text(encoding="utf-8")
ui_api = (root / "CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
ui_plugin_api = (root / "CommonwealthOnlineUI/src/PluginAPI.cpp").read_text(encoding="utf-8")
ui_facade = (root / "CommonwealthOnlineUI/src/F4TServerBrowserBridgeFacade.cpp").read_text(encoding="utf-8")
session_state_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/session-state.js").read_text(encoding="utf-8")
client_identity = (root / "CO-BETA/plugin/src/F4TClientIdentity.cpp").read_text(encoding="utf-8")
browser_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/browser.js").read_text(encoding="utf-8")
plugin_api = (root / "CO-BETA/plugin/include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")

for state in (
    "Connecting",
    "Connected",
    "Ready",
    "CharacterLoaded",
    "SpaceChange",
    "Disconnected",
):
    assert state in lifecycle_h

for symbol in ("ToWireName", "IsSpawnable", "CanTransition", "Transition", "LifecycleTracker"):
    assert symbol in lifecycle_h

assert "a_state == LifecycleState::Ready" in lifecycle_cpp
assert "a_state == LifecycleState::CharacterLoaded" in lifecycle_cpp
assert "a_state == LifecycleState::SpaceChange" in lifecycle_cpp
assert "a_to == LifecycleState::Connected" in lifecycle_cpp
assert "a_to == LifecycleState::Ready" in lifecycle_cpp

assert "sessionState" in protocol_cpp
assert "session-lifecycle-v1" in welcome_cpp
assert "SendLifecycleState(a_connectionId, a_session, F4T::Session::LifecycleState::Connected" in protocol_handlers
assert 'Has("session-lifecycle-v1")' in protocol_cpp
assert 'if (!result.accepted || !a_session.compatibilityCapabilities.Has("session-lifecycle-v1"))' in protocol_cpp
assert "sessionState" not in protocol_impl[protocol_impl.index("Json welcome"):protocol_impl.index("std::vector<OutboundFrame> HandleDisconnected")]
assert "F4T::Session::LifecycleState::Ready" in protocol_handlers
assert "LifecycleFramesForMovement" in protocol_handlers
assert "LifecycleState::Disconnected" in protocol_impl
assert "BaseWelcomeCapabilities(" in world_policy_inc
assert "Json capabilities = BuildCapabilities();" in protocol_impl
assert protocol_impl.index("Json capabilities = BuildCapabilities();") < protocol_impl.index("Json welcome")

assert 'if (type && *type == "sessionState")' in adapter
assert adapter.index('*type == "sessionState"') < adapter.index('server sent gameplay traffic before Protocol V2 sessionReady')

assert "sessionState" in message_types
assert "session-lifecycle-v1" in message_types
assert "### Session State Packet" in packets
assert "session-lifecycle-v1" in packets

assert "DefaultAdmissionTimeoutSeconds = 30.0" in admission_h
assert "ticketId" in admission_h
assert "Claim(" in admission_h
assert "Update(" in admission_h
assert "Reject(" in admission_h
assert "Sweep(" in admission_h
assert "a_nowUnixSeconds + _timeoutSeconds" in admission_cpp
assert "_timedOut" in admission_cpp
assert "_lifetimeSeconds" in admission_cpp
assert "LastRejectReason" in admission_h
assert "F4T::Session::LifecycleState::Connected" in protocol_handlers[protocol_handlers.index("std::vector<OutboundFrame> ProtocolHost::Impl::HandleHello("):]
assert '"session-lifecycle-v1"' in client_identity
assert 'if (!state.sessionLifecycleSeen) hideJoinOverlay();' in browser_js
assert "admissionAlive" in runtime

assert "sessionAdmissions.Begin" in runtime
assert "sessionAdmissions.Update" in runtime
assert "sessionAdmissions.Claim" in runtime
assert "sessionAdmissions.Commit" in runtime
assert "sessionAdmissions.Reject" in runtime
assert "admission_timeout" in maintenance
assert "sessionAdmissions.IsExpired" in maintenance
assert "sessionAdmissions.Sweep" in maintenance
assert runtime.index("sessionAdmissions.Begin") < runtime.index("runtimeConnections[event.connection_id] = RuntimeConnectionState{")
assert runtime.index("sessionAdmissions.Commit") > runtime.index("FramesContainType(frames, event.connection_id, \"sessionReady\")")

assert "steamId" in runtime
assert "nickname" in runtime
assert "PushSessionState" in ui_api
assert "InterfaceVersion::V6" in ui_api
assert "Api_PushSessionState" in ui_plugin_api
assert "DispatchEventToView(state)" in ui_facade
assert "InterfaceVersion::V6" in ui_plugin_api
assert "CO_SessionState" in session_state_js
assert 'event.state === "ready"' in session_state_js
assert "void HandleSessionStatePacket(const Json& a_packet)" in inbound
assert "HandleSessionStatePacket(packet);" in inbound
assert "ui->PushSessionState(text.c_str());" in inbound
assert "IVCommonwealthOnlineUI6>();" in inbound
assert '*packetType == "sessionState"' in inbound
assert inbound.index('*packetType == "welcome"') < inbound.index('*packetType == "sessionState"') < inbound.index('*packetType == "transform"')
assert "struct IVCommonwealthOnlineUI6" in plugin_api
assert "V6 = 6" in plugin_api
