import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def private_doc_has(relative, text):
    path = ROOT / relative
    return not path.is_file() or text in path.read_text(encoding="utf-8")


def test_server_routes_and_limits_vendor_packets():
    hit = read("CO-SERVER/server-cpp/src/ServerNpcHitImpl.inc")
    for packet in ("vendorStock", "vendorStockQuery", "vendorTrade"):
        assert f'"{packet}"' in hit
    assert "HandleVendorPacket" in hit
    assert "vendorTrade.Trade(std::string{ SessionSocialKeyView(a_session) }, a_packet, [&a_session](std::uint32_t a_formId) -> std::optional<std::string> {" in hit
    assert "F4T::LoadOrder::ResolveStableFormKey(*a_session.pluginList, a_formId)" in hit
    protocol = read("CO-SERVER/server-cpp/src/ServerProtocol.cpp")
    assert "kMaximumVendorBytes" in protocol
    book = read("CO-SERVER/server-cpp/src/ServerVendorTrade.cpp")
    for reply in ("vendorStockState", "vendorStockUnseeded", "vendorStockRejected", "vendorTradeResult"):
        assert f'"{reply}"' in book
    assert "a_identity.sourceFormId" not in book and "std::to_string(a_identity.npcId)" in book


def test_plugin_receives_every_server_reply_and_ticks_the_client():
    inbound = read("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")
    assert 'packetType->rfind("vendor", 0) == 0' in inbound
    assert "F4T::VendorTradeClient::HandlePacket(a_packet);" in read("CO-BETA/plugin/src/F4TTradeClient.cpp")
    assert "F4T::VendorTradeClient::Tick();" in read("CO-BETA/plugin/src/F4TReplicationRuntime.cpp")
    assert read("CO-BETA/plugin/src/F4TNetworking.cpp").count("F4T::VendorTradeClient::Reset();") == 2


def test_plugin_sends_every_client_packet_and_escrows_before_sending():
    client = read("CO-BETA/plugin/src/F4TVendorTradeClient.cpp")
    for packet in ("vendorStockQuery", "vendorStock", "vendorTrade"):
        assert f'"{packet}"' in client
    assert client.index("for (const auto& sell : sells)") < client.index('packet["type"] = "vendorTrade";')
    assert "ApplyEscrow(*player, escrow, false);" in client
    assert "ApplyEscrow(*player, payout.escrow, payout.grant);" in client
    assert client.index("if (!SendPacket(escrow.packet))") < client.index("g_unresolved.emplace(operationId, std::move(escrow));")
    assert "HideBarterMenu();" in client
    assert "VT::ParseTradeRequest(cart)" in client
    assert "entry.value(" not in client


def test_trade_window_routes_vendor_actions_and_keeps_vendor_state():
    window = read("CO-BETA/plugin/src/F4TTradeWindow.cpp")
    assert "F4T::VendorTradeClient::HandleWindowAction(action)" in window
    assert "if (F4T::VendorTradeClient::Active()) {" in window
    assert "void PushRawState(" in window


def test_vendor_page_has_clickable_lists_and_a_cart():
    html = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/trade/index.html")
    for element in ("coVendorStock", "coVendorInventory", "coVendorCart", "coVendorTotal", "coVendorConfirm"):
        assert f'id="{element}"' in html
    assert 'src="F4TVendorWindow.js"' in html
    script = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/trade/F4TVendorWindow.js")
    for fragment in ('"vendorConfirm"', '"vendorClose"', 'addEventListener("click", onClick)', "window.coTradeSetState"):
        assert fragment in script


