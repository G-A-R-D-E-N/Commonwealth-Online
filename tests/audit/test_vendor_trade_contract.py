from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def test_server_routes_and_limits_vendor_packets():
    hit = read("CO-SERVER/server-cpp/src/ServerNpcHitImpl.inc")
    for packet in ("vendorStock", "vendorStockQuery", "vendorTrade"):
        assert f'"{packet}"' in hit
    assert "HandleVendorPacket" in hit
    assert "vendorTrade.Trade(std::string{ SessionSocialKeyView(a_session) }, a_packet)" in hit
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
        "constexpr auto kResendDelay = std::chrono::seconds{ 5 };",
        "ServiceUnresolved(reconnected);",
        "g_unresolved.find(result->operationId)",
        'SetStatus("Waiting for the trader...");',
        "busy = !g_session.pendingOperation.empty();",
        'state["canClose"] = g_session.pendingOperation.empty();',
        "constexpr std::uint32_t kMaximumSnapshotQueries = 6;",
        'constexpr std::string_view kTraderUnavailable = "The trader is unavailable.";',
        "exhausted = waiting && !due && g_session.status != kTraderUnavailable;",
        "if (exhausted) PushState();",
        "FlushPayouts();",
    ):
        assert fragment in client
    reset = client[client.index("void Reset()"):]
    assert "g_payouts" not in reset
    assert "ApplyEscrow" not in reset
    assert "F4T::TradeWindow::NoteStateChanged();" in reset
    assert client.index("void Tick()") < client.index("ServiceUnresolved(reconnected);")
    assert client.index("void Tick()") < client.index("RetrySnapshotQuery();") < client.index("FlushPayouts();")
    assert "kMaximumResends" not in client and "did not confirm" not in client
    book = read("CO-SERVER/server-cpp/src/ServerVendorTrade.cpp")
    assert '"malformed"' in book
    header = read("CO-SERVER/server-cpp/src/ServerVendorTrade.h")
    assert "kVendorOperationRetention" in header and "kMaximumVendorOperationsPerProfile = 64" in header
    assert "std::map<std::string, std::deque<Operation>> operations;" in header
    assert book.index("if (a_profileKey.empty()) {") < book.index("VT::ParseTradeRequest(a_packet)")
    assert "kVendorOperationRetention" in book


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


def main():
    for name, function in list(globals().items()):
        if name.startswith("test_") and callable(function):
            function()
    print("Vendor trade contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
