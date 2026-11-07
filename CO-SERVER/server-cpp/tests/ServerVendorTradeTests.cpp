#include "ServerVendorTrade.h"

#include <cassert>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::VendorTradeBook;

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

	void TestSeedQueryAndTrade()
	{
		VendorTradeBook book;
		const auto alwaysActive = [](std::uint32_t) { return true; };

		const auto unseeded = book.Query(Query());
		assert(unseeded.size() == 1 && unseeded[0].packet["type"] == "vendorStockUnseeded");

		const auto seeded = book.Publish(1, Publish(100, 5), alwaysActive);
		assert(seeded.size() == 1 && seeded[0].broadcast && seeded[0].packet["type"] == "vendorStockState" && seeded[0].packet["revision"] == 1);

		const auto known = book.Query(Query());
		assert(known[0].packet["type"] == "vendorStockState" && known[0].packet["caps"] == 100);

		const auto buy = book.Trade("buyer", Trade("a", 1, -2, 30));
		assert(buy.size() == 2 && buy[0].packet["accepted"] == true && !buy[0].broadcast);
		assert(buy[1].broadcast && buy[1].packet["revision"] == 2 && buy[1].packet["caps"] == 130);

		const auto replay = book.Trade("buyer", Trade("a", 1, -2, 30));
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
		VendorTradeBook book;
		assert(book.Publish(1, Publish(10, 1), [](std::uint32_t) { return true; }).front().broadcast);
		const auto stranger = book.Publish(2, Publish(5, 1), [](std::uint32_t) { return true; });
		assert(stranger.size() == 1 && stranger.front().packet["type"] == "vendorStockRejected" && stranger.front().packet["reason"] == "notAuthority");
		const auto takeover = book.Publish(2, Publish(20, 2), [](std::uint32_t) { return false; });
		assert(takeover.front().packet["type"] == "vendorStockState" && takeover.front().packet["authorityPlayerId"] == 2);
	}

	void TestReferencesSharingABaseFormKeepSeparateStock()
	{
		VendorTradeBook book;
		const auto alwaysActive = [](std::uint32_t) { return true; };
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);
		Json other = Publish(50, 2);
		other["npcId"] = 43;
		assert(book.Publish(1, other, alwaysActive).front().packet["revision"] == 1);
		Json otherQuery = Query();
		otherQuery["npcId"] = 43;
		assert(book.Query(otherQuery)[0].packet["caps"] == 50);
		Json otherTrade = Trade("x", 1, -1, 0);
		otherTrade["npcId"] = 43;
		assert(book.Trade("buyer", otherTrade)[0].packet["accepted"] == true);
		assert(book.Query(Query())[0].packet["revision"] == 1 && book.Query(Query())[0].packet["caps"] == 100);
	}

	void TestReplayIsKeyedByProfile()
	{
		VendorTradeBook book;
		const auto alwaysActive = [](std::uint32_t) { return true; };
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);
		assert(book.Trade("buyer", Trade("a", 1, -2, 30)).front().packet["accepted"] == true);

		const auto otherProfile = book.Trade("other", Trade("a", 1, -2, 30));
		assert(otherProfile.size() == 1 && otherProfile[0].packet["accepted"] == false && otherProfile[0].packet["reason"] == "staleRevision");

		const auto keyless = book.Trade("", Trade("e", 2, -1, 0));
		assert(keyless.empty());
	}

	void TestKeylessTradeSendsNothingAndRecordsNothing()
	{
		VendorTradeBook book;
		const auto alwaysActive = [](std::uint32_t) { return true; };
		assert(book.Publish(1, Publish(100, 5), alwaysActive).front().broadcast);

		assert(book.Trade("", Trade("e", 1, -2, 30)).empty());
		auto malformed = Trade("m", 1, -1, 0);
		malformed["deltas"].push_back(malformed["deltas"].front());
		assert(book.Trade("", malformed).empty());
		assert(book.Query(Query())[0].packet["revision"] == 1 && book.Query(Query())[0].packet["caps"] == 100);

		const auto accepted = book.Trade("buyer", Trade("e", 1, -2, 30));
		assert(accepted.front().packet["accepted"] == true && accepted.front().packet["operationId"] == "e");
	}
}

int main()
{
	TestSeedQueryAndTrade();
	TestAuthorityTakeover();
	TestReferencesSharingABaseFormKeepSeparateStock();
	TestReplayIsKeyedByProfile();
	TestKeylessTradeSendsNothingAndRecordsNothing();
	return 0;
}