def test_plugin_keeps_unresolved_trades_until_the_server_answers():
    client = read("CO-BETA/plugin/src/F4TVendorTradeClient.cpp")
    for fragment in (
        "std::map<std::string, Escrow> g_unresolved;",
        "std::vector<Payout> g_payouts;",
        "ServiceUnresolved(reconnected);",
        "auto escrow = VE::TakeResolved(g_unresolved, result->operationId, context);",
        'SetStatus("Waiting for the trader...");',
        "busy = !g_session.pendingOperation.empty();",
        'state["canClose"] = g_session.pendingOperation.empty();',
        "constexpr std::uint32_t kMaximumSnapshotQueries = 6;",
        "exhausted = waiting && !due && g_session.status != kTraderUnavailable;",
        "if (exhausted) PushState();",
        "FlushPayouts();",
        'escrow.packet["resend"] = false;',
    ):
        assert fragment in client
    assert 'constexpr std::string_view kTraderUnavailable = "The trader is unavailable.";' in read("CO-BETA/plugin/include/F4TVendorTradePricing.h")
    assert 'if (a_reason == "noProfile") return std::string{ kTraderUnavailable };' in read("CO-BETA/plugin/src/F4TVendorTradePricing.cpp")
    reset = client[client.index("void Reset()"):]
    assert "g_payouts" not in reset
    assert "ApplyEscrow" not in reset
    assert "F4T::TradeWindow::NoteStateChanged();" in reset
    assert client.index("void Tick()") < client.index("ServiceUnresolved(reconnected);")
    assert client.index("void Tick()") < client.index("RetrySnapshotQuery();") < client.index("FlushPayouts();")
    assert "kMaximumResends" not in client and "did not confirm" not in client
    book = read("CO-SERVER/server-cpp/src/ServerVendorTrade.cpp")
    assert '"malformed"' in book and '"noProfile"' in book and "IsResend(a_packet)" in book
    header = read("CO-SERVER/server-cpp/src/ServerVendorTrade.h")
    assert "kVendorOperationRetention" in header and "kMaximumVendorOperationsPerProfile = 64" in header
    assert "std::map<std::string, std::deque<Operation>> operations;" in header
    assert book.index("if (a_profileKey.empty()) {") < book.index("VT::ParseTradeRequest(a_packet)")
    assert "kVendorOperationRetention" in book


def test_plugin_shows_plain_text_for_a_mispriced_refusal():
    pricing = read("CO-BETA/plugin/src/F4TVendorTradePricing.cpp")
    assert 'if (a_reason == "mispriced") return "The caps do not match the price.";' in pricing
    assert 'if (a_reason == "unknownItem") return "That item has no known value.";' in pricing


def test_plugin_does_not_requery_a_permanent_vendor_refusal():
    client = read("CO-BETA/plugin/src/F4TVendorTradeClient.cpp")
    start = client.index("void HandleRejectedPublish(")
    body = client[start:client.index("void HandleTradeResult(", start)]
    assert 'const bool transient = reason == "staleRevision" || reason == "notAuthority";' in body
    assert "g_session.status = ReasonText(reason);" in body
    assert "g_session.nextSnapshotQuery = std::chrono::steady_clock::time_point::max();" in body
    assert body.index("g_session.status = ReasonText(reason);") < body.index("if (transient) {") < body.index("RequestSnapshot();") < body.index("PushState();")
    assert "waiting = g_session.active && !g_session.snapshot.has_value() && std::chrono::steady_clock::now() >= g_session.nextSnapshotQuery;" in client


def test_item_values_load_outside_configure_workshop():
    workshop = read("CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc")
    start = workshop.index("std::size_t ConfigureWorkshop(")
    body = workshop[start:workshop.index("void ConfigureVendorItemValues(", start)]
    assert "item-values" not in body and "LoadItemValues" not in body
    assert "(void)vendorTrade.LoadItemValuesFile(a_itemValuesFile);" in workshop
    assert "_impl->ConfigureVendorItemValues(a_persistence, a_itemValuesFile, a_restockHours);" in read("CO-SERVER/server-cpp/src/ServerProtocolApi.inc")


def test_plugin_settles_only_the_trade_it_waits_for_and_keeps_completion_text():
    client = read("CO-BETA/plugin/src/F4TVendorTradeClient.cpp")
    for fragment in (
        "g_session.completed = accepted;",
        'g_session.status = accepted ? "Trade complete." : ReasonText(result->outcome.reason);',
        "if (g_session.pendingOperation.empty() && !g_session.completed) {",
        "if (g_session.active && g_session.tradeId == tradeId) {",
        "if (StockTooLarge(*chest)) {",
        "if (StockTooLarge(a_container)) {",
    ):
        assert fragment in client


def test_vendor_window_keys_cart_by_direction_and_drops_stale_lines():
    script = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/trade/F4TVendorWindow.js")
    for fragment in (
        "function cartKey(buy, ",
        '(buy ? "b:" : "s:")',
        "function prune() {",
        "prune();",
        "if (vendor && !vendor.canClose) {",
        'byId("coVendorClose").disabled = !vendor.canClose;',
        'send({ op: "vendorConfirm", tradeId: vendor.tradeId, lines: lines });',
    ):
        assert fragment in script


