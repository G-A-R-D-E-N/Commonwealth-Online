from pathlib import Path
import re


root = Path(__file__).resolve().parents[3]
plugin = root / "CO-BETA/plugin"
admin_cpp = (plugin / "src/F4TAdminItems.cpp").read_text(encoding="utf-8")
admin_h = (plugin / "include/F4TAdminItems.h").read_text(encoding="utf-8")
queue_h = (plugin / "include/F4TAdminItemQueue.h").read_text(encoding="utf-8")
inbound = (plugin / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8") + (plugin / "src/F4TNetworkingInboundAdmin.cpp").read_text(encoding="utf-8")
outbound = (plugin / "src/F4TNetworkingOutbound.cpp").read_text(encoding="utf-8")
networking_h = (plugin / "include/F4TNetworking.h").read_text(encoding="utf-8")
networking_cpp = (plugin / "src/F4TNetworking.cpp").read_text(encoding="utf-8")
runtime = (plugin / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8")
identity = (plugin / "src/F4TClientIdentity.cpp").read_text(encoding="utf-8")
workflow_path = next(path for path in (root / ".github/public-ci/workflows/co-beta-ci.yml", root / ".github/workflows/co-beta-ci.yml") if path.is_file())
workflow = workflow_path.read_text(encoding="utf-8")

handle_line = inbound[inbound.index("void HandlePacketLine(const std::string& a_line)"):]

assert 'kCapability = "admin-items-v1"' in admin_h
assert 'emplace_back(std::string{ F4T::AdminItems::kCapability })' in identity
assert "#include \"F4TAdminItems.h\"" in identity

assert '*packetType == "adminItemOp"' in handle_line
assert "HandleAdminItemOpPacket(packet);" in handle_line
assert '*packetType == "adminInventoryRequest"' in handle_line
assert "HandleAdminInventoryRequestPacket(packet);" in handle_line
assert 'ReadString(a_packet, "itemId")' in inbound
assert 'QueueItemOp(op, itemId, count, *opId)' in inbound
assert 'QueueInventoryRequest(*opId)' in inbound

assert "a_itemId.size() != 8" in admin_cpp
assert "value == 0" in admin_cpp
assert "std::from_chars(a_itemId.data(), end, value, 16)" in admin_cpp
assert "kMaxGiveTakeCount = 9999" in admin_cpp
assert "kMaxSpawnCount = 10" in admin_cpp
assert "IsValidCount(*op, a_count)" in admin_cpp
assert "AddObjectToContainer(" in admin_cpp
assert "a_player.RemoveItem(removal);" in admin_cpp
assert "GetInventoryObjectCount(object)" in admin_cpp
assert "player.placeatme {:08X} {}" in admin_cpp
assert "F4T::ConsoleGuard::ExecutePluginCommand(command.c_str());" in admin_cpp
assert "ui->GetMenuOpen<RE::LoadingMenu>()" in admin_cpp
assert "inventoryList->data" in admin_cpp
assert "kInventoryItemBudget = 60 * 1024" in admin_cpp
assert "kMaxInventoryItems = 512" in admin_cpp
assert "kMaxNameLength = 64" in admin_cpp
assert "kMaxPendingRequests = 64" in queue_h
assert "return QueueResult::kBusy;" in queue_h
assert "#include \"F4TAdminItemQueue.h\"" in admin_cpp
assert "class PendingQueue" in queue_h
assert "StillCurrent(entry.connectionGeneration, a_currentConnectionGeneration, a_connected)" in queue_h
assert "return a_connected && a_entryGeneration == a_currentGeneration;" in queue_h
assert "result.admitted.push_back(std::move(entry.request));" in queue_h
assert "entries.swap(_entries);" in queue_h
assert "std::uint64_t GetConnectionGeneration();" in networking_h
assert "std::uint64_t GetConnectionGeneration()" in networking_cpp
assert "return g_connectionGeneration.load();" in networking_cpp
assert "g_pendingRequests.Enqueue(" in admin_cpp
assert "F4T::Networking::GetConnectionGeneration());" in admin_cpp
assert "PendingQueue<PendingRequest> g_pendingRequests;" in admin_cpp
assert "g_pendingMutex" not in admin_cpp
apply_body = admin_cpp[admin_cpp.index("void ApplyPendingAdminItems()"):]
assert apply_body.index("const auto connected = F4T::Networking::IsConnectedToServer();") < apply_body.index("const auto connectionGeneration = F4T::Networking::GetConnectionGeneration();")
assert "g_pendingRequests.Drain(connectionGeneration, connected)" in apply_body
assert "return StillCurrent(a_connectionGeneration, currentGeneration, connected);" in admin_cpp
assert apply_body.index("if (!IsStillCurrent(connectionGeneration)) {") < apply_body.index("ApplyRequest(request, connectionGeneration);")
assert "connection changed mid-batch; dropped remaining admin request(s)" in admin_cpp
connect_body = networking_cpp[networking_cpp.index("bool ConnectToServerInternal("):]
assert connect_body.index("StopReceiveThread();") < connect_body.index("g_connectionGeneration.fetch_add(1);") < connect_body.index("if (!ResolveAndConnectIPv4(")
assert "for (const auto& request : drained.admitted) {" in admin_cpp
assert "dropped {} stale admin request(s)" in admin_cpp
assert "RE::" not in queue_h
assert "F4TNetworking" not in queue_h
assert '"busy"' in inbound
assert '"invalid request"' in inbound
assert inbound.count("const auto connectionGeneration = F4T::Networking::GetConnectionGeneration();") == 2
assert 'SendAdminItemResult(*opId, false, 0, "busy", connectionGeneration);' in inbound
assert 'SendAdminItemResult(*opId, false, 0, "invalid request", connectionGeneration);' in inbound
assert "> kMaxNameLength" in admin_cpp
assert "SanitizeName(" in admin_cpp
assert "lead != 0x7F" in admin_cpp
assert '"?"' in admin_cpp
assert "ClampName" not in admin_cpp

server_runtime = (root / "CO-SERVER/server-cpp/src/ServerRuntime.inc").read_text(encoding="utf-8")
server_ops = set(re.findall(r'\{ "op", "(\w+)" \}', server_runtime))
client_ops = set(re.findall(r'a_op == "(\w+)"', admin_cpp))
assert server_ops == {"give_item", "take_item", "spawn_object"}, server_ops
assert client_ops == server_ops, (client_ops, server_ops)
assert "SendInventoryReport(a_opId, truncated, items, a_connectionGeneration);" in admin_cpp
assert "SendAdminItemResult(a_request.opId, false, 0, \"form not found\", a_connectionGeneration);" in admin_cpp
reply_lines = [line.strip() for line in admin_cpp.splitlines() if "F4T::Networking::SendAdminItemResult(" in line]
assert len(reply_lines) == 8 and all(line.endswith("a_connectionGeneration);") for line in reply_lines), reply_lines

for marker in ("kEnable", "constexpr bool", "if (false", "#if", "#ifdef", "NotImplemented"):
    assert marker not in admin_cpp, marker

assert "bool SendAdminItemResult(" in outbound
assert "bool SendInventoryReport(" in outbound
assert "std::string_view a_error,\n\t\tstd::uint64_t a_connectionGeneration)" in outbound
assert "std::span<const AdminInventoryEntry> a_items,\n\t\tstd::uint64_t a_connectionGeneration)" in outbound
assert outbound.count("SendPacketBytesForGeneration(packetStr, a_connectionGeneration)") == 2
assert '{ "type", "adminItemResult" }' in outbound
assert '{ "type", "inventoryReport" }' in outbound
assert '{ "itemId", std::format("{:08X}", item.itemId) }' in outbound
assert "packet.dump();" in outbound
assert "bool SendAdminItemResult(" in networking_h
assert "bool SendInventoryReport(" in networking_h
assert "std::string_view a_error,\n\t\tstd::uint64_t a_connectionGeneration);" in networking_h
assert "std::span<const AdminInventoryEntry> a_items,\n\t\tstd::uint64_t a_connectionGeneration);" in networking_h

assert "F4T::AdminItems::ApplyPendingAdminItems();" in runtime
assert runtime.index("F4T::WorldStateSync::ApplyPendingState();") < runtime.index("F4T::AdminItems::ApplyPendingAdminItems();")

assert "scripts/run-python-tests.py --group plugin" in workflow

print("F4TAdminItemsContract: all checks passed.")
