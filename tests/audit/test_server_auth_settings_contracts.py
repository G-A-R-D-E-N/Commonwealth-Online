#!/usr/bin/env python3
"""test_server_auth_settings_contracts: consolidated contract sections.

Consolidated from 4 single-contract scripts so the group runs one process
instead of 4. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_server_auth_settings_contracts.py
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


def _section_001_test_server_auth_security_contract():
    """Contract moved verbatim from tests/audit/test_server_auth_security_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_server_auth_security_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]

    CLIENT = "CO-BETA/plugin/gns-bridge/co_gns_client_bridge.cpp"
    PROTOCOL = "CO-BETA/plugin/gns-bridge/co_gns_auth_protocol.h"
    SERVER_INTERNAL = "CO-SERVER/server/native_transport/co_gns_server_bridge_internal.h"
    SERVER_BRIDGE = "CO-SERVER/server/native_transport/co_gns_server_bridge.cpp"


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def index_of(text: str, needle: str, failures: list[str], label: str) -> int:
        position = text.find(needle)
        if position < 0:
            failures.append(f"{label}: missing {needle!r}")
        return position


    def main() -> int:
        failures: list[str] = []

        client = read(CLIENT)
        protocol = read(PROTOCOL)
        server_internal = read(SERVER_INTERNAL)
        server_bridge = read(SERVER_BRIDGE)

        require('#include "co_gns_auth_protocol.h"' in client, "L4-CLIENT: the GNS client bridge must include the shared transport-auth protocol", failures)
        require("co_gns_auth_protocol.h" in server_internal, "L4-SERVER: the GNS server bridge must include the shared transport-auth protocol", failures)

        require("kHelloMagic{ 'C', 'O', 'A', 'H' }" in protocol, "L4-WIRE: HELLO magic must be COAH", failures)
        require("kReplyMagic{ 'C', 'O', 'A', 'R' }" in protocol, "L4-WIRE: REPLY magic must be COAR", failures)
        require("kProtocolVersion = 1" in protocol, "L4-WIRE: protocol version must be pinned", failures)
        require('kSignatureDomain{ "co-gns-server-auth-v1" }' in protocol, "L4-WIRE: the signature must be domain separated", failures)
        for source, label in ((client, "client"), (server_internal, "server"), (server_bridge, "server-test-client")):
            require('"COAH"' not in source and '"COAR"' not in source, f"L4-WIRE: {label} must not redefine the frame magic outside the shared header", failures)

        donna_guard = protocol.find("#if defined(_WIN32)")
        donna_extern = protocol.find('extern "C"')
        require(0 <= donna_guard < donna_extern < protocol.find("#endif", donna_guard), "L4-CRYPTO: the vendored ed25519 symbols must only be declared for the Windows BCrypt build", failures)
        require("void ed25519_publickey(" in protocol and "void ed25519_sign(" in protocol and "int ed25519_sign_open(" in protocol, "L4-CRYPTO: the vendored ed25519 symbols must be declared", failures)
        require("ed25519_publickey(a_seed.data(), a_out.data())" in protocol, "L4-CRYPTO: the Windows server public key must be derived from the persisted seed", failures)
        require("ed25519_sign(a_message, a_size, a_seed.data(), publicKey.data(), a_out.data())" in protocol, "L4-CRYPTO: the Windows server must sign the client nonce", failures)
        require("ed25519_sign_open(a_message, a_size, a_publicKey.data(), a_signature.data()) == 0" in protocol, "L4-CRYPTO: the Windows client must verify the identity signature", failures)
        require("EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519" in protocol and "EVP_PKEY_get_raw_public_key(" in protocol, "L4-CRYPTO: non-Windows builds must derive Ed25519 keys through OpenSSL EVP", failures)
        require("EVP_DigestSign(" in protocol and "EVP_DigestVerify(" in protocol, "L4-CRYPTO: non-Windows builds must sign and verify Ed25519 through OpenSSL EVP", failures)

        client_connected = index_of(client, "void AdvanceAuthentication()", failures, "L4-CLIENT")
        client_connected_event = index_of(client, "CO_GNS_EVENT_CONNECTED", failures, "L4-CLIENT")
        require(client.count("CO_GNS_EVENT_CONNECTED") == 1, "L4-CLIENT: CONNECTED must be emitted exactly once, from the post-authentication path", failures)
        require(client_connected >= 0 and client_connected_event > client_connected, "L4-CLIENT: CONNECTED must be emitted only after server authentication completes", failures)
        require("authState_ = AuthState::HelloPending;" in client, "L4-CLIENT: the connected callback must start the identity challenge instead of reporting a session", failures)
        hello_branch = client.find("k_ESteamNetworkingConnectionState_Connected")
        require(hello_branch >= 0 and "CO_GNS_EVENT_CONNECTED" not in client[hello_branch:hello_branch + 900], "L4-CLIENT: the connected callback must not queue CONNECTED before authentication", failures)

        require("authState_ != AuthState::Authenticated" in client, "L4-CLIENT: application sends must be refused until the server identity is proven", failures)
        check_pin = index_of(client, "co_gns::auth::CheckPin(", failures, "L4-CLIENT-PIN")
        verify = index_of(client, "co_gns::auth::VerifyNonce(", failures, "L4-CLIENT-PIN")
        require(0 <= verify < check_pin, "L4-CLIENT-PIN: the signature must verify before the pin is compared, so an unverified key is never written as a first-use pin", failures)
        require(verify < client_connected_event, "L4-CLIENT-PIN: the session must not be reported before the identity signature verifies", failures)
        require("kAuthFailureReason" in client and "FailAuthentication" in client, "L4-CLIENT: an identity failure must close the connection with a distinct reason", failures)
        require("the server sent application data before proving its identity" in client, "L4-CLIENT: pre-authentication application data must abort the session", failures)
        require("authReply_.assign" in client, "L4-CLIENT: the identity proof must be captured and checked by the bridge, not surfaced as application data", failures)
        require("authReplyReceived_" in client and "} else if (authHeldFrames_.size() < kMaximumHeldFrames) {" in client, "L4-RACE-CLIENT: a frame that follows the identity proof must be held within a bound, not treated as unexpected data", failures)
        held_flush = index_of(client, "for (auto& frame : authHeldFrames_) {", failures, "L4-RACE-CLIENT")
        require(0 <= client_connected_event < held_flush, "L4-RACE-CLIENT: held frames must be delivered only after CONNECTED", failures)
        require(client.count("ResetAuthentication();") == 3 and client.count("MakeFrameEvent(connection, message->m_pData, size)") == 1, "L4-RACE-CLIENT: connect, disconnect and stop must reset the handshake state, and post-authentication frames must go through MakeFrameEvent", failures)

        require("PinResult::Mismatch" in protocol, "L4-PIN: a pin mismatch must be a distinct outcome", failures)
        require('EnvValue("CO_GNS_CLIENT_PIN_FILE")' in protocol, "L4-PIN: the pin store path must be overridable", failures)
        require('EnvValue("CO_GNS_SERVER_PIN")' in protocol, "L4-PIN: an out-of-band pin must be usable", failures)
        require("LOCALAPPDATA" in protocol, "L4-PIN: the pin store must persist under the user profile by default", failures)
        pin_match = index_of(protocol, "if (entry.value == a_publicKey) {", failures, "L4-PIN")
        pin_write = index_of(protocol, "if (!WritePinFile(path, entries)) {", failures, "L4-PIN")
        require(0 <= pin_match < pin_write, "L4-PIN: an existing pin must be matched before any first-use write", failures)
        require("could not persist the first-use identity pin" in protocol, "L4-PIN: an unpersistable first-use pin must fail closed", failures)

        require("_dupenv_s" in protocol and "std::getenv(" not in protocol.split("#else")[0], "L4-BUILD: the MSVC build must read environment variables without the deprecated getenv (fatal under /W4 /WX)", failures)
        require("std::fopen" not in protocol and "std::ofstream" in protocol, "L4-BUILD: file access must avoid the deprecated fopen (fatal under /W4 /WX)", failures)
        require("while (const auto" not in protocol, "L4-BUILD: no while-condition statement declaration (not valid C++; MSVC rejects it)", failures)
        seed_unlink = index_of(protocol, "::unlink(a_temporary.c_str());", failures, "L4-SEED-FILE")
        seed_open = index_of(protocol, "::open(a_temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)", failures, "L4-SEED-FILE")
        seed_sync = index_of(protocol, "::fsync(descriptor) == 0", failures, "L4-SEED-FILE")
        seed_rename = index_of(protocol, "std::filesystem::rename(a_temporary, a_path, renameError)", failures, "L4-SEED-FILE")
        require(0 <= seed_unlink < seed_open < seed_sync < seed_rename, "L4-SEED-FILE: the server seed must drop a stale temporary, create it exclusively owner-only, sync it, then rename", failures)
        require("WritePrivateSeedFile(temporaryPath, a_path, contents)" in protocol and "WriteTextFile(temporaryPath, contents)" in protocol, "L4-SEED-FILE: non-Windows seed writes must use the private writer and Windows must keep the existing writer", failures)
        require("(void)::chmod(path.c_str(), 0600);" in protocol, "L4-SEED-FILE: an existing identity file must be restricted to owner-only on load", failures)

        require('EnvValue("CO_GNS_SERVER_IDENTITY_FILE")' in protocol, "L4-SERVER-KEY: the server identity path must be overridable", failures)
        exists_check = index_of(protocol, "if (std::filesystem::exists(path)) {", failures, "L4-SERVER-KEY")
        keygen = index_of(protocol, "const auto seed = RandomSeed();", failures, "L4-SERVER-KEY")
        require(0 <= exists_check < keygen, "L4-SERVER-KEY: an existing identity file must never be silently regenerated", failures)
        require("exists but is unreadable or malformed" in protocol, "L4-SERVER-KEY: a malformed identity file must fail startup instead of rotating the key", failures)
        require('path.string() + ".pub"' in protocol and "server identity public key" in protocol, "L4-SERVER-KEY: the server must publish its public key as a stable file and a log line", failures)

        require("identity_.LoadOrCreate(identityError)" in server_internal, "L4-SERVER: the listener must load or create the persistent identity before admitting peers", failures)
        require("pendingAuth_.contains(connection)" in server_internal, "L4-SERVER: peers must be held in a pre-authentication state", failures)
        require("size == co_gns::auth::kHelloSize" in server_internal and "co_gns::auth::ParseHello(" in server_internal, "L4-SERVER: the first client frame must be a well-formed identity challenge", failures)
        parse_hello = index_of(server_internal, "co_gns::auth::ParseHello(", failures, "L4-SERVER-SIGN")
        sign_nonce = index_of(server_internal, "identity_.Sign(nonce, signature);", failures, "L4-SERVER-SIGN")
        require(0 <= parse_hello < sign_nonce, "L4-SERVER-SIGN: the server must only sign a challenge it parsed", failures)
        require("pendingAuth_.erase(connection) > 0" in server_internal, "L4-SERVER: a peer becomes authenticated only after the proof is sent", failures)
        require(server_internal.count("CO_GNS_EVENT_CONNECTED") == 1, "L4-SERVER: CONNECTED must be emitted exactly once, after authentication", failures)
        require("co_gns::auth::kAuthRequiredReason" in server_internal, "L4-SERVER: an unauthenticated peer must be closed with a distinct reason", failures)
        require("server authentication handshake required; the client may be outdated" in server_internal, "L4-SERVER: an outdated client must get an explicit diagnostic", failures)
        require("server authentication handshake timed out" in server_internal and "now >= entry->second" in server_internal, "L4-SERVER: an outdated client must not hang the pending set", failures)
        require("pendingAuth_.emplace(" in server_internal, "L4-SERVER: the authentication deadline must start when the transport connects", failures)
        require("reservedConnections_.insert(info.m_hConn)" in server_internal and "if (reservedConnections_.erase(connection) > 0) {" in server_internal, "L4-PREAUTH-DROP: an admitted peer must keep its admission reservation until CONNECTED or DISCONNECTED is queued", failures)
        require("DropPendingAuth(" in server_internal and "Disconnect(connection, co_gns::auth::kAuthRequiredReason" not in server_internal and "Disconnect(rejectedConnection, co_gns::auth::kAuthRequiredReason" not in server_internal, "L4-PREAUTH-DROP: a pre-authentication drop must release the reservation through DropPendingAuth, not the public Disconnect", failures)

        require("co_gns::auth::VerifyNonce(" in server_bridge and "authState_ != AuthState::Authenticated" in server_bridge, "L4-TEST-CLIENT: the bundled bridge client must authenticate the server before sending application data", failures)
        require("FormatPinKey" not in server_bridge and "pinKey_" not in server_bridge, "L4-TEST-CLIENT: the bundled bridge client keeps no pin, so no pin state may remain", failures)
        require("authReplyReceived_" in server_bridge and "} else if (authHeldFrames_.size() < kMaximumHeldFrames) {" in server_bridge, "L4-RACE-TEST-CLIENT: a frame that follows the identity proof must be held within a bound, not treated as unexpected data", failures)
        server_connected_event = index_of(server_bridge, "CO_GNS_EVENT_CONNECTED", failures, "L4-RACE-TEST-CLIENT")
        server_held_flush = index_of(server_bridge, "for (auto& frame : authHeldFrames_) {", failures, "L4-RACE-TEST-CLIENT")
        require(0 <= server_connected_event < server_held_flush, "L4-RACE-TEST-CLIENT: held frames must be delivered only after CONNECTED", failures)
        require(server_bridge.count("ResetAuthentication();") == 2 and server_bridge.count("MakeFrameEvent(connection, message->m_pData, size)") == 1, "L4-RACE-TEST-CLIENT: connect and disconnect must reset the handshake state, and post-authentication frames must go through MakeFrameEvent", failures)
        cmake = read("CO-SERVER/server/native_transport/CMakeLists.txt")
        require("find_package(OpenSSL REQUIRED)" in cmake and "set(CO_GNS_AUTH_LIBRARIES OpenSSL::Crypto)" in cmake, "L4-BUILD-OPENSSL: non-Windows builds must find and link OpenSSL for the Ed25519 primitives", failures)
        require(cmake.count("${CO_GNS_AUTH_LIBRARIES}") == 4, "L4-BUILD-OPENSSL: the bridge and every test target that includes the protocol must link OpenSSL", failures)
        server_test = read("CO-SERVER/server/native_transport/test_co_gns_server_bridge.cpp")
        require("CheckRfc8032Vector();" in server_test and "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c" in server_test and "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00" in server_test, "L4-KAT: the Ed25519 primitives must pass the RFC 8032 test 2 vector", failures)
        require("AdmitEveryPeer" in server_test and "CheckAdmittedPeerRejectedFrameDisconnectsOnce();" in server_test and "disconnects == 1" in server_test, "L4-PREAUTH-TEST: an admitted peer that skips the hello must produce exactly one DISCONNECTED", failures)
        client_test = read("CO-SERVER/server/native_transport/test_co_gns_client_bridge.cpp")
        require("assert(welcome->payload == g_rawServerWelcome);" in client_test, "L4-RACE-TEST: the client test must deliver an application frame right behind the identity proof", failures)

        require(not any(line.strip().startswith("#include") and "ThirdParty" in line for line in protocol.splitlines()), "L4-READONLY: the vendored GameNetworkingSockets tree must not be included or modified", failures)

        steam_sources = sorted((ROOT / "CO-BETA/plugin/src").glob("F4TSteam*"))
        require(bool(steam_sources), "L4-STEAM: Steam transport sources were not found; the no-op claim cannot be verified", failures)
        for steam_source in steam_sources:
            steam_text = steam_source.read_text(encoding="utf-8", errors="ignore")
            require("co_gns::auth" not in steam_text and "co_gns_auth_protocol" not in steam_text, f"L4-STEAM: {steam_source.name} must not go through the IP-join pinning path", failures)
        require("ConnectP2P" in (ROOT / "CO-BETA/plugin/src/F4TSteamApiLoader.cpp").read_text(encoding="utf-8"), "L4-STEAM: Steam joins must keep using ConnectP2P, which is already identity-authenticated", failures)

        for source in (client, protocol, server_internal, server_bridge):
            require("Co-Authored-By" not in source and "Generated with" not in source, "L4-AUTHORSHIP: no AI authorship trailers are allowed", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("GNS transport server-authentication contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_002_test_identity_release_security_contract():
    """Contract moved verbatim from tests/audit/test_identity_release_security_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_identity_release_security_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def function_body(text: str, signature: str) -> str:
        start = text.find(signature)
        if start < 0:
            return ""
        end = text.find("\n\t}", start)
        return text[start:] if end < 0 else text[start:end]


    def main() -> int:
        failures: list[str] = []

        header = read("co-core/include/F4TIdentityProtocol.h")
        manager = read("co-core/src/session/F4TSessionIdentityManager.cpp")
        protocol = read("co-core/src/session/F4TIdentityProtocol.cpp")

        # Constraint (M1): the store delete cannot remove a confirmed row and stays an optional override.
        store = header[header.find("class ResumeCredentialStore"):header.find("class TokenService")]
        require("DeleteUnconfirmed" in store, "M1: ResumeCredentialStore needs DeleteUnconfirmed", failures)
        require("persisted->confirmed" in store and "persisted->previousTokenHash" in store,
                "M1: DeleteUnconfirmed must refuse confirmed and superseded rows", failures)
        require(re.search(r"virtual bool DeleteUnconfirmed\(std::string_view a_profileId\)\s*\n\s*\{", store) is not None,
                "M1: DeleteUnconfirmed must stay a defaulted interface method so existing stores keep compiling", failures)

        # Constraint (M1): a hello that never confirms is released when its connection closes.
        closed = function_body(manager, "void SessionIdentityManager::ConnectionClosed(")
        require(closed != "", "M1: SessionIdentityManager::ConnectionClosed must exist", failures)
        require("_resumeByProfile.erase(resume)" in closed, "M1: ConnectionClosed must erase the unconfirmed record", failures)
        require("resume->second.confirmed || resume->second.previousTokenHash || resume->second.activeConnectionKey" in closed,
                "M1: a released record must be unconfirmed, unrotated and unbound (confirmed records unchanged)", failures)
        require("DeleteUnconfirmed" in closed, "M1: ConnectionClosed must drop the persisted unconfirmed row", failures)
        release = closed[closed.find("DeleteUnconfirmed"):]
        require("try" in closed[:closed.find("DeleteUnconfirmed")] and "catch (...)" in release,
                "M1: resume-store failures on release must be swallowed like the rest of the file", failures)
        require("RememberReadmission" in closed,
                "M1: dropped first-issuance handshakes must stay re-joinable through the bounded bootstrapping path", failures)
        require("activeConnectionKey.reset(); return; }" in " ".join(closed.split()),
                "M1: a committed record whose active connection closes must be kept, not released", failures)

        # Constraint (M1): stale unconfirmed records are evictable and the bootstrapping set is bounded.
        require("StaleUnconfirmedSeconds" in header and "MaximumReadmissionProfiles" in header,
                "M1: stale-release window and readmission bound must be named constants", failures)
        require("a_nowUnixSeconds - record.issuedAt >= StaleUnconfirmedSeconds" in manager,
                "M1: eviction must consider unconfirmed records older than the stale window", failures)
        require("MaximumReadmissionProfiles" in manager,
                "M1: the readmission set must be bounded so it cannot become a new unbounded table", failures)

        # Constraint (M1): no new source-IP plumbing is invented in this layer.
        for forbidden in ("remoteIp", "peerIp", "sourcePeer", "ConnectionPolicy", "remoteAddress"):
            require(forbidden not in manager,
                    f"M1: per-IP limiting must not invent new plumbing in this layer (found {forbidden})", failures)

        # Constraint (L3): opt-in allow-list that defaults to the existing trust-on-first-use path.
        require(re.search(r"bool requireKnownProfile\{\s*false\s*\};", header) is not None,
                "L3: Requirements needs requireKnownProfile defaulting to false", failures)
        require(re.search(r"const bool joinedBefore = resumeRecord &&\s*\(resumeRecord->confirmed \|\| resumeRecord->previousTokenHash \|\| \(presentedHash && RecordAccepts\(\*resumeRecord, \*presentedHash\)\)\);\s*if \(_requirements\.requireKnownProfile && !joinedBefore\)", manager) is not None,
                "L3: ValidateAndReserve must reject unknown profiles only when the requirement is set, and a record is joined only when confirmed, mid-rotation, or the presented token is proven", failures)
        joined_before = manager[manager.find("const bool joinedBefore"):]
        joined_before = joined_before[:joined_before.find(";") + 1]
        require("RecordAccepts(*resumeRecord, *presentedHash)" in joined_before,
                "L3: a record must count as joined when the presented token is one it accepts", failures)
        require("evictedTokenProven" in manager,
                "M1: an evicted unconfirmed record must prove its token through the readmission map", failures)
        require("RememberReadmission(evictedProfileId" in manager,
                "M1: eviction must remember the evicted token hash for readmission", failures)
        require('"unknown_profile"' in manager, "L3: the refusal needs a distinct unknown_profile code", failures)
        server_options = read("co-core/include/F4TServerConfiguration.h")
        server_parse = read("co-core/src/config/F4TServerConfiguration.cpp")
        server_config = read("CO-SERVER/server-cpp/src/ServerConfiguration.cpp")
        require(re.search(r"bool requireKnownProfile\{\s*false\s*\};", server_options) is not None,
                "L3: ServerConfiguration::Options needs requireKnownProfile defaulting to false", failures)
        require('"require_known_profile", "requireKnownProfile"), false, "require_known_profile")' in server_parse,
                "L3: the require_known_profile key must be read opt-in with a false default", failures)
        require(".requireKnownProfile = a_options.requireKnownProfile" in function_body(server_config, "F4T::Identity::Requirements IdentityRequirements("),
                "L3: IdentityRequirements must wire the config key into the identity gate", failures)

        # Impersonation protection must survive: constant-time token compare + resume_required.
        require("FixedTimeEquals" in manager and "RecordAccepts" in manager,
                "L3: resume tokens must still be compared in constant time", failures)
        require('"resume_required"' in manager, "L3: confirmed profiles must still require their resume token", failures)
        require("requireKnownProfile" not in protocol,
                "L3: the allow-list check must not move into NormalizeHello", failures)

        # Constraint: source-level contract coverage lives in the C++ test suites.
        capacity = read("co-core/tests/F4TIdentityCapacityTests.cpp")
        require("releaseManager.ConnectionClosed" in capacity,
                "M1: capacity test must cover releasing unconfirmed identities", failures)
        require("releaseStore.records.empty()" in capacity,
                "M1: capacity test must prove the persisted rows are deleted", failures)
        require("Deletes" in capacity or "deletes" in capacity,
                "M1: capacity test must count resume-store deletes", failures)
        protocol_tests = read("co-core/tests/F4TIdentityProtocolTests.cpp")
        require('"unknown_profile"' in protocol_tests and "requireKnownProfile" in protocol_tests,
                "L3: protocol test must cover the opt-in allow-list", failures)

        identity_gate = read("CO-SERVER/server-cpp/src/IdentityGate.cpp")
        identity_gate_header = read("CO-SERVER/server-cpp/include/IdentityGate.h")
        require('"reservation_rate_limited"' in identity_gate,
                "#2: unreserved new-profile hellos must be refused with a distinct code", failures)
        require("ReservationAllowed" in identity_gate and "kMaximumReservationsPerSourcePerWindow" in identity_gate,
                "#2: unauthenticated reservations must be capped per source", failures)
        require("_reservationWindows" in identity_gate_header and "RateLimitedReservationCount" in identity_gate_header,
                "#2: the reservation cap must be observable", failures)
        gate_tests = read("CO-SERVER/server-cpp/tests/IdentityGateTests.cpp")
        require("reservation_rate_limited" in gate_tests and "RateLimitedReservationCount()" in gate_tests,
                "#2: a C++ test must cover the reservation cap", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("Identity release and allow-list security contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_003_test_infra_trust_contract():
    """Contract moved verbatim from tests/audit/test_infra_trust_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_infra_trust_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def index_of(text: str, needle: str, failures: list[str], label: str) -> int:
        position = text.find(needle)
        if position < 0:
            failures.append(f"{label}: missing {needle!r}")
        return position


    def main() -> int:
        failures: list[str] = []

        migrations = sorted((ROOT / "supabase/migrations").glob("*_owned_live_server_registration.sql"))
        require(len(migrations) == 1, "INFRA-5: exactly one additive ownership migration must exist", failures)
        migration_path = migrations[0] if migrations else None
        migration = migration_path.read_text(encoding="utf-8") if migration_path else ""
        reconciles = sorted((ROOT / "supabase/migrations").glob("*_reconcile_live_server_directory.sql"))
        require(len(reconciles) == 1, "INFRA-5: exactly one reconcile migration must exist", failures)
        reconcile_path = reconciles[0] if reconciles else None
        reconcile = reconcile_path.read_text(encoding="utf-8") if reconcile_path else ""
        applied = sorted((ROOT / "supabase/migrations").glob("*.sql"))
        if migration_path:
            require(
                all(applied_path.name <= migration_path.name or applied_path == reconcile_path for applied_path in applied),
                "INFRA-5: the ownership migration must precede only the reconcile migration (applied migrations are never rewritten)",
                failures,
            )
        if reconcile_path:
            require(
                all(applied_path.name <= reconcile_path.name for applied_path in applied),
                "INFRA-5: the reconcile migration must be the newest migration",
                failures,
            )
        require(
            "create or replace function public.register_live_server_owned(" in reconcile
            and "drop function if exists public.list_live_servers();" in reconcile,
            "INFRA-5: the reconcile migration must carry the ownership RPC and the list shape",
            failures,
        )
        for applied_path in applied:
            if applied_path in (migration_path, reconcile_path):
                continue
            require(
                "listing_conflict" not in applied_path.read_text(encoding="utf-8"),
                f"INFRA-5: {applied_path.name} is already applied and must not be modified",
                failures,
            )

        require("create or replace function public.register_live_server_owned(" in reconcile, "INFRA-5: the additive RPC must be registered", failures)
        require("public.live_server_credentials_match(" in reconcile, "INFRA-5: replacement must verify listing credentials", failures)
        require(
            "encode(extensions.digest(p_presented_secret, 'sha256'), 'hex')" in reconcile,
            "INFRA-5: the presented secret must be hashed and compared (never the raw secret)",
            failures,
        )
        require("p_presented_id = p_id" in reconcile, "INFRA-5: replacement must require the existing listing id", failures)
        require("409, 'listing_conflict'" in reconcile, "INFRA-5: an unproven replacement must return a distinct conflict code", failures)
        require("'status', 201" in reconcile, "INFRA-5: first-time registration must still return 201", failures)
        require("'directory_full'" in reconcile and "'too_many_servers'" in reconcile, "INFRA-5: first-time capacity limits must be preserved", failures)
        require(
            "on conflict (host, port) do update" not in reconcile,
            "INFRA-5: the insert must not blindly update on (host, port) conflict",
            failures,
        )
        insert_index = index_of(reconcile, "insert into public.live_servers", failures, "INFRA-5")
        race_index = index_of(reconcile, "'registration_race'", failures, "INFRA-5")
        conflict_index = index_of(reconcile, "listing_conflict", failures, "INFRA-5")
        update_index = index_of(reconcile, "update public.live_servers set", failures, "INFRA-5")
        require(
            insert_index < race_index < conflict_index < update_index,
            "INFRA-5: a lost registration race must re-check ownership before any replacement",
            failures,
        )
        require("registration_key_hash = v_key_hash" in reconcile, "INFRA-5: the registration key must be stored only as a hash", failures)
        require(
            "existing.registration_key_hash = v_key_hash" in reconcile,
            "INFRA-5: a registration key may prove ownership only by matching its stored hash",
            failures,
        )
        validated_index = index_of(reconcile, "exception when raise_exception then", failures, "INFRA-5")
        lookup_index = index_of(reconcile, "select * into existing from public.live_servers where host = source", failures, "INFRA-5")
        require(0 <= validated_index < lookup_index < insert_index, "INFRA-5: listing metadata must be validated before any insert or replacement", failures)
        require(
            "return public.register_live_server_owned(p_source, p_body, p_allow_private, null, null);" in reconcile,
            "INFRA-5: the legacy register RPC must delegate to the owner-checked RPC",
            failures,
        )

        handler = read("supabase/functions/server-directory/handler.ts")
        require('rpc("register_live_server_owned"' in handler, "INFRA-5: the directory handler must call the ownership RPC", failures)
        require("p_previous_secret: readBearer(request)" in handler, "INFRA-5: the handler must present the listing secret as a bearer credential", failures)
        require("p_previous_id: previousId" in handler, "INFRA-5: the handler must present the previous listing id", failures)
        require('"secret_in_body"' in handler, "INFRA-5: the handler must reject a secret placed in the request body", failures)
        require("delete metadata.previousId" in handler, "INFRA-5: previousId must not flow into listing metadata", failures)
        rate_limit = index_of(handler, 'limited("register"', failures, "INFRA-5")
        rpc_call = index_of(handler, 'rpc("register_live_server_owned"', failures, "INFRA-5")
        require(0 <= rate_limit < rpc_call, "INFRA-5: registration rate limiting must stay in front of the RPC", failures)

        registrar = read("CO-SERVER/server-cpp/src/DirectoryRegistrar.cpp")
        require(
            "if (!previousId.empty() && !previousSecret.empty())" in registrar,
            "INFRA-5: the registrar must only present credentials it actually holds",
            failures,
        )
        require('registration["previousId"] = previousId;' in registrar, "INFRA-5: the registrar must present the previous listing id", failures)
        require(
            'body["registrationKey"] = a_state.registrationKey;' in registrar,
            "INFRA-5: the registrar must present its registration key so a lost response can be retried",
            failures,
        )
        require(
            'a_state.directoryUrl + "/register", previousSecret, registration.dump()' in registrar,
            "INFRA-5: the registrar must present the previous secret as the bearer credential",
            failures,
        )
        conflict = index_of(registrar, "if (response.status == 409)", failures, "INFRA-5")
        generic = index_of(registrar, "if (response.status != 200 && response.status != 201)", failures, "INFRA-5")
        require(0 <= conflict < generic, "INFRA-5: the conflict result must be handled before the generic status check", failures)
        require(
            "The listing expires shortly after its last heartbeat" in registrar and "change the advertised port" in registrar,
            "INFRA-5: the conflict must produce an actionable diagnostic instead of a silent retry",
            failures,
        )
        conflict_block = registrar[conflict:generic] if 0 <= conflict < generic else ""
        require("a_state.status.serverId.clear();" in conflict_block, "INFRA-5: a conflict must drop stale credentials instead of retrying them", failures)
        heartbeat_block_start = registrar.find("response.status == 401 || response.status == 403 || response.status == 404")
        heartbeat_block = registrar[heartbeat_block_start:heartbeat_block_start + 700] if heartbeat_block_start >= 0 else ""
        require(heartbeat_block_start >= 0, "INFRA-5: the heartbeat re-registration path must exist", failures)
        require("a_state.secret.clear()" not in heartbeat_block, "INFRA-5: a rejected heartbeat must keep the secret so re-registration can prove ownership", failures)
        require(
            "registering = a_state.status.state == DirectoryState::Registering" in registrar,
            "INFRA-5: a rejected heartbeat must trigger re-registration rather than another heartbeat",
            failures,
        )

        workflow_files = sorted((ROOT / ".github").rglob("*.yml"))
        require(bool(workflow_files), "INFRA-18: workflow files were not found", failures)
        for workflow in workflow_files:
            text = workflow.read_text(encoding="utf-8")
            require("self-hosted" not in text, f"INFRA-18: {workflow.name} must not run on a self-hosted runner", failures)

        for source in (migration, handler, registrar):
            require("Co-Authored-By" not in source and "Generated with" not in source, "INFRA-AUTHORSHIP: no AI authorship trailers are allowed", failures)

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("Infra trust contracts passed")
        return 0


    if True:
        _merge_exit(main())

def _section_004_test_gog_support_contract():
    """Contract moved verbatim from tests/audit/test_gog_support_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_gog_support_contract.py")
    from pathlib import Path


    ROOT = Path(__file__).resolve().parents[2]


    def read(path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")


    def require(condition: bool, message: str, failures: list[str]) -> None:
        if not condition:
            failures.append(message)


    def main() -> int:
        failures: list[str] = []

        identity_header = read("co-core/include/F4TIdentityProtocol.h")
        config_header = read("co-core/include/F4TServerConfiguration.h")
        config_source = read("co-core/src/config/F4TServerConfiguration.cpp") + read("co-core/src/config/F4TServerConfigModpack.cpp")
        server_config = read("CO-SERVER/server-cpp/src/ServerConfiguration.cpp")
        gate_header = read("CO-SERVER/server-cpp/include/IdentityGate.h")
        gate = read("CO-SERVER/server-cpp/src/IdentityGate.cpp")
        runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc").replace('#include "ServerRuntimeMessage.inc"', read("CO-SERVER/server-cpp/src/ServerRuntimeMessage.inc"))
        maintenance =read("CO-SERVER/server-cpp/src/ServerMaintenance.inc")
        bootstrap = read("CO-SERVER/server-cpp/src/ServerBootstrap.cpp")
        steam_admin = read("CO-SERVER/server-cpp/src/SteamAdmin.cpp")
        alert = read("CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp")
        gate_tests = read("CO-SERVER/server-cpp/tests/IdentityGateSteamTests.cpp")
        config_tests = read("CO-SERVER/server-cpp/tests/ServerConfigurationTests.cpp")
        discover = read("CO-Launcher/launcher-core/src/discover.rs")
        ops = read("CO-Launcher/launcher-gui/src/ops.rs")

        require(
            "bool requireSteamIdentity{ false };" in identity_header,
            "F4TIdentityProtocol.h: requireSteamIdentity must default to false",
            failures,
        )
        require(
            "bool requireSteamIdentity{ false };" in config_header,
            "F4TServerConfiguration.h: requireSteamIdentity must default to false",
            failures,
        )
        require(
            '"require_steam_identity"' in config_source
            and "require_v2_identity can no longer be disabled" in config_source,
            "F4TServerConfiguration.cpp: require_steam_identity must be read and require_v2_identity must stay mandatory",
            failures,
        )
        require(
            ".requireSteamIdentity = a_options.requireSteamIdentity" in server_config,
            "ServerConfiguration.cpp: the option must reach the identity requirements",
            failures,
        )
        require(
            "inline constexpr std::string_view kSteamRequiredReason" in gate_header,
            "IdentityGate.h: the steam_required reason text must be a shared constant",
            failures,
        )
        require(
            '.code = "steam_required"' in gate
            and "kSteamVerdictHoldSeconds = 30.0" in gate
            and "_steamVerdictHolds.contains(a_connectionId)" in gate,
            "IdentityGate.cpp: refuse with steam_required and hold gameplay during the verdict window",
            failures,
        )
        require(
            "std::optional<std::vector<std::uint8_t>> TakeParkedHello(std::uint32_t a_connectionId);" in gate_header
            and "bool steamVerdictHeld{};" in gate_header
            and "_parkedHellos[a_connectionId] = std::vector<std::uint8_t>(a_payload.begin(), a_payload.end());" in gate
            and "_parkedHellos.erase(a_connectionId);" in gate
            and "std::vector<std::uint32_t> IdentityGate::TakeExpiredSteamVerdictHolds()" in gate,
            "IdentityGate: a held hello is parked, dropped on disconnect and expired by the verdict loop",
            failures,
        )
        require(
            "ReleaseSteamVerdictHold(verdict.connectionId)" in maintenance
            and "TakeExpiredSteamVerdictHolds()" in maintenance
            and maintenance.count('"steam_required", CommonwealthOnline::Server::kSteamRequiredReason') == 2,
            "ServerMaintenance.inc: a failed or expired hold must end the session with steam_required",
            failures,
        )
        require(
            "identity->TakeParkedHello(verdict.connectionId)" in maintenance
            and "deliverToProtocol(verdict.connectionId, tracked->second, *parked, loopMonotonic, nowUnix, nullptr)" in maintenance
            and "const auto deliverToProtocol = [&](std::uint32_t a_connectionId, auto& a_state" in runtime
            and "if (decision.steamVerdictHeld) {" in runtime,
            "ServerMaintenance.inc: a Verified verdict must replay the parked hello through the live delivery path",
            failures,
        )
        require(
            "if (a_out.options.requireSteamIdentity) {" in bootstrap
            and "LoadSteamWebApiKey(a_out.baseDirectory, keyError)" in bootstrap
            and "require_steam_identity needs a Steam Web API key" in bootstrap,
            "ServerBootstrap.cpp: startup must refuse require_steam_identity without a Steam Web API key",
            failures,
        )
        require(
            "bans->BanProfile(profileId" in steam_admin and "bans->BanSteamId(steamId" in steam_admin,
            "SteamAdmin.cpp: a player ban must also ban the profile and the verified Steam ID",
            failures,
        )
        require(
            'a_code == "steam_required"' in alert and "Steam Sign-In Required" in alert,
            "F4THandshakeRejectionAlert.cpp: steam_required must show the Steam Sign-In Required alert",
            failures,
        )
        require(
            '"steam_required"' in gate_tests,
            "IdentityGateSteamTests.cpp: the steam_required refusal and hold must be covered",
            failures,
        )
        require(
            "requireSteamIdentity" in config_tests,
            "ServerConfigurationTests.cpp: require_steam_identity parsing must be covered",
            failures,
        )
        require(
            r'r"SOFTWARE\WOW6432Node\GOG.com\Games"' in discover and r'r"C:\GOG Games\Fallout 4"' in discover,
            "discover.rs: GOG installs must be found in the GOG Galaxy registry and the default folder",
            failures,
        )
        require(
            "pub fn is_gog_install(path: &Path) -> bool {" in discover
            and 'name.starts_with("goggame-") && name.ends_with(".info")' in discover,
            "discover.rs: is_gog_install must be public for the launcher GUI and recognise the goggame marker",
            failures,
        )
        start = ops.index("fn steam_ready(")
        steam = ops[start:ops.index("\n}\n", start)]
        steam_gate = steam.find("if is_gog_install(game) {")
        require(
            steam_gate != -1 and steam.find("wait_for(steam_signed_in") > steam_gate,
            "ops.rs: steam_ready() must wait for Steam unless the game is a GOG install",
            failures,
        )
        require(
            "steam_ready(&pin.game)" in ops and "steam_ready(game)" in ops,
            "ops.rs: the MO2 and loader launch paths must both gate on steam_ready()",
            failures,
        )
        require(
            "const NO_GAME_OTHER: &str =" in ops,
            "ops.rs: GOG installs need their own failure text without Steam wording",
            failures,
        )
        for path, phrase in (
            ("CO-BETA/docs/version-targets.md", "GOG: Fallout 4 1.10.163 (OG)"),
            ("CO-BETA/README.md", "the GOG copy is Fallout 4 1.10.163 (OG)"),
            ("wiki/Installing.md", "A GOG copy is Fallout 4 1.10.163 (Old-Gen)"),
            ("wiki/Troubleshooting.md", "A GOG copy is Fallout 4 1.10.163 (OG)"),
        ):
            if not (ROOT / path).exists():
                continue
            text = read(path)
            require(
                phrase in text and "needs an in-game check" not in text,
                f"{path}: GOG must be documented as Fallout 4 1.10.163 (OG) without the old in-game check",
                failures,
            )
        if failures:
            for failure in failures:
                print(f"FAIL: {failure}")
            return 1

        print("GOG and Steam identity contracts passed")
        return 0


    if True:
        _merge_exit(main())


_SECTIONS = [
    ("tests/audit/test_server_auth_security_contract.py", _section_001_test_server_auth_security_contract),
    ("tests/audit/test_identity_release_security_contract.py", _section_002_test_identity_release_security_contract),
    ("tests/audit/test_infra_trust_contract.py", _section_003_test_infra_trust_contract),
    ("tests/audit/test_gog_support_contract.py", _section_004_test_gog_support_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_server_auth_settings_contracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
