#!/usr/bin/env python3
"""F4TNetworkServerContracts: consolidated contract sections.

Consolidated from 8 single-contract scripts so the group runs one process
instead of 8. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python CO-BETA/plugin/tests/F4TNetworkServerContracts.py
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


def _section_001_f4tfurnituresynccontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TFurnitureSyncContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TFurnitureSyncContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"
    SERVER = ROOT / "CO-SERVER/server-cpp"
    CORE = ROOT / "co-core"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(path):
        if not path.exists():
            failures.append(f"missing {path.relative_to(ROOT).as_posix()}")
            return ""
        return path.read_text(encoding="utf-8")


    for path in (
        CORE / "include/F4TFurnitureSync.h",
        CORE / "src/validation/F4TFurnitureSync.cpp",
        CORE / "tests/F4TFurnitureSyncTests.cpp",
        SERVER / "src/ServerFurnitureRelay.h",
        SERVER / "src/ServerFurnitureRelay.cpp",
        SERVER / "tests/ServerFurnitureRelayTests.cpp",
        PLUGIN / "include/F4TFurnitureSeatSync.h",
        PLUGIN / "src/F4TFurnitureSeatSync.cpp",
    ):
        check(path.exists(), f"missing {path.relative_to(ROOT).as_posix()}")

    core_header = read(CORE / "include/F4TFurnitureSync.h")
    core_code = read(CORE / "src/validation/F4TFurnitureSync.cpp")
    relay = read(SERVER / "src/ServerFurnitureRelay.cpp")
    relay_tests = read(SERVER / "tests/ServerFurnitureRelayTests.cpp")
    server_impl = read(SERVER / "src/ServerProtocolImpl.inc")
    welcome = read(SERVER / "src/ServerWelcome.cpp")
    server_xmake = read(SERVER / "xmake.lua")
    core_xmake = read(CORE / "xmake.lua")
    core_tests_xmake = read(CORE / "xmake-tests.lua")
    seat = read(PLUGIN / "src/F4TFurnitureSeatSync.cpp")
    outbound = read(PLUGIN / "src/F4TNetworkingOutbound.cpp")
    inbound = read(PLUGIN / "src/F4TNetworkingInbound.cpp")
    identity = read(PLUGIN / "src/F4TClientIdentity.cpp")
    runtime = read(PLUGIN / "src/F4TReplicationRuntime.cpp")
    networking_header = read(PLUGIN / "include/F4TNetworking.h")

    check('kCapabilityName = "furniture-sync-v1"' in core_header, "furniture-sync-v1 capability name changed")
    check("ParseClientUse" in core_code and "ParseRelayedUse" in core_code, "core must define both parsers")
    check("FurnitureRelay::HandleFurnitureUse(" in server_impl and '*type == "furnitureUse"' in server_impl,
          "server dispatch must route furnitureUse to the relay")
    check("compatibilityCapabilities.Has(F4T::FurnitureSync::kCapabilityName)" in relay, "relay must check the sender's capability")
    check("TakeToken(a_sender.furnitureUses" in relay, "relay must spend the per-connection furnitureUses bucket")
    check("std::string{ F4T::FurnitureSync::kCapabilityName }" in welcome, "welcome must advertise furniture-sync-v1")
    check("ServerFurnitureRelay.cpp" in server_xmake, "server xmake must compile the relay")
    check("ServerFurnitureRelayTests.cpp" in server_xmake, "server xmake must register the relay tests")
    check("F4TFurnitureSync.cpp" in core_xmake, "co-core xmake must compile the validation")
    check("F4TFurnitureSyncTests.cpp" in core_tests_xmake, "co-core xmake must register the validation tests")
    check("SendFurnitureUsePacket(std::string_view a_action)" in outbound and '{ "type", "furnitureUse" }' in outbound,
          "networking must send furnitureUse")
    check("bool SendFurnitureUsePacket(std::string_view a_action);" in networking_header, "networking header must declare the sender")
    check('*packetType == "furnitureUse"' in inbound and "F4T::FurnitureSeatSync::HandleRelayedPacket(packet);" in inbound,
          "inbound dispatch must route furnitureUse to the seat sync")
    check("metadata.capabilities.emplace_back(std::string{ F4T::FurnitureSync::kCapabilityName });" in identity,
          "client must advertise furniture-sync-v1")
    check("F4T::FurnitureSeatSync::Tick();" in runtime, "runtime must tick the seat sync")
    gate_start = runtime.find("F4T::SurvivalEnforcer::Tick(connection, g_runtime.CurrentGeneration(), loaded, [&] {")
    gate_end = runtime.find("\n\t\t\t\t});", gate_start)
    check(gate_start >= 0 and gate_end > gate_start and "F4T::FurnitureSeatSync::PublishLocal();" in runtime[gate_start:gate_end], "seat publication must be Survival gated")
    check(seat.index("void PublishLocal()") < seat.index("SendFurnitureUsePacket(") < seat.index("void Tick()"), "seat publication must remain separate from inbound replay")

    check("AnimationGraphOverride::SendGraphEvent(" in seat, "seat sync must play graph events on the proxy")
    check('"sitStartFromStand"' in seat and '"sitStop"' in seat, "seat sync must name the enter and exit events")
    check("PutActorInChairBedQuick" not in seat, "seat sync must not seat the proxy with PutActorInChairBedQuick")
    check("ActorInPowerArmor(*player)" in seat, "seat sync must exclude power armor frames")
    check(seat.count("{") == seat.count("}"), "F4TFurnitureSeatSync.cpp braces must balance")
    check(relay_tests.count("int main()") == 1, "relay tests must define one main")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TFurnitureSyncContract: all checks passed.")

def _section_002_f4tgnsfailuresourcecontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TGnsFailureSourceContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TGnsFailureSourceContract.py")
    from pathlib import Path


    root = Path(__file__).resolve().parents[3]
    gns = (root / "CO-BETA/plugin/src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")
    shim = "\n".join((root / "CO-BETA/plugin/src" / name).read_text(encoding="utf-8") for name in ("F4TGnsWinsockShim.cpp", "F4TGnsWinsockShimBridge.cpp", "F4TGnsWinsockShimFrames.cpp"))
    adapter = (root / "co-core/src/protocol/F4TGnsProtocolAdapter.cpp").read_text(encoding="utf-8")
    attempt = (root / "CO-BETA/plugin/include/F4TValidationAttempt.h").read_text(encoding="utf-8")
    ui = (root / "CO-BETA/plugin/src/F4TUIClient.cpp").read_text(encoding="utf-8")

    assert "using ValidationAttempt = F4T::ValidationAttempt" in gns
    assert "struct ValidationAttempt" in attempt
    assert "thread_local std::shared_ptr<ValidationAttempt> g_activeValidationAttempt" in gns
    assert "std::unordered_map<SOCKET, std::weak_ptr<ValidationAttempt>> g_validationSockets" in gns
    assert "g_validationSockets[socketValue] = g_activeValidationAttempt" in gns
    assert "attempt->Publish(std::move(*failure))" in gns
    assert "std::make_shared<ValidationAttempt>()" in gns
    assert "attempt->Take()" in gns
    assert "g_terminalFailure" not in gns
    assert "g_terminalFailure" not in shim
    assert "g_terminalFailure" not in adapter
    assert "LoadLibraryW(" not in shim and "emplace_back(kBridgeDllName)" not in shim

    for fragment in (
        "ConnectFailureKind::AdmissionRefused",
        "ConnectFailureKind::SessionRejected",
        "ConnectFailureKind::LocalHandshakeFailure",
        "SanitizeFailureText",
        "transportReason",
        "a_failure.code",
        "a_failure.detail",
    ):
        assert fragment in shim if fragment == "SanitizeFailureText" else fragment in gns or fragment in shim

    assert "connectionError.c_str()" in ui
    assert "ConnectToLocalServer(error)" in ui

    connect_failure = (root / "CO-BETA/plugin/include/F4TConnectFailure.h").read_text(encoding="utf-8")
    assert "namespace F4T::Networking" in connect_failure
    assert "enum class ConnectFailureKind" in connect_failure
    assert "struct ConnectFailure" in connect_failure
    assert "inline int ConnectFailurePriority(" in connect_failure
    assert "ConnectFailurePriority" in attempt
    assert "int FailurePriority(" not in attempt
    assert not (root / "CO-BETA/plugin/include/F4TGnsFailure.h").exists()

def _section_003_f4thandshakerejectionactioncontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4THandshakeRejectionActionContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4THandshakeRejectionActionContract.py")
    from pathlib import Path


    root = Path(__file__).resolve().parents[3]
    source = (root / "CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp").read_text(encoding="utf-8")

    assert "g_lastRejectedServerKey" not in source

    assert '"Reset Session"' in source
    assert 'ClearResumeCredential(boundServerKey, resumeToken)' in source
    assert 'const bool resumeRejected = code == "invalid_resume" || code == "unknown_resume";' in source
    assert 'resumeRejected ? F4T::ServerProfileStore::LookupResumeCredential' in source
    assert '} else if (!resumeRejected && uiV2' in source
    assert 'result.title = "Session Not Found";' in source
    assert 'result.title = "Session Unavailable";' in source
    assert 'resumeToken = std::move(it->second.resumeToken)' in source
    assert 'Your saves are kept. Then host or join again.' in source
    assert 'ClearRejectedResumeCredentialAndRetry' not in source

    assert "struct RejectionAlertContext" in source
    assert "g_alertContexts" in source
    assert "OnAlertResponse(const char*, const char* a_actionId, void* a_userData)" in source
    assert "presentation.id, generation" in source
    assert "hasClearResume" in source
    assert "g_alertContexts.erase(generation);" in source

    class RejectionAlertLifecycleModel:
        def __init__(self):
            self.generation = 0
            self.contexts = {}
            self.cleared = []
            self.retried = []
            self.failed = []
            self.tokens = {}

        def show_rejection(self, code: str, server_key: str, ui_available: bool = True, has_last_join: bool = True, show_succeeds: bool = True):
            if not ui_available:
                return None
            has_clear_resume = (code in ("invalid_resume", "unknown_resume") and bool(server_key))
            gen = 0
            if has_clear_resume:
                self.generation += 1
                gen = self.generation
                self.tokens.setdefault(server_key, "original-token")
                self.contexts[gen] = (server_key, self.tokens[server_key])
            if not show_succeeds:
                if gen > 0:
                    self.contexts.pop(gen, None)
                return None
            alert_id = f"session-resume-invalid-{gen}" if gen > 0 else "session-rejected"
            return {"id": alert_id, "user_data": gen}

        def on_alert_response(self, action_id: str, user_data: int, task_interface_available: bool = True):
            bound_key = None
            if user_data in self.contexts:
                bound_key = self.contexts.pop(user_data)
            if not action_id or not task_interface_available:
                return
            if action_id == "clear_resume":
                self.clear_rejected(bound_key)

        def clear_rejected(self, context):
            if not context:
                self.failed.append("no_key")
                return
            server_key, expected_token = context
            if self.tokens.get(server_key) != expected_token:
                self.failed.append("changed_token")
                return
            del self.tokens[server_key]
            self.cleared.append(server_key)


    model = RejectionAlertLifecycleModel()

    assert model.show_rejection("invalid_resume", "server-1", ui_available=False) is None
    assert len(model.contexts) == 0

    alert_no_retry = model.show_rejection("unknown_resume", "server-1", has_last_join=False)
    assert alert_no_retry is not None
    assert alert_no_retry["user_data"] > 0
    model.on_alert_response("clear_resume", alert_no_retry["user_data"])
    assert model.cleared == ["server-1"]
    assert not model.retried
    model = RejectionAlertLifecycleModel()

    assert model.show_rejection("invalid_resume", "server-1", show_succeeds=False) is None
    assert len(model.contexts) == 0

    alert_task_fail = model.show_rejection("invalid_resume", "server-1")
    assert alert_task_fail is not None
    assert len(model.contexts) == 1
    model.on_alert_response("clear_resume", alert_task_fail["user_data"], task_interface_available=False)
    assert len(model.contexts) == 0
    assert len(model.cleared) == 0

    alert_a = model.show_rejection("invalid_resume", "server-alpha")
    alert_b = model.show_rejection("invalid_resume", "server-beta")

    assert alert_a["id"] == f"session-resume-invalid-{alert_a['user_data']}"
    assert alert_b["id"] == f"session-resume-invalid-{alert_b['user_data']}"
    assert model.contexts[alert_a["user_data"]] == ("server-alpha", "original-token")
    assert model.contexts[alert_b["user_data"]] == ("server-beta", "original-token")

    model.on_alert_response("clear_resume", alert_a["user_data"])
    assert model.cleared == ["server-alpha"]
    assert model.retried == []
    assert alert_a["user_data"] not in model.contexts
    assert model.contexts[alert_b["user_data"]] == ("server-beta", "original-token")

    model.on_alert_response("clear_resume", alert_b["user_data"])
    assert model.cleared == ["server-alpha", "server-beta"]
    assert model.retried == []
    assert len(model.contexts) == 0

    model.on_alert_response("clear_resume", alert_a["user_data"])
    assert len(model.failed) == 1
    assert model.cleared == ["server-alpha", "server-beta"]
    assert model.retried == []

    stale = model.show_rejection("invalid_resume", "server-newer")
    model.tokens["server-newer"] = "replacement-token"
    model.on_alert_response("clear_resume", stale["user_data"])
    assert model.tokens["server-newer"] == "replacement-token"
    assert "server-newer" not in model.cleared
    assert model.failed[-1] == "changed_token"

    expired = model.show_rejection("invalid_resume", "server-evicted")
    cleared_before = list(model.cleared)
    model.on_alert_response("expire", expired["user_data"])
    assert expired["user_data"] not in model.contexts
    assert model.cleared == cleared_before
    assert not model.retried
    model.on_alert_response("clear_resume", expired["user_data"])
    assert model.cleared == cleared_before

    print("Handshake rejection action contract tests passed")


def _section_005_f4tidentityorderingcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TIdentityOrderingContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TIdentityOrderingContract.py")
    from pathlib import Path
    import re


    root = Path(__file__).resolve().parents[3]
    gns = (root / "CO-BETA/plugin/src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")
    identity = (root / "CO-BETA/plugin/src/F4TClientIdentity.cpp").read_text(encoding="utf-8")
    session = (root / "CO-BETA/plugin/src/F4TSessionLaunch.cpp").read_text(encoding="utf-8")
    ui = (root / "CO-BETA/plugin/src/F4TUIClient.cpp").read_text(encoding="utf-8")

    start = gns.index("bool ConnectAndValidateServerOnce(")
    end = gns.index("\n\tbool ConnectAndValidateServer(", start)
    connect = gns[start:end]
    wrapper = gns[end:gns.index("\n\tbool ConnectToServer", end)]

    for fragment in (
        "NormalizeConnectEndpoint(host, port)",
        "F4T::ClientIdentity::ClearConnectionIdentity();",
        "PrepareConnectionIdentity(host, port, a_identityError, a_serverPassword, a_joinId)",
        "F4T::ClientIdentity::HoldConnectionIdentity(std::move(a_identity));",
        "ConnectAndValidateServerLegacy(host.c_str(), port, a_connectError, a_generationOut)",
    ):
        assert fragment in connect

    assert connect.index("NormalizeConnectEndpoint(host, port)") < connect.index("ClearConnectionIdentity")
    assert connect.index("ClearConnectionIdentity") < connect.index("PrepareConnectionIdentity")
    assert connect.index("PrepareConnectionIdentity") < connect.index("HoldConnectionIdentity")
    assert connect.index("HoldConnectionIdentity") < connect.index("ConnectAndValidateServerLegacy")
    legacy_call = connect.index("ConnectAndValidateServerLegacy")
    assert connect.index("ClearConnectionIdentity", legacy_call) > legacy_call
    assert "a_identity.modpackFingerprint = a_modpackFingerprint;" in connect
    assert "const auto cachedFingerprint = endpointValid ? F4T::ClientModpackProbe::CachedFingerprint(serverKey) : std::optional<std::string>{};" in wrapper
    assert "F4T::ServerProfileStore::ProfileServerKey(endpointHost, endpointPort, a_joinId)" in wrapper
    assert "auto connected = ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, fingerprint, a_joinId, true, cancelled, a_backgroundValidation, a_generationOut);" in wrapper
    assert wrapper.index("CachedFingerprint(serverKey)") < wrapper.index("ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, fingerprint, a_joinId, true, cancelled, a_backgroundValidation, a_generationOut)")
    assert "if (!connected && failure && failure->code == \"modpack_mismatch\" && !failure->matchingModpackFingerprint.empty()) {" in wrapper
    assert "fingerprint = failure->matchingModpackFingerprint;" in wrapper
    assert "connected = ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, fingerprint, a_joinId, true, cancelled, a_backgroundValidation, a_generationOut);" in wrapper
    assert "F4T::ClientModpackProbe::Forget(serverKey)" in wrapper
    shim = (root / "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")
    assert ".modpackFingerprint = identity->modpackFingerprint" in shim
    probe = (root / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
    assert "if (const auto matching = ModpackManifest::MatchingFingerprint(" in probe
    assert "failure.matchingModpackFingerprint = *matching;" in probe

    assert "const auto profileId = ServerProfileStore::EnsureProfileId(serverKey);" in identity
    assert "if (profileId.empty())" in identity
    assert "return std::nullopt;" in identity[identity.index("if (profileId.empty())"):]
    assert '#include "F4TWorldPolicy.h"' in identity, (
        "the client must claim the world-policy capability from the shared constant, so the hello "
        "cannot spell it differently from the server's welcome capability and its send gate."
    )
    assert "metadata.capabilities.emplace_back(std::string{ F4T::WorldPolicy::kCapabilityName });" in identity, (
        "the server sends worldPolicy and worldPolicyPart only to sessions that claimed "
        "world-policy-v1, so a client that stops claiming it silently stops receiving the ruleset."
    )
    assert "ConnectToLocalServer(error)" in ui
    assert "PrepareConnectionIdentity" not in session
    assert "HoldConnectionIdentity" not in session
    assert "ClearConnectionIdentity" not in session
    assert "SetConnectedServerAdmin" not in session
    session_identity_calls = set(re.findall(r"F4T::ClientIdentity::(\w+)", session))
    assert session_identity_calls <= {"IsConnectedServerAdmin"}, session_identity_calls

def _section_006_f4tjoinfailuresurfacingcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TJoinFailureSurfacingContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TJoinFailureSurfacingContract.py")
    from pathlib import Path
    import re


    plugin = Path(__file__).resolve().parents[1]
    launch = (plugin / "src/F4TSessionLaunch.cpp").read_text(encoding="utf-8")
    uiclient = (plugin / "src/F4TUIClient.cpp").read_text(encoding="utf-8")


    def block_before_failure(marker):
        match = re.search(re.escape(marker) + r"(.*?)return LaunchOutcome::Failed;", launch, re.S)
        assert match is not None, f"refusal block not found: {marker}"
        return match.group(1)


    for marker in ("if (!joinSave.allowed) {", "if (!latestSave.allowed) {"):
        body = block_before_failure(marker)
        assert "LogWarning(" in body and "profileKey" in body and "*saveFile" in body and "reason" in body, \
            f"{marker} must log a warning with the profile key, save name and reason"

    deferred = re.search(r"if \(outcome == LaunchOutcome::Failed\) \{(.*?)onComplete\(false, error\);", launch, re.S)
    assert deferred is not None, "the deferred launch failure must still report through onComplete"
    assert "LogWarning(" in deferred.group(1), "a failed deferred launch must log before it reports"

    cleanup = re.search(r"DisconnectFromLocalServerIfGeneration\((.*?),\s*\[attemptToken\]", uiclient, re.S)
    assert cleanup is not None, "the failed-join cleanup must disconnect through DisconnectFromLocalServerIfGeneration"
    assert cleanup.group(1).strip() == "ownedGeneration", \
        "the failed-join cleanup must target the generation owned by this attempt"

    assert "if (a_onComplete) a_onComplete(false, reason.c_str(), a_userData);" in uiclient, \
        "a failed join cleanup must report the reason to the UI"
    assert uiclient.count('F4T::DebugLogging::Log("UIClient", "failed join') == 2, \
        "every silent failed-join completion branch must log"

    networking = (plugin / "src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")
    policy = (plugin.parents[1] / "co-core/include/F4TConnectionPolicy.h").read_text(encoding="utf-8")
    assert re.search(r"inline bool DecideResumeRecovery\(\s*std::string_view a_failureCode,\s*bool a_credentialPresented\)", policy)
    assert 'a_failureCode == "unknown_resume"' in policy, \
        "the resume recovery must drop only on unknown_resume, the no-record refusal, never on invalid_resume"
    assert networking.count("DecideResumeRecovery(") == 1, \
        "the resume recovery must have exactly one call site, so a second unknown_resume is never recovered again"
    assert networking.count("ClearResumeCredential(serverKey, presentedCredential->token)") == 1, \
        "the resume recovery must clear only the credential it presented, through one call"
    connect_start = networking.index("\tbool ConnectAndValidateServer(\n")
    connect_end = networking.index("\tbool ConnectAndValidateServer(const char* a_host, std::uint16_t a_port, std::string& a_error)")
    connect = networking[connect_start:connect_end]
    lookup = re.search(r"const auto presentedCredential =\s*endpointValid\s*\?\s*F4T::ServerProfileStore::LookupResumeCredential\(serverKey\)\s*:\s*std::nullopt;", connect)
    first_attempt = re.search(r"auto connected = ConnectAndValidateServerOnce\(", connect)
    recovery = re.search(
        r"if \(!connected && failure && failure->kind == ConnectFailureKind::SessionRejected &&\s*"
        r"F4T::ConnectionPolicy::DecideResumeRecovery\(failure->code, presentedCredential\.has_value\(\)\)\) \{(.*?)\n\t\t\}",
        connect, re.S)
    stale = connect.index("const bool staleCacheRejected")
    assert lookup and first_attempt and recovery, \
        "the credential must be read before the first attempt and the recovery must follow the first attempt's unknown_resume"
    assert lookup.start() < first_attempt.start() < recovery.start() < stale, \
        "the one-time drop and fresh attempt must sit between the first attempt and the stale-cache check"
    body = recovery.group(1)
    clear = re.search(r"if \(F4T::ServerProfileStore::ClearResumeCredential\(serverKey, presentedCredential->token\)\) \{", body)
    assert clear is not None, "the recovery must only retry when the clear of the presented credential succeeds"
    retry = re.search(r"connected = ConnectAndValidateServerOnce\(", body)
    failed_clear = re.search(r"\}\s*else\s*\{", body)
    assert retry is not None and failed_clear is not None and clear.start() < retry.start() < failed_clear.start(), \
        "a changed or uncleared credential must skip the fresh attempt and keep the first failure"
    assert "ConnectAndValidateServerOnce(" not in body[failed_clear.start():], \
        "the else branch of the recovery must not attempt a join"
    assert body.count("DebugLogging::Log(") == 2, \
        "both the cleared and the not-cleared recovery outcomes must log"
    assert connect.count("ConnectAndValidateServerOnce(") == 4, \
        "ConnectAndValidateServer must attempt the join once, once more after a resume drop, once more after a modpack match, and once per backoff retry"

    print("join failure surfacing contract ok")

def _section_007_f4treconnectcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TReconnectContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TReconnectContract.py")
    from pathlib import Path
    import re
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def strip_comments(text):
        return re.sub(r"//[^\n]*", "", text)


    def read_code(relative):
        return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


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


    def in_order(text, *needles):
        positions = []
        for needle in needles:
            index = text.find(needle)
            if index < 0:
                return False
            positions.append(index)
        return positions == sorted(positions)


    reconnect = read_code("src/F4TReconnect.cpp")
    networking = read_code("src/F4TNetworking.cpp")
    networking_gns = read_code("src/F4TNetworkingGns.cpp")
    networking_inbound = read_code("src/F4TNetworkingInbound.cpp")
    networking_header = read_code("include/F4TNetworking.h")
    ui_client = read_code("src/F4TUIClient.cpp")
    slots = read_code("src/F4TCharacterSlotsClient.cpp")
    identity = read_code("src/F4TClientIdentity.cpp")
    identity_header = (ROOT / "co-core/include/F4TIdentityProtocol.h").read_text(encoding="utf-8")

    window = re.search(r"kReconnectWindow = std::chrono::seconds\((\d+)\);", reconnect)
    check(window is not None and int(window.group(1)) == 55 and int(window.group(1)) < 60,
          "the reconnect window must be 55 seconds, under the server's 60 second hold")
    check("kReconnectInterval = std::chrono::seconds(3);" in reconnect,
          "reconnect attempts must repeat every 3 seconds")
    check('PushConnectionStatus("Reconnecting...")' in reconnect,
          "the reconnect window must show Reconnecting while it runs")

    attempt = function_body(reconnect, "Attempt TryResumeOnce(")
    check(re.search(r"ConnectAndValidateServerOnce\([\s\S]*?a_target\.joinId,\s*false,\s*\[a_deadline\]\s*\{\s*return Cancelled\(\) \|\| Clock::now\(\) >= a_deadline;\s*\},\s*true\)", attempt) is not None,
          "a resume must reconnect with the join flow off and cancel validation at the reconnect deadline")
    check("replied && Clock::now() < a_deadline" in attempt,
          "a reply arriving after the reconnect deadline must not be accepted")
    check("ConnectAndValidateServer(" not in reconnect.replace("ConnectAndValidateServerOnce(", ""),
          "a resume must not use the connect wrapper that remembers the target and launches the session")
    check("LaunchValidatedSession" not in reconnect,
          "a resume must not launch a validated session again")
    check(reconnect.count("F4T::JoinFlow::") == 1 and 'F4T::JoinFlow::ReleaseVaultExitControls("reconnect", true);' in reconnect,
          "a resume must not touch the join flow, except one vault-exit release when the window does not resume")
    check("GetAssignedPlayerId() == a_previousPlayerId" in attempt
          and "!F4T::CharacterSlotsClient::NeedsSelection()" in attempt,
          "a resume counts only when the same player id comes back without a character selection")
    check("F4T::RemotePlayerState::GetAssignedPlayerResumed() &&" in attempt,
          "a resume counts only when the server flags the session as resumed, so a restarted server that reissues the same id is not a resume")
    check(re.search(r"DisconnectFromLocalServer\(\);\s*\}\s*if \(replied && !verified\) \{\s*return Cancelled\(\) \? Attempt::stop : Attempt::rejoin;\s*\}", attempt) is not None,
          "a completed hello that did not resume must disconnect, then rejoin through the join path unless the window was cancelled, because the server already consumed the hold and a retry cannot resume it")
    check(re.search(r'"kicked"\)\)\s*\{\s*F4T::DebugLogging::Log\([^;]*;\s*return Attempt::stop;', attempt) is not None
          and 'failure->code == "banned" || failure->code == "profile_banned" || failure->code == "kicked"' in attempt,
          "banned, profile-banned and kicked refusals must stop the window at once")
    check(re.search(r'"unknown_resume"\)\s*\{[^}]*return Cancelled\(\) \? Attempt::stop : Attempt::rejoin;', attempt) is not None,
          "a resume refused as unknown_resume must hand off to the join path, which recovers the credential, not retry the stale token until the window ends")
    check(in_order(function_body(reconnect, "bool CommitResume("), "g_cancelled", "F4T::Networking::IsConnectedToServer()"),
          "a resume must not commit after a cancel or a dropped connection")

    run_window = function_body(reconnect, "void RunWindow(")
    check("t_worker = true;" in run_window,
          "the window worker must mark itself so its own retry cleanup does not cancel it")
    check(in_order(run_window, "if (Cancelled() || (rejoin && F4T::UIClient::IsJoinInProgress())) {", "window ended without a resume"),
          "a join that the user started during the window must end the window without the could-not-reconnect notice")
    check("bool IsJoinInProgress();" in read_code("include/F4TUIClient.h"),
          "the UI client must declare IsJoinInProgress for the reconnect window to read")
    check(in_order(run_window, "window ended without a resume", "NotifyTransportDisconnected("),
          "NotifyTransportDisconnected must only be called when the window ends without a resume")
    check(reconnect.count("NotifyTransportDisconnected(") == 2,
          "the reconnect code must call NotifyTransportDisconnected only when the window ends without a resume or a rejoin fails")
    check(in_order(run_window, "if (!resumed) {", "AddTask(", 'ReleaseVaultExitControls("reconnect", true);', "const bool rejoined = rejoin && RejoinThroughJoinPath(a_target, deadline);", "WipePassword(a_target.serverPassword);", "if (rejoined) {", "if (resumed) {"),
          "a reconnect that does not resume must release the vault-exit controls on the game thread before a rejoin starts, and a rejoin must start before the password is wiped and must return before the resume and disconnect paths")
    check("const auto deadline = a_lostAt + kReconnectWindow;" in run_window,
          "the reconnect window must run from the moment the connection was lost, so the loss-save wait counts against the server's hold")
    check(in_order(function_body(reconnect, "void MarkTransportLost("), "std::scoped_lock lock{ g_mutex };", "g_lostAt = Clock::now();"),
          "the loss time must be recorded under the reconnect lock when the connection is lost")
    check(in_order(function_body(reconnect, "bool TryArm()"), "lostAt = g_lostAt.value_or(Clock::now());", "g_lostAt.reset();", "if (g_active || !g_target || !previousPlayerId) {", "g_active = true;"),
          "the arm must take the recorded loss time and clear it before the window opens")
    check(in_order(function_body(reconnect, "void Remember("), "std::scoped_lock lock{ g_mutex };", "g_target = std::move(a_target);", "g_lostAt.reset();"),
          "remembering a new target must clear any recorded loss time, so a stale loss stamp cannot end a later window at once")
    rejoin_path = function_body(reconnect, "bool RejoinThroughJoinPath(")
    check(in_order(rejoin_path, "while (FinishUnlessCancelled()) {", "errorBuf[0] = '\\0';", "F4T::UIClient::BeginJoin(&request, a_target.serverPassword, OnRejoinComplete, nullptr, errorBuf, sizeof(errorBuf));"),
          "a rejoin must close the reconnect window before each join starts, because the join refuses while a window is open, and must clear the error buffer before each attempt")
    check(in_order(rejoin_path, "started || !IsTransientRefusal(errorBuf) || Clock::now() + kReconnectInterval >= a_deadline", "Rearm();", "while (Clock::now() < nextAttempt && !Cancelled()) {", "return started;"),
          "a rejoin refused by a finishing disconnect or a session save must re-arm the window and wait with the cancel check until the reconnect window deadline")
    check(in_order(function_body(reconnect, "bool FinishUnlessCancelled("), "std::scoped_lock lock{ g_mutex };", "if (g_cancelled || F4T::UIClient::IsVoluntaryExitRequested()) {", "g_active = false;"),
          "the window must be closed under the same lock as the cancel check, so a cancel cannot be lost between the check and the join")
    check(in_order(function_body(reconnect, "bool Cancelled("), "std::scoped_lock lock{ g_mutex };", "return g_cancelled || F4T::UIClient::IsVoluntaryExitRequested();"),
          "the retry wait's cancel check must honor a user cancel and the user's voluntary exit")
    check(in_order(function_body(reconnect, "void Rearm("), "std::scoped_lock lock{ g_mutex };", "g_active = true;"),
          "the retry wait must keep the reconnect window active so a user cancel reaches it")
    transient_refusal = function_body(reconnect, "bool IsTransientRefusal(")
    check('"Finishing the previous disconnect; try again shortly."' in transient_refusal and '"Saving the current session before disconnect; try again shortly."' in transient_refusal,
          "only the two transient join refusals may be retried")
    check('#include "F4TJoinFlow.h"' in reconnect and '#include "F4TUIClientJoin.h"' in reconnect,
          "the reconnect code must include the join declarations it calls")
    check(in_order(function_body(reconnect, "void OnRejoinComplete("), "if (a_success) {", "NotifyTransportDisconnected("),
          "a failed rejoin must show the disconnect screen, and a successful one must leave the join to report")

    forget = function_body(reconnect, "void Forget()")
    check("if (g_active && !t_worker) {" in forget and "g_cancelled = true;" in forget,
          "forgetting the target must cancel an active window unless the window itself is the caller")

    check(networking.count("TryReconnectAfterTransportLoss()") == 1,
          "only the generic receive-loop exit may arm a reconnect")
    check("if (g_receiveThreadRunning.exchange(false) && !F4T::UIClient::TryReconnectAfterTransportLoss()) {" in networking,
          "the generic receive-loop exit must be the only place that arms a reconnect")
    check(re.search(r'exchange\(false\)\) \{\s*F4T::UIClient::NotifyTransportDisconnected\("Server violated the receive boundary\."\);', networking) is not None,
          "a receive-boundary drop must notify directly and never arm a reconnect")
    check(re.search(r'exchange\(false\)\) \{\s*F4T::UIClient::NotifyTransportDisconnected\("Server sent an oversized unterminated frame\."\);', networking) is not None,
          "an oversized-frame drop must notify directly and never arm a reconnect")
    check(in_order(function_body(networking, "void DisconnectFromLocalServer()"), "F4T::Reconnect::Forget();", "StopReceiveThread();"),
          "a deliberate disconnect must forget the reconnect target before the receive thread stops")

    session_ended = function_body(networking_inbound, "void HandleSessionEndedPacket(")
    check(in_order(session_ended, "F4T::Networking::DisconnectFromLocalServer();", "F4T::UIClient::NotifyTransportDisconnected("),
          "a kick or ban must disconnect before it notifies, so the receive loop never arms a reconnect")

    check(in_order(function_body(ui_client, "void Callback_Disconnect()"), "g_voluntaryExitRequested.store(true);", "F4T::Reconnect::Cancel();"),
          "the deliberate disconnect must mark the exit voluntary before it cancels any reconnect window, so a rejoin that has not started yet refuses to start")
    check("F4T::Reconnect::Cancel();" in function_body(ui_client, "void NotifyTransportDisconnected("),
          "any transport notification must cancel an active reconnect window")
    check("F4T::Reconnect::IsActive()" in function_body(ui_client, "bool Callback_JoinSession("),
          "a join must be refused while a reconnect window is open")
    check("F4T::Reconnect::IsActive()" in function_body(ui_client, "bool Callback_ConnectLocal("),
          "a local connect must be refused while a reconnect window is open")
    check(in_order(function_body(ui_client, "bool Callback_JoinSession("), "const auto attemptToken = g_connectionTokenCounter.fetch_add(1) + 1;", "if (F4T::Reconnect::OnWorkerThread() && g_voluntaryExitRequested.load()) {", "Disconnect requested before the rejoin started."),
          "a rejoin must refuse to start when a disconnect was requested after its window closed, and the check must come after the connection token is taken")
    check("if (!F4T::Reconnect::OnWorkerThread()) { g_voluntaryExitRequested.store(false); }" in function_body(ui_client, "bool Callback_JoinSession("),
          "only a user join may clear the voluntary-exit flag, so a disconnect recorded before a rejoin is not erased")
    check('"Finishing the previous disconnect; try again shortly."' in function_body(ui_client, "bool Callback_JoinSession(") and '"Saving the current session before disconnect; try again shortly."' in function_body(ui_client, "bool Callback_JoinSession("),
          "the join path must report the two transient refusals that the rejoin retries, using the exact text the window matches")
    check(in_order(function_body(ui_client, "bool TryReconnectAfterTransportLoss()"),
                   "g_joinInProgress.load()", "g_voluntaryExitRequested.load()",
                   "F4T::SessionSaveGuard::IsSessionExitPending()", "F4T::Reconnect::TryArm()"),
          "a join, a voluntary exit or a pending session exit must never arm a reconnect")
    check(ui_client.count("F4T::Reconnect::TryArm()") == 1,
          "only TryReconnectAfterTransportLoss may arm a reconnect")

    loss_save_source = read_code("src/F4TNetworkingLossSave.cpp")
    loss_save = function_body(loss_save_source, "void CloseAfterLossSave(")
    check("CloseAfterLossSave(g_receiveThreadRunning, CloseSocket);" in function_body(networking, "void CloseSocketAfterLossSave()") and
          in_order(loss_save, "F4T::Reconnect::MarkTransportLost();", "F4T::JoinFlow::IsActive()", "ShouldSaveBeforeLossClose()", "g_lossSaveHold.store(true);", "RunOrDeferSessionExit(", "a_close();\n\t\tg_lossSaveHold.store(false);"),
          "an unexpected loss must record the loss time, skip the save while the join flow is active, keep the socket open for the exit save and only then close it")
    receive_loop = function_body(networking, "void ReceiveLoop()")
    check(in_order(receive_loop, "closed the connection.", "CloseSocketAfterLossSave();") and
          in_order(receive_loop[receive_loop.index("while receiving."):], "CloseSocketAfterLossSave();"),
          "both unexpected receive-loop losses must take the save-then-close path")
    check("F4T::Networking::IsLossSaveHeld()" in function_body(networking, "bool SendPacketBytes(std::string_view a_packet, std::optional"),
          "sends must be refused while the loss save holds the dead socket")
    check("bool IsLossSaveHeld();" in read_code("include/F4TNetworking.h") and "return g_lossSaveHold.load();" in function_body(loss_save_source, "bool IsLossSaveHeld()"),
          "the loss-save hold must be readable so the late-exit window arms only while the connection-lost save runs")
    check(in_order(function_body(loss_save_source, "bool ShouldSaveBeforeLossClose()"),
                   "F4T::UIClient::IsJoinInProgress()", "F4T::UIClient::IsVoluntaryExitRequested()",
                   "F4T::SessionSaveGuard::IsSessionExitPending()", "F4T::RemotePlayerState::GetAssignedPlayerId()", "F4T::Reconnect::IsActive()"),
          "the loss save must never run during a join, a voluntary exit, a pending session exit or an open reconnect window")
    join_flow = read_code("src/F4TJoinFlow.cpp")
    check(in_order(function_body(join_flow, "void EndForFirstTimeJoin()"), "SetVaultExitControlsDisabled(false"),
          "the first-time join end must release the vault-exit control lock")

    check(re.search(r"if \(a_joinFlow\) \{\s*F4T::JoinFlow::BeginConnecting\(\);\s*\}", networking_gns) is not None,
          "BeginConnecting must run only for a join flow")
    check(re.search(r"if \(a_joinFlow\) \{\s*F4T::JoinFlow::NotifyAuthenticated\(\);\s*\}", networking_gns) is not None,
          "NotifyAuthenticated must run only for a join flow")
    check("bool a_joinFlow = true," in networking_header,
          "the join flow argument must default to true so existing callers keep their join flow")
    once_start = networking_gns.index("bool ConnectAndValidateServerOnce(")
    wrapper_start = networking_gns.index("\n\tbool ConnectAndValidateServer(", once_start)
    check("F4T::Reconnect::Remember(" not in networking_gns[once_start:wrapper_start],
          "the once-only connect used by a resume must not remember the target")
    check(re.search(r"if \(connected\) \{\s*F4T::Reconnect::Remember\(", networking_gns[wrapper_start:]) is not None,
          "the join wrapper must remember the target only after a connected session")

    check("if (NeedsSelection() && !F4T::Reconnect::IsActive()) {" in slots,
          "a reconnect window must not open the character selection")
    check("return g_reconnecting || F4T::Reconnect::IsActive();" in slots,
          "IsReconnecting must report an open reconnect window")
    check(re.search(r'if \(F4T::Reconnect::IsActive\(\)\) \{\s*block\["needsSelection"\] = false;', slots) is not None,
          "an open reconnect window must report that no character selection is needed")
    check("++g_listStateSequence;" in slots and "std::uint64_t ListStateSequence()" in slots,
          "the welcome must bump the list-state sequence that the resume waits on")

    check('#include "F4TIdentityProtocol.h"' in identity,
          "the client identity must include the identity protocol header")
    check("metadata.capabilities.emplace_back(std::string{ F4T::Identity::ReconnectResumeCapability });" in identity,
          "every session hello must advertise the reconnect resume capability through the header constant")
    check('"reconnect-resume-v1"' not in identity,
          "the resume capability must not be a string literal in the client")
    check(re.search(r'ReconnectResumeCapability\s*=\s*"reconnect-resume-v1"', identity_header) is not None,
          "F4TIdentityProtocol.h must define ReconnectResumeCapability as reconnect-resume-v1")
    adapter = strip_comments((ROOT / "co-core/src/protocol/F4TGnsProtocolAdapterInbound.cpp").read_text(encoding="utf-8"))
    check(in_order(function_body(adapter, "InboundResult Session::Impl::HandleSessionReady("), 'readyWelcome["playerId"] = *playerId;', 'readyWelcome["resumed"] = resumed;'),
          "the ready welcome must carry the resumed flag so the client can tell a resume from a restarted server")
    check(in_order(function_body(networking_inbound, "void HandleWelcomePacket("), "SetAssignedPlayerResumed(", "SetAssignedPlayerId(*playerId);"),
          "the welcome must record the resumed flag before the player id, so a resume is never judged without it")
    remote_state = read_code("src/F4TRemotePlayerState.cpp")
    check("g_assignedPlayerResumed = false;" in function_body(remote_state, "void ClearAssignedPlayerId()"),
          "clearing the player id must also clear the resumed flag")
    remote_state_header = read_code("include/F4TRemotePlayerState.h")
    check("void SetAssignedPlayerResumed(bool a_resumed);" in remote_state_header
          and "bool GetAssignedPlayerResumed();" in remote_state_header,
          "the remote player state must declare the resumed flag accessors")
    check("void ReleaseVaultExitControls(std::string_view a_reason, bool a_onlyWhenDisabled = false);" in read_code("include/F4TJoinFlow.h"),
          "the join flow must declare the vault-exit control release")
    check(in_order(function_body(join_flow, "void ReleaseVaultExitControls("), "!F4T::SessionWorldState::IsPlayerInWorld()", "g_vaultExitControlsDisabled = false;", "a_onlyWhenDisabled && !g_vaultExitControlsDisabled", '"EnablePlayerControls"'),
          "releasing the vault-exit controls must clear the lock when no player is in the world, before the only-when-disabled check and before it re-enables the player controls")
    check(in_order(function_body(join_flow, "void SetVaultExitControlsDisabled("), "!F4T::SessionWorldState::IsPlayerInWorld()", "g_vaultExitControlsDisabled = false;", "!a_disabled && !g_vaultExitControlsDisabled", "g_vaultExitControlsDisabled = a_disabled;", '"DisablePlayerControls"'),
          "setting the vault-exit controls must clear the lock when no player is in the world, and must otherwise record only controls that were really changed")
    check(in_order(join_flow, "vanilla controls were not restored before the wait expired", 'ReleaseVaultExitControls("vanilla pod-exit wait expired");'),
          "an expired vanilla pod-exit wait must release the controls it locked")
    check(in_order(function_body(join_flow, "void OnWorldReset("), "if (g_active) {", "ReleaseVaultExitControls(", 'SetVaultExitControlsDisabled(false, "world reset");'),
          "a world reset must release the controls whether or not a join is running")
    setup_flow = read_code("src/F4TSessionCharacterSetupFlow.cpp")
    abort_body = function_body(setup_flow, "void Abort()")
    check(in_order(abort_body, "if (!g_onboardingIssued.exchange(false) || !F4T::SessionWorldState::IsPlayerInWorld()) {", 'cqf CO_CharacterSetupQuest AbortOnboarding'),
          "aborting setup must run AbortOnboarding only once onboarding was issued and a player is in the world, so the main menu never runs the console command")
    check(all(networking.count("if (!F4T::Networking::IsResumeAttempt()) " + call) == 2 for call in ("F4T::TradeClient::Reset();", "F4T::CharacterSlotsClient::Reset();")) and
          "F4T::Networking::SetResumeAttempt(true)" in read_code("src/F4TReconnect.cpp") and "constexpr auto kLossSaveWait = 20s;" in loss_save_source,
          "a resume attempt must keep committed trades and slot state, and the loss save must leave most of the reconnect window")
    world_state = read_code("src/F4TSessionWorldState.cpp")
    check("[[nodiscard]] bool IsPlayerInWorld();" in read_code("src/F4TSessionWorldState.h") and "return !IsMainMenuOpen() && RE::PlayerCharacter::GetSingleton() != nullptr;" in function_body(world_state, "bool IsPlayerInWorld()"),
          "the player-in-world check must be false on the main menu and when no player exists, so console calls are skipped there")
    session_launch = read_code("src/F4TSessionLaunch.cpp")
    check(in_order(function_body(session_launch, "LaunchOutcome LaunchValidatedSession("), "F4T::SessionCharacterSetupFlow::Abort();", 'F4T::JoinFlow::ReleaseVaultExitControls("join launch", true);', "ResetFirstJoinState();"),
          "a validated launch must abort setup and release the controls before it resets the first-join state")
    check("F4T::SessionSaveGuard::TakeLateExitSave()" in session_launch,
          "the post-save bind must take the late-exit save window so a save after proceed still binds")
    launch_state = read_code("src/F4TSessionLaunchState.cpp")
    check(in_order(function_body(launch_state, "void CompletePendingJoin("), "F4T::SessionCharacterSetupFlow::Abort();", "F4T::JoinFlow::ReleaseVaultExitControls(a_reason, true);", "SetPhase(CharGenPhase::Failed, a_reason);"),
          "a failed join must abort setup and release the controls before it marks the phase failed")
    guard = read_code("src/F4TSessionSaveGuard.cpp")
    check('retry = outcome == "timeout" && !landed && !F4T::Networking::IsLossSaveHeld() && !g_sessionExitRetried' in guard,
          "a loss save must get one attempt, so a timed-out save must not retry while the loss hold is up")
    check("bool TakeLateExitSave();" in read_code("include/F4TSessionSaveGuard.h"),
          "the save guard must declare TakeLateExitSave")
    tick_body = function_body(guard, "void Tick()")
    late_exit_body = tick_body[tick_body.index("if (!postSaveSeen) {"):]
    check(in_order(late_exit_body, "std::scoped_lock lock{ g_sessionExitMutex };", "if (F4T::Networking::IsLossSaveHeld()) {", "g_lateExitSaveArmedAt = std::chrono::steady_clock::now();", "proceed();"),
          "the late-exit window must arm only from the connection-lost save hold, checked under the session exit lock before proceed runs, so a save that arrives after proceed is still taken")
    check(in_order(function_body(guard, "bool TakeLateExitSave()"), "std::scoped_lock lock{ g_sessionExitMutex };", "g_lateExitSaveArmedAt.reset();"),
          "taking the late-exit save must read and clear the armed window under the session exit lock")
    lineage = read_code("src/F4TSaveLineageSession.cpp")
    check("(!F4T::Networking::IsConnectedToServer() && !a_lateExitSave)" in function_body(lineage, "void TryUpdateActiveProfileBinding("),
          "a late-exit save must still bind the profile while disconnected")
    poll_body = function_body(lineage, "void PollPendingRecord()")
    check(in_order(poll_body, "CanRebindProfileSave(", "GetConnectionGeneration() != generation", "RecordSettledSave(serverKey, saveName);", "RecordActiveSave(serverKey, saveName);") and "IsConnectedToServer" not in poll_body,
          "a settled save must check ownership first, persist without announcing when the connection generation changed, and announce only on the same connection")
    settled_body = function_body(lineage, "void RecordSettledSave(")
    check("RecordAttestedSave(" in settled_body and "AnnounceSaveAttestation(" not in settled_body,
          "a save that settles after the connection changed must be persisted without announcing it to the current server")


    def fixture(name):
        return (PLUGIN / "tests" / name).read_text(encoding="utf-8")


    check("namespace F4T::Reconnect { void Forget() {} }" in fixture("F4TConnectionLifecycleTests.cpp"),
          "the lifecycle fixture must stub Reconnect::Forget")
    check("bool TryReconnectAfterTransportLoss() { return false; }" in fixture("F4TReceiveThreadOwnershipTests.cpp"),
          "the receive-thread fixture must stub TryReconnectAfterTransportLoss")
    check("bool TryReconnectAfterTransportLoss() { return false; }" in fixture("F4TReceiveNotificationTests.cpp"),
          "the receive-notification fixture must stub TryReconnectAfterTransportLoss")
    check("namespace F4T::Reconnect { bool IsActive() { return false; } void Cancel() {} bool OnWorkerThread() { return false; } }" in fixture("F4TJoinCancellationTests.cpp"),
          "the join-cancellation fixture must stub Reconnect::IsActive, Cancel and OnWorkerThread")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TReconnectContract: all checks passed.")

def _section_008_f4treliableoutboxcontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TReliableOutboxContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TReliableOutboxContract.py")
    from pathlib import Path
    import sys

    ROOT = Path(__file__).resolve().parents[3]
    PLUGIN = ROOT / "CO-BETA/plugin"

    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    def read(relative):
        path = PLUGIN / relative
        if not path.exists():
            failures.append(f"missing {relative}")
            return ""
        return path.read_text(encoding="utf-8")


    def body_after(text, anchor):
        start = text.find(anchor)
        if start < 0:
            return None
        brace = text.index("{", start + len(anchor))
        depth = 0
        for index in range(brace, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    return text[brace + 1:index]
        return None


    shim = "\n".join(read(f"src/{name}") for name in ("F4TGnsWinsockShim.cpp", "F4TGnsWinsockShimBridge.cpp", "F4TGnsWinsockShimFrames.cpp"))
    state_header = read("src/F4TGnsWinsockShimState.h")
    glue = read("src/F4TGnsReliableSend.cpp")
    outbox = read("include/F4TReliableOutbox.h")

    check('#include "F4TReliableOutbox.h"' in shim, "shim must use the reliable outbox")
    check("#include \"RE/" not in outbox and "Windows.h" not in outbox, "outbox header must stay engine and platform independent")
    check("sleep_for" not in outbox, "the outbox must never sleep")

    state = body_after(state_header, "struct SocketState")
    check(state is not None and "F4T::ReliableOutbox::Outbox reliableOutbox;" in state, "each connection must own one reliable outbox")

    send = body_after(shim, "bool SendAdaptedMessages(")
    check(send is not None, "SendAdaptedMessages not found")
    if send is not None:
        check("sleep_for" not in send, "SendAdaptedMessages must not sleep on backpressure")
        lock = send.find("const std::scoped_lock bridgeLock(a_state->bridgeMutex);")
        flush = send.find("DrainReliableOutboxLocked(*api, *a_state)")
        submit = send.find("a_state->reliableOutbox.Submit(")
        check(0 <= lock < flush < submit, "gameplay sends must drain queued reliable messages before submitting new ones, under the bridge lock")
        unreliable = send.find("if (message.delivery == F4T::GnsProtocolAdapter::Delivery::UnreliableSequenced) {")
        tolerated = send.find("if (result != kSendSent && result != kSendDropped && result != kSendBackpressure) {", unreliable)
        check(0 <= unreliable < tolerated < submit, "UnreliableSequenced snapshots must keep dropping on backpressure and never enter the outbox")
        check("SubmitResult::Full" in send, "a full outbox must fail the send")

    recv = body_after(shim, "int Recv(SOCKET a_socket")
    check(recv is not None, "Recv not found")
    if recv is not None:
        check("sleep_for" not in recv, "Recv must not sleep")
        flush = recv.find("FlushReliableOutbox(state)")
        poll = recv.find("api->poll(")
        check(0 <= flush < poll, "the receive loop must drain the reliable outbox before polling")

    flush_fn = body_after(shim, "bool FlushReliableOutbox(")
    check(flush_fn is not None, "FlushReliableOutbox not found")
    if flush_fn is not None:
        lock = flush_fn.find("const std::scoped_lock bridgeLock(a_state->bridgeMutex);")
        flush = flush_fn.find("DrainReliableOutboxLocked(*api, *a_state)")
        check(0 <= lock < flush, "FlushReliableOutbox must drain under the bridge lock")

    drain = body_after(glue, "bool DrainReliableOutboxLocked(")
    check(drain is not None, "DrainReliableOutboxLocked not found")
    if drain is not None:
        check("a_state.reliableOutbox.Flush(" in drain, "the drain must flush the connection outbox")
        check("FlushResult::TimedOut" in drain and "SetError(WSAENOBUFS)" in drain, "a timed-out reliable message must fail the send like the old retry window")
    check((shim + glue).count("reliableOutbox.Flush(") == 1, "only DrainReliableOutboxLocked may flush the outbox")
    check(shim.count("DrainReliableOutboxLocked(*api, *a_state)") == 2, "exactly the gameplay send and the receive loop drain the outbox")

    close = body_after(shim, "int CloseSocket(SOCKET a_socket)")
    check(close is not None and "state->reliableOutbox.Clear();" in close, "closing a connection must discard its queued reliable messages")

    check("kBackpressureSleep" not in shim, "the backpressure sleep constant must be gone")
    check("kReliableBackpressureTimeout" not in shim, "the acceptance timeout must live in the outbox")
    check("std::chrono::milliseconds{ 250 }" in outbox, "the per-message acceptance timeout must stay 250 ms")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TReliableOutboxContract: all checks passed.")

def _section_009_f4tremotenameresolutioncontract():
    """Contract moved verbatim from CO-BETA/plugin/tests/F4TRemoteNameResolutionContract.py."""
    __file__ = str(_MERGE_ROOT / "CO-BETA/plugin/tests/F4TRemoteNameResolutionContract.py")
    from pathlib import Path
    import sys

    PLUGIN = Path(__file__).resolve().parents[1]


    def read(relative: str) -> str:
        return (PLUGIN / relative).read_text(encoding="utf-8")


    def check(condition: bool, message: str) -> None:
        if not condition:
            print(f"FAIL: {message}")
            _merge_exit(1)


    remote = read("src/F4TRemotePlayerState.cpp")
    inbound = read("src/F4TNetworkingInbound.cpp")
    nameplate = read("include/F4TSocialNameplate.h")

    start = remote.index("void UpdateRemotePlayer(RemotePlayerState a_state)")
    end = remote.index("\n\t}\n", start)
    body = remote[start:end]

    call = body.find("F4T::SocialNameplate::ResolveNameFlag(")
    check(call != -1, "UpdateRemotePlayer must derive nameResolved through SocialNameplate::ResolveNameFlag")
    check(call < body.find("if (existing != g_remotePlayers.end()) {"),
          "the resolved flag must be derived before the carry block overwrites characterName and displayName")
    check("nameResolved = existing->second.nameResolved" not in body,
          "the carry block must not copy nameResolved; ResolveNameFlag decides it from the frame")
    check("nameResolved" not in inbound,
          "F4TNetworkingInbound.cpp must not write nameResolved; the frame parser only fills the wire fields")
    check("return a_carriedResolved;" in nameplate and "if (!a_characterName.empty()) {" in nameplate,
          "ResolveNameFlag must return true for characterName, false for displayName alone, and carry otherwise")
    check("if (!a_displayName.empty()) {" in nameplate and "return a_displayName" not in nameplate,
          "ResolveDisplayName must keep preferring displayName so a forgotten player never shows a stale characterName")

    check("if (!a_state.hasPowerArmorUpdate && existing->second.hasPowerArmorUpdate) {" in body,
          "omitted power armor must retain the previous remote state")
    for field in ("hasPowerArmorUpdate", "powerArmorInFrame", "powerArmorFrameBaseFormId", "powerArmorPieces"):
        check(f"a_state.{field} = " in body, f"power armor carry must preserve {field}")

    check("if (!a_state.hasFactionTagUpdate && existing->second.hasFactionTagUpdate) {" in body,
          "omitted faction tag must retain the previous remote state")
    check("remoteState.factionTag = F4T::ChatFactionTag::Sanitize(" in inbound and "remoteState.hasFactionTagUpdate = true;" in inbound,
          "a playerState that carries displayName must parse factionTag through ChatFactionTag::Sanitize and mark it updated")

    print("F4TRemoteNameResolutionContract passed")

def _section_010_f4tsleepqueuecontract():
    """Sleep packets wait in a queue until the server accepts them; seat events wait for their proxy."""
    from pathlib import Path

    PLUGIN = Path(__file__).resolve().parents[1]
    failures = []


    def check(condition, message):
        if not condition:
            failures.append(message)


    sleep = (PLUGIN / "src/F4TSleepController.cpp").read_text(encoding="utf-8")
    seat = (PLUGIN / "src/F4TFurnitureSeatSync.cpp").read_text(encoding="utf-8")

    check("SendOrQueue(SleepBedIdentity::MakeSleepExit());" in sleep, "a failed sleep exit must be queued, not dropped")
    check("SendOrQueue(*packet);" in sleep and "g_enterSent = true;" in sleep, "a failed sleep enter must be queued and still closed by an exit")
    check(sleep.index("FlushUnsentUses();") < sleep.index("if (g_state == State::Idle) {", sleep.index("void Tick()")),
          "queued sleep packets must flush before the idle return in Tick")
    check(sleep.index("g_unsentUses.clear();") > sleep.index("void OnDisconnect()"), "disconnect must drop queued sleep packets")
    check("kPendingWindow{ 30 }" in seat and "g_pending.erase(use.playerId);" in seat,
          "an unproxied seat event must wait for its proxy within a bounded window")
    check("generation != g_pendingGeneration" in seat, "a new connection generation must clear pending seat events")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        _merge_exit(1)

    print("F4TSleepQueueContract: all checks passed.")

_SECTIONS = [
    ("CO-BETA/plugin/tests/F4TFurnitureSyncContract.py", _section_001_f4tfurnituresynccontract),
    ("CO-BETA/plugin/tests/F4TGnsFailureSourceContract.py", _section_002_f4tgnsfailuresourcecontract),
    ("CO-BETA/plugin/tests/F4THandshakeRejectionActionContract.py", _section_003_f4thandshakerejectionactioncontract),
    ("CO-BETA/plugin/tests/F4TIdentityOrderingContract.py", _section_005_f4tidentityorderingcontract),
    ("CO-BETA/plugin/tests/F4TJoinFailureSurfacingContract.py", _section_006_f4tjoinfailuresurfacingcontract),
    ("CO-BETA/plugin/tests/F4TReconnectContract.py", _section_007_f4treconnectcontract),
    ("CO-BETA/plugin/tests/F4TReliableOutboxContract.py", _section_008_f4treliableoutboxcontract),
    ("CO-BETA/plugin/tests/F4TRemoteNameResolutionContract.py", _section_009_f4tremotenameresolutioncontract),
    ("CO-BETA/plugin/tests/F4TNetworkServerContracts.py", _section_010_f4tsleepqueuecontract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "F4TNetworkServerContracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