def test_trade_page_has_three_columns_acceptance_and_partner_name():
    html = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/trade/index.html")
    for element in (
        "coTradeHoldings",
        "coTradeOwnOffer",
        "coTradePartnerOffer",
        "coTradeOwnBadge",
        "coTradePartnerBadge",
        "coTradeAccept",
        "coTradeDecline",
        "coTradeConfirm",
        "coTradeCancel",
        "coTradeHide",
    ):
        assert f'id="{element}"' in html
    assert 'class="trade-board"' in html
    assert 'src="F4TVendorWindow.js"' in html
    script = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/trade/trade.js")
    for fragment in (
        '"offer"',
        '"confirm"',
        '"cancel"',
        '"respond"',
        '"hide"',
        "window.coTradeSetState",
        "window.handleControllerInput",
        "partnerName",
        'addEventListener("click"',
        "Accept Trade",
        "wants to trade with you",
        "Waiting for ",
        "Negotiating: add items, then press Accept Trade",
        "You have no items",
        "Nothing offered yet",
    ):
        assert fragment in script
    window = read("CO-BETA/plugin/src/F4TTradeWindow.cpp")
    assert '#include "F4TSocialNameplate.h"' in window
    assert 'state["partnerName"] = PartnerName(trade->partnerPlayerId);' in window


def test_docs_describe_the_vendor_packets():
    if not (ROOT / "CO-BETA/docs/protocol/VendorTradePackets.md").is_file():
        return
    doc = read("CO-BETA/docs/protocol/VendorTradePackets.md")
    for packet in ("vendorStockQuery", "vendorStock", "vendorTrade", "vendorStockState", "vendorTradeResult"):
        assert f"`{packet}`" in doc
    assert "VendorTradePackets.md" in read("CO-BETA/docs/protocol/packets.md")


def test_pause_menu_and_lockout_end_the_vendor_session():
    view = read("CommonwealthOnlineUI/src/F4TTradeView.cpp")
    assert '{ "op", "vendorClose" }' in view
    assert 'state.value("kind", std::string{}) == "vendor"' in view


def test_resume_button_also_ends_the_trade_deferral():
    menu = read("CommonwealthOnlineUI/src/F4TPrismaUI_Menu.cpp")
    resume = menu[menu.index("void RequestResumeFromPauseMenu()"):]
    assert 'F4T::TradeView::Close("PauseMenuHidden");' in resume.split("RequestNativeCursorRestore();")[0]


def test_cart_drops_the_opposite_direction_line():
    script = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/trade/F4TVendorWindow.js")
    assert "delete cart[cartKey(!buy," in script


def test_plugin_binds_unresolved_trades_to_their_server_and_character():
    client = read("CO-BETA/plugin/src/F4TVendorTradeClient.cpp")
    for fragment in (
        "namespace VE = F4T::VendorTradeEscrow;",
        "using VE::kResendDelay;",
        "VE::Context CurrentContext()",
        "F4T::ClientIdentity::ConnectedServerKey(), F4T::ServerProfileStore::ConnectedWorldId(), F4T::ClientIdentity::ConnectedProfileId(), F4T::ServerProfileStore::ConnectedCharacterSlot()",
        "session.context = CurrentContext();",
        "!VE::MayReconcile(g_session.context, context)",
        "escrow.context = context;",
        "VE::DueResends(g_unresolved, context, a_reconnected, std::chrono::steady_clock::now())",
        "VE::TakeDuePayouts(g_payouts, context)",
        "|| SessionInOtherContext()) {",
    ):
        assert fragment in client
    escrow = read("CO-BETA/plugin/src/F4TVendorTradeEscrow.cpp")
    assert 'packet["resend"] = true;' in escrow
    assert "return !a_current.serverKey.empty() && a_recorded == a_current;" in escrow
    header = read("CO-BETA/plugin/include/F4TVendorTradeEscrow.h")
    assert "inline constexpr auto kResendDelay = std::chrono::seconds{ 5 };" in header
    assert "src/F4TVendorTradeEscrow.cpp" in read("CO-BETA/plugin/xmake.lua")
    workflow = next(path for path in (".github/workflows/co-beta-ci.yml", ".github/public-ci/workflows/co-beta-ci.yml") if (ROOT / path).is_file())
    assert "xmake test -y CommonwealthOnlineVendorTradeEscrowTests/default" in read(workflow)


def test_plugin_retries_a_failed_send_only_after_the_delay():
    escrow = read("CO-BETA/plugin/src/F4TVendorTradeEscrow.cpp")
    assert escrow.index('packet["resend"] = true;') < escrow.index("escrow.nextResend = a_now + kResendDelay;") < escrow.index("due.emplace_back(operationId, escrow.packet);")
    client = read("CO-BETA/plugin/src/F4TVendorTradeClient.cpp")
    assert "found->second.nextResend = std::chrono::steady_clock::now() + kResendDelay;" not in client
    for fragment in (
        "(void)SendPacket(resend.second);",
        "if (!VE::MayReconcile(g_session.context, context)) {",
        "g_session.status = kTraderUnavailable;",
        "g_unresolved.size() + g_payouts.size() >= VE::kMaximumParkedEntries",
        "VE::CountParkedFor(g_unresolved, g_payouts, context) >= VE::kMaximumContextEntries",
        '"Too many trades are still waiting. Try again later."',
        "if (!VE::FitsParked(SnapshotParked(), escrow)) {",
    ):
        assert fragment in client
    assert client.index("escrow.context = context;") < client.index("VE::FitsParked(SnapshotParked(), escrow)") < client.index("if (!SendPacket(escrow.packet))")


