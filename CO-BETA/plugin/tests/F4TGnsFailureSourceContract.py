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
