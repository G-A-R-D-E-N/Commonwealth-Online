from pathlib import Path

root = Path(__file__).resolve().parents[3]

inbound = (root / "CO-BETA/plugin/src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
shim = (root / "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")
load_order = (root / "CO-BETA/plugin/src/F4TLoadOrderSync.cpp").read_text(encoding="utf-8")

assert '#include "F4TLoadOrderSync.h"' in inbound, "the inbound dispatcher must include the load-order sync"
assert "F4T::LoadOrderSync::NoteWelcome();" in inbound, (
    "the welcome handler must announce the local plugin list and install the local translation table"
)
welcome = inbound[inbound.index("void HandleWelcomePacket") : inbound.index("void HandleWorldStateHostPacket")]
assert "F4T::RemotePlayerState::SetAssignedPlayerId(*playerId);" in welcome
assert welcome.index("F4T::RemotePlayerState::SetAssignedPlayerId(*playerId);") < welcome.index("F4T::LoadOrderSync::NoteWelcome();"), (
    "the plugin list and translation table are installed after the player id is known, matching "
    "source:CO-BETA/plugin/src/F4TNetworkingInbound.cpp:HandleWelcomePacket which stamps playerId first"
)

dispatch = inbound[inbound.index("void HandlePacketLine") :]
translate = dispatch.index("F4T::LoadOrderSync::TranslateInboundPacket(packet);")
assert dispatch.index('const auto packetType = ReadString(packet, "type");') < translate, (
    "every inbound packet is translated after its type is read"
)
assert translate < dispatch.index('if (*packetType == "welcome")'), (
    "translation runs before dispatch so every handler sees local IDs, matching "
    "source:CO-BETA/plugin/src/F4TLoadOrderSync.cpp:TranslateInboundPacket"
)
assert 'else if (*packetType == "pluginList")' in dispatch, "the pluginList packet must reach its handler"
assert "F4T::LoadOrderSync::TryHandlePacket(packet);" in dispatch

disconnect = inbound[inbound.index("void HandleDisconnectPacket") : inbound.index("void HandleSessionEndedPacket")]
assert "F4T::LoadOrderSync::Forget(*playerId);" in disconnect, (
    "a leaving player's translation table must be dropped with its remote-player state"
)

assert '#include "F4TLoadOrderSync.h"' in shim
assert "state->protocol.SetFormIdTranslator(" in shim, (
    "the GNS shim must install the translator on the protocol session"
)
assert "F4T::LoadOrderSync::ToLocal(a_playerId, a_formId)" in shim, (
    "NPC authority scopes are translated through the same load-order registry, "
    "source:co-core/src/identity/F4TLoadOrder.cpp:Registry::ToLocal"
)
reset = shim.index("state->protocol.Reset();")
assert reset < shim.index("state->protocol.SetFormIdTranslator("), (
    "Reset clears the translator, source:co-core/src/protocol/F4TGnsProtocolAdapter.cpp:Session::Reset, "
    "so the translator must be installed after it on every connect"
)

for fragment in ("Known(*playerId)", "Identical(*playerId)", "TranslateNode", "g_registry.ToLocal"):
    assert fragment in load_order, f"F4TLoadOrderSync.cpp must keep {fragment}"

for compile_time_switch in ("#if", "F4T_LOAD_ORDER"):
    assert compile_time_switch not in inbound, "load-order translation must always be on, with no compile-time switch"
    assert compile_time_switch not in shim, "the FormID translator must always be installed, with no compile-time switch"

networking = (root / "CO-BETA/plugin/src/F4TNetworking.cpp").read_text(encoding="utf-8")
assert networking.count("F4T::LoadOrderSync::Reset();") == 2, (
    "both connection-reset blocks must clear the load-order registry beside ClearRemotePlayers, "
    "source:CO-BETA/plugin/src/F4TNetworking.cpp"
)

print("Load-order translation integration contract tests passed")
