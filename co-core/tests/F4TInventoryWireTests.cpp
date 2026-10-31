#include "F4TInventoryWire.h"

#include "F4TInventoryContract.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::InventoryWire;

	const std::string key = "test.esm:00000123";

	Json Reference()
	{
		return Json{ { "cellId", "0000CAFE" }, { "refId", "00001234" } };
	}

	Json Line(const std::string& a_key, std::uint32_t a_count)
	{
		return Json{ { "itemKey", a_key }, { "count", a_count } };
	}

	Json ContainerRequest()
	{
		return Json{ { "type", "inventoryContainerRequest" }, { "operationId", "op-1" }, { "expectedRevision", 0u },
			{ "direction", "take" }, { "container", Reference() }, { "itemKey", key }, { "count", 1u } };
	}

	Json ContainerResult()
	{
		return Json{ { "type", "inventoryContainerResult" }, { "operationId", "op-1" }, { "status", "committed" },
			{ "revision", 3u }, { "direction", "take" }, { "itemKey", key }, { "count", 1u } };
	}

	Json TradeRequest()
	{
		return Json{ { "type", "inventoryTradeRequest" }, { "operationId", "op-1" }, { "expectedRevision", 2u },
			{ "vendor", Reference() }, { "sell", Json::array({ Line(key, 1u) }) }, { "buy", Json::array() } };
	}

	Json TradeResult()
	{
		return Json{ { "type", "inventoryTradeResult" }, { "operationId", "op-1" }, { "status", "committed" }, { "revision", 2u } };
	}

	void TestContainerRequest()
	{
		assert(F4T::InventoryContract::ParseItemKey(key));
		const auto request = ContainerRequest();
		assert(NormalizeContainerRequest(request) == request);
		auto put = request;
		put["direction"] = "put";
		assert(NormalizeContainerRequest(put) == put);
		auto direction = request;
		direction["direction"] = "move";
		assert(!NormalizeContainerRequest(direction));
		auto zeroCount = request;
		zeroCount["count"] = 0u;
		assert(!NormalizeContainerRequest(zeroCount));
		auto tooMany = request;
		tooMany["count"] = 65536u;
		assert(!NormalizeContainerRequest(tooMany));
		auto signedCount = request;
		signedCount["count"] = -1;
		assert(!NormalizeContainerRequest(signedCount));
		auto missingRevision = request;
		missingRevision.erase("expectedRevision");
		assert(!NormalizeContainerRequest(missingRevision));
		auto mixedType = request;
		mixedType["type"] = "inventoryTradeRequest";
		assert(!NormalizeContainerRequest(mixedType));
		auto badKey = request;
		badKey["itemKey"] = "test.esm:0000000";
		assert(!NormalizeContainerRequest(badKey));
		auto colonKey = request;
		colonKey["itemKey"] = "bad:plugin:00000123";
		assert(!NormalizeContainerRequest(colonKey));
		auto operation64 = request;
		operation64["operationId"] = std::string(64, 'a');
		assert(NormalizeContainerRequest(operation64) == operation64);
		auto operation65 = request;
		operation65["operationId"] = std::string(65, 'a');
		assert(!NormalizeContainerRequest(operation65));
	}

	void TestContainerReference()
	{
		const auto request = ContainerRequest();
		auto runtimeRef = request;
		runtimeRef["container"] = Json{ { "cellId", "0000CAFE" }, { "refId", "FF001234" } };
		assert(!NormalizeContainerRequest(runtimeRef));
		auto zeroRef = request;
		zeroRef["container"] = Json{ { "cellId", "0000CAFE" }, { "refId", "00000000" } };
		assert(!NormalizeContainerRequest(zeroRef));
		auto shortRef = request;
		shortRef["container"] = Json{ { "cellId", "0000CAFE" }, { "refId", "0001234" } };
		assert(!NormalizeContainerRequest(shortRef));
		auto longRef = request;
		longRef["container"] = Json{ { "cellId", "0000CAFE" }, { "refId", "000001234" } };
		assert(!NormalizeContainerRequest(longRef));
		auto lower = request;
		lower["container"] = Json{ { "cellId", "0000cafe" }, { "refId", "00001234" } };
		assert(NormalizeContainerRequest(lower) == lower);
		auto zeroCell = request;
		zeroCell["container"] = Json{ { "cellId", "00000000" }, { "refId", "00001234" } };
		assert(NormalizeContainerRequest(zeroCell) == zeroCell);
		auto extraReference = request;
		extraReference["container"] = Json{ { "cellId", "0000CAFE" }, { "refId", "00001234" }, { "worldspaceId", "0000F00D" } };
		assert(!NormalizeContainerRequest(extraReference));
		auto missingReference = request;
		missingReference["container"] = Json{ { "cellId", "0000CAFE" } };
		assert(!NormalizeContainerRequest(missingReference));
	}

	void TestContainerRequestKeySet()
	{
		const auto request = ContainerRequest();
		auto extra = request;
		extra["unexpected"] = 1u;
		assert(!NormalizeContainerRequest(extra));
		for (const char* name : { "type", "operationId", "expectedRevision", "direction", "container", "itemKey", "count" }) {
			auto missing = request;
			missing.erase(name);
			assert(!NormalizeContainerRequest(missing));
		}
	}

	void TestContainerResult()
	{
		const auto committed = ContainerResult();
		assert(NormalizeContainerResult(committed) == committed);
		auto withContainerRevision = committed;
		withContainerRevision["containerRevision"] = 4u;
		assert(NormalizeContainerResult(withContainerRevision) == withContainerRevision);
		auto direction = committed;
		direction["direction"] = "give";
		assert(!NormalizeContainerResult(direction));
		auto noRevision = committed;
		noRevision.erase("revision");
		assert(!NormalizeContainerResult(noRevision));
		auto withReason = committed;
		withReason["reason"] = "capacityExceeded";
		assert(!NormalizeContainerResult(withReason));
		auto extra = committed;
		extra["unexpected"] = true;
		assert(!NormalizeContainerResult(extra));
		auto rejected = committed;
		rejected["status"] = "rejected";
		rejected["reason"] = "capacityExceeded";
		assert(NormalizeContainerResult(rejected) == rejected);
		auto limited = committed;
		limited["status"] = "rejected";
		limited["reason"] = "rateLimited";
		limited.erase("revision");
		assert(NormalizeContainerResult(limited) == limited);
		auto limitedWithRevision = committed;
		limitedWithRevision["status"] = "rejected";
		limitedWithRevision["reason"] = "rateLimited";
		assert(!NormalizeContainerResult(limitedWithRevision));
		auto rejectedNoReason = committed;
		rejectedNoReason["status"] = "rejected";
		assert(!NormalizeContainerResult(rejectedNoReason));
		auto unknownReason = committed;
		unknownReason["status"] = "rejected";
		unknownReason["reason"] = "containerLedgerUnavailable";
		assert(NormalizeContainerResult(unknownReason) == unknownReason);
		auto unknownStatus = committed;
		unknownStatus["status"] = "pending";
		assert(!NormalizeContainerResult(unknownStatus));
	}

	void TestTradeRequest()
	{
		const auto request = TradeRequest();
		assert(NormalizeTradeRequest(request) == request);
		auto full = request;
		auto sell = Json::array();
		auto buy = Json::array();
		for (std::uint32_t index = 0; index < kMaximumTradeLines; ++index) {
			sell.push_back(Line("test.esm:000001" + std::string(index < 10 ? "0" : "") + std::to_string(index), index + 1));
		}
		buy.push_back(Line("caps.esm:0000000F", 12u));
		full["sell"] = sell;
		full["buy"] = buy;
		assert(NormalizeTradeRequest(full) == full);
		auto tooLong = full;
		tooLong["sell"].push_back(Line("test.esm:00000123", 1u));
		assert(!NormalizeTradeRequest(tooLong));
		auto duplicate = request;
		duplicate["sell"] = Json::array({ Line(key, 1u), Line(key, 2u) });
		assert(!NormalizeTradeRequest(duplicate));
		auto bothSides = request;
		bothSides["buy"] = Json::array({ Line(key, 1u) });
		assert(NormalizeTradeRequest(bothSides) == bothSides);
		auto empty = request;
		empty["sell"] = Json::array();
		assert(NormalizeTradeRequest(empty) == empty);
		auto zeroCount = request;
		zeroCount["sell"] = Json::array({ Line(key, 0u) });
		assert(!NormalizeTradeRequest(zeroCount));
		auto extraLine = request;
		extraLine["sell"] = Json::array({ Json{ { "itemKey", key }, { "count", 1u }, { "price", 2u } } });
		assert(!NormalizeTradeRequest(extraLine));
		auto invalidKey = request;
		invalidKey["sell"] = Json::array({ Line("not-a-key", 1u) });
		assert(!NormalizeTradeRequest(invalidKey));
		auto extra = request;
		extra["unexpected"] = 1u;
		assert(!NormalizeTradeRequest(extra));
		auto missingBuy = request;
		missingBuy.erase("buy");
		assert(!NormalizeTradeRequest(missingBuy));
		auto runtimeVendor = request;
		runtimeVendor["vendor"] = Json{ { "cellId", "0000CAFE" }, { "refId", "FF001234" } };
		assert(!NormalizeTradeRequest(runtimeVendor));
		auto missingCell = request;
		missingCell["vendor"] = Json{ { "refId", "00001234" } };
		assert(!NormalizeTradeRequest(missingCell));
	}

	void TestTradeResult()
	{
		const auto committed = TradeResult();
		assert(NormalizeTradeResult(committed) == committed);
		auto withReason = committed;
		withReason["reason"] = "insufficientItems";
		assert(!NormalizeTradeResult(withReason));
		auto noRevision = committed;
		noRevision.erase("revision");
		assert(!NormalizeTradeResult(noRevision));
		auto extra = committed;
		extra["unexpected"] = 1u;
		assert(!NormalizeTradeResult(extra));
		auto rejected = committed;
		rejected["status"] = "rejected";
		rejected["reason"] = "insufficientItems";
		assert(NormalizeTradeResult(rejected) == rejected);
		auto limited = committed;
		limited["status"] = "rejected";
		limited["reason"] = "inventoryAuthorityUnavailable";
		limited.erase("revision");
		assert(NormalizeTradeResult(limited) == limited);
		auto available = committed;
		available["status"] = "rejected";
		available["reason"] = "insufficientItems";
		available.erase("revision");
		assert(!NormalizeTradeResult(available));
		auto missingOperation = committed;
		missingOperation.erase("operationId");
		assert(!NormalizeTradeResult(missingOperation));
	}

	void TestCrossTypeRejection()
	{
		assert(!NormalizeContainerResult(ContainerRequest()));
		assert(!NormalizeContainerRequest(ContainerResult()));
		assert(!NormalizeTradeResult(TradeRequest()));
		assert(!NormalizeTradeRequest(TradeResult()));
		assert(!NormalizeContainerRequest(Json::array()));
	}
}

int main()
{
	TestContainerRequest();
	TestContainerReference();
	TestContainerRequestKeySet();
	TestContainerResult();
	TestTradeRequest();
	TestTradeResult();
	TestCrossTypeRejection();
}
