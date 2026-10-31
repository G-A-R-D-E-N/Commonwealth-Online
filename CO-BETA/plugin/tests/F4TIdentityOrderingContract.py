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
    "ConnectAndValidateServerLegacy(host.c_str(), port, a_connectError)",
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
assert "auto connected = ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, cachedFingerprint, a_joinId);" in wrapper
assert wrapper.index("CachedFingerprint(serverKey)") < wrapper.index("ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, cachedFingerprint, a_joinId)")
assert "if (!connected && failure && !failure->matchingModpackFingerprint.empty()) {" in wrapper
assert "connected = ConnectAndValidateServerOnce(a_host, a_port, a_error, &failure, a_serverPassword, matching, a_joinId);" in wrapper
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
