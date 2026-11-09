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
	constexpr std::uint32_t kUnlistedItem = 0x000C0001;
	constexpr std::uint32_t kUnstockedItem = 0x000D0001;
	constexpr std::uint32_t kCapsItem = 0x0000000F;

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

	ItemValues Values()
	{
		return ItemValues{ { kFirstItem, 25 }, { kSecondItem, 20 } };
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
		const auto values = Values();
		const auto grown = ApplyTrade(StockOfLines(MaximumSoldLines - 1), Trade(3, { LineDelta{ kFirstItem, 1 } }, -12), values);
		assert(grown.accepted && grown.stock->lines.size() == MaximumSoldLines);

		const auto refused = ApplyTrade(StockOfLines(MaximumSoldLines), Trade(3, { LineDelta{ kFirstItem, 1 } }, -12), values);
		assert(!refused.accepted && refused.reason == "stockLimit");

		const auto existing = ApplyTrade(Stock(3, 100, { StockLine{ kFirstItem, 1 } }), Trade(3, { LineDelta{ kFirstItem, 1 } }, -12), values);
		assert(existing.accepted);
	}

	void TestSoldStockIsBoundedPerLine()
	{
		const auto values = Values();
		const auto limit = SeedCountLimit(25);
		const auto full = ApplyTrade(Stock(3, 100000, { StockLine{ kFirstItem, limit - 1 } }), Trade(3, { LineDelta{ kFirstItem, 1 } }, -12), values);
		assert(full.accepted && full.stock->lines[0].count == limit);

		const auto over = ApplyTrade(full.stock, Trade(4, { LineDelta{ kFirstItem, 1 } }, -12), values);
		assert(!over.accepted && over.reason == "stockLimit");

		const auto bought = ApplyTrade(Stock(3, 100000, { StockLine{ kFirstItem, limit + 5 } }), Trade(3, { LineDelta{ kFirstItem, -1 } }, 25), values);
		assert(bought.accepted);
	}

	void TestRestockHoursAreClamped()
	{
		assert(DefaultRestockHours == 24);
		assert(ClampRestockHours(-5) == 1 && ClampRestockHours(0) == 1 && ClampRestockHours(24) == 24 && ClampRestockHours(720) == 720 && ClampRestockHours(721) == 720 && ClampRestockHours(9000000000LL) == 720);
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
		const auto first = ApplyPublish(std::nullopt, Stock(0, 50, { StockLine{ kFirstItem, 5 } }), 1, 1, Values());
		assert(first.accepted && first.stock && first.stock->revision == 1);

		const auto stranger = ApplyPublish(first.stock, Stock(0, 0, {}), 2, 1, Values());
		assert(!stranger.accepted && stranger.reason == "notAuthority" && stranger.stock == first.stock);

		const auto second = ApplyPublish(first.stock, Stock(0, 60, {}), 1, 1, Values());
		assert(second.accepted && second.stock->revision == 2 && second.stock->caps == 50 && second.stock->lines.empty());

		const auto guestSeed = ApplyPublish(std::nullopt, Stock(0, 10, {}), 2, 1, Values());
		assert(guestSeed.accepted && guestSeed.stock->authorityPlayerId == 1);
	}

	void TestPublishDropsUnknownItems()
	{
		const auto values = Values();
		const auto unlisted = ApplyPublish(std::nullopt, Stock(0, 50, { StockLine{ kUnlistedItem, 5 } }), 1, 1, values);
		assert(unlisted.accepted && unlisted.reason == "accepted" && unlisted.stock->revision == 1 && unlisted.stock->caps == 50 && unlisted.stock->lines.empty());

		const auto mixed = ApplyPublish(std::nullopt, Stock(0, 50, { StockLine{ kFirstItem, 5 }, StockLine{ kUnlistedItem, 1 } }), 1, 1, values);
		assert(mixed.accepted && (mixed.stock->lines == std::vector<StockLine>{ StockLine{ kFirstItem, 5 } }));

		assert(ApplyPublish(std::nullopt, Stock(0, 50, { StockLine{ kFirstItem, 5 } }), 1, 1, ItemValues{}).stock->lines.empty());

		const auto reseed = ApplyPublish(mixed.stock, Stock(0, 60, { StockLine{ kUnlistedItem, 2 } }), 1, 1, values);
		assert(reseed.accepted && reseed.stock->revision == 2 && reseed.stock->caps == 50 && (reseed.stock->lines == std::vector<StockLine>{}));
	}

	void TestSeedIsBoundedAndLaterPublishesOnlyLower()
	{
		const auto values = Values();
		assert(SeedCountLimit(0) == MaximumSeedCount && SeedCountLimit(1) == MaximumSeedCount);
		assert(SeedCountLimit(100) == 1000 && SeedCountLimit(1000) == 100 && SeedCountLimit(4000) == 25 && SeedCountLimit(4294967295U) == 1);

		const auto seeded = ApplyPublish(std::nullopt, Stock(0, MaximumCaps, { StockLine{ kFirstItem, 65535 }, StockLine{ kSecondItem, 3 } }), 1, 1, values);
		assert(seeded.accepted && seeded.stock->caps == MaximumSeedCaps);
		assert((seeded.stock->lines == std::vector<StockLine>{ StockLine{ kFirstItem, SeedCountLimit(25) }, StockLine{ kSecondItem, 3 } }));

		const auto raised = ApplyPublish(seeded.stock, Stock(0, MaximumSeedCaps + 1, { StockLine{ kFirstItem, 65535 }, StockLine{ kSecondItem, 4 }, StockLine{ kUnstockedItem, 1 } }), 1, 1, values);
		assert(raised.accepted && raised.stock->revision == 2 && raised.stock->caps == MaximumSeedCaps);
		assert(raised.stock->lines == seeded.stock->lines);

		const auto lowered = ApplyPublish(seeded.stock, Stock(0, 10, { StockLine{ kFirstItem, 7 } }), 1, 1, values);
		assert(lowered.accepted && lowered.stock->caps == 10 && (lowered.stock->lines == std::vector<StockLine>{ StockLine{ kFirstItem, 7 } }));
	}

	void TestTradeOrderingAndRollbackBase()
	{
		const auto values = Values();
		const std::optional<StockSnapshot> current = Stock(3, 100, { StockLine{ kFirstItem, 5 } });

		const auto purchase = ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -2 } }, 50), values);
		assert(purchase.accepted && purchase.stock->revision == 4 && purchase.stock->caps == 150);
		assert((purchase.stock->lines == std::vector<StockLine>{ StockLine{ kFirstItem, 3 } }));

		const auto stale = ApplyTrade(purchase.stock, Trade(3, { LineDelta{ kFirstItem, -1 } }, 25), values);
		assert(!stale.accepted && stale.reason == "staleRevision" && stale.stock == purchase.stock);

		const auto soldOut = ApplyTrade(purchase.stock, Trade(4, { LineDelta{ kFirstItem, -3 } }, 75), values);
		assert(soldOut.accepted && soldOut.stock->lines.empty());

		const auto sale = ApplyTrade(current, Trade(3, { LineDelta{ kSecondItem, 1 } }, -10), values);
		assert(sale.accepted && sale.stock->caps == 90 && sale.stock->lines.size() == 2);

		assert(ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -6 } }, 0), values).reason == "insufficientStock");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kSecondItem, 20 } }, -200), values).reason == "insufficientCaps");
		assert(ApplyTrade(std::nullopt, Trade(3, {}, 1), values).reason == "unseededVendor");
	}

	void TestPricedPurchasesAndSalesAreChecked()
	{
		const auto values = Values();
		const std::optional<StockSnapshot> current = Stock(3, 100, { StockLine{ kFirstItem, 5 } });

		assert(ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -2 } }, 49), values).reason == "mispriced");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -2 } }, 51), values).reason == "mispriced");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -2 } }, 0), values).reason == "mispriced");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kSecondItem, 1 } }, -9), values).reason == "mispriced");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kSecondItem, 1 } }, -11), values).reason == "mispriced");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kSecondItem, 1 } }, 0), values).reason == "mispriced");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -2 } }, 50), values).accepted);
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kSecondItem, 1 } }, -10), values).accepted);
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kSecondItem, -1 } }, 1), values).reason == "insufficientStock");

		Json overCount = StockPublishToJson(Stock(0, 0, { StockLine{ kFirstItem, 1 } }));
		overCount["items"][0]["count"] = MaximumCount + 1;
		assert(!ParseStockPublish(overCount));
		assert(!ParseStockPublish(StockPublishToJson(Stock(0, MaximumCaps + 1, {}))));
	}

	void TestItemValueTableParsing()
	{
		const auto parsed = ParseItemValues(Json{ { "10", 1 }, { "16779393", 0 } });
		assert(parsed && parsed->size() == 2 && parsed->at(10) == 1 && parsed->at(16779393) == 0);

		assert(!ParseItemValues(Json::object()));
		assert(!ParseItemValues(Json::array()));
		assert(!ParseItemValues(Json{ { "10", -1 } }));
		assert(!ParseItemValues(Json{ { "10", 1.5 } }));
		assert(!ParseItemValues(Json{ { "10", "5" } }));
		assert(!ParseItemValues(Json{ { "10", 4294967296 } }));
		assert(!ParseItemValues(Json{ { "x10", 1 } }));
		assert(!ParseItemValues(Json{ { "0", 1 } }));
		assert(!ParseItemValues(Json{ { "4294967296", 1 } }));
		assert(!ParseItemValues(Json{ { "010", 1 }, { "10", 2 } }));
	}

	void TestPricesMatchTheClientFormula()
	{
		assert(BuyPrice(0) == 1 && SellPrice(0) == 1);
		assert(BuyPrice(1) == 1 && SellPrice(1) == 1);
		assert(BuyPrice(2) == 2 && SellPrice(2) == 1);
		assert(BuyPrice(25) == 25 && SellPrice(25) == 12);
		assert(BuyPrice(100) == 100 && SellPrice(100) == 50);
	}

	void TestMixedCartIsPricedPerLine()
	{
		const auto values = Values();
		const std::optional<StockSnapshot> current = Stock(3, 100, { StockLine{ kFirstItem, 5 } });
		const std::vector<LineDelta> cart{ LineDelta{ kFirstItem, -2 }, LineDelta{ kSecondItem, 1 } };

		const auto accepted = ApplyTrade(current, Trade(3, cart, 40), values);
		assert(accepted.accepted && accepted.stock->caps == 140);
		assert((accepted.stock->lines == std::vector<StockLine>{ StockLine{ kFirstItem, 3 }, StockLine{ kSecondItem, 1 } }));

		assert(ApplyTrade(current, Trade(3, cart, 50), values).reason == "mispriced");
		assert(ApplyTrade(current, Trade(3, cart, -10), values).reason == "mispriced");
		assert(ApplyTrade(current, Trade(3, cart, 39), values).reason == "mispriced");
	}

	void TestUnknownItemsRefused()
	{
		const auto values = Values();
		const std::optional<StockSnapshot> current = Stock(3, 100, { StockLine{ kFirstItem, 5 }, StockLine{ kUnlistedItem, 5 } });

		assert(ApplyTrade(current, Trade(3, { LineDelta{ kUnlistedItem, -1 } }, 10), values).reason == "unknownItem");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kCapsItem, 1 } }, -10), values).reason == "unknownItem");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kFirstItem, -1 } }, 25), ItemValues{}).reason == "unknownItem");
		assert(ApplyTrade(current, Trade(3, { LineDelta{ kUnstockedItem, -1 } }, 10), values).reason == "insufficientStock");
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
	TestPublishDropsUnknownItems();
	TestSeedIsBoundedAndLaterPublishesOnlyLower();
	TestTradeOrderingAndRollbackBase();
	TestPricedPurchasesAndSalesAreChecked();
	TestItemValueTableParsing();
	TestPricesMatchTheClientFormula();
	TestMixedCartIsPricedPerLine();
	TestUnknownItemsRefused();
	TestTradeRequestAndResultParsing();
	TestStockLineLimitHoldsOnTrade();
	TestSoldStockIsBoundedPerLine();
	TestRestockHoursAreClamped();
	return 0;
}
