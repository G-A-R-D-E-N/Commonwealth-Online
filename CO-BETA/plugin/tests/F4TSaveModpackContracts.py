#!/usr/bin/env python3
"""F4TSaveModpackContracts: consolidated contract sections.

Consolidated from 8 single-contract scripts so the group runs one process
instead of 8. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TSaveModpackContracts.py
"""
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[3]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok


def _section_001_f4tclientmodpackprobecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TClientModpackProbeContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TClientModpackProbeContract.py")
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]

    manifest_header = (root / "co-core/include/F4TModpackManifest.h").read_text(encoding="utf-8")
    manifest_source = (root / "co-core/src/manifest/F4TModpackManifest.cpp").read_text(encoding="utf-8")
    probe_header = (root / "co-core/include/F4TModpackProbe.h").read_text(encoding="utf-8")
    probe_source = (root / "co-core/src/manifest/F4TModpackProbe.cpp").read_text(encoding="utf-8")
    plugin_probe = (root / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
    load_order = (root / "CO-BETA/plugin/src/F4TLoadOrderSync.cpp").read_text(encoding="utf-8")
    main_source = (root / "CO-BETA/plugin/src/main.cpp").read_text(encoding="utf-8")
    detail_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/modpack-detail.js").read_text(encoding="utf-8")

    assert "archive" in manifest_header, "the manifest entry model must define an archive kind"
    assert "wrongArchive" in manifest_header, "the diff must classify archive content"
    assert "wrongView" in manifest_header, "the diff must classify resource view content"
    for fragment in ('"archive"', "EntryKind::archive", "Wrong archive:", "wrongArchive", '"view"', "EntryKind::view", "Wrong view:", "wrongView"):
        assert fragment in manifest_source, f"F4TModpackManifest.cpp must implement {fragment}"

    for fragment in ("RankRequiredPlugins", "ContiguousOrdersFromRanks", "FindExtraPlugins", "FindExtraNatives", "kMissingPluginOrder", "PluginTypeFromName"):
        assert fragment in probe_header, f"F4TModpackProbe.h must declare {fragment}"
    for fragment in ("NormalizeManifest", "unordered_map", "unordered_set", "WindowsCaseFold::Key", "ContiguousOrdersFromRanks", "stable_sort"):
        assert fragment in probe_source, f"F4TModpackProbe.cpp must use {fragment}"

    for fragment in (
        "F4T::LoadOrderSync::LoadedPluginNames()",
        "F4T::ModpackProbe::RankRequiredPlugins",
        "F4T::ModpackProbe::FindExtraPlugins",
        "F4T::ModpackProbe::FindExtraNatives",
        "std::filesystem::status(a_path, error)",
        "F4T::UserPaths::GameDataDirectory()",
        "F4T::UserPaths::ModuleDataDirectory()",
        "kExtraNativesHeader",
        "kLoadOrderUnavailable",
        "rank = ranks[nextRank++];",
        "acceptedRanks.push_back(rank);",
        "F4T::ModpackProbe::ContiguousOrdersFromRanks(acceptedRanks)",
        "probedEntry.order = orders[pluginIndex++];",
    ):
        assert fragment in plugin_probe, f"F4TClientModpackProbe.cpp must use {fragment}"
    assert "std::optional<std::filesystem::path> GameExecutableDirectory()" not in plugin_probe, (
        "the probe must use the shared Data folder resolver, not a local copy"
    )
    assert "item.entry->order = nextOrder++" not in plugin_probe, (
        "required plugins must take their order from the client's loaded rank, not the server's manifest position"
    )
    assert "symlink_status" not in plugin_probe, (
        "the probe must follow symlinks and junctions so Vortex deploys count, while still rejecting broken links "
        "and links to non-regular files"
    )
    for compile_time_switch in ("#if", "F4T_MODPACK"):
        assert compile_time_switch not in plugin_probe, "the modpack gate must always be on, with no compile-time switch"

    assert "void CaptureLoadedPlugins()" in load_order, "the plugin list must be captured on the game thread"
    assert "std::scoped_lock" in load_order, "the captured plugin list must be shared under a lock"
    assert "F4T::LoadOrderSync::CaptureLoadedPlugins();" in main_source, (
        "kGameDataReady must capture the loaded plugin list so the probe never reads the engine off the game thread"
    )

    for fragment in (
        "Wrong load order",
        "Unexpected",
        "Different view",
        "co-modpack-detail-entry--order",
        "Extra F4SE plugins (allowed, not required):",
    ):
        assert fragment in detail_js, f"modpack-detail.js must show {fragment}"

    print("Client modpack probe contract tests passed")


def _section_002_f4tloadordercontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TLoadOrderContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TLoadOrderContract.py")
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]

    core_header = (root / "co-core/include/F4TLoadOrder.h").read_text(encoding="utf-8")
    core_source = (root / "co-core/src/identity/F4TLoadOrder.cpp").read_text(encoding="utf-8")
    plugin = (root / "CO-BETA/plugin/src/F4TLoadOrderSync.cpp").read_text(encoding="utf-8")
    server_inc = (root / "CO-SERVER/server-cpp/src/ServerLoadOrder.inc").read_text(encoding="utf-8")
    server_impl = (root / "CO-SERVER/server-cpp/src/ServerProtocolImpl.inc").read_text(encoding="utf-8")
    server_protocol = (root / "CO-SERVER/server-cpp/src/ServerProtocol.cpp").read_text(encoding="utf-8")
    server_handlers = (root / "CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp").read_text(encoding="utf-8")
    server_session_types = (root / "CO-SERVER/server-cpp/src/ServerSessionTypes.h").read_text(encoding="utf-8")

    for fragment in (
        "kFormatVersion = 1",
        "kMaximumFullPlugins = 254",
        "kMaximumLightPlugins = 4096",
        "kMaximumPluginNameBytes = 255",
        "kMaximumPayloadBytes",
        "class Registry",
        "TranslationTable",
        "BuildTranslationTable",
    ):
        assert fragment in core_header, f"F4TLoadOrder.h must define {fragment}"

    for fragment in (
        "Base64Decode",
        "DecodePluginList",
        "BuildPluginListPacket",
        "Registry::ToLocal",
        "IsValidPluginList",
    ):
        assert fragment in core_source, f"F4TLoadOrder.cpp must implement {fragment}"

    for fragment in (
        "RE::TESDataHandler::GetSingleton()",
        "compiledFileCollection.files",
        "compiledFileCollection.smallFiles",
        "GetCompileIndex()",
        "GetSmallFileCompileIndex()",
        "F4T::LoadOrder::BuildPluginListPacket",
        "F4T::LoadOrder::DecodePluginList",
        "TranslateInboundPacket",
        "g_registry.ToLocal",
    ):
        assert fragment in plugin, f"F4TLoadOrderSync.cpp must use {fragment}"

    for feature_switch in ("F4T_LOAD_ORDER", "#if", "#ifdef"):
        assert feature_switch not in plugin, (
            "plugin translation must be always on, with no compile-time switch"
        )

    for fragment in (
        "F4T::LoadOrder::DecodePluginList",
        "pluginList",
        "session.pluginListPacket",
        "ExistingPluginListFrames",
    ):
        assert fragment in server_inc, f"ServerLoadOrder.inc must use {fragment}"

    assert '%:include "ServerLoadOrder.inc"' in server_impl
    assert '*type == "pluginList"' in server_impl
    assert "ExistingPluginListFrames(a_connectionId)" in server_handlers
    assert "kMaximumPluginListBytes" in server_protocol
    assert 'a_type == "pluginList"' in server_protocol
    assert "std::optional<Json> pluginListPacket;" in server_session_types

    print("Load order contract tests passed")


