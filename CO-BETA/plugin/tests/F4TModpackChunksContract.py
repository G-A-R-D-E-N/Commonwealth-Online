from pathlib import Path

root = Path(__file__).resolve().parents[3]

core_header = (root / "co-core/include/F4TModpackChunks.h").read_text(encoding="utf-8")
core_source = (root / "co-core/src/manifest/F4TModpackChunks.cpp").read_text(encoding="utf-8")
core_tests = (root / "co-core/tests/F4TModpackChunksTests.cpp").read_text(encoding="utf-8")
core_xmake = (root / "co-core/xmake.lua").read_text(encoding="utf-8")
core_tests_xmake = (root / "co-core/xmake-tests.lua").read_text(encoding="utf-8") + (root / "co-core/xmake-tests-modpack.lua").read_text(encoding="utf-8")
identity_header = (root / "co-core/include/F4TIdentityProtocol.h").read_text(encoding="utf-8")
identity_source = (root / "co-core/src/session/F4TIdentityProtocol.cpp").read_text(encoding="utf-8")
identity_tests = (root / "co-core/tests/F4TIdentityProtocolTests.cpp").read_text(encoding="utf-8")
adapter_header = (root / "co-core/include/F4TGnsProtocolAdapter.h").read_text(encoding="utf-8")
adapter_source = (root / "co-core/src/protocol/F4TGnsProtocolAdapter.cpp").read_text(encoding="utf-8")
server_runtime = (root / "CO-SERVER/server-cpp/src/ServerRuntime.inc").read_text(encoding="utf-8")
server_ended = (root / "CO-SERVER/server-cpp/src/ServerSessionEnded.inc").read_text(encoding="utf-8")
server_support = (root / "CO-SERVER/server-cpp/include/ServerRuntimeSupport.h").read_text(encoding="utf-8")
server_gate = (root / "CO-SERVER/server-cpp/src/IdentityGate.cpp").read_text(encoding="utf-8")
server_gate_header = (root / "CO-SERVER/server-cpp/include/IdentityGate.h").read_text(encoding="utf-8")
server_config = (root / "CO-SERVER/server-cpp/src/ServerConfiguration.cpp").read_text(encoding="utf-8")
server_config_tests = (root / "CO-SERVER/server-cpp/tests/ServerConfigurationTests.cpp").read_text(encoding="utf-8")
server_chunks = (root / "CO-SERVER/server-cpp/src/ServerManifestChunks.cpp").read_text(encoding="utf-8")
server_xmake = (root / "CO-SERVER/server-cpp/xmake.lua").read_text(encoding="utf-8")
identity_client = (root / "CO-BETA/plugin/src/F4TClientIdentity.cpp").read_text(encoding="utf-8")
fetch = (root / "CO-BETA/plugin/src/F4TGnsManifestFetch.cpp").read_text(encoding="utf-8")
frames = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimFrames.cpp").read_text(encoding="utf-8")
shim_state = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimState.h").read_text(encoding="utf-8")
shim = (root / "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")
shim_header = (root / "CO-BETA/plugin/include/F4TGnsWinsockShim.h").read_text(encoding="utf-8")
server_protocol = (root / "CO-SERVER/server-cpp/src/ServerProtocolImpl.h").read_text(encoding="utf-8")
plugin_internal = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimInternal.h").read_text(encoding="utf-8")
bridge = (root / "CO-BETA/plugin/gns-bridge/co_gns_client_bridge.cpp").read_text(encoding="utf-8")
manifest_header = (root / "co-core/include/F4TModpackManifest.h").read_text(encoding="utf-8")

for fragment in ("kCapabilityName", "kChunkPayloadBytes = 32 * 1024", "kMaximumTotalBytes = 2 * 1024 * 1024",
                 "kMaximumChunks", "kChunkPacketType", "kEndPacketType", "kRequestField", "Reassembler", "Split", "Describe"):
    assert fragment in core_header, f"F4TModpackChunks.h must define {fragment}"
assert ("EncodeBase64" in core_source and "DecodeBase64" in core_source and "AddResult::outOfOrder" in core_source
        and "expected.sha256" in core_source)
for fragment in ("TestSplitBoundaries", "TestMissingChunk", "TestDuplicateChunk", "TestOutOfOrderChunk",
                 "TestBadIndexAndOversize", "TestBadHashAndTotal", "TestBase64RoundTrip"):
    assert fragment in core_tests, f"the chunk unit tests must cover {fragment}"
assert "tests/F4TModpackChunksTests.cpp" in core_tests_xmake
assert "src/manifest/F4TModpackChunks.cpp" in core_xmake

assert "modpackChunks" in identity_header and "modpackFingerprint" in identity_header
assert "F4T::ModpackChunks::Describe(manifestText)" in identity_source
assert "Update Commonwealth Online" in identity_source, "an old client must be told to update"
assert "chunkCapable" in identity_tests and "oldClientRejection" in identity_tests, (
    "the identity tests must cover the chunked and old-client rejections"
)
assert "ModpackChunksInfo" in adapter_header
adapter_modpack = (root / "co-core/src/protocol/F4TGnsProtocolAdapterModpack.cpp").read_text(encoding="utf-8")
assert "ReadRequiredModpackChunks" in adapter_modpack
assert "ReadRequiredModpackName" in adapter_modpack and '"requiredModpackName"' in adapter_modpack
assert '"requiredModpackFingerprint"' in adapter_modpack and '"requiredModpackChunks"' in adapter_modpack
assert "Detail::ReadRequiredModpackFingerprint" in adapter_source and "Detail::ReadRequiredModpackChunks" in adapter_source
assert "tests" not in adapter_source or "F4TGnsProtocolAdapterModpack" in adapter_source

