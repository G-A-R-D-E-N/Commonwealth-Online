from pathlib import Path


root = Path(__file__).resolve().parents[3]
source = (root / "CO-BETA/plugin/src/F4TServerProfileStore.cpp").read_text(encoding="utf-8")
shim = (root / "CO-BETA/plugin/src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8")
ui = (root / "CO-BETA/plugin/src/F4TUIClient.cpp").read_text(encoding="utf-8")

assert "serverPassword" not in source, "the server profile store must never read or persist the server password"
assert ".serverPassword = identity->serverPassword" in shim, "the Winsock shim must copy serverPassword into the ClientIdentity it hands to co-core"
assert "host.c_str(), port, connectionError, &connectionFailure, serverPassword, id)" in ui, "the UI join worker must pass the join password into ConnectAndValidateServer"
