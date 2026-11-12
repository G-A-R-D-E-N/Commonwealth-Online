#include "ServerVendorTrade.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::VendorTradeBook;
	namespace VT = F4T::VendorTrade;

	Json Publish(std::uint32_t a_caps, std::uint32_t a_count)
	{
		return Json{
			{ "npcId", 42 }, { "sourceFormId", 7 }, { "cellId", 9 }, { "worldspaceId", 3 }, { "authorityEpoch", 1 },
			{ "caps", a_caps }, { "items", Json::array({ Json{ { "formId", 100 }, { "count", a_count } } }) }
		};
	}

	Json Query()
	{
		return Json{ { "npcId", 42 }, { "sourceFormId", 7 }, { "cellId", 9 }, { "worldspaceId", 3 }, { "authorityEpoch", 1 } };
	}

	Json Trade(const std::string& a_op, std::uint32_t a_base, int a_delta, int a_caps)
	{
		Json packet = Query();
		packet["operationId"] = a_op;
		packet["baseRevision"] = a_base;
		packet["capsDelta"] = a_caps;
		packet["deltas"] = Json::array({ Json{ { "formId", 100 }, { "delta", a_delta } } });
		return packet;
	}

	VendorTradeBook PricedBook()
	{
		VendorTradeBook priced;
		assert(priced.LoadItemValues(Json{ { "100", 10 }, { "200", 7 } }));
		return priced;
	}

	Json Priced(const std::string& a_op, std::uint32_t a_base, std::vector<VT::LineDelta> a_lines, int a_caps)
	{
		auto request = VT::ParseTradeRequest(Trade(a_op, a_base, -1, a_caps));
		request->lineDeltas = std::move(a_lines);
		return VT::TradeRequestToJson(*request);
	}

	void TestSeedQueryAndTrade()
	{
		VendorTradeBook book = PricedBook();
		const auto alwaysActive = [](std::uint32_t) { return true; };

		const auto unseeded = book.Query(Query());
		assert(unseeded.size() == 1 && unseeded[0].packet["type"] == "vendorStockUnseeded");

		const auto seeded = book.Publish(1, Publish(100, 5), alwaysActive);
		assert(seeded.size() == 1 && seeded[0].broadcast && seeded[0].packet["type"] == "vendorStockState" && seeded[0].packet["revision"] == 1);

		const auto known = book.Query(Query());
		assert(known[0].packet["type"] == "vendorStockState" && known[0].packet["caps"] == 100);

		const auto buy = book.Trade("buyer", Trade("a", 1, -2, 20));
		assert(buy.size() == 2 && buy[0].packet["accepted"] == true && !buy[0].broadcast);
		assert(buy[1].broadcast && buy[1].packet["revision"] == 2 && buy[1].packet["caps"] == 120);

		const auto replay = book.Trade("buyer", Trade("a", 1, -2, 20));
		assert(replay.size() == 1 && replay[0].packet["accepted"] == true);
		assert(book.Query(Query())[0].packet["revision"] == 2);

		const auto stale = book.Trade("other", Trade("b", 1, -1, 0));
		assert(stale.size() == 1 && stale[0].packet["accepted"] == false && stale[0].packet["reason"] == "staleRevision");

		const auto tooMany = book.Trade("other", Trade("c", 2, -4, 0));
		assert(tooMany[0].packet["reason"] == "insufficientStock");

		auto malformed = Trade("m", 1, -1, 0);
		malformed["deltas"].push_back(malformed["deltas"].front());
		const auto refused = book.Trade("other", malformed);
		assert(refused.size() == 1 && refused[0].packet["operationId"] == "m" && refused[0].packet["accepted"] == false && refused[0].packet["reason"] == "malformed");
	}

	void TestAuthorityTakeover()
	{
		VendorTradeBook book = PricedBook();
		assert(book.Publish(1, Publish(10, 1), [](std::uint32_t) { return true; }).front().broadcast);
		const auto stranger = book.Publish(2, Publish(5, 1), [](std::uint32_t) { return true; });
		assert(stranger.size() == 1 && stranger.front().packet["type"] == "vendorStockRejected" && stranger.front().packet["reason"] == "notAuthority");
		const auto takeover = book.Publish(2, Publish(20, 2), [](std::uint32_t) { return false; });
		assert(takeover.front().packet["type"] == "vendorStockState" && takeover.front().packet["authorityPlayerId"] == 2);
	}

	void TestReferencesSharingABaseFormKeepSeparateStock()
	{
		VendorTradeBook book = PricedBook();
		const auto alwaysActive = [](std::uint32_t) { return true; };
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);
		Json other = Publish(50, 2);
		other["npcId"] = 43;
		assert(book.Publish(1, other, alwaysActive).front().packet["revision"] == 1);
		Json otherQuery = Query();
		otherQuery["npcId"] = 43;
		assert(book.Query(otherQuery)[0].packet["caps"] == 50);
		Json otherTrade = Trade("x", 1, -1, 10);
		otherTrade["npcId"] = 43;
		assert(book.Trade("buyer", otherTrade)[0].packet["accepted"] == true);
		assert(book.Query(Query())[0].packet["revision"] == 1 && book.Query(Query())[0].packet["caps"] == 100);
	}

	void TestReplayIsKeyedByProfile()
	{
		VendorTradeBook book = PricedBook();
		const auto alwaysActive = [](std::uint32_t) { return true; };
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);
		assert(book.Trade("buyer", Trade("a", 1, -2, 20)).front().packet["accepted"] == true);

		const auto otherProfile = book.Trade("other", Trade("a", 1, -2, 20));
		assert(otherProfile.size() == 1 && otherProfile[0].packet["accepted"] == false && otherProfile[0].packet["reason"] == "staleRevision");

		const auto keyless = book.Trade("", Trade("e", 2, -1, 0));
		assert(keyless.size() == 1 && keyless[0].packet["reason"] == "noProfile");
	}

	void TestPricesAndTamperedSeedOverTheBook()
	{
		VendorTradeBook book = PricedBook();
		const auto alwaysActive = [](std::uint32_t) { return true; };
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);

		const auto zeroCaps = book.Trade("buyer", Trade("z", 1, -1, 0));
		assert(zeroCaps.size() == 1 && zeroCaps[0].packet["accepted"] == false && zeroCaps[0].packet["reason"] == "mispriced");

		const auto underpriced = book.Trade("buyer", Trade("y", 1, -2, 1));
		assert(underpriced.size() == 1 && underpriced[0].packet["reason"] == "mispriced");

		VendorTradeBook unstocked;
		Json noItems = Publish(100, 5);
		noItems["items"] = Json::array();
		assert(unstocked.Publish(1, noItems, alwaysActive).front().broadcast);
		assert(unstocked.Trade("buyer", Trade("u", 1, -1, 1))[0].packet["reason"] == "insufficientStock");

		assert(book.Query(Query())[0].packet["revision"] == 1 && book.Query(Query())[0].packet["caps"] == 100);

		VendorTradeBook fresh;
		assert(fresh.Publish(1, Publish(100, 65536), alwaysActive).empty() && fresh.Size() == 0);
	}

	struct Inventory
	{
		std::map<std::string, std::uint32_t> held;
		std::map<std::string, std::uint64_t> removed;
		double now = 1000.0;
	};

	VendorTradeBook LedgerBook(Inventory& a_inventory, Json* a_saved = nullptr)
	{
		VendorTradeBook book = PricedBook();
		CommonwealthOnline::Server::VendorLedgerHooks hooks;
		hooks.persist = [a_saved](const Json& a_ledger) {
			if (a_saved != nullptr) {
				*a_saved = a_ledger;
			}
			return true;
		};
		hooks.held = [&a_inventory](const std::string& a_profile, const std::string& a_key) -> std::optional<std::uint32_t> {
			const auto found = a_inventory.held.find(a_profile + "|" + a_key);
			if (found == a_inventory.held.end()) {
				return std::nullopt;
			}
			return found->second;
		};
		hooks.removed = [&a_inventory](const std::string& a_profile, const std::string& a_key) { return a_inventory.removed[a_profile + "|" + a_key]; };
		hooks.now = [&a_inventory] { return a_inventory.now; };
		book.SetHooks(std::move(hooks));
		return book;
	}

	std::optional<std::string> KeyOf(std::uint32_t a_formId)
	{
		return "k" + std::to_string(a_formId);
	}

	void TestSalesMixedCartsAndUnknownItemsArePriced()
	{
		Inventory inventory;
		inventory.held["seller|k200"] = 5;
		VendorTradeBook book = LedgerBook(inventory);
		const auto alwaysActive = [](std::uint32_t) { return true; };
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);

		assert(book.Trade("seller", Priced("s1", 1, { VT::LineDelta{ 200, 1 } }, -4), KeyOf).front().packet["reason"] == "mispriced");
		assert(book.Trade("seller", Priced("s2", 1, { VT::LineDelta{ 300, 1 } }, -1), KeyOf).front().packet["reason"] == "unknownItem");
		assert(book.Trade("seller", Priced("s3", 1, { VT::LineDelta{ 200, 1 } }, -3), KeyOf).front().packet["accepted"] == true);
		assert(book.Trade("seller", Priced("m1", 2, { VT::LineDelta{ 100, -2 }, VT::LineDelta{ 200, 1 } }, 20), KeyOf).front().packet["reason"] == "mispriced");
		const auto mixed = book.Trade("seller", Priced("m2", 2, { VT::LineDelta{ 100, -2 }, VT::LineDelta{ 200, 1 } }, 17), KeyOf);
		assert(mixed.front().packet["accepted"] == true && book.Query(Query())[0].packet["caps"] == 114);

		VendorTradeBook invalid = PricedBook();
		assert(invalid.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);
		assert(!invalid.LoadItemValues(Json{ { "100", -1 } }));
		assert(invalid.Trade("buyer", Trade("t", 1, -1, 10))[0].packet["reason"] == "unknownItem");
	}

	void TestKeylessTradeRefusesFirstSendAndStaysSilentOnResend()
	{
		VendorTradeBook book = PricedBook();
		const auto alwaysActive = [](std::uint32_t) { return true; };
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);

		const auto refused = book.Trade("", Trade("e", 1, -2, 30));
		assert(refused.size() == 1 && refused[0].packet["operationId"] == "e" && refused[0].packet["accepted"] == false && refused[0].packet["reason"] == "noProfile");
		auto resend = Trade("e", 1, -2, 30);
		resend["resend"] = true;
		assert(book.Trade("", resend).empty());
		auto malformed = Trade("m", 1, -1, 0);
		malformed["deltas"].push_back(malformed["deltas"].front());
		const auto refusedMalformed = book.Trade("", malformed);
		assert(refusedMalformed.size() == 1 && refusedMalformed[0].packet["reason"] == "noProfile");
		malformed["resend"] = true;
		assert(book.Trade("", malformed).empty());
		assert(book.Query(Query())[0].packet["revision"] == 1 && book.Query(Query())[0].packet["caps"] == 100);

		const auto accepted = book.Trade("buyer", Trade("e", 1, -2, 20));
		assert(accepted.front().packet["accepted"] == true && accepted.front().packet["operationId"] == "e");
	}

	void TestItemValuesLoadWithoutZones()
	{
		const auto directory = std::filesystem::temp_directory_path() / "co-vendor-item-values-tests";
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory);
		{
			std::ofstream output(directory / "item-values.json");
			output << R"({ "100": 10 })";
		}
		assert(!std::filesystem::exists(directory / "workshop-zones.json"));
		VendorTradeBook book;
		assert(book.LoadItemValuesFile(directory / "item-values.json"));
		assert(book.Publish(1, Publish(100, 5), [](std::uint32_t) { return true; }).front().broadcast);
		std::filesystem::remove_all(directory);
	}

	void TestUnknownSeedDropsUnlistedLines()
	{
		VendorTradeBook book = PricedBook();
		const auto alwaysActive = [](std::uint32_t) { return true; };
		Json unknown = Publish(100, 5);
		unknown["items"].push_back(Json{ { "formId", 300 }, { "count", 1 } });
		const auto seeded = book.Publish(1, unknown, alwaysActive);
		assert(seeded.size() == 1 && seeded.front().broadcast && seeded.front().packet["type"] == "vendorStockState");
		assert(seeded.front().packet["items"] == Json::array({ Json{ { "formId", 100 }, { "count", 5 } } }));
		assert(book.Size() == 1);
		assert(book.Trade("seller", Priced("u1", 1, { VT::LineDelta{ 300, 1 } }, -1)).front().packet["reason"] == "unknownItem");
		assert(book.Trade("buyer", Priced("u2", 1, { VT::LineDelta{ 300, -1 } }, 10)).front().packet["reason"] == "insufficientStock");
	}

	void TestSeedIsClampedAndLaterPublishesCannotRaise()
	{
		VendorTradeBook book = PricedBook();
		const auto alwaysActive = [](std::uint32_t) { return true; };
		const auto seeded = book.Publish(1, Publish(VT::MaximumCaps, 60000), alwaysActive);
		assert(seeded.front().packet["caps"] == VT::MaximumSeedCaps);
		assert(seeded.front().packet["items"] == Json::array({ Json{ { "formId", 100 }, { "count", VT::MaximumSeedCount } } }));

		const auto raised = book.Publish(1, Publish(VT::MaximumCaps, 60000), alwaysActive);
		assert(raised.front().packet["caps"] == VT::MaximumSeedCaps && raised.front().packet["revision"] == 2);
		assert(raised.front().packet["items"] == seeded.front().packet["items"]);

		const auto lowered = book.Publish(1, Publish(10, 2), alwaysActive);
		assert(lowered.front().packet["caps"] == 10 && lowered.front().packet["items"] == Json::array({ Json{ { "formId", 100 }, { "count", 2 } } }));
		const auto grown = book.Publish(1, Publish(500, 50), alwaysActive);
		assert(grown.front().packet["caps"] == 10 && grown.front().packet["items"] == lowered.front().packet["items"]);
	}

	void TestSaleNeedsOwnershipAndARemoval()
	{
		Inventory inventory;
		VendorTradeBook book = LedgerBook(inventory);
		assert(book.Publish(1, Publish(100, 5), [](std::uint32_t) { return true; }).front().broadcast);

		const auto phantom = book.Trade("seller", Priced("s1", 1, { VT::LineDelta{ 200, 1 } }, -3), KeyOf);
		assert(phantom.size() == 1 && phantom[0].packet["reason"] == "notOwned" && book.PendingSales() == 0);
		assert(book.Query(Query())[0].packet["revision"] == 1);
		assert(book.Trade("seller", Priced("s0", 1, { VT::LineDelta{ 200, 1 } }, -3))[0].packet["reason"] == "notOwned");

		inventory.held["seller|k200"] = 1;
		const auto sold = book.Trade("seller", Priced("s2", 1, { VT::LineDelta{ 200, 1 } }, -3), KeyOf);
		assert(sold.size() == 2 && sold[0].packet["accepted"] == true && book.PendingSales() == 1);
		assert(book.Trade("seller", Priced("s3", 2, { VT::LineDelta{ 200, 1 } }, -3), KeyOf)[0].packet["reason"] == "notOwned");

		inventory.removed["seller|k200"] = 1;
		assert(book.Query(Query()).size() == 1 && book.PendingSales() == 0);

		inventory.held["seller|k200"] = 5;
		assert(book.Trade("seller", Priced("s5", 2, { VT::LineDelta{ 200, 1 } }, -3), KeyOf)[0].packet["accepted"] == true);
		assert(book.PendingSales() == 1 && book.Query(Query())[0].packet["caps"] == 94);
		inventory.now += CommonwealthOnline::Server::kSaleConfirmSeconds + 1.0;
		const auto reversed = book.Query(Query());
		assert(reversed.size() == 2 && reversed[0].broadcast && reversed[0].packet["revision"] == 4 && reversed[0].packet["caps"] == 97);
		assert(reversed[0].packet["items"] == Json::array({ Json{ { "formId", 100 }, { "count", 5 } }, Json{ { "formId", 200 }, { "count", 1 } } }) && book.PendingSales() == 0);
	}

	void TestLedgerSurvivesRestart()
	{
		Inventory inventory;
		Json saved;
		VendorTradeBook book = LedgerBook(inventory);
		CommonwealthOnline::Server::VendorLedgerHooks hooks;
		hooks.held = [](const std::string&, const std::string&) { return std::optional<std::uint32_t>{ 3 }; };
		hooks.removed = [](const std::string&, const std::string&) { return std::uint64_t{}; };
		hooks.persist = [&saved](const Json& a_ledger) { saved = a_ledger; return true; };
		hooks.now = [&inventory] { return inventory.now; };
		book.SetHooks(std::move(hooks));
		assert(book.Publish(1, Publish(100, 5), [](std::uint32_t) { return true; }).front().broadcast);
		assert(book.Trade("seller", Priced("p1", 1, { VT::LineDelta{ 200, 1 } }, -3), KeyOf)[0].packet["accepted"] == true);

		VendorTradeBook restored = PricedBook();
		CommonwealthOnline::Server::VendorLedgerHooks restoredHooks;
		restoredHooks.now = [&inventory] { return inventory.now; };
		restored.SetHooks(std::move(restoredHooks));
		assert(!restored.LoadLedger(Json::object()));
		assert(restored.LoadLedger(saved) && restored.Size() == 1 && restored.PendingSales() == 1 && restored.LedgerToJson() == saved);
		const auto state = restored.Query(Query());
		assert(state[0].packet["revision"] == 2 && state[0].packet["caps"] == 97);
	}

	constexpr double kDay = 86400.0;

	void TestRestockRaisesOnlyAndKeepsPlayerSoldStock()
	{
		Inventory inventory;
		VendorTradeBook book = LedgerBook(inventory);
		assert(book.RestockHours() == 24);
		assert(book.LoadItemValues(Json{ { "100", 10 }, { "200", 7 }, { "300", 4 } }));
		Json seed = Publish(100, 5);
		seed["items"].push_back(Json{ { "formId", 200 }, { "count", 4 } });
		assert(book.Publish(1, seed, [](std::uint32_t) { return true; }).front().broadcast);

		assert(book.Trade("buyer", Trade("b1", 1, -5, 50), KeyOf).front().packet["accepted"] == true);
		inventory.held["seller|k200"] = 3;
		assert(book.Trade("seller", Priced("s1", 2, { VT::LineDelta{ 200, 3 } }, -9), KeyOf).front().packet["accepted"] == true);
		inventory.removed["seller|k200"] = 3;
		inventory.held["seller|k300"] = 40;
		assert(book.Trade("seller", Priced("s2", 3, { VT::LineDelta{ 300, 40 } }, -80), KeyOf).front().packet["accepted"] == true);
		inventory.removed["seller|k300"] = 40;

		inventory.now += kDay - 1.0;
		const auto early = book.Query(Query());
		assert(early.size() == 1 && early[0].packet["revision"] == 4 && early[0].packet["caps"] == 61);

		inventory.now += 1.0;
		const auto restocked = book.Query(Query());
		assert(restocked.size() == 2 && restocked[0].broadcast && restocked[0].packet["revision"] == 5 && restocked[0].packet["caps"] == 100);
		assert(restocked[0].packet["items"] == Json::array({ Json{ { "formId", 100 }, { "count", 5 } }, Json{ { "formId", 200 }, { "count", 7 } }, Json{ { "formId", 300 }, { "count", 40 } } }));

		inventory.now += kDay;
		const auto unchanged = book.Query(Query());
		assert(unchanged.size() == 1 && unchanged[0].packet["revision"] == 5 && unchanged[0].packet["items"] == restocked[0].packet["items"]);
	}

	void TestLastRestockSurvivesARestart()
	{
		Inventory inventory;
		Json saved;
		VendorTradeBook book = LedgerBook(inventory, &saved);
		book.SetRestockHours(0);
		assert(book.RestockHours() == 1);
		book.SetRestockHours(5000);
		assert(book.RestockHours() == 720);
		book.SetRestockHours(1);
		assert(book.Publish(1, Publish(100, 5), [](std::uint32_t) { return true; }).front().broadcast);
		assert(book.Trade("buyer", Trade("b1", 1, -5, 50)).front().packet["accepted"] == true);
		assert(saved["seeds"].size() == 1 && saved["seeds"][0]["lastRestock"] == 1000.0);

		VendorTradeBook restored = LedgerBook(inventory);
		restored.SetRestockHours(1);
		assert(restored.LoadLedger(saved) && restored.LedgerToJson() == saved);
		inventory.now += 3599.0;
		const auto early = restored.Query(Query());
		assert(early.size() == 1 && early[0].packet["revision"] == 2 && early[0].packet["items"].empty());
		inventory.now += 1.0;
		const auto restocked = restored.Query(Query());
		assert(restocked.size() == 2 && restocked[0].packet["revision"] == 3 && restocked[0].packet["caps"] == 150);
		assert(restocked[0].packet["items"] == Json::array({ Json{ { "formId", 100 }, { "count", 5 } } }));

		Json oldLedger = saved;
		oldLedger.erase("seeds");
		VendorTradeBook upgraded = LedgerBook(inventory);
		upgraded.SetRestockHours(1);
		assert(upgraded.LoadLedger(oldLedger));
		inventory.now += 3599.0;
		assert(upgraded.Query(Query()).size() == 1);
		inventory.now += 1.0;
		assert(upgraded.Query(Query()).size() == 1 && upgraded.Query(Query())[0].packet["items"].empty());
	}

	void TestPendingSaleReversesCorrectlyAfterARestock()
	{
		Inventory inventory;
		VendorTradeBook book = LedgerBook(inventory);
		book.SetRestockHours(1);
		assert(book.Publish(1, Publish(100, 5), [](std::uint32_t) { return true; }).front().broadcast);

		inventory.now += 3590.0;
		inventory.held["seller|k200"] = 20;
		assert(book.Trade("seller", Priced("s1", 1, { VT::LineDelta{ 200, 20 } }, -60), KeyOf).front().packet["accepted"] == true);
		assert(book.PendingSales() == 1);

		inventory.now += 10.0;
		const auto restocked = book.Query(Query());
		assert(restocked.size() == 2 && restocked[0].packet["revision"] == 3 && restocked[0].packet["caps"] == 100 && book.PendingSales() == 1);

		inventory.now += CommonwealthOnline::Server::kSaleConfirmSeconds + 1.0;
		const auto reversed = book.Query(Query());
		assert(reversed.size() == 2 && reversed[0].packet["revision"] == 4 && reversed[0].packet["caps"] == 160 && book.PendingSales() == 0);
		assert(reversed[0].packet["items"] == Json::array({ Json{ { "formId", 100 }, { "count", 5 } } }));
	}

	void TestReversalLeavesACapsDebtThatBlocksSales()
	{
		const std::string capsKey = "seller|fallout4.esm:0000000F";
		Inventory inventory;
		Json saved;
		VendorTradeBook book = LedgerBook(inventory, &saved);
		assert(book.Publish(1, Publish(100, 5), [](std::uint32_t) { return true; }).front().broadcast);

		inventory.held["seller|k200"] = 1;
		inventory.removed["seller|k200"] = 7;
		assert(book.Trade("seller", Priced("s1", 1, { VT::LineDelta{ 200, 1 } }, -3), KeyOf).front().packet["accepted"] == true);
		assert(book.Query(Query()).size() == 1 && book.PendingSales() == 1 && book.Debts() == 0);

		inventory.now += CommonwealthOnline::Server::kSaleConfirmSeconds + 1.0;
		const auto reversed = book.Query(Query());
		assert(reversed.size() == 2 && reversed[0].packet["revision"] == 3 && reversed[0].packet["caps"] == 100);
		assert(book.PendingSales() == 0 && book.Debts() == 1);
		assert(saved["debts"].size() == 1 && saved["debts"][0]["profileId"] == "seller" && saved["debts"][0]["amount"] == 3 && saved["debts"][0]["target"] == 3);

		inventory.held["seller|k200"] = 5;
		const auto refused = book.Trade("seller", Priced("s2", 3, { VT::LineDelta{ 200, 1 } }, -3), KeyOf);
		assert(refused.size() == 1 && refused[0].packet["accepted"] == false && refused[0].packet["reason"] == "notOwned" && book.PendingSales() == 0);
		inventory.now += CommonwealthOnline::Server::kSaleConfirmSeconds + 1.0;
		assert(book.Query(Query()).size() == 1 && book.Debts() == 1 && saved["debts"][0]["amount"] == 3);
		assert(book.Trade("buyer", Trade("b1", 3, -1, 10)).front().packet["accepted"] == true);

		VendorTradeBook restored = LedgerBook(inventory);
		assert(restored.LoadLedger(saved) && restored.Debts() == 1);
		assert(restored.Trade("seller", Priced("s3", 4, { VT::LineDelta{ 200, 1 } }, -3), KeyOf)[0].packet["reason"] == "notOwned");

		inventory.removed[capsKey] = 3;
		const auto repaid = restored.Trade("seller", Priced("s4", 4, { VT::LineDelta{ 200, 1 } }, -3), KeyOf);
		assert(repaid.front().packet["accepted"] == true && restored.Debts() == 0);
	}

	void TestSalesAreRefusedWithoutInventoryHooks()
	{
		const auto alwaysActive = [](std::uint32_t) { return true; };
		VendorTradeBook book = PricedBook();
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);
		const auto refused = book.Trade("seller", Priced("s1", 1, { VT::LineDelta{ 200, 1 } }, -3), KeyOf);
		assert(refused.size() == 1 && refused[0].packet["accepted"] == false && refused[0].packet["reason"] == "notOwned" && book.PendingSales() == 0);

		CommonwealthOnline::Server::VendorLedgerHooks half;
		half.held = [](const std::string&, const std::string&) { return std::optional<std::uint32_t>{ 9 }; };
		book.SetHooks(std::move(half));
		assert(book.Trade("seller", Priced("s2", 1, { VT::LineDelta{ 200, 1 } }, -3), KeyOf)[0].packet["reason"] == "notOwned");

		assert(book.Trade("buyer", Trade("b1", 1, -2, 20)).front().packet["accepted"] == true);
		const auto state = book.Query(Query());
		assert(state[0].packet["revision"] == 2 && state[0].packet["caps"] == 120);
	}

	void TestPerProfilePendingSaleCap()
	{
		Inventory inventory;
		VendorTradeBook book = LedgerBook(inventory);
		assert(book.Publish(1, Publish(VT::MaximumSeedCaps, 5), [](std::uint32_t) { return true; }).front().broadcast);
		inventory.held["seller|k200"] = 1000;
		inventory.held["other|k200"] = 1;
		const auto limit = CommonwealthOnline::Server::kMaximumPendingVendorSalesPerProfile;
		for (std::size_t sale = 0; sale < limit; ++sale) {
			const auto result = book.Trade("seller", Priced("p" + std::to_string(sale), static_cast<std::uint32_t>(1 + sale), { VT::LineDelta{ 200, 1 } }, -3), KeyOf);
			assert(result.front().packet["accepted"] == true);
		}
		assert(book.PendingSales() == limit);
		const auto refused = book.Trade("seller", Priced("over", static_cast<std::uint32_t>(1 + limit), { VT::LineDelta{ 200, 1 } }, -3), KeyOf);
		assert(refused.size() == 1 && refused[0].packet["reason"] == "notOwned" && book.PendingSales() == limit);
		const auto other = book.Trade("other", Priced("o1", static_cast<std::uint32_t>(1 + limit), { VT::LineDelta{ 200, 1 } }, -3), KeyOf);
		assert(other.front().packet["accepted"] == true && book.PendingSales() == limit + 1);
	}

	void TestFullBookEvictsOnlyForAnAcceptedVendor()
	{
		VendorTradeBook book = PricedBook();
		const auto alwaysActive = [](std::uint32_t) { return true; };
		for (std::uint32_t npc = 1; npc <= CommonwealthOnline::Server::kMaximumVendorStocks; ++npc) {
			Json packet = Publish(100, 5);
			packet["npcId"] = npc;
			assert(book.Publish(1, packet, alwaysActive).front().broadcast);
		}
		assert(book.Size() == CommonwealthOnline::Server::kMaximumVendorStocks);

		Json taken = Publish(100, 5);
		taken["npcId"] = 1;
		const auto refused = book.Publish(2, taken, alwaysActive);
		assert(refused.size() == 1 && refused.front().packet["reason"] == "notAuthority");
		assert(book.Size() == CommonwealthOnline::Server::kMaximumVendorStocks);

		Json fresh = Publish(100, 5);
		fresh["npcId"] = CommonwealthOnline::Server::kMaximumVendorStocks + 1;
		assert(book.Publish(1, fresh, alwaysActive).front().broadcast);
		assert(book.Size() == CommonwealthOnline::Server::kMaximumVendorStocks);
	}
}

int main()
{
	TestSeedQueryAndTrade();
	TestAuthorityTakeover();
	TestReferencesSharingABaseFormKeepSeparateStock();
	TestReplayIsKeyedByProfile();
	TestPricesAndTamperedSeedOverTheBook();
	TestSalesMixedCartsAndUnknownItemsArePriced();
	TestKeylessTradeRefusesFirstSendAndStaysSilentOnResend();
	TestItemValuesLoadWithoutZones();
	TestUnknownSeedDropsUnlistedLines();
	TestSeedIsClampedAndLaterPublishesCannotRaise();
	TestSaleNeedsOwnershipAndARemoval();
	TestLedgerSurvivesRestart();
	TestRestockRaisesOnlyAndKeepsPlayerSoldStock();
	TestLastRestockSurvivesARestart();
	TestPendingSaleReversesCorrectlyAfterARestock();
	TestReversalLeavesACapsDebtThatBlocksSales();
	TestSalesAreRefusedWithoutInventoryHooks();
	TestPerProfilePendingSaleCap();
	TestFullBookEvictsOnlyForAnAcceptedVendor();
	return 0;
}