def _section_004_f4tmodpackautofixcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TModpackAutoFixContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TModpackAutoFixContract.py")
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

def _section_005_f4tmodpackcachecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TModpackCacheContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TModpackCacheContract.py")
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]

    cache_header = (root / "co-core/include/F4TModpackProbeCache.h").read_text(encoding="utf-8")
    cache_source = (root / "co-core/src/manifest/F4TModpackProbeCache.cpp").read_text(encoding="utf-8")
    core_xmake = (root / "co-core/xmake.lua").read_text(encoding="utf-8")
    store_header = (root / "CO-BETA/plugin/include/F4TServerModpackCache.h").read_text(encoding="utf-8")
    store_source = (root / "CO-BETA/plugin/src/F4TServerModpackCache.cpp").read_text(encoding="utf-8")
    probe_header = (root / "CO-BETA/plugin/include/F4TClientModpackProbe.h").read_text(encoding="utf-8")
    probe = (root / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
    networking = (root / "CO-BETA/plugin/src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")
    shim_state = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimState.h").read_text(encoding="utf-8")
    shim_frames = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimFrames.cpp").read_text(encoding="utf-8")
    shim = (root / "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")

    for fragment in (
        "struct FileFact",
        "struct Entry",
        "enum class Match",
        "SerializeEntry",
        "ParseEntry",
        "Validate",
        "kMaximumBlobBytes",
        "kMissingFileSize",
    ):
        assert fragment in cache_header, f"F4TModpackProbeCache.h must define {fragment}"
    for fragment in ("IsValidPath", "IsValidPluginName", "changedPlugins", "changedFile", "missingFile"):
        assert fragment in cache_source, f"F4TModpackProbeCache.cpp must implement {fragment}"
    assert "src/manifest/F4TModpackProbeCache.cpp" in core_xmake, "the cache source must be in CoreLib"
    assert "include/F4TModpackProbeCache.h" in core_xmake, "the cache header must be in CoreLib"

    for fragment in ("Lookup", "Store", "Clear", "SetCachePathForTesting", "modpack-cache.json"):
        assert fragment in store_header + store_source, f"the per-server cache store must handle {fragment}"
    assert "kMaximumServers" in store_source and "kMaximumFileBytes" in store_source, (
        "the cache store must bound servers and file size"
    )

    for fragment in ("CachedFingerprint", "RememberMatch", "Forget", "fileFacts", "loadedPlugins"):
        assert fragment in probe_header, f"F4TClientModpackProbe.h must expose {fragment}"
    for fragment in (
        "ServerModpackCache::Lookup",
        "ServerModpackCache::Store",
        "ServerModpackCache::Clear",
        "ModpackProbeCache::Validate",
        "ModpackProbeCache::Match::hit",
        "LastWriteTicks",
        "result.fileFacts.push_back",
    ):
        assert fragment in probe, f"F4TClientModpackProbe.cpp must use {fragment}"
    assert "RememberMatch(std::string{ a_serverKey }, *matching, probed)" in probe, (
        "a matching probe must refresh the per-server cache"
    )

    cache_lookup = networking.index("F4T::ClientModpackProbe::CachedFingerprint(serverKey)")
    first_connect = networking.index("ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, fingerprint, a_joinId, true, cancelled, a_backgroundValidation, a_generationOut)")
    assert cache_lookup < first_connect, "a validated cached fingerprint must be sent on the FIRST connect"
    assert "staleCacheRejected" in networking and "F4T::ClientModpackProbe::Forget(serverKey)" in networking, (
        "a cached fingerprint the server still rejects must be dropped, then the normal probe retried once"
    )
    assert networking.count("a_serverPassword, fingerprint, a_joinId, true, cancelled, a_backgroundValidation, a_generationOut)") == 4, (
        "the first attempt, the resume-credential retry, the modpack retry and the duplicate-identity retry all use the fingerprint that last worked"
    )
    assert networking.count("fingerprint = failure->matchingModpackFingerprint;") == 2, (
        "the modpack retry uses the fingerprint the server reported, and a retry that reports a new one replaces it"
    )
    assert networking.count("ConnectAndValidateServerOnce") == 5, (
        "the wrapper has the first attempt, one resume-credential retry, one modpack retry and one bounded retry-loop attempt (duplicate-identity interval or modpack immediate)"
    )

    assert "std::string serverKey;" in shim_state
    assert "state->serverKey = serverKey;" in shim
    assert "MakeSessionRejectionFailure(rejection, serverKey, userCancelled, maxDuration)" in networking, (
        "the rejection handler must pass the server key so a match can be cached"
    )

    for compile_time_switch in ("#if", "F4T_MODPACK_CACHE"):
        assert compile_time_switch not in probe, "the probe cache must always be on, with no compile-time switch"

    print("Modpack probe cache contract tests passed")

def _section_006_f4tmodpackchunkscontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TModpackChunksContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TModpackChunksContract.py")
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]

    core_header = (root / "co-core/include/F4TModpackChunks.h").read_text(encoding="utf-8")
    core_source = (root / "co-core/src/manifest/F4TModpackChunks.cpp").read_text(encoding="utf-8")
    core_xmake = (root / "co-core/xmake.lua").read_text(encoding="utf-8")
    identity_header = (root / "co-core/include/F4TIdentityProtocol.h").read_text(encoding="utf-8")
    identity_source = (root / "co-core/src/session/F4TIdentityProtocol.cpp").read_text(encoding="utf-8")
    adapter_header = (root / "co-core/include/F4TGnsProtocolAdapter.h").read_text(encoding="utf-8")
    adapter_source = "".join((root / name).read_text(encoding="utf-8") for name in (
        "co-core/src/protocol/F4TGnsProtocolAdapter.cpp",
        "co-core/src/protocol/F4TGnsProtocolAdapterInternal.h",
        "co-core/src/protocol/F4TGnsProtocolAdapterImpl.h",
        "co-core/src/protocol/F4TGnsProtocolAdapterOutbound.cpp",
        "co-core/src/protocol/F4TGnsProtocolAdapterInbound.cpp",
    ))
    server_runtime = (root / "CO-SERVER/server-cpp/src/ServerRuntime.inc").read_text(encoding="utf-8").replace('#include "ServerRuntimeMessage.inc"', (root / "CO-SERVER/server-cpp/src/ServerRuntimeMessage.inc").read_text(encoding="utf-8"))
    server_maintenance = (root / "CO-SERVER/server-cpp/src/ServerMaintenance.inc").read_text(encoding="utf-8")
    server_ended = (root / "CO-SERVER/server-cpp/src/ServerSessionEnded.inc").read_text(encoding="utf-8")
    server_support = (root / "CO-SERVER/server-cpp/include/ServerRuntimeSupport.h").read_text(encoding="utf-8")
    server_gate = (root / "CO-SERVER/server-cpp/src/IdentityGate.cpp").read_text(encoding="utf-8")
    server_gate_header = (root / "CO-SERVER/server-cpp/include/IdentityGate.h").read_text(encoding="utf-8")
    server_config = (root / "CO-SERVER/server-cpp/src/ServerConfiguration.cpp").read_text(encoding="utf-8")
    server_chunks = (root / "CO-SERVER/server-cpp/src/ServerManifestChunks.cpp").read_text(encoding="utf-8")
    server_xmake = (root / "CO-SERVER/server-cpp/xmake.lua").read_text(encoding="utf-8")
    identity_client = (root / "CO-BETA/plugin/src/F4TClientIdentity.cpp").read_text(encoding="utf-8")
    fetch = (root / "CO-BETA/plugin/src/F4TGnsManifestFetch.cpp").read_text(encoding="utf-8")
    frames = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimFrames.cpp").read_text(encoding="utf-8")
    networking = (root / "CO-BETA/plugin/src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")
    shim_state = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimState.h").read_text(encoding="utf-8")
    shim = (root / "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")
    shim_header = (root / "CO-BETA/plugin/include/F4TGnsWinsockShim.h").read_text(encoding="utf-8")
    server_protocol = (root / "CO-SERVER/server-cpp/src/ServerProtocolImpl.h").read_text(encoding="utf-8")
    plugin_internal = (root / "CO-BETA/plugin/src/F4TGnsWinsockShimInternal.h").read_text(encoding="utf-8")
    bridge = (root / "CO-BETA/plugin/gns-bridge/co_gns_client_bridge.cpp").read_text(encoding="utf-8")
    transport_limits = (root / "co-core/include/F4TTransportLimits.h").read_text(encoding="utf-8")
    manifest_header = (root / "co-core/include/F4TModpackManifest.h").read_text(encoding="utf-8")

    for fragment in ("kCapabilityName", "kChunkPayloadBytes = 32 * 1024", "kMaximumTotalBytes = 8 * 1024 * 1024",
                     "kMaximumChunks", "kChunkPacketType", "kEndPacketType", "kRequestField", "Reassembler", "Split", "Describe"):
        assert fragment in core_header, f"F4TModpackChunks.h must define {fragment}"
    assert ("EncodeBase64" in core_source and "DecodeBase64" in core_source and "AddResult::outOfOrder" in core_source
            and "expected.sha256" in core_source)
    assert "src/manifest/F4TModpackChunks.cpp" in core_xmake

    assert "modpackChunks" in identity_header and "modpackFingerprint" in identity_header
    assert "F4T::ModpackChunks::Describe(manifestText)" in identity_source
    assert "Update Commonwealth Online" in identity_source, "an old client must be told to update"
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
    assert "src/ServerManifestChunks.cpp" in server_xmake
    assert "AdvanceManifestChunkQueue" in server_chunks and "ManifestChunkQueue" in server_chunks
    assert "!CommonwealthOnline::Server::HasManifestChunkDelivery(server, connectionId)" in server_maintenance
    assert "AdvanceManifestChunkDeliveries(server" in server_maintenance and "kDeliveryTimeoutSeconds" in server_chunks
    assert "ForgetManifestChunkDelivery(server, event.connection_id)" in server_runtime

    assert server_chunks.count("F4T::ModpackChunks::Split(") == 1 and server_chunks.count("BuildManifestChunkPackets(") == 2, (
        "the chunk packets must be built only by BuildManifestChunkPackets"
    )
    assert "std::shared_ptr<const ManifestChunkPackets> g_packets" in server_chunks, "the chunk packets must be shared, not copied per delivery"
    assert "fullManifest ? g_packets : g_viewlessPackets" in server_chunks
    assert (server_chunks.index("if (!packets || packets->fingerprint != requiredFingerprint)")
            < server_chunks.index("packets = BuildManifestChunkPackets(requiredFingerprint")
            < server_chunks.index("delivery.packets = packets")), (
        "a delivery must reuse the cached chunk packets and rebuild them only when the fingerprint changes"
    )

    assert "F4T::ModpackChunks::kCapabilityName" in identity_client, "the client must advertise the chunk capability"
    assert "FetchManifest" in fetch and "Reassembler" in fetch and "kRequestField" in fetch
    assert "GnsManifestFetch::FetchManifest" in networking
    assert "cancelRequested" in shim_state, "the shim socket must carry a cancellation flag"
    assert "Shutdown(SOCKET a_socket, int a_how) noexcept" in shim_header and "#define shutdown(" in shim_header, (
        "the shim must route shutdown() so cancelling a join can interrupt the fetch"
    )
    assert "int Shutdown(SOCKET a_socket, int) noexcept" in shim and "cancelRequested.store(true, std::memory_order_release)" in shim, (
        "the shim Shutdown and CloseSocket must request cancellation"
    )
    assert "cancelRequested" in fetch, "FetchManifest must poll the cancellation flag"
    assert "rejection.requiredModpackFingerprint, password, *rejection.requiredModpackChunks, userCancelled)" in networking, (
        "the join operation must propagate cancellation to manifest fetching"
    )
    assert "event.payloadSize > payload.size()" in fetch, "FetchManifest must bound the received payload before parsing it"
    assert "index->get<std::uint64_t>() >= a_chunks.chunkCount" in fetch, (
        "FetchManifest must range-check the chunk index before the uint32 cast"
    )
    assert "host" in shim_state and "port" in shim_state
    assert '"serverPassword"' in fetch and "a_serverPassword.empty()" in fetch, (
        "FetchManifest must send the join attempt's serverPassword in its hello, and only when one is held"
    )
    assert "state->serverPassword = identity->serverPassword" in shim and "WipeServerPassword(state->serverPassword)" in shim, (
        "the held password must come from the connection identity and be wiped once the session is ready"
    )
    assert "WipeServerPassword(a_state->serverPassword)" in frames and "a_serverPassword ? *a_serverPassword" in networking, (
        "the rejection path must wipe the socket copy and use the join password for manifest fetching"
    )
    assert "~SocketState()" in shim_state and "WipeServerPassword(serverPassword);" in shim_state, (
        "closing a socket must wipe the password it still holds"
    )
    assert "state->host = host.data();" in shim and "state->port = remotePort;" in shim

    assert "kMaximumMessageBytes = 256uz * 1024uz" in plugin_internal, "the one-message cap must not change"
    assert "DefaultMaximumMessageBytes = 256uz * 1024uz" in transport_limits, "the server message cap must not change"
    assert "kMaximumAdmissionManifestBytes = 48 * 1024" in identity_header, "the admission frame cap must not change"
    assert "kMaximumManifestBytes = 8 * 1024 * 1024" in manifest_header, "the manifest file ceiling must match the chunk ceiling"
    assert "kDefaultMaximumMessageBytes = 256u * 1024u" in bridge, "the bridge message cap must not change"

    assert "CheckServerPassword(" in server_chunks and "ManifestChunkRequest::refused" in server_chunks and "a_disconnect(" in server_chunks, (
        "the chunk path must refuse a request that fails the server password check"
    )
    assert server_chunks.index("CheckServerPassword(") < server_chunks.index("g_deliveries.contains"), (
        "the password check must run before any delivery is queued"
    )
    assert "ManifestChunkRequest::refused" in server_runtime or "notRequest" in server_runtime

    print("Modpack manifest chunk delivery contract tests passed")