def test_plugin_persists_parked_trades_in_the_save():
    header = read("CO-BETA/plugin/include/F4TVendorTradeSave.h")
    for fragment in (
        "inline constexpr std::uint32_t kRecordType = 'VTPK';",
        "inline constexpr std::uint32_t kRecordVersion = 1;",
    ):
        assert fragment in header
    save = read("CO-BETA/plugin/src/F4TVendorTradeSave.cpp")
    for fragment in (
        "VE::EncodeParked(F4T::VendorTradeClient::SnapshotParked())",
        "a_intfc->WriteRecord(kRecordType, kRecordVersion,",
        "a_version != kRecordVersion || a_length > VE::kMaximumParkedBytes",
        "a_intfc->ReadRecordData(text.data(), a_length) != a_length",
        "VE::DecodeParked(text)",
        "F4T::VendorTradeClient::RestoreParked(std::move(*parked));",
        "F4T::VendorTradeClient::RestoreParked(VE::Parked{});",
    ):
        assert fragment in save
    main_cpp = read("CO-BETA/plugin/src/main.cpp")
    for fragment in (
        '#include "F4TVendorTradeSave.h"',
        "F4T::VendorTradeSave::Save(a_intfc);",
        "F4T::VendorTradeSave::Clear();",
        "} else if (type == F4T::VendorTradeSave::kRecordType) {",
        "F4T::VendorTradeSave::Load(a_intfc, version, length);",
    ):
        assert fragment in main_cpp
    escrow_header = read("CO-BETA/plugin/include/F4TVendorTradeEscrow.h")
    assert "inline constexpr std::size_t kMaximumContextEntries = 64;" in escrow_header
    assert "inline constexpr std::size_t kMaximumParkedEntries = 256;" in escrow_header
    assert "inline constexpr std::size_t kMaximumParkedBytes = 1024U * 1024U;" in escrow_header


def test_item_values_table_is_shipped_and_loaded():
    table = json.loads(read("CO-SERVER/server-cpp/data/item-values.json"))
    assert len(table) > 5000
    for item_id, value in (("10", 1), ("500", 0), ("2072", 2), ("17983", 100), ("103784", 5), ("107311", 7), ("583479", 100)):
        assert table[item_id] == value, item_id
    assert "15" not in table
    assert all(type(value) is int and value >= 0 for value in table.values())
    assert "protocol.ConfigureVendorItemValues(persistence, bootstrap.vendorItemValuesPath, options.vendorRestockHours);" in read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc")
    assert '"item-values.json"' in read("CO-SERVER/server-cpp/src/ServerBootstrap.cpp")
    assert 'os.cp(path.join(os.scriptdir(), "data", "item-values.json"), target:targetdir())' in read("CO-SERVER/server-cpp/xmake.lua")
    for script in ("CO-SERVER/server-cpp/scripts/package-native-release.sh", "CO-SERVER/server-cpp/scripts/verify-native-package.sh"):
        assert "item-values.json" in read(script), script


