#include "DurableStateFileStore.h"
#include "F4TPlayerStateValidation.h"
#include "F4TWorkshopRaids.h"
#include "PersistenceGate.h"
#include "ServerFactionTestSupport.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

using namespace FactionTestSupport;

namespace
{
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::ProtocolHost;
	using Json = nlohmann::json;

	Json OperationRecord(std::uint64_t a_operationId)
	{
		return Json{ { "operationId", a_operationId }, { "profileId", "profile-owner" }, { "items", Json::array() } };
	}

	void TestOperationIdSurvivesEraseOfOnlyRecord()
	{
		const auto directory = TempDirectory("charge-id-erase");
		{
			DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string(32, '1'); });
			assert(store.PostSettlementCharge(OperationRecord(5000)));
			assert(store.EraseSettlementCharge(5000));
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().empty());
			assert(store.SettlementChargeNextOperationId() == 5001);
		}
		std::filesystem::remove_all(directory);
	}

	void TestOperationIdSurvivesRestart()
	{
		const auto directory = TempDirectory("charge-id-restart");
		const auto path = directory / "durable-state.json";
		{
			DurableStateFileStore store(path, [] { return 1000.0; }, [] { return std::string(32, '2'); });
			assert(store.PostSettlementCharge(OperationRecord(6000)));
			assert(store.EraseSettlementCharge(6000));
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, [] { return 1000.0; }, [] { return std::string(32, '2'); });
			assert(store.PendingSettlementCharges().empty());
			assert(store.SettlementChargeNextOperationId() == 6001);
		}
		std::filesystem::remove_all(directory);
	}

	void TestOperationIdNeverLowered()
	{
		const auto directory = TempDirectory("charge-id-monotonic");
		{
			DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string(32, '3'); });
			assert(store.PostSettlementCharge(OperationRecord(9000)));
			assert(store.PostSettlementCharge(OperationRecord(100)));
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().size() == 2);
			assert(store.SettlementChargeNextOperationId() == 9001);
		}
		std::filesystem::remove_all(directory);
	}

	void TestBenefitWriteErasesChargeRecord()
	{
		const auto directory = TempDirectory("charge-benefit-erase");
		{
			DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string(32, '5'); });
			assert(store.PostSettlementCharge(OperationRecord(8000)));
			assert(store.SaveSettlementAccess(Json::object(), 8000));
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().empty());
			assert(store.GetSettlementAccess() == Json::object());
			assert(!store.SaveSettlementAccess(Json::object(), 8000));
			assert(store.SaveSettlementAccess(Json::object()));
			store.WaitForCommits();
		}
		std::filesystem::remove_all(directory);
	}

	void TestSnapshotRemovalCountsOnlyClientDecrease()
	{
		StockedInventory stock{ "charge-removal-counter", 100 };
		ProtocolHost host{ "Settlement Removal Counter" };
		assert(host.EnableInventoryTransactions(stock.gate));
		(void)Join(host, 70, "profile-owner", 1.0, 1001.0, InventoryCapabilities());
		assert(VerifyStartingGrant(host, stock.gate, 70, "profile-owner", 1.01, 1001.01));
		const std::string itemKey = stock.store.GetInventory("profile-owner")->at("items").begin().key();
		assert(stock.store.SnapshotRemovals("profile-owner", itemKey) == 0);
		const Json charge{ { "items", Json::array({ Json{ { "itemKey", itemKey }, { "count", 10 } } }) } };
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 3.0);
		assert(stock.store.SnapshotRemovals("profile-owner", itemKey) == 10);
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", Json{ { "items", Json::array() } }, 5.0);
		assert(stock.store.SnapshotRemovals("profile-owner", itemKey) == 10);
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 7.0);
		assert(stock.store.SnapshotRemovals("profile-owner", itemKey) == 20);
		assert(stock.store.SnapshotRemovals("profile-owner", "no:such") == 0);
		assert(stock.store.SnapshotRemovals("other-profile", itemKey) == 0);
	}

	void TestEraseOfHighestRecordKeepsOperationId()
	{
		const auto directory = TempDirectory("charge-id-highest");
		{
			DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string(32, '4'); });
			assert(store.PostSettlementCharge(OperationRecord(7000)));
			assert(store.PostSettlementCharge(OperationRecord(7001)));
			assert(store.EraseSettlementCharge(7001));
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().size() == 1);
			assert(store.SettlementChargeNextOperationId() == 7002);
			assert(store.EraseSettlementCharge(7000));
			store.WaitForCommits();
			assert(store.SettlementChargeNextOperationId() == 7002);
		}
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	try {
		TestOperationIdSurvivesEraseOfOnlyRecord();
		TestOperationIdSurvivesRestart();
		TestOperationIdNeverLowered();
		TestBenefitWriteErasesChargeRecord();
		TestSnapshotRemovalCountsOnlyClientDecrease();
		TestEraseOfHighestRecordKeepsOperationId();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "ServerSettlementChargeIdentityTests unexpected exception: %s\n", error.what());
		return 1;
	}
	return 0;
}
