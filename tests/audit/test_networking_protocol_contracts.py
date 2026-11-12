#!/usr/bin/env python3
"""test_networking_protocol_contracts: consolidated contract sections.

Consolidated from 7 single-contract scripts so the group runs one process
instead of 7. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_networking_protocol_contracts.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


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




def _section_001_test_inbound_codec_decode_contract():
    """Contract moved verbatim from tests/audit/test_inbound_codec_decode_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_inbound_codec_decode_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(name):
        return (ROOT / name).read_text(encoding="utf-8")


    def function_body(text, signature):
        start = text.index(signature)
        indent = text[text.rindex("\n", 0, start) + 1:start]
        assert indent.strip() == "", signature
        return text[start:text.index("\n" + indent + "}\n", start)]


    CODEC_HEADER = "co-core/include/F4TPayloadCodec.h"
    CODEC_SOURCE = "co-core/src/transport/F4TPayloadCodec.cpp"
    CODEC_TESTS = "co-core/tests/F4TPayloadCodecTests.cpp"
    FRAGMENTS_SOURCE = "co-core/src/transport/F4TMessageFragments.cpp"
    FRAGMENTS_TESTS = "co-core/tests/F4TMessageFragmentsTests.cpp"
    RUNTIME = "CO-SERVER/server-cpp/src/ServerRuntime.inc"
    RUNTIME_MESSAGE = "CO-SERVER/server-cpp/src/ServerRuntimeMessage.inc"
    PROTOCOL_HEADER ="CO-SERVER/server-cpp/include/ServerProtocol.h"
    PROTOCOL_SOURCE = "CO-SERVER/server-cpp/src/ServerProtocol.cpp"
    BRIDGE = "CO-SERVER/server/native_transport/co_gns_server_bridge_internal.h"
    SHIM = "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp"
    DECODE_SCAN = ["co-core/src", "CO-SERVER/server-cpp/src", "CO-SERVER/server/native_transport", "CO-BETA/plugin/src"]
    CAP_SCAN = ["CO-SERVER/server-cpp/include", "CO-SERVER/server-cpp/src"]


    def test_codec_decode_requires_a_caller_cap():
        header = read(CODEC_HEADER)
        assert "Decode(std::span<const std::uint8_t> a_framed, std::size_t a_maxDecoded);" in header
        assert "a_maxDecoded = " not in header


    def test_codec_checks_every_size_before_allocating():
        codec = read(CODEC_SOURCE)
        assert "if (a_framed.size() > a_maxDecoded) {" in codec
        assert "if (body.size() > a_maxDecoded) {" in codec
        assert codec.index("if (declared > a_maxDecoded) {") < codec.index("std::vector<std::uint8_t> out(static_cast<std::size_t>(declared));")
        assert "return DecodeUnknownSize(body, a_maxDecoded);" in codec


    def test_unknown_size_frames_stream_under_the_cap():
        streaming = function_body(read(CODEC_SOURCE), "std::optional<std::vector<std::uint8_t>> DecodeUnknownSize(")
        assert "chunk(ZSTD_DStreamOutSize())" in streaming
        assert "ZSTD_decompressStream(stream, &output, &input)" in streaming
        assert "output.pos > a_maxDecoded - out.size()" in streaming
        assert "if (input.pos != input.size || out.empty()) {" in streaming
        assert "ZSTD_freeDStream(stream);" in streaming


    def test_every_production_decode_passes_a_cap():
        sites = []
        for directory in DECODE_SCAN:
            for path in sorted((ROOT / directory).rglob("*")):
                if path.suffix not in {".cpp", ".h", ".inc"}:
                    continue
                text = path.read_text(encoding="utf-8", errors="replace")
                for match in re.finditer(r"PayloadCodec::Decode\(", text):
                    sites.append(text[match.end():text.index(");", match.end())])
        assert len(sites) >= 2
        assert all("," in args for args in sites), sites


    def test_inbound_decode_runs_once_after_reassembly_and_before_identity():
        runtime = read(RUNTIME).replace('#include "ServerRuntimeMessage.inc"', read(RUNTIME_MESSAGE))
        fragments = runtime.index("F4T::MessageFragments::IsFragment(message)")
        decode = runtime.index("F4T::PayloadCodec::Decode(message, CommonwealthOnline::Server::kMaximumMessageBytes)")
        rate = runtime.index("CommonwealthOnline::Server::ObserveInboundDatagram(state.rate")
        identity = runtime.index("identity->InspectInbound(event.connection_id, message, unixNow, capacityAvailable, &packet)")
        assert fragments < decode < rate < identity
        assert runtime.count("PayloadCodec::Decode(") == 1
        assert 'protocol.QueueViolation(event.connection_id, CommonwealthOnline::Server::Violation::MalformedPayload, "Malformed inbound payload");' in runtime


    def test_parse_packet_only_parses_an_already_decoded_payload():
        protocol = read(PROTOCOL_SOURCE)
        body = function_body(protocol, "std::optional<Json> ParsePacket(std::span<const std::uint8_t> a_payload)")
        assert "a_payload.size() > kMaximumMessageBytes" in body
        assert "PayloadCodec" not in body
        assert "PayloadCodec::Decode" not in protocol


    def test_message_cap_is_defined_once_in_the_protocol_header():
        assert "constexpr std::size_t kMaximumMessageBytes = 256 * 1024;" in read(PROTOCOL_HEADER)
        definitions = []
        for directory in CAP_SCAN:
            for path in sorted((ROOT / directory).rglob("*")):
                if path.suffix in {".cpp", ".h", ".inc"} and "kMaximumMessageBytes =" in path.read_text(encoding="utf-8", errors="replace"):
                    definitions.append(path.relative_to(ROOT).as_posix())
        assert definitions == [PROTOCOL_HEADER]


    def test_bridge_and_plugin_limits_match_the_protocol():
        bridge = read(BRIDGE)
        assert "kDefaultMaximumMessageBytes = 256uz * 1024uz;" in bridge
        assert "if (a_bytes < kMinimumMaximumMessageBytes || a_bytes > kDefaultMaximumMessageBytes) {" in bridge
        assert "g_maximumMessageBytes.store(a_bytes, std::memory_order_relaxed);" in bridge
        assert "F4T::PayloadCodec::Decode(message, F4T::MessageFragments::MaximumAssembledBytes)" in read(SHIM)


    def test_fragment_assembler_rejects_empty_and_overcounted_messages():
        fragments = read(FRAGMENTS_SOURCE)
        assert "header.fragmentCount > header.totalSize" in fragments
        assert "if (body.empty()) {" in fragments
        tests = read(FRAGMENTS_TESTS)
        assert "headerOnly.resize(HeaderBytes);" in tests
        assert "const std::vector<std::uint8_t> overcounted{" in tests


    def test_codec_tests_cover_the_caps_and_unknown_size_streaming():
        tests = read(CODEC_TESTS)
        assert "assert(!Decode(rawPrefixed, 1));" in tests
        assert "assert(!Decode(plain, 99));" in tests
        assert "assert(Decode(plain, 100));" in tests
        assert "assert(!Decode(framed, large.size() - 1));" in tests
        assert "ZSTD_c_contentSizeFlag, 0" in tests
        assert "assert(!Decode(framed, 1024));" in tests
        assert "assert(*decoded == source);" in tests


    def main() -> int:
        tests = [value for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for test in tests:
            test()
        print(f"inbound codec decode contract: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_002_test_protocol_cache_and_ready_contract():
    """Contract moved verbatim from tests/audit/test_protocol_cache_and_ready_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_protocol_cache_and_ready_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    ADAPTER_FAMILY = ("co-core/src/protocol/F4TGnsProtocolAdapter.cpp", "co-core/src/protocol/F4TGnsProtocolAdapterInternal.h", "co-core/src/protocol/F4TGnsProtocolAdapterImpl.h", "co-core/src/protocol/F4TGnsProtocolAdapterOutbound.cpp", "co-core/src/protocol/F4TGnsProtocolAdapterInbound.cpp")
    PROFILE_STORE ="CO-BETA/plugin/src/F4TServerProfileStore.cpp"
    PROFILE_STORE_HEADER = "CO-BETA/plugin/include/F4TServerProfileStore.h"
    PROFILE_STORE_TESTS = "CO-BETA/plugin/tests/F4TServerProfileStoreTests.cpp"
    ADAPTER_TESTS = "co-core/tests/F4TGnsProtocolAdapterTests.cpp"
    CLIENT_BRIDGE = "CO-BETA/plugin/gns-bridge/co_gns_client_bridge.cpp"

    PER_PLAYER_CACHES = (
        "remoteDurablePlayerState",
        "latestRemoteTransform",
        "pendingRemoteActionEvents",
        "pendingRemoteVitals",
    )


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def block(text: str, start: str, end: str) -> str:
        begin = text.find(start)
        if begin < 0:
            return ""
        finish = text.find(end, begin + len(start))
        return text[begin:] if finish < 0 else text[begin:finish]


    def main() -> int:
        failures: list[str] = []

        adapter = "".join(read(name) for name in ADAPTER_FAMILY)

        session_ready = block(adapter, "InboundResult Session::Impl::HandleSessionReady(const Json& packet)", "InboundResult Session::Impl::HandleSessionEnded(")
        require(bool(session_ready), "#8: HandleSessionReady must exist in the protocol adapter", failures)

        require('packet.value("resumeAckRequired"' not in adapter,
                "#8: resumeAckRequired must be strict-type read, not packet.value (which throws on a wrong type)", failures)
        require('ReadBoolean(packet, "resumeAckRequired")' in session_ready,
                "#8: resumeAckRequired must be read through ReadBoolean", failures)
        require('ReadBoolean(packet, "multiplayerSaveAdmin")' in session_ready,
                "#8: multiplayerSaveAdmin must be read through ReadBoolean before the commit", failures)

        commit_call = session_ready.find("readyIdentityCommitter(*readyIdentity")
        ack_read = session_ready.find('ReadBoolean(packet, "resumeAckRequired")')
        staging_dump = session_ready.find("readyWelcomeText = readyWelcome.dump();")
        catch_reject = session_ready.find("} catch (...) {")
        ack_payload = session_ready.find("ackPayload = ack.dump();")
        require(commit_call >= 0, "#8: the credential committer must still be invoked on the accepted path", failures)
        require(0 <= ack_read < commit_call, "#8: the acknowledgement flag must be validated before the credential commit", failures)
        require(0 <= ack_payload < commit_call, "#8: the acknowledgement payload must be built before the credential commit", failures)
        require(0 <= staging_dump < commit_call,
                "#8: the full welcome packet must be serialised before the credential commit", failures)
        require(0 <= catch_reject < commit_call,
                "#8: packet staging must sit inside a try/catch that rejects before the credential commit", failures)
        require("result.protocolViolation = true" in session_ready[catch_reject:catch_reject + 200],
                "#8: a staging failure must raise a protocol violation", failures)

        staging = session_ready[session_ready.find("try {"):catch_reject]
        require("readyIdentityCommitter(" not in staging,
                "#8: no credential commit may run inside the staging block", failures)
        commit_tail = session_ready[commit_call:]
        require("packet." not in commit_tail,
                "#8: no packet field may be read after the credential commit", failures)
        require('.dump()' not in commit_tail,
                "#8: no serialisation may run after the credential commit", failures)

        store = read(PROFILE_STORE)
        store_header = read(PROFILE_STORE_HEADER)
        store_tests = read(PROFILE_STORE_TESTS)
        require("LookupPreviousResumeCredential" in store_header,
                "#8: the profile store must expose the retained credential through LookupPreviousResumeCredential", failures)
        require("resumeTokenPreviousProtected" in store and "resumeExpiresAtPrevious" in store,
                "#8: the profile store must persist the replaced credential as a retained record", failures)

        store_credential = block(store, "bool StoreResumeCredential(", "bool ClearResumeCredential(")
        require("resumeTokenPreviousProtected" in store_credential,
                "#8: StoreResumeCredential must retain the replaced credential", failures)
        retain = store_credential.find('entry["resumeTokenPreviousProtected"] = *activeToken;')
        overwrite = store_credential.find('entry["resumeTokenProtected"] = *protectedToken;')
        require(0 <= retain < overwrite,
                "#8: the retained credential must be written in the same save before the active record is replaced", failures)
        require("SaveProfiles()" in store_credential[retain:],
                "#8: the retained record must be committed in the same atomic SaveProfiles call", failures)

        store_lookup = block(store, "std::optional<StoredResumeCredential> LookupResumeCredential(", "std::optional<StoredResumeCredential> LookupPreviousResumeCredential(")
        require("resumeTokenPreviousProtected" in store_lookup,
                "#8: LookupResumeCredential must fall back to the retained credential", failures)
        promote = store_lookup.find('mutableEntry["resumeTokenProtected"] = *previousToken;')
        require(promote >= 0,
                "#8: an unusable active record must promote the retained credential back to active", failures)
        require(0 <= store_lookup.find('mutableEntry.erase("resumeTokenPreviousProtected");') > promote,
                "#8: promotion must consume the retained record", failures)

        store_clear = block(store, "bool ClearResumeCredential(", "#ifdef F4T_SERVER_PROFILE_STORE_TESTING")
        require('entry->erase("resumeTokenPreviousProtected")' in store_clear,
                "#8: ClearResumeCredential must erase the retained credential too", failures)
        require('entry->erase("resumeExpiresAtPrevious")' in store_clear,
                "#8: ClearResumeCredential must erase the retained expiry too", failures)

        require("known-good-token" in store_tests and "restorable-token" in store_tests,
                "#8: the profile-store test must cover retention and recovery of the replaced credential", failures)
        require("LookupPreviousResumeCredential" in store_tests,
                "#8: the profile-store test must observe the retained credential", failures)

        adapter_tests = read(ADAPTER_TESTS)
        for test_name in (
            "TestSessionReadyAckFlagIsValidatedBeforeCommit",
            "TestSessionReadyInvalidUtf8NeverReachesCommit",
            "TestRemoteDisconnectEvictsCaches",
            "TestRosterPruneRequiresTwoAbsences",
            "TestRemotePlayerCacheCapacityRejectsNewIds",
            "TestNpcAuthorityScopeCap",
        ):
            require(test_name in adapter_tests, f"#7/#8: co-core adapter test must include {test_name}", failures)
            require(f"{test_name}();" in adapter_tests, f"#7/#8: {test_name} must be run from main", failures)

        bridge = read(CLIENT_BRIDGE)
        require("sent application data before proving its identity" in bridge,
                "#8: the client bridge must refuse application traffic from an unauthenticated server", failures)
        require("authState_ = AuthState::Authenticated" in bridge,
                "#8: the client bridge must gate application traffic on the pinned server identity", failures)
        auth_state = bridge.find("authState_ = AuthState::Authenticated")
        connected_event = bridge.find("MakeEvent(CO_GNS_EVENT_CONNECTED")
        require(0 <= auth_state < connected_event,
                "#8: the connected event must stay deferred until the server identity proof is verified", failures)

        forget = block(adapter, "void Session::Impl::ForgetRemotePlayer(std::uint32_t a_playerId)", "bool Session::Impl::ReserveRemotePlayer(")
        require(bool(forget), "#7: ForgetRemotePlayer must exist", failures)
        for cache in PER_PLAYER_CACHES:
            require(f"{cache}.erase(a_playerId);" in forget,
                    f"#7: ForgetRemotePlayer must erase {cache}", failures)
        for membership in ("trackedRemotePlayers.erase(a_playerId);", "liveRemotePlayers.erase(a_playerId);", "rosterAbsence.erase(a_playerId);"):
            require(membership in forget, f"#7: ForgetRemotePlayer must clear {membership}", failures)

        reserve = block(adapter, "bool Session::Impl::ReserveRemotePlayer(std::uint32_t a_playerId)", "void Session::Impl::HandleRemotePlayerDisconnect(")
        require("F4T::RemotePlayerCapacity::Decide(" in reserve,
                "#7: remote-player admission must use F4T::RemotePlayerCapacity::Decide", failures)
        require("F4T::RemotePlayerCapacity::Admission::RejectAtCapacity" in reserve,
                "#7: the cap path must be explicit", failures)
        require("liveRemotePlayers.contains(a_playerId)" in reserve and "ForgetRemotePlayer(*evicted)" in reserve,
                "#7: a live member at capacity must evict one tracked non-member", failures)

        disconnect = block(adapter, "void Session::Impl::HandleRemotePlayerDisconnect(const Json& packet)", "void Session::Impl::HandlePlayerRoster(")
        require("ForgetRemotePlayer(*playerId)" in disconnect, "#7: the disconnect packet must evict the player caches", failures)

        roster = block(adapter, "void Session::Impl::HandlePlayerRoster(const Json& packet)", "std::vector<OutboundMessage> Session::Impl::AdaptTransform(")
        require("players->size() > kMaximumRosterEntries" in roster, "#7: roster parsing must be bounded", failures)
        require("rosterAbsence.contains(playerId)" in roster, "#7: roster pruning must require two consecutive absences", failures)
        require("departed.push_back(playerId)" in roster, "#7: a second absence must evict the cached player", failures)
        require("liveRemotePlayers = std::move(membership);" in roster,
                "#7: the roster must only update the prune hint, never gate inserts", failures)

        reset = block(adapter, "void Session::Impl::Reset()", "void Session::Impl::ForgetRemotePlayer(")
        for membership in ("trackedRemotePlayers.clear();", "liveRemotePlayers.clear();", "rosterAbsence.clear();"):
            require(membership in reset, f"#7: Reset must clear {membership}", failures)

        require("kMaximumNpcAuthorityScopes = F4T::NpcAuthority::MaximumTrackedEpochScopes" in adapter,
                "#7: the authority scope cap must reuse F4T::NpcAuthority::MaximumTrackedEpochScopes", failures)
        authority = block(adapter, "void Session::Impl::HandleNpcAuthority(const Json& packet)", "InboundResult Session::Impl::HandlePlayerState(")
        require("npcAuthority.size() >= kMaximumNpcAuthorityScopes" in authority,
                "#7: new authority scopes must be dropped at the aggregate cap", failures)

        disconnect_dispatch = block(adapter, 'if (type && (*type == "disconnect" || *type == "peerHidden")) {',
                                    'if (type && *type == "playerRoster") {')
        require("HandleRemotePlayerDisconnect(packet);" in disconnect_dispatch,
                "#7: accepted disconnects must evict the player cache before fallthrough", failures)
        require("legacyMessages.push_back(packet.dump());" in disconnect_dispatch,
                "#7: accepted disconnects must still be forwarded unchanged", failures)
        require("visibilitySequences[*playerId]" in disconnect_dispatch,
                "#7: ordered disconnects must reject stale visibility events", failures)
        adapter_tests = read(ADAPTER_TESTS)
        require("TestRemoteDisconnectEvictsCaches" in adapter_tests and "TestOrderedPeerVisibility" in adapter_tests,
                "#7: adapter tests must cover legacy disconnect forwarding and ordered stale events", failures)
        dispatch_roster = adapter.find('if (type && *type == "playerRoster") {')
        require(dispatch_roster >= 0 and "HandlePlayerRoster(packet);" in adapter[dispatch_roster:dispatch_roster + 200],
                "#7: the roster packet must be handled before it falls through", failures)
        require(adapter[dispatch_roster:dispatch_roster + 260].find("legacyMessages.push_back(packet.dump());") >= 0,
                "#7: the roster packet must still be forwarded unchanged", failures)

        handle_transform = block(adapter, "InboundResult Session::Impl::HandleTransform(const Json& packet)", "InboundResult Session::Impl::AdaptInbound(")
        require("!ReserveRemotePlayer(*playerId)" in handle_transform and "legacyMessages.push_back(packet.dump());" in handle_transform,
                "#7: a transform whose cached update is dropped must still be forwarded for gameplay", failures)

        adapter_header = read("co-core/include/F4TGnsProtocolAdapter.h")
        require("resumeAcknowledged" in adapter_header,
                "#8: the adapter result must expose an acknowledgement-complete flag", failures)
        require("result.resumeAcknowledged = true;" in adapter,
                "#8: the flag must be set only after the credential commit", failures)
        require("g_visibilityPauseSupported" in adapter_header and "VisibilityPauseSupported()" in adapter_header,
                "#9: the adapter header must expose the visibilityPause gate", failures)
        inbound_src = read("co-core/src/protocol/F4TGnsProtocolAdapterInbound.cpp")
        pause_assign = inbound_src.find("visibilityPauseSupported = pauseSupported;")
        require(pause_assign >= 0 and inbound_src.find("g_visibilityPauseSupported.store(pauseSupported", pause_assign) > pause_assign,
                "#9: session-ready must store the gate right after visibilityPauseSupported is assigned", failures)
        publish_src = read("CO-BETA/plugin/src/F4TReplicationRuntimePublish.cpp")
        suppress_gate = publish_src.find("if (F4T::GnsProtocolAdapter::VisibilityPauseSupported()) {")
        require(suppress_gate >= 0 and suppress_gate < publish_src.find('SendPacketBytes(R"({"type":"visibilityPause","paused":true})")'),
                "#9: the solo-cell suppress branch must be gated on VisibilityPauseSupported()", failures)
        shim = read("CO-BETA/plugin/src/F4TGnsWinsockShim.cpp")
        require("FinalizeResumeCredential" in shim and "adapted.resumeAcknowledged" in shim,
                "#8: the retained credential must be finalized only after the acknowledgement is sent", failures)
        require("FinalizeResumeCredential" in store_header and "FinalizeResumeCredential" in store,
                "#8: the profile store must expose one-generation finalize", failures)
        require("FinalizeResumeCredential" in store_tests,
                "#8: finalize must be covered by the store tests", failures)
        require("resumeAcknowledged" in read("co-core/tests/F4TGnsProtocolAdapterAttestationTests.cpp"),
                "#8: the adapter tests must cover the acknowledgement flag", failures)
        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("Protocol cache and session-ready credential security contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_003_test_production_transport_contract():
    """Contract moved verbatim from tests/audit/test_production_transport_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_production_transport_contract.py")
    from pathlib import Path
    import re
    import runpy
    import tempfile

    ROOT = Path(__file__).resolve().parents[2]
    verify = runpy.run_path(str(ROOT / "scripts/verify-production-transport.py"))["verify"]
    XMAKE = "CO-BETA/plugin/xmake.lua"
    PLUGIN_SOURCES = ["CO-BETA/plugin/src/" + name for name in (
        "F4TNetworking.cpp", "F4TNetworkingPacketReaders.cpp", "F4TNetworkingInbound.cpp",
        "F4TNetworkingOutbound.cpp", "F4TNetworkingGns.cpp")]
    GNS_COMPILE = "CO-BETA/plugin/include/F4TNetworkingGnsCompile.h"
    GNS_INTERNAL = "CO-BETA/plugin/include/F4TNetworkingGnsInternal.h"
    RUNTIME = "CO-SERVER/server-cpp/src/ServerRuntime.inc"
    RUNTIME_MESSAGE = "CO-SERVER/server-cpp/src/ServerRuntimeMessage.inc"
    BOOTSTRAP = "CO-SERVER/server-cpp/src/ServerBootstrap.cpp"
    RUNTIME_SUPPORT = "CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp"
    ACCEPTANCE = next(
        relative for relative in (".github/public-ci/workflows/co-server-acceptance.yml", ".github/workflows/co-server-acceptance.yml")
        if (ROOT / relative).is_file())
    FILES = [XMAKE, *PLUGIN_SOURCES, GNS_COMPILE, GNS_INTERNAL, RUNTIME, RUNTIME_MESSAGE, BOOTSTRAP, RUNTIME_SUPPORT, ACCEPTANCE]
    with tempfile.TemporaryDirectory(prefix="co-production-transport-") as directory:
        root = Path(directory)
        for name in FILES:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / name).read_bytes())
        verify(root)
        glob = re.compile(r'(add_files\("src/\*\*\.cpp[^"\n]*)(?="\))')
        xmake_path = root / XMAKE
        xmake_path.write_text(glob.sub(r'\1|UnrelatedProbe*.cpp', xmake_path.read_text(encoding="utf-8"), count=1), encoding="utf-8")
        verify(root)
        rejected = 0

        def reject(name, change):
            nonlocal rejected
            path = root / name
            previous = path.read_text(encoding="utf-8")
            changed = change(previous)
            assert changed != previous
            path.write_text(changed, encoding="utf-8")
            try:
                try:
                    verify(root)
                except ValueError:
                    rejected += 1
                else:
                    raise AssertionError(f"accepted invalid transport contract: {name}")
            finally:
                path.write_text(previous, encoding="utf-8")

        for source in ("F4TNetworking.cpp", "F4TNetworkingOutbound.cpp"):
            reject(XMAKE, lambda text, source=source: text.replace("|" + source, "", 1))
            reject(XMAKE, lambda text, source=source: text.replace(
                f'add_files("src/{source}", {{\n        defines = networking_legacy_defines,',
                f'add_files("src/{source}", {{\n        defines = {{}},', 1))
            reject(XMAKE, lambda text, source=source: text.replace(
                f'add_files("src/{source}", {{\n        defines = networking_legacy_defines,\n        forceincludes = "F4TNetworkingGnsCompile.h"',
                f'add_files("src/{source}", {{\n        defines = networking_legacy_defines,\n        forceincludes = "incorrect.h"', 1))
        reject(XMAKE, lambda text: glob.sub(r'\1|F4TNetworkingInbound.cpp', text, count=1))
        reject(XMAKE, lambda text: text + '\nremove_files("src/F4TNetworkingInbound.cpp", "src/F4TNetworkingOutbound.cpp")\n')
        reject(XMAKE, lambda text: text.replace('    add_client_sources()', '    missing_sources()', 1))
        reject(RUNTIME, lambda text: text + "\nTcpListener\n")
        reject(BOOTSTRAP, lambda text: text + "\nSOCK_STREAM\n")
        reject(RUNTIME_SUPPORT, lambda text: text + "\nTcpTransport\n")
        reject(BOOTSTRAP, lambda text: text.replace("co_gns_server_create(", "co_gns_server_missing(", 1))
        reject(ACCEPTANCE, lambda text: text + "\nNamedAcceptance\n")
        reject(PLUGIN_SOURCES[0], lambda text: text + '\n#include "hidden.cpp"\n')
        reject(GNS_COMPILE, lambda text: text.replace('#define F4T_GNS_WINSOCK_REDIRECT 1', '#define F4T_GNS_WINSOCK_REDIRECT 0'))
        missing = root / GNS_INTERNAL
        previous = missing.read_bytes()
        missing.unlink()
        try:
            try:
                verify(root)
            except ValueError:
                rejected += 1
            else:
                raise AssertionError("accepted missing GNS internal header")
        finally:
            missing.write_bytes(previous)
        legacy = root / ".github/workflows/co-server-gns.yml"
        legacy.parent.mkdir(parents=True, exist_ok=True)
        legacy.write_text("legacy", encoding="utf-8")
        try:
            try:
                verify(root)
            except ValueError:
                rejected += 1
            else:
                raise AssertionError("accepted restored managed GNS workflow")
        finally:
            legacy.unlink()
        verify(root)
        assert rejected == 18
    print(f"production transport contract: valid split and {rejected} rejected mutations PASS")

