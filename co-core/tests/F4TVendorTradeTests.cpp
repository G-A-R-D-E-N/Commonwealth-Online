#include "F4TVendorTrade.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using namespace F4T::VendorTrade;
	using Json = nlohmann::json;

	constexpr std::uint32_t kFirstItem = 0x000A1234;
	constexpr std::uint32_t kSecondItem = 0x000B5678;

	Identity TestIdentity()
	{
		return Identity{ .npcId = 42, .sourceFormId = 0x1234, .cellId = 0xCAFE, .worldspaceId = 0xF00D, .authorityEpoch = 2 };
	}

	StockSnapshot Stock(std::uint32_t a_revision, std::uint32_t a_caps, std::vector<StockLine> a_lines)
	{
		StockSnapshot stock;
		stock.identity = TestIdentity();
		stock.revision = a_revision;
		stock.authorityPlayerId = 1;
		stock.caps = a_caps;
		stock.lines = std::move(a_lines);
		return stock;
	}

	TradeRequest Trade(std::uint32_t a_baseRevision, std::vector<LineDelta> a_deltas, std::int32_t a_capsDelta)
	{
		TradeRequest trade;
		trade.identity = TestIdentity();
		trade.operationId = "t-" + std::to_string(a_baseRevision);
		trade.baseRevision = a_baseRevision;
		trade.lineDeltas = std::move(a_deltas);
		trade.capsDelta = a_capsDelta;
		return trade;
	}

	StockSnapshot StockOfLines(std::size_t a_count)
	{
		std::vector<StockLine> lines;
		for (std::size_t i = 0; i < a_count; ++i) {
			lines.push_back(StockLine{ static_cast<std::uint32_t>(0x00100000 + i), 1 });
		}
		return Stock(3, 100, std::move(lines));
	}

	void TestStockLineLimitHoldsOnTrade()
	{
		const auto grown = ApplyTrade(StockOfLines(MaximumStockLines - 1), Trade(3, { LineDelta{ kFirstItem, 1 } }, 0));
		assert(grown.accepted && grown.stock->lines.size() == MaximumStockLines);

		const auto refused = ApplyTrade(StockOfLines(MaximumStockLines), Trade(3, { LineDelta{ kFirstItem, 1 } }, 0));
		assert(!refused.accepted && refused.reason == "stockLimit");
	}

	void TestPublishAndRelayRoundTrip()
	{
		const auto published = Stock(0, 100, { StockLine{ kFirstItem, 5 }, StockLine{ kSecondItem, 1 } });
		const auto parsed = ParseStockPublish(StockPublishToJson(published));
		assert(parsed && parsed->identity == published.identity && parsed->caps == 100 && parsed->lines == published.lines);

		const auto relayed = ParseStockSnapshot(StockSnapshotToJson(Stock(7, 100, published.lines)));
		assert(relayed && relayed->revision == 7 && relayed->authorityPlayerId == 1);

		Json missingRevision = StockSnapshotToJson(Stock(7, 100, published.lines));
		missingRevision.erase("revision");
		assert(!ParseStockSnapshot(missingRevision));
	}

	void TestRejectsMalformedStock()
	{
		Json zeroCount = StockPublishToJson(Stock(0, 0, { StockLine{ kFirstItem, 1 } }));
		zeroCount["items"][0]["count"] = 0;
		assert(!ParseStockPublish(zeroCount));

		assert(!ParseStockPublish(StockPublishToJson(Stock(0, 0, { StockLine{ kFirstItem, 1 }, StockLine{ kFirstItem, 2 } }))));

		Json noCell = StockPublishToJson(Stock(0, 0, {}));
		noCell.erase("cellId");
		assert(!ParseStockPublish(noCell));

		Json noNpc = StockPublishToJson(Stock(0, 0, {}));
		noNpc["npcId"] = 0;
		assert(!ParseStockPublish(noNpc));
	}

	void TestPublishAuthority()
	{
		const auto first = ApplyPublish(std::nullopt, Stock(0, 50, { StockLine{ kFirstItem, 5 } }), 1, 1);
		assert(first.accepted && first.stock && first.stock->revision == 1);

		const auto stranger = ApplyPublish(first.stock, Stock(0, 0, {}), 2, 1);
		assert(!stranger.accepted && stranger.reason == "notAuthority" && stranger.stock == first.stock);

		const auto second = ApplyPublish(first.stock, Stock(0, 60, {}), 1, 1);
		assert(second.accepted && second.stock->revision == 2 && second.stock->caps == 60 && second.stock->lines.empty());

		const auto guestSeed = ApplyPublish(std::nullopt, Stock(0, 10, {}), 2, 1);
		assert(guestSeed.accepted && guestSeed.stock->authorityPlayerId == 1);
	}

	void TestTradeOrderingAndRollbackBase()
	{
		const std::optional<StockSnapshot> current = Stock(3, 100, { StockLine{ kFirstItem, 5 } });

		const auto purchase = ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -2 } }, 50));
		assert(purchase.accepted && purchase.stock->revision == 4 && purchase.stock->caps == 150);
		assert((purchase.stock->lines == std::vector<StockLine>{ StockLine{ kFirstItem, 3 } }));

		const auto stale = ApplyTrade(purchase.stock, Trade(3, { LineDelta{ kFirstItem, -1 } }, 0));
		assert(!stale.accepted && stale.reason == "staleRevision" && stale.stock == purchase.stock);

		const auto soldOut = ApplyTrade(purchase.stock, Trade(4, { LineDelta{ kFirstItem, -3 } }, 0));
		assert(soldOut.accepted && soldOut.stock->lines.empty());

		const auto sale = ApplyTrade(current, Trade(3, { LineDelta{ kSecondItem, 1 } }, -20));
		assert(sale.accepted && sale.stock->caps == 80 && sale.stock->lines.size() == 2);

		assert(ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -6 } }, 0)).reason == "insufficientStock");
		assert(ApplyTrade(current, Trade(3, {}, -200)).reason == "insufficientCaps");
		assert(ApplyTrade(std::nullopt, Trade(3, {}, 1)).reason == "unseededVendor");
	}

	void TestTradeRequestAndResultParsing()
	{
		const auto trade = Trade(3, { LineDelta{ kFirstItem, -2 } }, 50);
		const auto parsed = ParseTradeRequest(TradeRequestToJson(trade));
		assert(parsed && parsed->lineDeltas == trade.lineDeltas && parsed->capsDelta == 50 && parsed->baseRevision == 3);

		assert(!ParseTradeRequest(TradeRequestToJson(Trade(3, {}, 0))));
		assert(!ParseTradeRequest(TradeRequestToJson(Trade(3, { LineDelta{ kFirstItem, 0 } }, 0))));
		assert(!ParseTradeRequest(TradeRequestToJson(Trade(3, { LineDelta{ kFirstItem, -1 }, LineDelta{ kFirstItem, -1 } }, 0))));

		const TradeResult accepted{ .operationId = "t-3", .outcome = Outcome{ .accepted = true, .reason = "accepted", .stock = Stock(4, 150, {}) } };
		const auto parsedAccepted = ParseTradeResult(TradeResultToJson(accepted));
		assert(parsedAccepted && parsedAccepted->outcome.accepted && parsedAccepted->outcome.stock->revision == 4);

		const TradeResult unknown{ .operationId = "t-4", .outcome = Outcome{ .reason = "unseededVendor" } };
		const auto parsedUnknown = ParseTradeResult(TradeResultToJson(unknown));
		assert(parsedUnknown && !parsedUnknown->outcome.accepted && !parsedUnknown->outcome.stock);
	}
}

int main()
{
	TestPublishAndRelayRoundTrip();
	TestRejectsMalformedStock();
	TestPublishAuthority();
	TestTradeOrderingAndRollbackBase();
	TestTradeRequestAndResultParsing();
	TestStockLineLimitHoldsOnTrade();
	return 0;
}