def _section_007_f4tmodpackgateuxcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TModpackGateUxContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TModpackGateUxContract.py")
    from pathlib import Path

    root = Path(__file__).resolve().parents[3]

    config_header = (root / "co-core/include/F4TServerConfiguration.h").read_text(encoding="utf-8")
    config_source = (root / "co-core/src/config/F4TServerConfiguration.cpp").read_text(encoding="utf-8") + (root / "co-core/src/config/F4TServerConfigModpack.cpp").read_text(encoding="utf-8")
    identity_header = (root / "co-core/include/F4TIdentityProtocol.h").read_text(encoding="utf-8")
    identity_source = (root / "co-core/src/session/F4TIdentityProtocol.cpp").read_text(encoding="utf-8")
    settings_source = (root / "co-core/src/config/F4TServerSettingsFile.cpp").read_text(encoding="utf-8")
    server_config = (root / "CO-SERVER/server-cpp/src/ServerConfiguration.cpp").read_text(encoding="utf-8")
    bootstrap = (root / "CO-SERVER/server-cpp/src/ServerBootstrap.cpp").read_text(encoding="utf-8")
    bridge = (root / "CommonwealthOnlineUI/src/F4TServerSettingsBridge.cpp").read_text(encoding="utf-8")
    settings_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/server-settings.js").read_text(encoding="utf-8")
    markup = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/browser-markup.js").read_text(encoding="utf-8")
    browser_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/browser.js").read_text(encoding="utf-8")
    failure_header = (root / "CO-BETA/plugin/include/F4TConnectFailure.h").read_text(encoding="utf-8")
    probe = (root / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
    ui_client = (root / "CO-BETA/plugin/src/F4TUIClient.cpp").read_text(encoding="utf-8")
    alert = (root / "CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp").read_text(encoding="utf-8")

    assert "requiredModpackUrl" in config_header, "the options must carry the optional modpack link"
    for fragment in ("required_modpack_url", "RequiredModpackPath", "LoadRequiredModpack", "RequiredModpackError"):
        assert fragment in config_source, f"F4TServerConfiguration.cpp must handle {fragment}"
    assert "required_modpack_url must be an https:// URL" in config_source, (
        "the modpack link must be validated with a clear error"
    )
    assert "LoadRequiredModpack(const std::filesystem::path&" not in server_config, (
        "the server must use the shared loader instead of keeping its own copy"
    )
    assert "F4T::ServerConfiguration::LoadConfiguredRequiredModpack" in server_config

    assert "required_modpack_path is not set" in bootstrap, (
        "the server must log a clear startup warning when no required modpack is configured"
    )

    assert "ServerConfiguration::RequiredModpackError(plan.document" in settings_source, (
        "the host panel save path must check required_modpack_path like the server parser does"
    )
    assert "ServerConfiguration::RequiredModpackError(document" in bridge, (
        "the host panel load path must surface a bad required_modpack_path as an error"
    )
    assert "required_modpack_url" in settings_js and "not anti-cheat" in settings_js, (
        "the host panel help must document the modpack gate and that a faked fingerprint is accepted by design"
    )

    assert "kModpackPageMarker" in identity_header
    assert "rejection.reason += kModpackPageMarker;" in identity_source, (
        "the modpack mismatch reason must carry the optional modpack page link"
    )
    assert "NormalizeModpackUrl" in identity_source, "the link must be validated before it reaches a client"
    assert "bool IsAllowedModpackPageUrl(std::string_view a_url) noexcept;" in identity_header, (
        "co-core must publish the one modpack link validator the server config and the client probe both use"
    )
    assert "bool IsAllowedModpackPageUrl(std::string_view a_url) noexcept" in identity_source
    assert "std::optional<std::string> NormalizeModpackUrl(const std::optional<std::string>& a_value);" in identity_header
    assert "NormalizeModpackUrl(a_options.requiredModpackUrl)" in config_source, (
        "the server must validate required_modpack_url with the shared normalizer, so an over-long link fails "
        "validation instead of being silently dropped at handshake time"
    )
    assert "kMaximumModpackUrlBytes" in identity_header

    assert "modpackUrl" in failure_header, "the client failure must carry the optional modpack link"
    for fragment in ("F4T::Identity::ExtractModpackPageUrl", "failure.modpackUrl ="):
        assert fragment in probe, f"the client probe must extract the link ({fragment})"
    for prefix_check in ('starts_with("https://")', 'starts_with("http://localhost")', "bool IsAllowedModpackPageUrl("):
        assert prefix_check not in probe, (
            "the client probe must call the shared co-core validator instead of testing URL prefixes "
            f"({prefix_check} would accept localhost.evil.com and 127.0.0.1@evil.com)"
        )
    assert "connectionFailure->modpackUrl" in ui_client, "the join UI must receive the link"
    assert "OpenModpackPage" in alert and '"modpack", "Get Modpack"' in alert, (
        "the rejection alert must offer a button that opens the server's modpack page"
    )
    assert "ShellExecuteW" in alert, "the alert must open the link with the shell, with no auto-download"
    assert "F4T::Identity::ModpackUrlHost" in alert and 'presentation.message += std::format("\\nGet Modpack opens {}", host);' in alert, (
        "the rejection alert must show the destination host, so the player sees where Get Modpack goes"
    )
    assert "std::string_view ModpackUrlHost(std::string_view a_url) noexcept;" in identity_header, (
        "the host shown in the alert must come from the same parser that validates the link"
    )

    assert 'id="coCopyModpackButton"' in markup, "the join overlay must mount a Copy list button"
    copy_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/modpack-copy.js").read_text(encoding="utf-8")
    assert "CO_ModpackCopy" in copy_js and "navigator.clipboard" in copy_js, (
        "the Copy list button must copy the mismatch lines"
    )
    assert ".then(function () { return clipboard.writeText(text); })" in copy_js and "execCommand" in copy_js, (
        "the copy button must await the clipboard write and fall back to execCommand when the write is refused"
    )
    assert 'notice.textContent = copied ? "Copied to clipboard." : "Copy failed.";' in copy_js, (
        "the notice must come from the real copy result"
    )
    assert "modpackCopy.show(modpack.lines, isError)" in browser_js, "the overlay must hand the lines to the copy component"

    print("Modpack gate host and player UX contract tests passed")

def _section_008_f4tmodpackprogresscontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TModpackProgressContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TModpackProgressContract.py")
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

def _section_009_f4tsavelineageenforcementcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TSaveLineageEnforcementContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TSaveLineageEnforcementContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"
    SERVER = ROOT / "CO-SERVER/server-cpp"
    CORE = ROOT / "co-core"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def strip_comments(text):
        return re.sub(r"//[^\n]*", "", text)


    def function_body(code, signature):
        start = code.index(signature)
        brace = code.index("{", start)
        depth = 0
        for index in range(brace, len(code)):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    return code[brace:index + 1]
        raise AssertionError(f"unterminated body for {signature}")


    lineage = strip_comments((PLUGIN / "src/F4TSaveLineageSession.cpp").read_text(encoding="utf-8"))
    launch = strip_comments((PLUGIN / "src/F4TSessionLaunch.cpp").read_text(encoding="utf-8"))
    ui = strip_comments((PLUGIN / "src/F4TUIClient.cpp").read_text(encoding="utf-8"))
    identity = strip_comments((PLUGIN / "src/F4TClientIdentity.cpp").read_text(encoding="utf-8"))
    logic = strip_comments((CORE / "src/domain/F4TSaveProfileLogic.cpp").read_text(encoding="utf-8"))
    gate = strip_comments((SERVER / "src/IdentityGate.cpp").read_text(encoding="utf-8"))

    admit_join = function_body(logic, "SaveAdmission AdmitJoinSave(")
    check("a_isAdmin || a_resolution.saveFile.has_value()" in admit_join,
          "AdmitJoinSave must admit only an admin or a resolved attested save")
    check("std::string{ SaveNotMultiplayerCode }" in admit_join,
          "AdmitJoinSave must deny with SaveNotMultiplayerCode")
    check(re.search(r"SaveAdmission\{ true, \{\}, \{\} \};\s*\}\s*return SaveAdmission\{", admit_join) is not None,
          "AdmitJoinSave must not fall through to an allow")

    admit_claim = function_body(logic, "SaveAdmission AdmitAttestedSaveClaim(")
    check("if (a_standing.isAdmin) {" in admit_claim,
          "AdmitAttestedSaveClaim must admit an admin unconditionally")
    check("a_claim && (verified || (a_serverHistory.empty() && !a_standing.profileKnown))" in admit_claim,
          "AdmitAttestedSaveClaim must record a claim for a verified profile, or against an empty history for a never-seen profile")
    check("SameAttestedSave(a_entry, *a_claim)" in admit_claim,
          "AdmitAttestedSaveClaim must match the claim against the recorded history")
    check("recorded && (verified || a_standing.claimReadmission)" in admit_claim,
          "AdmitAttestedSaveClaim must readmit a recorded claim without the secret only through the readmission standing")
    check("std::string{ SaveNotMultiplayerCode }" in admit_claim,
          "AdmitAttestedSaveClaim must deny with SaveNotMultiplayerCode")

    join_save = function_body(lineage, "JoinSaveDecision ResolveJoinSave(")
    check("F4T::SaveProfileLogic::AdmitJoinSave(resolution, a_isAdmin)" in join_save,
          "SaveLineageSession::ResolveJoinSave must go through AdmitJoinSave")
    check("if (!admission.allowed)" in join_save and "decision.allowed = false;" in join_save,
          "SaveLineageSession::ResolveJoinSave must refuse the join when admission denies")
    check("decision.code = admission.code;" in join_save and "decision.reason = admission.reason;" in join_save,
          "SaveLineageSession::ResolveJoinSave must carry the refusal code and reason to the caller")
    check("resolution.verdict != F4T::SaveProfileLogic::LineageVerdict::Attested" in join_save and "!F4T::SaveProfileLogic::EqualsIgnoreCase(*resolution.saveFile, *a_latestSave)" in join_save and "return RefuseJoin" in join_save and "decision.saveFile = resolution.saveFile;" not in join_save,
          "SaveLineageSession::ResolveJoinSave must refuse rather than select an older save")
    check(re.search(r"allowed\s*=\s*true", join_save) is None,
          "SaveLineageSession::ResolveJoinSave must not re-allow a refused join")
    check("catch (...) {" in join_save and "RefuseJoin" in join_save.split("catch (...) {")[1],
          "SaveLineageSession::ResolveJoinSave must refuse the join when the lineage check throws")
    check(re.search(r"if \(!characterId\) \{\s*return RefuseJoin\(", join_save) is not None,
          "SaveLineageSession::ResolveJoinSave must refuse a save whose name carries no character id")
    check(re.search(r"if \(attested\.empty\(\)\) \{\s*return RefuseJoin\(", join_save) is not None and
          "RecordActiveSave(a_serverKey, *boundSave)" not in join_save,
          "SaveLineageSession::ResolveJoinSave must refuse a save without recorded lineage")
    check("AnnounceRecordedAttestations(a_serverKey, *characterId, attested)" in join_save,
          "SaveLineageSession::ResolveJoinSave must resend the recorded attestations on join")
    check("kEnableSaveLineage" not in lineage and "kEnableSaveLineage" not in (PLUGIN / "include/F4TSaveLineageSession.h").read_text(encoding="utf-8"),
          "the save lineage path must not be gated")

    announce = function_body(lineage, "bool AnnounceSaveAttestation(")
    check("F4T::SaveLineageStore::LookupServerSecret(a_serverKey)" in announce and '"profileSecret"' in announce,
          "every saveAttested announcement must carry the profile secret")

    launch_body = function_body(launch, "LaunchOutcome LaunchValidatedSession(")
    check("const bool isAdmin = F4T::ClientIdentity::IsConnectedServerAdmin();" in launch_body,
          "LaunchValidatedSession must take the admin exemption from the server's sessionReady flag")
    check(launch_body.index("IsConnectedServerAdmin()") < launch_body.index("ResolveJoinSave(profileKey"),
          "LaunchValidatedSession must resolve the admin flag before it resolves the join save")
    check(re.search(r"if \(!joinSave\.allowed\) \{\s*LogWarning\([^\n]*\);\s*a_error = joinSave\.reason;\s*return LaunchOutcome::Failed;", launch_body) is not None,
          "LaunchValidatedSession must fail the launch with the refusal reason")
    check(re.search(r"if \(!latestSave\.allowed\) \{\s*LogWarning\([^\n]*\);\s*a_error = latestSave\.reason;\s*return LaunchOutcome::Failed;", launch_body) is not None,
          "the missing-save retry must also fail the launch with the refusal reason")
    missing_save = launch_body[launch_body.index('"Associated save \'{}\' for server \'{}\' is missing;'):]
    missing_save = missing_save[:missing_save.index("} else {")]
    check("return LaunchOutcome::Failed;" in missing_save and "StartFirstTimeJoin(" not in missing_save and
          "contact the server administrator for recovery" in missing_save,
          "a missing associated save must refuse replacement onboarding and direct the player to recovery")

    ui_body = function_body(ui, "bool Callback_JoinSession(")
    check("finishJoin(ok, launchError);" in ui_body,
          "UIClient must finish refused launches through guarded asynchronous cleanup")
    check('a_onComplete(a_success, a_success ? "" : reason.c_str(), a_userData);' in ui_body,
          "UIClient must hand the refusal reason to the UI callback")

    resolve_claim = function_body(identity, "std::optional<F4T::SaveProfileLogic::AttestedSave> ResolveSaveAttestation(")
    check("F4T::SaveLineageStore::LookupAttestedSaves(a_serverKey)" in resolve_claim,
          "ClientIdentity::ResolveSaveAttestation must read the recorded attestations")
    check(resolve_claim.count("return std::nullopt;") >= 2,
          "ClientIdentity::ResolveSaveAttestation must claim nothing without a bound save or a record")
    check("Sha256HexFile" not in resolve_claim,
          "ClientIdentity::ResolveSaveAttestation must not attest a save at join time")

    check("F4T::SaveProfileLogic::SaveClaimStanding{ isAdmin, secretVerified, profileKnown, claimReadmission }" in gate,
          "IdentityGate must admit the hello against the recorded attestations with the verified secret, known-profile and readmission standing")
    check("claimReadmission = !_claimReadmitted.contains(profileId) && _identities.AwaitingConfirmation(profileId, a_nowUnixSeconds);" in gate,
          "IdentityGate must readmit a recorded claim without the secret only while the profile's secret delivery is unconfirmed and not yet readmitted")
    check("_claimReadmitted.insert(profileId);" in gate and "_claimReadmitted.erase(profile->second);" in gate,
          "IdentityGate must mark a readmission and clear the mark only when the readmitted session confirms its secret")
    check(gate.index("AdmitAttestedSaveClaim(") < gate.index("_identities.ValidateAndReserve(key"),
          "IdentityGate must admit the save claim before it reserves the identity, so a denied hello writes no pending state")
    check("const bool isAdmin = _profileAdminLookup && _profileAdminLookup(profileId) && secretVerified &&" in gate
          and "(!_adminSteamCheck || _adminSteamCheck(profileId," in gate,
          "IdentityGate must take the admin exemption from the server's own profile lookup")
    check("_saveAuthority->EnsureProfileSecret(profileId)" in gate and '(*packet)["profileSecret"] = secret->second;' in gate,
          "IdentityGate must issue the profile secret in sessionReady")
    check("_saveAuthority->ProfileSecret(a_profileId)" in gate,
          "IdentityGate must verify an admin against the profile secret it issued")
    check("save_attestation_unauthenticated" in gate and "save_attestation_rate_limited" in gate,
          "IdentityGate must reject an unauthenticated or rate-limited attestation")
    check("F4T::SaveProfileLogic::SameAttestedSave(recorded, attested)" in gate,
          "IdentityGate must skip an attestation it has already recorded")
    check("F4T::Identity::SaveAttestationCapability" in gate,
          "IdentityGate must refuse a client that does not report save attestation support")
    check(gate.count("multiplayerSaveAdmin") == 1 and "multiplayerSaveAdmin" in function_body(gate, "bool IdentityGate::EnrichSessionReady("),
          "multiplayerSaveAdmin must only be set on the outbound sessionReady frame, never read from a client packet")
    check(re.search(r"admission\.allowed[\s\S]{0,200}?\.forward = false", gate) is not None,
          "IdentityGate must not forward a hello whose save admission was denied")

    shim = strip_comments((PLUGIN / "src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8"))
    store = strip_comments((PLUGIN / "src/F4TSaveLineageStore.cpp").read_text(encoding="utf-8"))
    inbound = strip_comments((PLUGIN / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8"))

    commit = function_body(identity, "bool CommitReadyIdentity(")
    check("SaveLineageStore::StoreServerSecret(a_serverKey, *a_identity.profileSecret)" in commit,
          "ClientIdentity::CommitReadyIdentity must persist the profile secret the server issued in sessionReady")
    check("ServerProfileStore::StoreResumeCredential(a_serverKey, a_identity.resumeToken, a_identity.resumeExpiresAt)" in commit,
          "ClientIdentity::CommitReadyIdentity must keep storing the rotated resume credential")
    check("F4T::ClientIdentity::CommitReadyIdentity(serverKey, a_identity, a_error)" in shim,
          "the GNS connect committer must commit the ready identity through ClientIdentity")

    shim_identity = function_body(shim, "F4T::GnsProtocolAdapter::ClientIdentity{")
    check(".profileSecret = identity->profileSecret" in shim_identity,
          "the GNS shim must send the stored profile secret in the hello's ClientIdentity")

    store_secret = function_body(store, "bool StoreServerSecret(")
    check("!IsServerSecret(a_serverSecret)" in store_secret,
          "the sidecar must reject a profile secret that is not 32 lowercase hex characters")

    check('*packetType == "saveAttestedAck"' in inbound and '*packetType == "saveAttestedRejected"' in inbound,
          "the client must handle the server's save attestation acknowledgment and rejection")

    save_path = function_body(lineage, "std::optional<std::filesystem::path> SaveFilePath(")
    engine_dir = function_body(lineage, "std::filesystem::path EngineSaveDirectory(")
    check('RE::GetINISetting("sLocalSavePath:General")' in engine_dir and '"Saves"' not in lineage,
          "the save folder must come from the engine's sLocalSavePath, never a hard-coded Saves folder")
    check("FindSaveDirectory(saveFolder, fileName)" in save_path and "g_searchedSaveFiles.emplace(a_saveFile).second" in save_path,
          "SaveFilePath must search for a save it cannot find, once per save name")
    check(save_path.index("engineDirectory / fileName, ec") < save_path.index("g_saveDirectory / fileName, ec"),
          "the engine save folder must win over the last folder a search found")
    check("SaveFilePath(a_saveFile, true)" in lineage and "SaveFilePath(g_pendingRecord->saveName, false)" in lineage,
          "only hashing may search; the per-frame pending poll must not")

    announce_recorded = function_body(lineage, "void AnnounceRecordedAttestations(")
    check("if (!F4T::SaveProfileLogic::CanRebindProfileSave(a_characterId, save.saveFile)) continue;" in announce_recorded,
          "replayed attestations must exclude saves belonging to other characters")
    check(announce_recorded.index("CanRebindProfileSave") < announce_recorded.index("AnnounceSaveAttestation"),
          "character ownership must be checked before replaying each attestation")


    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TSaveLineageEnforcementContract: all checks passed.")


_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TClientModpackProbeContract.py", _section_001_f4tclientmodpackprobecontract),
    ("CO-BETA/plugin/tests/F4TLoadOrderContract.py", _section_002_f4tloadordercontract),
    ("CO-BETA/plugin/tests/F4TModpackAutoFixContract.py", _section_004_f4tmodpackautofixcontract),
    ("CO-BETA/plugin/tests/F4TModpackCacheContract.py", _section_005_f4tmodpackcachecontract),
    ("CO-BETA/plugin/tests/F4TModpackChunksContract.py", _section_006_f4tmodpackchunkscontract),
    ("CO-BETA/plugin/tests/F4TModpackGateUxContract.py", _section_007_f4tmodpackgateuxcontract),
    ("CO-BETA/plugin/tests/F4TModpackProgressContract.py", _section_008_f4tmodpackprogresscontract),
    ("CO-BETA/plugin/tests/F4TSaveLineageEnforcementContract.py", _section_009_f4tsavelineageenforcementcontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TSaveModpackContracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
