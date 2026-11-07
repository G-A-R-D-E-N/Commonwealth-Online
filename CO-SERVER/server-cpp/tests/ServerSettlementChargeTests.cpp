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
#include <format>
#include <string>

using namespace FactionTestSupport;

namespace
{
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::Workshop::BuildZonePolicy;
	using F4T::Workshop::FormKey;

	constexpr std::uint64_t kZone = 9;
	constexpr std::uint64_t kSecondZone = 10;

	BuildZonePolicy Zone9()
	{
		return BuildZonePolicy{ kZone, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 8, false, 0.0F, 0.0F, 0.0F, 0.0F, {} };
	}

	Json WorkshopTransform(double a_x)
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "spawn" }
		};
	}

	Json WalkTo(double a_x)
	{
		auto packet = WorkshopTransform(a_x);
		packet["movementType"] = "normal";
		return packet;
	}

	Json PlaceOp(std::uint64_t a_operationId, std::uint64_t a_revision, double a_x)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", kZone },
			{ "kind", "place" },
			{ "form", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 0x1234 } } },
			{ "space", Json{ { "plugin", "Fallout4.esm" }, { "localFormId", 1 } } },
			{ "transform", Json{
				{ "x", a_x },
				{ "y", 0.0 },
				{ "z", 0.0 },
				{ "pitch", 0.0 },
				{ "yaw", 0.0 },
				{ "roll", 0.0 },
				{ "scale", 1.0 }
			} }
		};
	}

	Json ScrapOp(std::uint64_t a_operationId, std::uint64_t a_revision, std::uint64_t a_objectId)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", kZone },
			{ "kind", "scrap" },
			{ "objectId", a_objectId }
		};
	}

	Json GrenadeHit(std::uint64_t a_objectId)
	{
		return Json{ { "type", "structureHit" }, { "buildZoneId", kZone }, { "objectId", a_objectId }, { "explosive", "grenade" } };
	}

	Json EquippedGrenade()
	{
		return Json{ { "type", "playerState" }, { "equippedItems", Json::array({ Json{ { "slot", F4T::PlayerStateValidation::kThrowableSlot }, { "form" "Id", std::format("{:08X}", F4T::WorkshopRaids::kFragGrenadeSourceFormId) } } }) } };
	}

	std::vector<OutboundFrame> ThrowGrenade(ProtocolHost& a_host, std::uint32_t a_connectionId, std::uint64_t a_objectId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		(void)Send(a_host, a_connectionId, EquippedGrenade(), a_monotonicSeconds, a_unixTimeSeconds);
		return Send(a_host, a_connectionId, GrenadeHit(a_objectId), a_monotonicSeconds, a_unixTimeSeconds);
	}

	Json RepairRequest(std::uint64_t a_objectId)
	{
		return Json{ { "type", "structureRepair" }, { "buildZoneId", kZone }, { "objectId", a_objectId } };
	}

	Json ChargeResult(std::uint64_t a_operationId, bool a_ok)
	{
		return Json{ { "type", "settlementChargeResult" }, { "operationId", a_operationId }, { "ok", a_ok } };
	}

	Json ZoneRequest(const char* a_type)
	{
		return Json{ { "type", a_type }, { "buildZoneId", kZone } };
	}

	Json CatalogFor(const Json& a_charge)
	{
		Json catalog = Json::array();
		for (const auto& line : a_charge.at("items")) {
			const auto itemKey = line.at("itemKey").get<std::string>();
			const auto separator = itemKey.find(':');
			catalog.push_back(Json{ { "plugin", itemKey.substr(0, separator) }, { "localFormId", itemKey.substr(separator + 1) }, { "count", std::uint64_t{ 100 } } });
		}
		return catalog;
	}

	struct Piece
	{
		std::uint64_t objectId{};
		std::uint64_t revision{};
	};

	Piece BuildPiece(ProtocolHost& a_host, std::uint32_t a_connectionId)
	{
		assert(FindFrame(Send(a_host, a_connectionId, WorkshopTransform(0.0), 2.0, 1002.0), a_connectionId, "workshopSnapshot") != nullptr);
		const auto commit = RequirePacket(Send(a_host, a_connectionId, PlaceOp(1, 0, 1.0), 2.5, 1002.5), a_connectionId, "workshopCommit");
		return Piece{ commit.at("objectId").get<std::uint64_t>(), commit.at("revision").get<std::uint64_t>() };
	}

	Joined JoinInventory(ProtocolHost& a_host, PersistenceGate& a_gate, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		auto joined = Join(a_host, a_connectionId, a_profileId, a_monotonicSeconds, a_unixTimeSeconds, InventoryCapabilities());
		(void)VerifyStartingGrant(a_host, a_gate, a_connectionId, a_profileId, a_monotonicSeconds + 0.01, a_unixTimeSeconds + 0.01);
		return joined;
	}

	void TestFailedCommitRefundsOnce()
	{
		StockedInventory stock{ "charge-refund", 100 };
		ProtocolHost host{ "Settlement Charge Refund" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		const auto hit = ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0);
		assert(!hit.empty());
		const auto hitRevision = RequirePacket(hit, 70, "structureDamage").at("revision").get<std::uint64_t>();
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		assert(RequirePacket(Send(host, 70, ScrapOp(2, hitRevision, piece.objectId), 34.1, 1034.1), 70, "workshopCommit").at("objectId").get<std::uint64_t>() == piece.objectId);
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 34.15);
		const auto acked = Send(host, 70, ChargeResult(operationId, true), 34.2, 1034.2);
		assert(RequirePacket(acked, 70, "settlementAccessReject").at("status") == "chargeFailed");
		assert(FindFrame(acked, 70, "campGrant") != nullptr);
		assert(stock.store.PendingCampGrants("profile-owner").size() == 1);
		assert(stock.store.PendingSettlementCharges().empty());
		assert(Send(host, 70, ChargeResult(operationId, true), 34.3, 1034.3).empty());
		assert(stock.store.PendingCampGrants("profile-owner").size() == 1);
	}

	void TestRefundStoreFailureHoldsAcknowledgedChargeUntilRecovered()
	{
		StockedInventory stock{ "charge-held", 100 };
		ProtocolHost host{ "Settlement Charge Held" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		const auto hit = ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0);
		assert(!hit.empty());
		const auto hitRevision = RequirePacket(hit, 70, "structureDamage").at("revision").get<std::uint64_t>();
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		assert(RequirePacket(Send(host, 70, ScrapOp(2, hitRevision, piece.objectId), 34.1, 1034.1), 70, "workshopCommit").at("objectId").get<std::uint64_t>() == piece.objectId);
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 34.15);
		const auto backup = stock.directory / "durable-state.json.bak";
		std::filesystem::remove(backup);
		std::filesystem::create_directories(backup / "occupied");
		const auto held = Send(host, 70, ChargeResult(operationId, true), 34.2, 1034.2);
		std::filesystem::remove_all(backup);
		assert(FindFrame(held, 70, "campGrant") == nullptr);
		assert(FindFrame(held, 70, "settlementAccessReject") == nullptr);
		assert(stock.store.PendingCampGrants("profile-owner").empty());
		assert(stock.store.PendingSettlementCharges().size() == 1);

		(void)host.HandleDisconnected(70, 1034.3);
		(void)host.FlushWorkshopPersistence(1134.3, 134.3);
		assert(stock.store.PendingSettlementCharges().size() == 1);
		RearmStartingGrantFixture(stock.store, "profile-owner");
		JoinInventory(host, stock.gate, 71, "profile-owner", 135.0, 1135.0);
		const auto resent = host.FlushWorkshopPersistence(1135.1, 135.1);
		const auto* resentCharge = FindFrame(resent, 71, "settlementCharge");
		assert(resentCharge != nullptr && DecodePacket(*resentCharge).at("operationId").get<std::uint64_t>() == operationId);
		(void)Send(host, 71, WorkshopTransform(0.0), 135.05, 1135.05);
		const auto refunded = Send(host, 71, ChargeResult(operationId, true), 135.2, 1135.2);
		assert(RequirePacket(refunded, 71, "settlementAccessReject").at("status") == "chargeFailed");
		assert(FindFrame(refunded, 71, "campGrant") != nullptr);
		assert(stock.store.PendingCampGrants("profile-owner").size() == 1);
		assert(stock.store.PendingSettlementCharges().empty());
	}

	void TestUnackedChargeExpiresAfterSixtySecondsOffline()
	{
		StockedInventory stock{ "charge-expiry", 100 };
		ProtocolHost host{ "Settlement Charge Expiry" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto first = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge").at("operationId").get<std::uint64_t>();

		(void)host.HandleDisconnected(70, 1034.1);
		(void)host.FlushWorkshopPersistence(1098.0, 98.0);
		RearmStartingGrantFixture(stock.store, "profile-owner");
		JoinInventory(host, stock.gate, 71, "profile-owner", 98.1, 1098.1);
		const auto resent = host.FlushWorkshopPersistence(1098.2, 98.2);
		const auto* charge = FindFrame(resent, 71, "settlementCharge");
		assert(charge != nullptr && DecodePacket(*charge).at("operationId").get<std::uint64_t>() == first);

		(void)host.HandleDisconnected(71, 1098.3);
		(void)host.FlushWorkshopPersistence(1163.1, 163.1);
		assert(stock.store.PendingSettlementCharges().size() == 1);
		(void)host.FlushWorkshopPersistence(1163.3, 163.3);
		assert(stock.store.PendingSettlementCharges().empty());

		RearmStartingGrantFixture(stock.store, "profile-owner");
		JoinInventory(host, stock.gate, 72, "profile-owner", 163.4, 1163.4);
		(void)Send(host, 72, WorkshopTransform(0.0), 163.45, 1163.45);
		const auto repeated = Send(host, 72, RepairRequest(piece.objectId), 163.5, 1163.5);
		assert(RequirePacket(repeated, 72, "settlementCharge").at("operationId").get<std::uint64_t>() != first);
	}

	void TestPendingChargeSurvivesRestartAndResendsToOwner()
	{
		const auto directory = TempDirectory("charge-restart");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		std::uint64_t operationId{};
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			AdmitProfile(store, "profile-owner");
			SeedInventory(store, "profile-owner", Json::array({ SettlerMaterial("000731A3", 100), SettlerMaterial("000731A4", 100), SettlerMaterial("00106D99", 100) }));
			ProtocolHost host{ "Settlement Charge Restart" };
			assert(host.EnableInventoryTransactions(gate));
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.EnableSettlementAccess(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			JoinInventory(host, gate, 70, "profile-owner", 1.0, 1001.0);
			std::string failure;
			(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
			assert(failure.empty());
			const auto piece = BuildPiece(host, 70);
			assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
			(void)host.FlushWorkshopPersistence(1033.0, 33.0);
			store.WaitForCommits();
			operationId = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge").at("operationId").get<std::uint64_t>();
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().size() == 1);
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Settlement Charge Restart" };
			assert(host.EnableInventoryTransactions(gate));
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.EnableSettlementAccess(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			assert(store.PendingSettlementCharges().size() == 1);
			(void)host.FlushWorkshopPersistence(1200.0, 200.0);
			assert(store.PendingSettlementCharges().size() == 1);
			RearmStartingGrantFixture(store, "profile-owner");
			JoinInventory(host, gate, 71, "profile-owner", 200.5, 1200.5);
			const auto resent = host.FlushWorkshopPersistence(1201.0, 201.0);
			const auto* charge = FindFrame(resent, 71, "settlementCharge");
			assert(charge != nullptr && DecodePacket(*charge).at("operationId").get<std::uint64_t>() == operationId);
			(void)Send(host, 71, WorkshopTransform(0.0), 201.4, 1201.4);
			(void)ConfirmChargeConsumed(host, 71, store, "profile-owner", DecodePacket(*charge), 201.45);
			const auto acked = Send(host, 71, ChargeResult(operationId, true), 201.5, 1201.5);
			assert(FindFrame(acked, 71, "structureDamage") != nullptr);
			assert(store.PendingSettlementCharges().empty());
			store.WaitForCommits();
		}
		std::filesystem::remove_all(directory);
	}

	void TestRepairChargeCapsAtOneUnit()
	{
		StockedInventory stock{ "charge-cap", 100 };
		ProtocolHost host{ "Settlement Charge Cap" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		assert(charge.at("items").size() == 1);
		assert(charge.at("items").at(0).at("count").get<std::uint64_t>() == 1);
	}

	void TestUpkeepWithNoObjectsRefuses()
	{
		StockedInventory stock{ "charge-upkeep", 1000 };
		ProtocolHost host{ "Settlement Charge Upkeep" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		assert(RequirePacket(Send(host, 70, ZoneRequest("upkeepPay"), 1.5, 1001.5), 70, "settlementAccessReject").at("status") == "nothingToRepair");
		(void)BuildPiece(host, 70);
		assert(FindFrame(Send(host, 70, ZoneRequest("upkeepPay"), 3.0, 1003.0), 70, "settlementCharge") != nullptr);
	}

	void TestWorkbenchRepairRefusesZoneWithoutWorkbench()
	{
		StockedInventory stock{ "charge-workbench", 100 };
		ProtocolHost host{ "Settlement Charge Workbench" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		assert(RequirePacket(Send(host, 70, ZoneRequest("workbenchRepair"), 2.0, 1002.0), 70, "settlementAccessReject").at("status") == "nothingToRepair");
	}

	void TestStructureRepairOwnerReachIsEnforced()
	{
		StockedInventory stock{ "charge-owner-reach", 100 };
		ProtocolHost host{ "Settlement Charge Owner Reach" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		(void)Send(host, 70, WalkTo(1000.0), 33.0, 1033.0);
		assert(RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementAccessReject").at("status") == "tooFar");
		(void)Send(host, 70, WalkTo(0.0), 34.5, 1034.5);
		assert(FindFrame(Send(host, 70, RepairRequest(piece.objectId), 35.0, 1035.0), 70, "settlementCharge") != nullptr);
	}

	void TestStructureRepairBuilderReachAndAuthorization()
	{
		StockedInventory stock{ "charge-builder", 100 };
		ProtocolHost host{ "Settlement Charge Builder" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto ownerCharge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		AdmitProfile(stock.store, "profile-builder");
		SeedInventory(stock.store, "profile-builder", CatalogFor(ownerCharge));
		(void)Send(host, 70, Json{ { "type", "settlementAccessUpdate" }, { "buildZoneId", kZone }, { "op", "grant" }, { "targetProfileId", "profile-builder" } }, 35.0, 1035.0);
		JoinInventory(host, stock.gate, 73, "profile-builder", 36.0, 1036.0);
		(void)Send(host, 73, WorkshopTransform(1000.0), 36.1, 1036.1);
		assert(RequirePacket(Send(host, 73, RepairRequest(piece.objectId), 40.0, 1040.0), 73, "settlementAccessReject").at("status") == "tooFar");
		(void)Send(host, 73, WalkTo(0.0), 41.0, 1041.0);
		const auto builderCharge = RequirePacket(Send(host, 73, RepairRequest(piece.objectId), 41.1, 1041.1), 73, "settlementCharge");
		(void)ConfirmChargeConsumed(host, 73, stock.store, "profile-builder", builderCharge, 41.15);
		assert(FindFrame(Send(host, 73, ChargeResult(builderCharge.at("operationId").get<std::uint64_t>(), true), 41.2, 1041.2), 73, "structureDamage") != nullptr);
		AdmitProfile(stock.store, "profile-stranger");
		JoinInventory(host, stock.gate, 74, "profile-stranger", 36.2, 1036.2);
		(void)Send(host, 74, WorkshopTransform(0.0), 41.3, 1041.3);
		assert(RequirePacket(Send(host, 74, RepairRequest(piece.objectId), 41.4, 1041.4), 74, "settlementAccessReject").at("status") == "notAllowed");
		assert(RequirePacket(Send(host, 73, ZoneRequest("upkeepPay"), 41.5, 1041.5), 73, "settlementAccessReject").at("status") == "notOwner");
	}

	void TestMovedAwayBeforeAckRefundsCharge()
	{
		StockedInventory stock{ "charge-moved", 100 };
		ProtocolHost host{ "Settlement Charge Moved" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		(void)Send(host, 70, WalkTo(1000.0), 34.1, 1034.1);
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 34.15);
		const auto acked = Send(host, 70, ChargeResult(operationId, true), 34.2, 1034.2);
		assert(RequirePacket(acked, 70, "settlementAccessReject").at("status") == "chargeFailed");
		assert(FindFrame(acked, 70, "campGrant") != nullptr);
		assert(FindFrame(acked, 70, "structureDamage") == nullptr);
		assert(stock.store.PendingCampGrants("profile-owner").size() == 1);
		assert(stock.store.PendingSettlementCharges().empty());
	}

	void TestOwnerChangeDoesNotBlockNewOwnerAndRefundsOldOwner()
	{
		StockedInventory stock{ "charge-owner-change", 100 };
		ProtocolHost host{ "Settlement Charge Owner Change" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto oldCharge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		AdmitProfile(stock.store, "profile-new");
		SeedInventory(stock.store, "profile-new", CatalogFor(oldCharge));
		(void)host.SetSettlementOwner(kZone, "profile-new", failure);
		assert(failure.empty());
		JoinInventory(host, stock.gate, 75, "profile-new", 35.0, 1035.0);
		(void)Send(host, 75, WorkshopTransform(0.0), 35.1, 1035.1);
		const auto newCharge = RequirePacket(Send(host, 75, RepairRequest(piece.objectId), 40.0, 1040.0), 75, "settlementCharge");
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", oldCharge, 40.45);
		const auto oldAck = Send(host, 70, ChargeResult(oldCharge.at("operationId").get<std::uint64_t>(), true), 40.5, 1040.5);
		assert(RequirePacket(oldAck, 70, "settlementAccessReject").at("status") == "chargeFailed");
		assert(FindFrame(oldAck, 70, "campGrant") != nullptr);
		assert(FindFrame(oldAck, 70, "structureDamage") == nullptr);
		assert(stock.store.PendingCampGrants("profile-owner").size() == 1);
		(void)ConfirmChargeConsumed(host, 75, stock.store, "profile-new", newCharge, 40.55);
		const auto newAck = Send(host, 75, ChargeResult(newCharge.at("operationId").get<std::uint64_t>(), true), 40.6, 1040.6);
		assert(FindFrame(newAck, 75, "structureDamage") != nullptr);
		assert(stock.store.PendingSettlementCharges().empty());
	}

	void TestDeliveredChargeSurvivesOfflineWindow()
	{
		StockedInventory stock{ "charge-delivered", 100 };
		ProtocolHost host{ "Settlement Charge Delivered" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto repair = Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0);
		const auto* charge = FindFrame(repair, 70, "settlementCharge");
		assert(charge != nullptr);
		const auto operationId = DecodePacket(*charge).at("operationId").get<std::uint64_t>();
		host.FrameDeliveryCompleted(*charge, true);
		(void)host.HandleDisconnected(70, 1034.1);
		(void)host.FlushWorkshopPersistence(1163.1, 163.1);
		assert(stock.store.PendingSettlementCharges().size() == 1);
		RearmStartingGrantFixture(stock.store, "profile-owner");
		JoinInventory(host, stock.gate, 71, "profile-owner", 163.2, 1163.2);
		(void)Send(host, 71, WorkshopTransform(0.0), 163.25, 1163.25);
		const auto resent = host.FlushWorkshopPersistence(1163.3, 163.3);
		const auto* resentCharge = FindFrame(resent, 71, "settlementCharge");
		assert(resentCharge != nullptr && DecodePacket(*resentCharge).at("operationId").get<std::uint64_t>() == operationId);
		(void)ConfirmChargeConsumed(host, 71, stock.store, "profile-owner", DecodePacket(*resentCharge), 163.35);
		assert(FindFrame(Send(host, 71, ChargeResult(operationId, true), 163.4, 1163.4), 71, "structureDamage") != nullptr);
		assert(stock.store.PendingSettlementCharges().empty());
	}

	void TestSnapshotBeforeResultCommitsOnce()
	{
		StockedInventory stock{ "charge-snapshot-first", 100 };
		ProtocolHost host{ "Settlement Charge Snapshot First" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		const auto before = stock.store.GetInventory("profile-owner")->at("items");
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 34.1);
		const auto acked = Send(host, 70, ChargeResult(operationId, true), 34.2, 1034.2);
		assert(FindFrame(acked, 70, "structureDamage") != nullptr);
		assert(FindFrame(acked, 70, "settlementAccessReject") == nullptr);
		assert(stock.store.PendingSettlementCharges().empty());
		const auto after = stock.store.GetInventory("profile-owner")->at("items");
		for (const auto& line : charge.at("items")) {
			const auto itemKey = line.at("itemKey").get<std::string>();
			assert(after.at(itemKey).at("count").get<std::uint64_t>() + line.at("count").get<std::uint64_t>() == before.at(itemKey).at("count").get<std::uint64_t>());
		}
		assert(Send(host, 70, ChargeResult(operationId, true), 34.3, 1034.3).empty());
		assert(stock.store.GetInventory("profile-owner")->at("items") == after);
	}

	void TestResultBeforeSnapshotCommitsOnce()
	{
		StockedInventory stock{ "charge-result-first", 100 };
		ProtocolHost host{ "Settlement Charge Result First" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		const auto before = stock.store.GetInventory("profile-owner")->at("items");
		const auto acked = Send(host, 70, ChargeResult(operationId, true), 34.1, 1034.1);
		assert(FindFrame(acked, 70, "structureDamage") == nullptr);
		assert(FindFrame(acked, 70, "settlementAccessReject") == nullptr);
		assert(stock.store.GetInventory("profile-owner")->at("items") == before);
		assert(stock.store.PendingSettlementCharges().size() == 1);
		const auto confirmed = ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 34.2);
		assert(FindFrame(confirmed, 70, "structureDamage") != nullptr);
		assert(FindFrame(confirmed, 70, "settlementAccessReject") == nullptr);
		assert(stock.store.PendingSettlementCharges().empty());
		const auto after = stock.store.GetInventory("profile-owner")->at("items");
		for (const auto& line : charge.at("items")) {
			const auto itemKey = line.at("itemKey").get<std::string>();
			assert(after.at(itemKey).at("count").get<std::uint64_t>() + line.at("count").get<std::uint64_t>() == before.at(itemKey).at("count").get<std::uint64_t>());
		}
		assert(Send(host, 70, ChargeResult(operationId, true), 34.3, 1034.3).empty());
	}

	void TestItemsStillPresentRefusesWithoutRefund()
	{
		StockedInventory stock{ "charge-items-present", 100 };
		ProtocolHost host{ "Settlement Charge Items Present" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		const auto before = stock.store.GetInventory("profile-owner")->at("items");
		const auto acked = Send(host, 70, ChargeResult(operationId, true), 34.1, 1034.1);
		assert(FindFrame(acked, 70, "structureDamage") == nullptr);
		const auto refused = ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", Json{ { "items", Json::array() } }, 34.2);
		assert(RequirePacket(refused, 70, "settlementAccessReject").at("status") == "chargeFailed");
		assert(FindFrame(refused, 70, "campGrant") == nullptr);
		assert(FindFrame(refused, 70, "structureDamage") == nullptr);
		assert(stock.store.PendingSettlementCharges().empty());
		assert(stock.store.PendingCampGrants("profile-owner").empty());
		assert(stock.store.GetInventory("profile-owner")->at("items") == before);
	}

	void TestUnconfirmedAckExpiresWithoutRefund()
	{
		StockedInventory stock{ "charge-unconfirmed", 100 };
		ProtocolHost host{ "Settlement Charge Unconfirmed" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		const auto before = stock.store.GetInventory("profile-owner")->at("items");
		const auto acked = Send(host, 70, ChargeResult(operationId, true), 34.2, 1034.2);
		assert(FindFrame(acked, 70, "structureDamage") == nullptr);
		assert(FindFrame(acked, 70, "settlementAccessReject") == nullptr);
		const auto early = host.FlushWorkshopPersistence(1093.9, 93.9);
		assert(FindFrame(early, 70, "settlementAccessReject") == nullptr);
		const auto expired = host.FlushWorkshopPersistence(1095.3, 95.3);
		assert(RequirePacket(expired, 70, "settlementAccessReject").at("status") == "chargeFailed");
		assert(FindFrame(expired, 70, "campGrant") == nullptr);
		assert(stock.store.PendingCampGrants("profile-owner").empty());
		assert(stock.store.PendingSettlementCharges().empty());
		assert(stock.store.GetInventory("profile-owner")->at("items") == before);
	}

	void TestDoubleOkRefundsOnceAfterSnapshot()
	{
		StockedInventory stock{ "charge-double-ok", 100 };
		ProtocolHost host{ "Settlement Charge Double Ok" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		const auto hit = ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0);
		assert(!hit.empty());
		const auto hitRevision = RequirePacket(hit, 70, "structureDamage").at("revision").get<std::uint64_t>();
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		assert(RequirePacket(Send(host, 70, ScrapOp(2, hitRevision, piece.objectId), 34.1, 1034.1), 70, "workshopCommit").at("objectId").get<std::uint64_t>() == piece.objectId);
		const auto firstAck = Send(host, 70, ChargeResult(operationId, true), 34.2, 1034.2);
		assert(FindFrame(firstAck, 70, "campGrant") == nullptr);
		assert(FindFrame(firstAck, 70, "settlementAccessReject") == nullptr);
		assert(Send(host, 70, ChargeResult(operationId, true), 34.25, 1034.25).empty());
		const auto confirmed = ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 34.3);
		assert(RequirePacket(confirmed, 70, "settlementAccessReject").at("status") == "chargeFailed");
		assert(FindFrame(confirmed, 70, "campGrant") != nullptr);
		assert(stock.store.PendingCampGrants("profile-owner").size() == 1);
		assert(stock.store.PendingSettlementCharges().empty());
		assert(Send(host, 70, ChargeResult(operationId, true), 34.35, 1034.35).empty());
		assert(stock.store.PendingCampGrants("profile-owner").size() == 1);
	}

	void AssertChargeCommitsOnce(ProtocolHost& a_host, StockedInventory& a_stock, const Json& a_request, double a_monotonic)
	{
		const auto before = a_stock.store.GetInventory("profile-owner")->at("items");
		const auto charge = RequirePacket(Send(a_host, 70, a_request, a_monotonic, a_monotonic + 1000.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		(void)ConfirmChargeConsumed(a_host, 70, a_stock.store, "profile-owner", charge, a_monotonic + 0.05);
		const auto acked = Send(a_host, 70, ChargeResult(operationId, true), a_monotonic + 0.1, a_monotonic + 1000.1);
		assert(FindFrame(acked, 70, "settlementAccessReject") == nullptr);
		const auto after = a_stock.store.GetInventory("profile-owner")->at("items");
		assert(!charge.at("items").empty());
		for (const auto& line : charge.at("items")) {
			const auto itemKey = line.at("itemKey").get<std::string>();
			assert(after.at(itemKey).at("count").get<std::uint64_t>() + line.at("count").get<std::uint64_t>() == before.at(itemKey).at("count").get<std::uint64_t>());
		}
		assert(a_stock.store.PendingSettlementCharges().empty());
		assert(Send(a_host, 70, ChargeResult(operationId, true), a_monotonic + 0.2, a_monotonic + 1000.2).empty());
		assert(a_stock.store.GetInventory("profile-owner")->at("items") == after);
	}

	void TestUpkeepPayCommitsOnce()
	{
		StockedInventory stock{ "charge-upkeep-commit", 1000 };
		ProtocolHost host{ "Settlement Charge Upkeep Commit" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		(void)BuildPiece(host, 70);
		AssertChargeCommitsOnce(host, stock, ZoneRequest("upkeepPay"), 3.0);
	}

	void TestWorkbenchRepairCommitsOnce()
	{
		StockedInventory stock{ "charge-workbench-commit", 1000 };
		ProtocolHost host{ "Settlement Charge Workbench Commit" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		auto policy = Zone9();
		policy.hasWorkbench = true;
		assert(host.RegisterWorkshopBuildZone(policy));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		(void)Send(host, 70, WorkshopTransform(0.0), 2.0, 1002.0);
		(void)ThrowGrenade(host, 70, 0, 3.0, 1003.0);
		AssertChargeCommitsOnce(host, stock, ZoneRequest("workbenchRepair"), 34.0);
	}

	void TestSettlementRecordUpsertsAndEraseIsIdempotent()
	{
		const auto directory = TempDirectory("charge-record");
		{
			DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string(32, '1'); });
			const Json record{ { "operationId", std::uint64_t{ 7 } }, { "profileId", "profile-owner" }, { "items", Json::array() } };
			assert(store.PostSettlementCharge(record));
			assert(store.PostSettlementCharge(record));
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().size() == 1);
			assert(store.EraseSettlementCharge(7));
			assert(store.EraseSettlementCharge(7));
			assert(store.EraseSettlementCharge(99));
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().empty());
		}
		std::filesystem::remove_all(directory);
	}

	void TestPendingChargeBlocksSecondZoneForSameProfile()
	{
		StockedInventory stock{ "charge-cross-zone", 100 };
		ProtocolHost host{ "Settlement Charge Cross Zone" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		auto second = Zone9();
		second.buildZoneId = kSecondZone;
		assert(host.RegisterWorkshopBuildZone(second));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		(void)host.SetSettlementOwner(kSecondZone, "profile-owner", failure);
		assert(failure.empty());
		(void)BuildPiece(host, 70);
		const auto first = RequirePacket(Send(host, 70, ZoneRequest("upkeepPay"), 3.0, 1003.0), 70, "settlementCharge");
		assert(!first.at("items").empty());
		const auto refused = Send(host, 70, Json{ { "type", "upkeepPay" }, { "buildZoneId", kSecondZone } }, 3.1, 1003.1);
		assert(FindFrame(refused, 70, "settlementCharge") == nullptr);
		assert(RequirePacket(refused, 70, "settlementAccessReject").at("status") == "chargeFailed");
	}

	void TestLegacyChargeRecordWithoutBaselineIsErasedOnRestore()
	{
		const auto directory = TempDirectory("charge-legacy");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string(32, '2'); };
		{
			DurableStateFileStore store(path, clock, instanceId);
			const Json legacy{
				{ "operationId", std::uint64_t{ 9 } },
				{ "profileId", "profile-owner" },
				{ "buildZoneId", kZone },
				{ "objectId", std::uint64_t{ 0 } },
				{ "purpose", std::uint64_t{ 0 } },
				{ "items", Json::array({ Json{ { "itemKey", "000731A3" }, { "count", std::uint32_t{ 5 } } } }) },
				{ "settled", false }
			};
			assert(store.PostSettlementCharge(legacy));
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().size() == 1);
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Settlement Charge Legacy" };
			assert(host.EnableInventoryTransactions(gate));
			assert(host.EnableWorkshopPersistence(&gate));
			store.WaitForCommits();
			assert(store.PendingSettlementCharges().empty());
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			assert(store.PendingSettlementCharges().empty());
		}
		std::filesystem::remove_all(directory);
	}

	void TestUnackedChargeStopsBlockingOtherZonesAfterLimit()
	{
		StockedInventory stock{ "charge-unacked-limit", 100 };
		ProtocolHost host{ "Settlement Charge Unacked Limit" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		auto second = Zone9();
		second.buildZoneId = kSecondZone;
		assert(host.RegisterWorkshopBuildZone(second));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		(void)host.SetSettlementOwner(kSecondZone, "profile-owner", failure);
		assert(failure.empty());
		(void)BuildPiece(host, 70);
		assert(FindFrame(Send(host, 70, ZoneRequest("upkeepPay"), 3.0, 1003.0), 70, "settlementCharge") != nullptr);
		const auto blocked = Send(host, 70, Json{ { "type", "upkeepPay" }, { "buildZoneId", kSecondZone } }, 3.1, 1003.1);
		assert(RequirePacket(blocked, 70, "settlementAccessReject").at("status") == "chargeFailed");
		(void)host.FlushWorkshopPersistence(1123.5, 123.5);
		assert(stock.store.PendingSettlementCharges().size() == 1);
		const auto unblocked = Send(host, 70, Json{ { "type", "upkeepPay" }, { "buildZoneId", kSecondZone } }, 123.6, 1123.6);
		assert(RequirePacket(unblocked, 70, "settlementAccessReject").at("status") == "nothingToRepair");
	}

	void TestSettledEntryDoesNotGate()
	{
		const auto directory = TempDirectory("charge-settled-gate");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string(32, '3'); };
		{
			DurableStateFileStore store(path, clock, instanceId);
			const Json settled{
				{ "operationId", std::uint64_t{ 9 } },
				{ "profileId", "profile-owner" },
				{ "buildZoneId", kZone },
				{ "objectId", std::uint64_t{ 0 } },
				{ "purpose", std::uint64_t{ 0 } },
				{ "items", Json::array({ Json{ { "itemKey", "000731A3" }, { "count", std::uint32_t{ 5 } }, { "baseline", std::uint32_t{ 100 } } } }) },
				{ "settled", true }
			};
			assert(store.PostSettlementCharge(settled));
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			AdmitProfile(store, "profile-owner");
			ProtocolHost host{ "Settlement Charge Settled Gate" };
			assert(host.EnableInventoryTransactions(gate));
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			auto second = Zone9();
			second.buildZoneId = kSecondZone;
			assert(host.RegisterWorkshopBuildZone(second));
			JoinInventory(host, gate, 70, "profile-owner", 1.0, 1001.0);
			std::string failure;
			(void)host.SetSettlementOwner(kSecondZone, "profile-owner", failure);
			assert(failure.empty());
			const auto request = Send(host, 70, Json{ { "type", "upkeepPay" }, { "buildZoneId", kSecondZone } }, 1.5, 1001.5);
			assert(RequirePacket(request, 70, "settlementAccessReject").at("status") == "nothingToRepair");
			store.WaitForCommits();
		}
		std::filesystem::remove_all(directory);
	}

	void TestStaleChargeCapRefusesFifthCharge()
	{
		StockedInventory stock{ "charge-stale-cap", 1000 };
		ProtocolHost host{ "Settlement Charge Stale Cap" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		(void)BuildPiece(host, 70);
		for (int i = 0; i < 4; ++i) {
			const double monotonic = 10.0 + 200.0 * i;
			assert(FindFrame(Send(host, 70, ZoneRequest("upkeepPay"), monotonic, monotonic + 1000.0), 70, "settlementCharge") != nullptr);
			(void)host.FlushWorkshopPersistence(monotonic + 1121.0, monotonic + 121.0);
		}
		assert(stock.store.PendingSettlementCharges().size() == 4);
		const auto refused = Send(host, 70, ZoneRequest("upkeepPay"), 810.0, 1810.0);
		assert(RequirePacket(refused, 70, "settlementAccessReject").at("status") == "chargeFailed");
		assert(stock.store.PendingSettlementCharges().size() == 4);
	}

	void TestStaleChargeAckedLaterCommitsOnce()
	{
		StockedInventory stock{ "charge-stale-ack", 100 };
		ProtocolHost host{ "Settlement Charge Stale Ack" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.EnableWorkshopPersistence(&stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		JoinInventory(host, stock.gate, 70, "profile-owner", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
		assert(failure.empty());
		const auto piece = BuildPiece(host, 70);
		assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
		const auto charge = RequirePacket(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge");
		const auto operationId = charge.at("operationId").get<std::uint64_t>();
		const auto before = stock.store.GetInventory("profile-owner")->at("items");
		(void)host.FlushWorkshopPersistence(1155.0, 155.0);
		(void)ConfirmChargeConsumed(host, 70, stock.store, "profile-owner", charge, 155.5);
		const auto acked = Send(host, 70, ChargeResult(operationId, true), 156.0, 1156.0);
		assert(FindFrame(acked, 70, "structureDamage") != nullptr);
		assert(stock.store.PendingSettlementCharges().empty());
		const auto after = stock.store.GetInventory("profile-owner")->at("items");
		for (const auto& line : charge.at("items")) {
			const auto itemKey = line.at("itemKey").get<std::string>();
			assert(after.at(itemKey).at("count").get<std::uint64_t>() + line.at("count").get<std::uint64_t>() == before.at(itemKey).at("count").get<std::uint64_t>());
		}
		assert(Send(host, 70, ChargeResult(operationId, true), 156.5, 1156.5).empty());
		assert(stock.store.GetInventory("profile-owner")->at("items") == after);
	}

	void TestRestoredUnackedChargeStopsGatingAfterLimit()
	{
		const auto directory = TempDirectory("charge-restored-stale");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string(32, '5'); };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			AdmitProfile(store, "profile-owner");
			SeedInventory(store, "profile-owner", Json::array({ SettlerMaterial("000731A3", 100), SettlerMaterial("000731A4", 100), SettlerMaterial("00106D99", 100) }));
			ProtocolHost host{ "Settlement Charge Restored Stale" };
			assert(host.EnableInventoryTransactions(gate));
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			JoinInventory(host, gate, 70, "profile-owner", 1.0, 1001.0);
			std::string failure;
			(void)host.SetSettlementOwner(kZone, "profile-owner", failure);
			assert(failure.empty());
			const auto piece = BuildPiece(host, 70);
			assert(!ThrowGrenade(host, 70, piece.objectId, 3.0, 1003.0).empty());
			assert(FindFrame(Send(host, 70, RepairRequest(piece.objectId), 34.0, 1034.0), 70, "settlementCharge") != nullptr);
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Settlement Charge Restored Stale" };
			assert(host.EnableInventoryTransactions(gate));
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			auto second = Zone9();
			second.buildZoneId = kSecondZone;
			assert(host.RegisterWorkshopBuildZone(second));
			RearmStartingGrantFixture(store, "profile-owner");
			JoinInventory(host, gate, 71, "profile-owner", 35.0, 1035.0);
			std::string failure;
			(void)host.SetSettlementOwner(kSecondZone, "profile-owner", failure);
			assert(failure.empty());
			const auto request = Json{ { "type", "upkeepPay" }, { "buildZoneId", kSecondZone } };
			assert(RequirePacket(Send(host, 71, request, 35.5, 1035.5), 71, "settlementAccessReject").at("status") == "chargeFailed");
			(void)host.FlushWorkshopPersistence(1036.0, 36.0);
			assert(RequirePacket(Send(host, 71, request, 36.1, 1036.1), 71, "settlementAccessReject").at("status") == "chargeFailed");
			(void)host.FlushWorkshopPersistence(1157.0, 157.0);
			assert(store.PendingSettlementCharges().size() == 1);
			assert(RequirePacket(Send(host, 71, request, 157.1, 1157.1), 71, "settlementAccessReject").at("status") == "nothingToRepair");
			store.WaitForCommits();
		}
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	try {
		TestFailedCommitRefundsOnce();
		TestRefundStoreFailureHoldsAcknowledgedChargeUntilRecovered();
		TestUnackedChargeExpiresAfterSixtySecondsOffline();
		TestPendingChargeSurvivesRestartAndResendsToOwner();
		TestRepairChargeCapsAtOneUnit();
		TestUpkeepWithNoObjectsRefuses();
		TestWorkbenchRepairRefusesZoneWithoutWorkbench();
		TestStructureRepairOwnerReachIsEnforced();
		TestStructureRepairBuilderReachAndAuthorization();
		TestMovedAwayBeforeAckRefundsCharge();
		TestOwnerChangeDoesNotBlockNewOwnerAndRefundsOldOwner();
		TestDeliveredChargeSurvivesOfflineWindow();
		TestSnapshotBeforeResultCommitsOnce();
		TestResultBeforeSnapshotCommitsOnce();
		TestItemsStillPresentRefusesWithoutRefund();
		TestUnconfirmedAckExpiresWithoutRefund();
		TestDoubleOkRefundsOnceAfterSnapshot();
		TestUpkeepPayCommitsOnce();
		TestWorkbenchRepairCommitsOnce();
		TestSettlementRecordUpsertsAndEraseIsIdempotent();
		TestPendingChargeBlocksSecondZoneForSameProfile();
		TestLegacyChargeRecordWithoutBaselineIsErasedOnRestore();
		TestUnackedChargeStopsBlockingOtherZonesAfterLimit();
		TestSettledEntryDoesNotGate();
		TestStaleChargeCapRefusesFifthCharge();
		TestStaleChargeAckedLaterCommitsOnce();
		TestRestoredUnackedChargeStopsGatingAfterLimit();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "ServerSettlementChargeTests unexpected exception: %s\n", error.what());
		return 1;
	}
	return 0;
}
