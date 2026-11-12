# address source:E:/RE-work/clnv-fix/include/RE/IDs.h PlayerCharacter::IsPipboyLightOn
from pathlib import Path


plugin = Path(__file__).resolve().parents[1]
repo = plugin.parents[1]
send = (plugin / "src/F4TLocalPlayerTransformSend.cpp").read_text(encoding="utf-8")
reader = (plugin / "src/F4TNetworkingPacketReaders.cpp").read_text(encoding="utf-8")
internal = (plugin / "src/F4TNetworkingInternal.h").read_text(encoding="utf-8")
inbound = (plugin / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
remote = (plugin / "include/F4TRemotePlayerState.h").read_text(encoding="utf-8")
proxy_header = (plugin / "include/F4TProxyActorControllerInternal.h").read_text(encoding="utf-8")
proxy_light = (plugin / "src/F4TProxyActorControllerPipboyLight.cpp").read_text(encoding="utf-8")
movement = (plugin / "src/F4TProxyActorControllerMovement.cpp").read_text(encoding="utf-8")
lifecycle = (plugin / "src/F4TProxyActorControllerSlotLifecycle.cpp").read_text(encoding="utf-8")
resolution = (plugin / "src/F4TProxyActorControllerSlotResolution.cpp").read_text(encoding="utf-8")
transform = (repo / "co-core/src/validation/F4TTransformValidation.cpp").read_text(encoding="utf-8")
validation_header = (repo / "co-core/include/F4TPlayerStateValidation.h").read_text(encoding="utf-8")
adapter = "".join((repo / name).read_text(encoding="utf-8") for name in (
    "co-core/src/protocol/F4TGnsProtocolAdapter.cpp",
    "co-core/src/protocol/F4TGnsProtocolAdapterInternal.h",
    "co-core/src/protocol/F4TGnsProtocolAdapterImpl.h",
    "co-core/src/protocol/F4TGnsProtocolAdapterOutbound.cpp",
    "co-core/src/protocol/F4TGnsProtocolAdapterInbound.cpp",
))
server_protocol = (repo / "CO-SERVER/server-cpp/src/ServerProtocol.cpp").read_text(encoding="utf-8")
server_handlers = (repo / "CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp").read_text(encoding="utf-8")
server_tests = (repo / "CO-SERVER/server-cpp/tests/ServerProtocolTests.cpp").read_text(encoding="utf-8")
workflow_path = next((p for p in (repo / ".github/public-ci/workflows/co-contracts.yml", repo / ".github/workflows/co-contracts.yml") if p.is_file()), None)
workflow = workflow_path.read_text(encoding="utf-8") if workflow_path else ""

send_params = send.split("bool Send(", 1)[1].split(")", 1)[0]
assert "REL::ID kIsPipboyLightOn{ 426550, 2233202, 2233202 }" in send, "the Pip-Boy light state id must be the OG 426550 / NG and AE 2233202 pair"
assert "IsPipboyLightOn(*player)" in send and "TryGetLocalPlayer()" in send, "the sender must read the local player's light"
assert 'packet["pipboyLight"] = nlohmann::json{ { "on", IsPipboyLightOn(*player) }, { "color", LocalPipboyColor() } };' in send, "the transform packet must carry pipboyLight on and color"
for channel in ("R", "G", "B"):
    assert f'"fPipboyEffectColor{channel}:Pipboy"' in send, f"the local color must read the {channel} INI channel"
assert "pipboyLight" not in send_params, "Send() must not gain a pipboyLight parameter"

transform_body = transform.split("std::optional<nlohmann::json> NormalizeTransform(const nlohmann::json& a_packet)", 1)[1].split("return normalized;", 1)[0]
assert 'normalized["pipboyLight"] = Json{ { "on", on->get<bool>() }, { "color", *value } };' in transform_body, "NormalizeTransform must keep pipboyLight so the server relays it"
assert "kMaximumPipboyLightColor = 16777215.0" in transform, "the color bound must be 0 to 0xFFFFFF"
relayed_line = validation_header.split("kRelayedPlayerStateFields", 1)[1].split(";", 1)[0]
assert "pipboyLight" not in relayed_line, "pipboyLight rides the transform, not the playerState relay"
durable_line = validation_header.split("kDurablePlayerStateFields", 1)[1].split(";", 1)[0]
assert "pipboyLight" not in durable_line, "pipboyLight is not durable state"
sticky_line = adapter.split("kStickyPlayerStateFields{", 1)[1].split("};", 1)[0]
assert "pipboyLight" not in sticky_line, "pipboyLight must not be sticky"
adapter_erase = 'for (const char* field : { "equippedItems", "appearance", "actionEvents", "characterName", "powerArmor" }) {'
assert adapter_erase in adapter, "the adapter must not move pipboyLight out of the transform"

server_normalize = server_protocol.split("std::optional<StoredMovementState> NormalizeTransform(Json& a_packet)", 1)[1].split("return StoredMovementState", 1)[0]
assert "pipboyLight" not in server_normalize, "the server transform normalization must not erase pipboyLight"
assert "auto current = NormalizeTransform(a_packet);" in server_handlers, "the transform handler must normalize through ServerProtocol"
assert "SnapshotFrame(connectionId, recipient, Family::Transform, a_packet)" in server_handlers, "the transform relay must carry the normalized packet to peers"
assert 'relayedSpawn.at("pipboyLight")' in server_tests, "the server transform test must assert pipboyLight survives relay"

assert "F4T::RemotePlayerState::RemotePipboyLight ReadPipboyLight(const Json& a_packet, bool& a_hasPipboyLightUpdate)" in reader, "ReadPipboyLight must be defined in the packet readers"
assert "ReadPipboyLight(const Json& a_packet, bool& a_hasPipboyLightUpdate);" in internal, "ReadPipboyLight must be declared in F4TNetworkingInternal.h"
assert "16777215.0" in reader, "the reader must bound the color to 0 to 0xFFFFFF"
assert "ReadPipboyLight(a_packet, hasPipboyLightUpdate)" in inbound, "the inbound transform must read pipboyLight"
assert "remoteState.hasPipboyLightUpdate = hasPipboyLightUpdate;" in inbound, "the inbound transform must store the update flag"
assert "remoteState.pipboyLightOn = pipboyLight.on;" in inbound, "the inbound transform must store the on state"
assert "remoteState.pipboyLightColor = pipboyLight.color;" in inbound, "the inbound transform must store the color"
assert "struct RemotePipboyLight" in remote, "RemotePipboyLight must be declared"
for field in ("bool hasPipboyLightUpdate{ false };", "bool pipboyLightOn{ false };", "std::uint32_t pipboyLightColor{ 0 };"):
    assert field in remote, f"RemotePlayerState must declare {field}"

assert "std::uint32_t pipboyLightRefFormId{ 0 };" in proxy_header, "ProxyActorSlot must store the placed light form id"
assert "RE::TESObjectCELL* pipboyLightCell{ nullptr };" in proxy_header, "ProxyActorSlot must store the light cell"
assert "bool pipboyLightArmor{ false };" in proxy_header, "ProxyActorSlot must store the placed armor state"
assert "RE::NiAVObject* pipboyLightTintedRoot{ nullptr };" in proxy_header, "ProxyActorSlot must cache the tinted root"
assert "std::uint32_t pipboyLightTintedColor{ 0 };" in proxy_header, "ProxyActorSlot must cache the last tinted color"
assert "void ApplyProxyPipboyLightFromRemoteState(" in proxy_header, "the apply function must be declared"
assert "void RemoveProxyPipboyLight(ProxyActorSlot& a_slot);" in proxy_header, "the remove function must be declared"
assert "void ApplyProxyPipboyLightFromRemoteState(" in proxy_light, "the apply function must be defined"
assert "void RemoveProxyPipboyLight(ProxyActorSlot& a_slot)" in proxy_light, "the remove function must be defined"
assert "kPipboyLightBaseFormId = 0x0001ED2D" in proxy_light, "the light record must be defaultPipboyLight 0x0001ED2D"
assert "base->GetFormID() == kPipboyLightBaseFormId" in proxy_light, "the placed ref must match the light base before use or delete"
assert "kPipboyLightChestHeight = 95.0F" in proxy_light and "kPipboyLightHelmetHeight = 150.0F" in proxy_light, "the light sits at chest or helmet height"
assert "F4T::PowerArmorSync::ProxyReportsInPowerArmor(a_proxy)" in proxy_light, "the height follows the proxy's power armor state"
assert "CreateReferenceAtLocation(data)" in proxy_light and "F4T::ProxySaveSafety::DestroyProxyActor(*ref)" in proxy_light, "the light is placed and removed as a reference through the shared deletion path"
assert "RE/N/NiUpdateData.h" in proxy_light and "node->UpdateWorldData(&updateData)" in proxy_light, "a light that lags its reference must be moved and its world data updated"
assert "netimmerse_cast<RE::NiLight*>" in proxy_light and "light->diff" in proxy_light, "the tint must set the NiLight diffuse"
assert "ApplyProxyPipboyLightFromRemoteState(a_slot, a_proxy, a_remotePlayer);" in movement, "the per-tick move path must apply the light"
assert "ApplyProxyPipboyLightFromRemoteState" not in lifecycle, "the restore path must not place the light"
assert "RemoveProxyPipboyLight(slot);" in lifecycle, "cell release must remove the light"
assert "RemoveProxyPipboyLight(a_slot);" in lifecycle, "a held slot must remove the light"
reset_body = resolution.split("void ResetSlotForRespawn(ProxyActorSlot& a_slot)", 1)[1].split("\n\t}", 1)[0]
assert "RemoveProxyPipboyLight(a_slot);" in reset_body, "slot reset for respawn, also used by the save teardown, must remove the light"

assert "plugin/tests/F4TPipboyLightContract.py" in workflow, "the contract must be registered in the plugin contract CI"

print("pipboy light contract OK")