assert "modpackFingerprint" in server_gate_header and "modpackChunks" in server_gate_header
assert "decision.modpackChunks = preflight.modpackChunks" in server_gate
assert "decision.modpackChunks = result.modpackChunks" in server_gate
assert '"requiredModpackFingerprint"' in server_ended and '"requiredModpackChunks"' in server_ended
assert "decision.modpackFingerprint" in server_runtime and "decision.modpackChunks" in server_runtime
assert "decision.modpackName" in server_runtime and '"requiredModpackName"' in server_ended
assert "ServeManifestChunksIfRequested" in server_runtime and "ServeManifestChunksIfRequested" in server_chunks
assert "kChunkPacketType" in server_chunks and "kEndPacketType" in server_chunks and "kReliable" not in server_chunks
assert "kMaximumAdmissionManifestBytes" not in server_config, (
    "the server must no longer refuse a manifest over the admission frame at startup"
)
assert "large.json" in server_config_tests and "huge.json" in server_config_tests
assert "src/ServerManifestChunks.cpp" in server_xmake
assert "AdvanceManifestChunkQueue" in server_chunks and "ManifestChunkQueue" in server_chunks
assert "tests/ServerManifestChunkQueueTests.cpp" in server_xmake, (
    "the chunk delivery state machine needs its behavioral unit test"
)

assert server_chunks.count("F4T::ModpackChunks::Split(") == 1 and server_chunks.count("BuildManifestChunkPackets(") == 2, (
    "the chunk packets must be built only by BuildManifestChunkPackets"
)
assert "std::shared_ptr<const ManifestChunkPackets> g_packets" in server_chunks, "the chunk packets must be shared, not copied per delivery"
assert (server_chunks.index("if (!g_packets || g_packets->fingerprint != requiredFingerprint)")
        < server_chunks.index("g_packets = BuildManifestChunkPackets(requiredFingerprint")
        < server_chunks.index("delivery.packets = g_packets")), (
    "a delivery must reuse the cached chunk packets and rebuild them only when the fingerprint changes"
)

assert "F4T::ModpackChunks::kCapabilityName" in identity_client, "the client must advertise the chunk capability"
assert "FetchManifest" in fetch and "Reassembler" in fetch and "kRequestField" in fetch
assert "GnsManifestFetch::FetchManifest" in frames
assert "cancelRequested" in shim_state, "the shim socket must carry a cancellation flag"
assert "Shutdown(SOCKET a_socket, int a_how) noexcept" in shim_header and "#define shutdown(" in shim_header, (
    "the shim must route shutdown() so cancelling a join can interrupt the fetch"
)
assert "int Shutdown(SOCKET a_socket, int) noexcept" in shim and "cancelRequested.store(true, std::memory_order_release)" in shim, (
    "the shim Shutdown and CloseSocket must request cancellation"
)
assert "cancelRequested" in fetch, "FetchManifest must poll the cancellation flag"
assert "event.payloadSize > payload.size()" in fetch, "FetchManifest must bound the received payload before parsing it"
assert "index->get<std::uint64_t>() >= a_chunks.chunkCount" in fetch, (
    "FetchManifest must range-check the chunk index before the uint32 cast"
)
assert "std::addressof(a_state->cancelRequested)" in frames, "the rejection path must pass the shim cancellation flag"
assert "host" in shim_state and "port" in shim_state
assert '"serverPassword"' in fetch and "a_serverPassword.empty()" in fetch, (
    "FetchManifest must send the join attempt's serverPassword in its hello, and only when one is held"
)
assert "state->serverPassword = identity->serverPassword" in shim and "WipeServerPassword(state->serverPassword)" in shim, (
    "the held password must come from the connection identity and be wiped once the session is ready"
)
assert "WipeServerPassword(a_state->serverPassword)" in frames and "a_state->serverPassword," in frames, (
    "the rejection path must hand the password to the manifest fetch and wipe it afterwards"
)
assert "~SocketState()" in shim_state and "WipeServerPassword(serverPassword);" in shim_state, (
    "closing a socket must wipe the password it still holds"
)
assert "state->host = host.data();" in shim and "state->port = remotePort;" in shim

assert "kMaximumMessageBytes = 64uz * 1024uz" in plugin_internal, "the one-message cap must not change"
assert "CT_64_KB" in server_protocol or "64 * 1024" in server_protocol or "65536" in server_protocol
assert "kMaximumAdmissionManifestBytes = 48 * 1024" in identity_header, "the admission frame cap must not change"
assert "kMaximumManifestBytes = 2 * 1024 * 1024" in manifest_header, "the manifest file ceiling must not change"
assert "CO_GNS_MAX_MESSAGE" in bridge or "64" in bridge

assert "CheckServerPassword(" in server_chunks and "ManifestChunkRequest::refused" in server_chunks and "a_disconnect(" in server_chunks, (
    "the chunk path must refuse a request that fails the server password check"
)
assert server_chunks.index("CheckServerPassword(") < server_chunks.index("g_deliveries.contains"), (
    "the password check must run before any delivery is queued"
)
assert "ManifestChunkRequest::refused" in server_runtime or "notRequest" in server_runtime

print("Modpack manifest chunk delivery contract tests passed")