def test_vendor_ledger_is_server_owned():
    book = read("CO-SERVER/server-cpp/src/ServerVendorTrade.cpp")
    core = read("co-core/src/npc/F4TVendorTrade.cpp")
    assert "next.caps = std::min(next.caps, MaximumSeedCaps);" in core
    assert "next.caps = std::min(next.caps, a_current->caps);" in core
    assert "line.count = held == a_current->lines.end() ? 0 : std::min(line.count, held->count);" in core
    assert "const auto itemKey = a_itemKeyOf ? a_itemKeyOf(formId) : std::nullopt;" in book
    assert "if (*held < outstanding + count) {" in book
    assert "target + count" in book and "hooks.removed(it->profileId, it->itemKey) >= it->target" in book
    assert 'outcome = VT::Outcome{ .accepted = false, .reason = "notOwned", .stock = current };' in book
    assert "a_store.SnapshotRemovals(a_profile, a_itemKey)" in book and "a_store.SetVendorLedger(a_ledger)" in book
    assert book.count("auto settled = Settle();") == 3
    workshop = read("CO-SERVER/server-cpp/src/ServerWorkshopImpl.inc")
    setup = workshop.index("void ConfigureVendorItemValues(")
    assert "vendorTrade.AttachStore(a_persistence->Store());" in workshop[setup:setup + 700]
    assert "vendorTrade.AttachStore" not in workshop[:setup] and "vendorTrade.AttachStore" not in workshop[setup + 700:]
    assert "protocol.ConfigureVendorItemValues(persistence, bootstrap.vendorItemValuesPath, options.vendorRestockHours);" in read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc")
    check = book[book.index("bool VendorTradeBook::CheckSales("):book.index("std::vector<VendorReply> VendorTradeBook::Settle()")]
    assert check.index("if (sold.empty()) {") < check.index("if (!hooks.held || !hooks.removed) {") < check.index("return false;")
    assert "return true;" not in check[check.index("if (!hooks.held || !hooks.removed) {"):check.index("if (debts.contains(a_profileKey)) {")]
    assert "kMaximumPendingVendorSalesPerProfile = 64;" in read("CO-SERVER/server-cpp/src/ServerVendorTrade.h")
    assert "profileSales + sold.size() > kMaximumPendingVendorSalesPerProfile" in book
    assert "std::function<bool(const nlohmann::json&)> persist;" in read("CO-SERVER/server-cpp/src/ServerVendorTrade.h")
    assert "if (hooks.persist && !hooks.persist(LedgerToJson())) {" in book
    assert "F4T::InventorySnapshot::kCapsItemKey" in book and "debt.target += it->capsPaid;" in book
    assert "hooks.removed(debt->first, capsKey) >= debt->second.target" in book and "debts.contains(a_profileKey)" in book
    assert '{ "debts", std::move(owed) }' in book
    hit = read("CO-SERVER/server-cpp/src/ServerNpcHitImpl.inc")
    assert "F4T::LoadOrder::ResolveStableFormKey(*a_session.pluginList, a_formId)" in hit
    store = read("CO-SERVER/server-cpp/src/DurableStateFileStoreCamps.cpp")
    start = store.index("bool DurableStateFileStore::SetVendorLedger(")
    assert "MutateAndPersistLocked(kVendorLedgerSections" in store[start:start + 400]
    assert "MutateProfilesAndPersistLocked" not in store[start:start + 400]
    assert 'kVendorLedgerSection = "vendorLedger"' in read("CO-SERVER/server-cpp/src/DurableStateSections.h")
    assert 'if (a_reason == "notOwned")' in read("CO-BETA/plugin/src/F4TVendorTradePricing.cpp")
    assert private_doc_has("CO-BETA/docs/protocol/VendorTradePackets.md", "`notOwned`")


def test_vendor_restock_timer():
    book = read("CO-SERVER/server-cpp/src/ServerVendorTrade.cpp")
    core = read("co-core/src/npc/F4TVendorTrade.cpp")
    header = read("co-core/include/F4TVendorTrade.h")
    assert "DefaultRestockHours = 24;" in header and "MinimumRestockHours = 1;" in header and "MaximumRestockHours = 720;" in header
    assert "MaximumSoldLines = 256;" in header and "next.lines.size() >= MaximumSoldLines" in core
    assert "delta.delta > 0 && after >" in core and "SeedCountLimit(value->second)" in core
    assert "static_cast<double>(restockHours) * 3600.0" in book
    assert "now - seed->second.lastRestock < restockSeconds" in book
    assert "ledger.caps = std::max(ledger.caps, seed->second.caps);" in book and "line->count < seedLine.count" in book
    assert "line->count = seedLine.count;" in book and "seed->second.lastRestock = now;" in book
    assert '{ "lastRestock", seed.lastRestock }' in book and '{ "seeds", std::move(seedList) }' in book
    assert "vendor_restock_hours" in read("co-core/src/config/F4TServerConfiguration.cpp")
    assert "std::uint32_t vendorRestockHours{ 24 };" in read("co-core/include/F4TServerConfiguration.h")
    assert json.loads(read("CO-SERVER/server-cpp/commonwealth-server-native.json"))["vendor_restock_hours"] == 24
    assert "vendor_restock_hours" in read("CO-SERVER/server-cpp/README.md")
    assert private_doc_has("wiki/Server-Configuration.md", "vendor_restock_hours")
    assert private_doc_has("CO-BETA/docs/protocol/VendorTradePackets.md", "vendor_restock_hours")


def main():
    for name, function in list(globals().items()):
        if name.startswith("test_") and callable(function):
            function()
    print("Vendor trade contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
