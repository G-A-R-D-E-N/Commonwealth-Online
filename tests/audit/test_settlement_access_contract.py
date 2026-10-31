from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


service = read("CO-SERVER/server-cpp/src/ServerSettlementAccess.cpp")
protocol = read("co-core/src/protocol/F4TSettlementAccessProtocol.cpp")
table = read("co-core/src/world/F4TSettlementAccess.cpp")
header = read("co-core/include/F4TSettlementAccess.h")
workshop = read("CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc")
dispatch = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
setup = read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc")
store = read("CO-SERVER/server-cpp/src/DurableStateFileStore.cpp")
gate = read("CO-SERVER/server-cpp/src/PersistenceGate.cpp")
server_xmake = read("CO-SERVER/server-cpp/xmake.lua")
core_xmake = read("co-core/xmake.lua")
core_tests = read("co-core/xmake-tests.lua")
workflow = read(".github/workflows/co-audit-regressions.yml")

assert "(void)table.Restore(a_snapshot);" in service, "a save that fails must roll the build list back"
assert "persistence->SaveSettlementAccess(F4T::SettlementAccessProtocol::MakeStoredJson(table.Snapshot()))" in service, "every committed build-list change must be saved"
assert "kRefillPerSecond = 1.0;" in service and "kBurst = 4.0;" in service, "settlement access requests must be rate limited"
assert '"settlementAccessGet"' in service and '"settlementAccessUpdate"' in service, "the server must answer get and update"
assert '"settlementAccessState"' in protocol and '"settlementAccessReject"' in protocol, "the protocol must name state and reject"
assert "MaximumAllowedProfiles = 15;" in header and "MaximumSettlements = 1024;" in header, "the list and settlement caps must stay at 15 and 1024"
assert "Identity::IsValidProfileId(a_profileId)" in table, "listed profile ids must pass the identity rule"
assert "workshopAuthority = SettlementAuthority();" in workshop and "workshopAuthority{ SettlementAuthority() };" in workshop, "the workshop authority must default to the build list"
assert "WorkshopAuthority::AllowAll" not in workshop, "allow-all must not be the default workshop authority"
assert "if (auto frames = DispatchSettlementPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds)) return std::move(*frames);" in dispatch, "settlement packets must reach the settlement service"
assert "if (!protocol.EnableSettlementAccess(persistence)) {" in setup, "stored build lists must be restored at startup"
assert '_root.value("settlementAccess", nlohmann::json::array())' in store and 'a_root["settlementAccess"] = a_settlementAccess;' in store, "build lists must persist under the settlementAccess root key"
assert "_store.GetSettlementAccess()" in gate and "if (!SavingEnabled()) return;\n\t\t_store.SaveSettlementAccess(a_settlementAccess);" in gate, "build lists must save only when saving is enabled"
assert 'add_files("src/ServerSettlementAccess.cpp")' in server_xmake, "the server build must compile the settlement service"
assert 'server_test("CommonwealthOnlineServerSettlementAccessTests", "tests/ServerSettlementAccessTests.cpp", "CommonwealthOnlineServerProtocol")' in server_xmake, "the server settlement tests must be registered"
assert 'add_files(path.join(co_core_dir, "src/world/F4TSettlementAccess.cpp"))' in core_xmake, "the core build must compile the settlement table"
assert 'add_files(path.join(co_core_dir, "src/protocol/F4TSettlementAccessProtocol.cpp"))' in core_xmake, "the core build must compile the settlement protocol"
assert 'core_target("CoreSettlementAccessTests", "CommonwealthOnlineCoreSettlementAccessTests")' in core_tests, "the core settlement table tests must be registered"
assert 'core_target("CoreSettlementAccessProtocolTests", "CommonwealthOnlineCoreSettlementAccessProtocolTests")' in core_tests, "the core settlement protocol tests must be registered"
assert "run: python3 tests/audit/test_settlement_access_contract.py" in workflow, "CI must run this contract"

print("test_settlement_access_contract: all checks passed.")