def _section_004_test_networking_session_shutdown():
    """Contract moved verbatim from tests/audit/test_networking_session_shutdown.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_networking_session_shutdown.py")
    import inspect
    import tempfile
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    NETWORK_IMPL_SOURCE = ROOT / "CO-BETA/plugin/src/F4TNetworkingInbound.cpp"
    UI_SOURCE = ROOT / "CO-BETA/plugin/src/F4TUIClient.cpp"
    UI_HEADER = ROOT / "CO-BETA/plugin/include/F4TUIClient.h"

    def test_server_session_end_invalidates_pending_join_on_the_game_thread():
        network = NETWORK_IMPL_SOURCE.read_text(encoding="utf-8")

        assert '#include "F4TSessionLaunch.h"' in network

        packet_start = network.index("void HandleSessionEndedPacket(")
        packet_end = network.index("\n\tvoid HandleUnknownPacket", packet_start)
        packet = network[packet_start:packet_end]

        assert "F4T::Networking::DisconnectFromLocalServer();" in packet
        assert 'std::string{ "Server ended the session." }' in packet, (
            "a session end without a server reason must still produce a specific fallback reason."
        )
        assert "F4T::UIClient::NotifyTransportDisconnected(disconnectReason.c_str());" in packet, (
            "a server-initiated session end must route through NotifyTransportDisconnected with its "
            "own specific reason, the same seam a receive-thread transport loss uses, so both paths "
            "cancel a pending join and clear connection ownership identically."
        )
        assert "taskInterface->AddTask(" in packet

        disconnect_index = packet.index("F4T::Networking::DisconnectFromLocalServer();")
        notify_index = packet.index("F4T::UIClient::NotifyTransportDisconnected(disconnectReason.c_str());")
        add_task_index = packet.index("taskInterface->AddTask(")
        assert disconnect_index < notify_index < add_task_index, (
            "the transport must be torn down, then the pending join invalidated, before the "
            "notification task is even queued, so a superseded/cancelled join can never be "
            "reported as successful by a task still in flight."
        )

        ui_source = UI_SOURCE.read_text(encoding="utf-8")
        notify_start = ui_source.index("void NotifyTransportDisconnected(")
        notify_end = ui_source.index("\n\tvoid ShowOfflineSaveIgnoredNotification", notify_start)
        notify = ui_source[notify_start:notify_end]

        assert "g_connectionOwnerToken.store(0);" in notify, (
            "NotifyTransportDisconnected must clear connection ownership synchronously so a stale "
            "token cannot outlive the transport, regardless of which caller (session-ended packet or "
            "receive-thread loss) observed the disconnect."
        )
        assert "taskInterface->AddTask(" in notify
        inner_task_start = notify.index("taskInterface->AddTask(")
        inner_task = notify[inner_task_start:]
        assert "F4T::SessionLaunch::CancelPendingJoin(" in inner_task, (
            "pending-join invalidation must run inside NotifyTransportDisconnected's own F4SE "
            "game-thread task, not on the receive thread that dispatches server packets."
        )
        assert "g_joinGeneration.fetch_add(1);" in inner_task, (
            "every transport-disconnect notification must also bump the join generation so any "
            "in-flight join continuation queued before the disconnect revalidates itself as stale."
        )

    def test_connection_ownership_token_is_reset_when_the_owned_connection_is_torn_down():
        source = UI_SOURCE.read_text(encoding="utf-8")

        disconnect_start = source.index("void Callback_Disconnect()")
        disconnect_end = source.index("\n\t\t}\n", disconnect_start) + len("\n\t\t}\n")
        disconnect = source[disconnect_start:disconnect_end]
        assert "RunOrDeferSessionExit(" in disconnect and "DisconnectNow" in disconnect, (
            "Callback_Disconnect must route the explicit disconnect through the save guard into DisconnectNow."
        )

        now_start = source.index("void DisconnectNow()\n")
        now_end = source.index("\n\t\t}\n", now_start) + len("\n\t\t}\n")
        now = source[now_start:now_end]
        assert "g_connectionOwnerToken.store(0);" in now, (
            "an explicit disconnect must clear connection ownership so a later stale "
            "attempt sharing that token cannot believe it still owns the torn-down connection."
        )

        launch_start = source.index("auto finishJoin =")
        launch_end = source.index("\n\t\tvoid Callback_Disconnect()", launch_start)
        launch = source[launch_start:launch_end]
        assert "finishJoin(ok, launchError);" in launch
        assert "if (disconnected) {" in launch
        assert "g_connectionOwnerToken.compare_exchange_strong(owner, 0);" in launch


        assert "void ResetConnectionOwnership()" in source
        reset_start = source.index("void ResetConnectionOwnership()")
        reset_end = source.index("\n\t}", reset_start)
        reset = source[reset_start:reset_end]
        assert "g_connectionOwnerToken.store(0);" in reset

        header = UI_HEADER.read_text(encoding="utf-8")
        assert "void ResetConnectionOwnership();" in header, (
            "ResetConnectionOwnership must be exported so other translation units (the "
            "server-session-end path) can clear ownership without reaching into UIClient's "
            "anonymous namespace."
        )

        network = NETWORK_IMPL_SOURCE.read_text(encoding="utf-8")
        packet_start = network.index("void HandleSessionEndedPacket(")
        packet_end = network.index("\n\tvoid HandleUnknownPacket", packet_start)
        packet = network[packet_start:packet_end]
        assert "F4T::UIClient::ResetConnectionOwnership();" in packet, (
            "a server-initiated session end must also clear connection ownership, matching "
            "the explicit-disconnect path, so a stale token cannot outlive its connection."
        )
        disconnect_call_index = packet.index("F4T::Networking::DisconnectFromLocalServer();")
        reset_call_index = packet.index("F4T::UIClient::ResetConnectionOwnership();")
        assert reset_call_index > disconnect_call_index


    def main() -> int:
        failures: list[str] = []
        tests = [(name, value) for name, value in sorted(_merge_ns.items()) if name.startswith("test_") and callable(value)]
        for name, func in tests:
            try:
                if "tmp_path" in inspect.signature(func).parameters:
                    with tempfile.TemporaryDirectory() as directory:
                        func(Path(directory))
                else:
                    func()
            except AssertionError as exc:
                failures.append(f"{name}: {exc}")
            except Exception as exc:  # noqa: BLE001 - report, don't hide, unexpected errors
                failures.append(f"{name}: unexpected {type(exc).__name__}: {exc}")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print(f"{Path(__file__).stem}: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_005_test_pr70_transport_shutdown_regressions():
    """Contract moved verbatim from tests/audit/test_pr70_transport_shutdown_regressions.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_pr70_transport_shutdown_regressions.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def main() -> int:
        failures: list[str] = []

        bridge = read("CO-SERVER/server/native_transport/co_gns_server_bridge.cpp") + read("CO-SERVER/server/native_transport/co_gns_server_bridge_internal.h")
        transport_failure = bridge.find("networking_->AcceptConnection(info.m_hConn) != k_EResultOK")
        transport_return = bridge.find("return;", transport_failure)
        transport_block = bridge[transport_failure:transport_return] if transport_failure >= 0 and transport_return >= 0 else ""
        require("applicationAdmitted" in bridge, "PR70-TRANSPORT: application admission must be tracked separately from GNS admission", failures)
        require("events_.push_back" in transport_block and "CO_GNS_EVENT_DISCONNECTED" in transport_block,
                "PR70-TRANSPORT: GNS failure after application admission must emit cleanup", failures)

        endpoint = read("CO-SERVER/server/native_transport/co_gns_server_endpoint.cpp")
        remote_start = endpoint.find("int co_gns_server_remote_ipv4(")
        remote_query = endpoint[remote_start:] if remote_start >= 0 else ""
        require("SteamNetworkingSockets()" not in remote_query and "server->Networking()" in remote_query,
                "PR70-ENDPOINT: remote IPv4 query must use the bridge handle interface, not the global GNS accessor", failures)
        require(0 <= remote_query.find("server->OwnsConnection(") < remote_query.find("GetConnectionInfo("),
                "PR70-ENDPOINT: remote IPv4 query must check handle ownership before querying GNS", failures)
        owns_start = bridge.find("bool OwnsConnection(")
        owns_block = bridge[owns_start:bridge.find("    };", owns_start)] if owns_start >= 0 else ""
        require("connections_.contains(connection)" in owns_block,
                "PR70-ENDPOINT: OwnsConnection must answer only for connections accepted by this bridge", failures)

        worker = read("CO-SERVER/server-cpp/include/WorkerExit.h")
        worker_grace = worker.find("WorkerStopGrace")
        require(worker_grace >= 0 and "a_exit.exited.wait_for(lock, WorkerStopGrace" in worker,
                "PR70-WORKER: a stop must wait a bounded grace for the worker before abandoning it", failures)
        abandon_store = worker.find("a_exit.abandoned.store(true")
        worker_join = worker.find("a_thread.join()")
        worker_detach = worker.find("a_thread.detach()")
        require(0 <= abandon_store < worker_detach,
                "PR70-WORKER: the abandon flag must be set before the worker is detached", failures)
        require(0 <= worker.find("if (finished) {") < worker_join < worker_detach,
                "PR70-WORKER: only a worker that reported finished may be joined", failures)

        verifier = read("CO-SERVER/server-cpp/src/SteamWebApiVerifier.cpp")
        verifier_header = read("CO-SERVER/server-cpp/include/SteamWebApiVerifier.h")
        require("std::thread([this]" not in verifier and "std::thread([state]" in verifier,
                "PR70-VERIFIER: the Steam worker must not capture the verifier it belongs to", failures)
        require("static void Run(std::shared_ptr<State> a_state);" in verifier_header,
                "PR70-VERIFIER: the Steam worker state must be shared with the thread", failures)
        require("std::shared_ptr<State> _state;" in verifier_header,
                "PR70-VERIFIER: the verifier must hold its worker state through a shared_ptr", failures)
        verifier_stop = verifier.find("void SteamWebApiVerifier::Stop()")
        verifier_stop_block = verifier[verifier_stop:verifier_stop + 200] if verifier_stop >= 0 else ""
        require("StopWorker(_thread, _state->exit)" in verifier_stop_block,
                "PR70-VERIFIER: shutdown must go through the bounded worker stop", failures)
        verifier_run = verifier.find("void SteamWebApiVerifier::Run(")
        verifier_run_block = verifier[verifier_run:] if verifier_run >= 0 else ""
        verifier_abandon = verifier_run_block.find("a_state->exit.abandoned.load(")
        require(0 <= verifier_abandon < verifier_run_block.find("SteamTicketOutcome::Retry && job.attempt"),
                "PR70-VERIFIER: an abandoned transfer must not be retried", failures)
        require(0 <= verifier_abandon < verifier_run_block.find("a_state->results.push_back("),
                "PR70-VERIFIER: an abandoned transfer must not deliver a verdict", failures)

        shim = read("CO-BETA/plugin/src/F4TGnsWinsockShim.cpp")
        idle = shim.find("if (pollResult == 0) {", shim.find("int Recv(SOCKET"))
        require(0 <= idle < shim.find("PollDeferredHello()", idle) < shim.find("SetError(WSAEWOULDBLOCK)", idle),
                "M14: the receive path must send the deferred hello when the socket is idle", failures)
        connect_source = read("CO-BETA/plugin/src/F4TNetworkingGns.cpp")
        loader_source = read("CO-BETA/plugin/src/F4TSteamApiLoader.cpp")
        require("CreateAuthTicketHex" not in connect_source and "a_identity.beginSteamTicket =" in connect_source and ".Request(" not in loader_source,
                "M14: the client must request its Steam ticket after welcome without blocking the receive thread", failures)
        require('welcome["steamTicketNonce"]' in read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
                and "F4T::SteamId::BoundWebApiIdentity(nonce))" in read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc"),
                "M14: the server must send a per-connection nonce in welcome and verify tickets against it", failures)

        registrar = read("CO-SERVER/server-cpp/src/DirectoryRegistrar.cpp")
        registrar_header = read("CO-SERVER/server-cpp/include/DirectoryRegistrar.h")
        require("std::thread([this]" not in registrar and "std::thread([state, exit]" in registrar,
                "PR70-DIRECTORY: the directory worker must not capture the registrar it belongs to", failures)
        require("static void Worker(std::shared_ptr<State> a_state, std::shared_ptr<WorkerExit> a_exit);" in registrar_header,
                "PR70-DIRECTORY: the directory worker state must be shared with the thread", failures)
        registrar_stop = registrar.find("void DirectoryRegistrar::Stop()")
        registrar_stop_block = registrar[registrar_stop:registrar_stop + 200] if registrar_stop >= 0 else ""
        require(0 <= registrar_stop_block.find("if (StopWorker(_thread, *_exit)) {") <
                registrar_stop_block.find("Unregister();"),
                "PR70-DIRECTORY: the listing may only be deleted when the worker exited in time", failures)
        for symbol, label in (("DirectoryRegistrar::RegisterServer", "registration"),
                              ("DirectoryRegistrar::SendHeartbeat", "heartbeat")):
            call_start = registrar.find("std::chrono::seconds " + symbol + "(")
            call_block = registrar[call_start:call_start + 3000] if call_start >= 0 else ""
            sender_call = call_block.find("a_state.sender(")
            exit_lock = call_block.find("const auto exitLock = LockExit(a_exit);")
            abandoned_check = call_block.find("if (Abandoned(a_exit)) {")
            require(0 <= sender_call < exit_lock < abandoned_check,
                    f"PR70-DIRECTORY: an abandoned {label} call must be dropped under the exit lock before it reports status", failures)
        require(registrar.count("activePlayers->load(") == 4,
                "PR70-DIRECTORY: the player count may only be read by the owner or under the worker's exit lock", failures)
        worker_start = registrar.find("void DirectoryRegistrar::Worker(")
        worker_block = registrar[worker_start:registrar.find("MarkWorkerExited", worker_start)] if worker_start >= 0 else ""
        require(0 <= worker_block.find("std::scoped_lock lock{ a_exit->mutex };")
                < worker_block.find("if (a_exit->stopping) {")
                < worker_block.find("players = a_state->activePlayers->load(")
                < worker_block.find("TickServer("),
                "PR70-DIRECTORY: the worker must read the player count under the exit lock after its stopping check", failures)
        require("activePlayers" not in registrar[registrar.find("Json DirectoryRegistrar::HeartbeatBody("):registrar.find("std::chrono::seconds DirectoryRegistrar::FailStatus(")],
                "PR70-DIRECTORY: the heartbeat body must use the player snapshot, not the live counter", failures)

        discovery = read("CO-SERVER/server-cpp/src/LanDiscoveryService.cpp")
        timeout_start = discovery.find("SO_RCVTIMEO")
        timeout_block = discovery[max(0, timeout_start - 120):timeout_start + 900] if timeout_start >= 0 else ""
        require("!ConfigureSocketTimeout(socketHandle, SO_RCVTIMEO" in timeout_block
                and "!ConfigureSocketTimeout(socketHandle, SO_SNDTIMEO" in timeout_block,
                "PR70-LAN: LAN discovery must install its receive and send timeouts", failures)
        require("return false" in timeout_block,
                "PR70-LAN: LAN discovery must fail startup when timeout installation fails", failures)
        helper_start = discovery.find("bool ConfigureSocketTimeout(")
        helper_block = discovery[helper_start:discovery.find("\n\t\t}", helper_start)] if helper_start >= 0 else ""
        require("setsockopt(" in helper_block and "a_option" in helper_block and "SocketTimeoutMilliseconds" in helper_block,
                "PR70-LAN: the socket timeout helper must install the requested option", failures)
        require("a_error = SocketError(a_operation)" in helper_block and "return !failed" in helper_block,
                "PR70-LAN: the socket timeout helper must report the failing call and fail startup", failures)

        stop_start = discovery.find("void LanDiscoveryService::Stop()")
        stop_block = discovery[stop_start:discovery.find("std::uint16_t LanDiscoveryService::LocalPort", stop_start)] if stop_start >= 0 else ""
        require(stop_block != "" and "shutdown(" not in stop_block,
                "PR70-LAN: shutdown() is inert on a datagram socket; Stop must close it instead", failures)
        windows_start = stop_block.find("#ifdef _WIN32")
        windows_block = stop_block[windows_start:stop_block.find("#else", windows_start)] if windows_start >= 0 else ""
        require(windows_block != "" and 0 <= windows_block.find("CloseSocket(") < windows_block.find("_thread.join()"),
                "PR70-LAN: Windows Stop must close the socket before joining the reader", failures)

        runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
        runtime_support = read("CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp")
        runtime_support_header = read("CO-SERVER/server-cpp/include/ServerRuntimeSupport.h")
        signal_units = runtime + runtime_support
        require("volatile std::sig_atomic_t g_running" in runtime_support,
                "PR70-SIGNAL: process shutdown state must use sig_atomic_t in the signal handler", failures)
        require("extern volatile std::sig_atomic_t g_running" in runtime_support_header,
                "PR70-SIGNAL: sig_atomic_t shutdown state must be shared through its own header", failures)
        require("std::atomic_bool g_running" not in signal_units + runtime_support_header,
                "PR70-SIGNAL: signal handler must not depend on std::atomic_bool::store", failures)
        require("g_running.store" not in signal_units and "g_running.load" not in signal_units,
                "PR70-SIGNAL: sig_atomic_t state must use signal-safe reads and writes", failures)
        require("g_running = 0" in runtime_support and "g_running = 0" in runtime,
                "PR70-SIGNAL: sig_atomic_t state must use assignment", failures)
        require("while (g_running != 0)" in runtime,
                "PR70-SIGNAL: sig_atomic_t state must use comparison", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("PR 70 transport/shutdown regression contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_006_test_client_version_gate_contract():
    """Contract moved verbatim from tests/audit/test_client_version_gate_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_client_version_gate_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]
    IDENTITY = ROOT / "co-core" / "src" / "session" / "F4TIdentityProtocol.cpp"
    SERVER_BOOTSTRAP = ROOT / "CO-SERVER" / "server-cpp" / "src" / "ServerBootstrap.cpp"
    ALERT = ROOT / "CO-BETA" / "plugin" / "src" / "F4THandshakeRejectionAlert.cpp"


    def _read(path):
        return path.read_text(encoding="utf-8").replace("\r\n", "\n")


    def _body(source, signature):
        start = source.index(signature)
        open_brace = source.index("{", start)
        depth = 0
        for index in range(open_brace, len(source)):
            depth += {"{": 1, "}": -1}.get(source[index], 0)
            if depth == 0:
                return source[open_brace:index + 1]
        raise AssertionError(f"unterminated body for {signature}")


    def _in_order(text, *needles):
        last = -1
        for needle in needles:
            position = text.find(needle, last + 1)
            assert position > last, f"{needle!r} is missing or out of order"
            last = position


    def test_client_version_reject_carries_the_plain_reason():
        identity = _read(IDENTITY)
        start = identity.index('ValidationResult::Reject("client_version_mismatch"')
        reject = identity[start:identity.index(");", start)]

        _in_order(
            reject,
            '"client_version_mismatch"',
            "You are running an older version of Commonwealth Online (",
            "Please update to ",
        )
        assert '"Server requires client version "' not in identity


    def test_server_defaults_the_required_version_before_the_configuration_is_parsed():
        prepare = _body(_read(SERVER_BOOTSTRAP), "ServerBootstrap::Prepare(")

        _in_order(
            prepare,
            "a_out.options.requiredClientVersion = std::string{ F4T::ClientCompatibility::kClientVersion };",
            "if (configPath) {",
            "a_out.options = Configuration::Parse(root, configPath->parent_path());",
        )


    def test_client_version_rejection_shows_the_server_reason_with_the_update_button():
        alert = _read(ALERT)
        presentation = _body(alert, "Presentation BuildPresentation(")
        branch = presentation[presentation.index('a_code == "client_version_mismatch"'):]
        branch = branch[:branch.index("} else if")]
        assert "result.updateClass = true;" in branch

        shown = alert[alert.index("auto presentation = BuildPresentation(code);"):alert.index("std::array<COMMONWEALTH_ONLINE_UI_API::AlertAction, 4>")]
        _in_order(
            shown,
            "auto presentation = BuildPresentation(code);",
            'if (code == "client_version_mismatch" && !reason.empty()) {',
            "presentation.message = reason;",
            r'presentation.message += std::format("\nReason: {}", reason);',
        )


    def main():
        tests = [v for k, v in sorted(_merge_ns.items()) if k.startswith("test_") and callable(v)]
        for test in tests:
            test()
        print(f"{Path(__file__).stem}: {len(tests)} checks passed")
        return 0


    _merge_ns = dict(locals())
    if True:
        _merge_exit(main())

def _section_007_test_required_client_version_contract():
    """Contract moved verbatim from tests/audit/test_required_client_version_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_required_client_version_contract.py")
    import json
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def test_shipped_required_client_version_matches_client_constant() -> None:
        header = (ROOT / "co-core/include/F4TClientCompatibility.h").read_text(encoding="utf-8")
        match = re.search(r'kClientVersion\s*=\s*"([^"]*)"', header)
        assert match, "F4TClientCompatibility.h must define kClientVersion as a string literal"
        shipped = json.loads((ROOT / "CO-SERVER/server-cpp/commonwealth-server-native.json").read_text(encoding="utf-8"))
        assert shipped.get("required_client_version") == match.group(1), (
            f"commonwealth-server-native.json required_client_version must be {match.group(1)!r} (kClientVersion)"
        )


    if True:
        test_shipped_required_client_version_matches_client_constant()


_SECTIONS = [
    ("tests/audit/test_inbound_codec_decode_contract.py", _section_001_test_inbound_codec_decode_contract),
    ("tests/audit/test_protocol_cache_and_ready_contract.py", _section_002_test_protocol_cache_and_ready_contract),
    ("tests/audit/test_production_transport_contract.py", _section_003_test_production_transport_contract),
    ("tests/audit/test_networking_session_shutdown.py", _section_004_test_networking_session_shutdown),
    ("tests/audit/test_pr70_transport_shutdown_regressions.py", _section_005_test_pr70_transport_shutdown_regressions),
    ("tests/audit/test_client_version_gate_contract.py", _section_006_test_client_version_gate_contract),
    ("tests/audit/test_required_client_version_contract.py", _section_007_test_required_client_version_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_networking_protocol_contracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
