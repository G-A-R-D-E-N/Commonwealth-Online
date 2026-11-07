#include "DurableStateFileStore.h"
#include "F4TPlayerStateValidation.h"
#include "F4TWorkshopRaids.h"
#include "PersistenceGate.h"
#include "ServerFactionTestSupport.h"
#include "ServerHeldWeaponTestSupport.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace FactionTestSupport;

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::Workshop::BuildZonePolicy;
	using F4T::Workshop::FormKey;
	using F4T::WorkshopRaids::kFragGrenadeSourceFormId;
	using F4T::WorkshopRaids::kMiniNukeSourceFormId;
	using F4T::WorkshopRaids::kSidearmWeaponFormId;

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json Decode(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{
			reinterpret_cast<const char*>(a_frame.payload.data()),
			a_frame.payload.size()
		});
	}

	const OutboundFrame* Find(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() && frame.payload.front() == '{' &&
				Decode(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	BuildZonePolicy ZoneAt(std::uint64_t a_zoneId)
	{
		return BuildZonePolicy{ a_zoneId, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 10.0F, 2.0F, 8 };
	}

	BuildZonePolicy Zone9()
	{
		return ZoneAt(9);
	}

	std::uint32_t Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const char* a_profile, double a_monotonic)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_monotonic);
		assert(!a_host.HandleMessage(a_connectionId, Encode(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonic + 0.1, a_monotonic + 1000.1).empty());
		a_host.BindSessionProfile(a_connectionId, a_profile);
		return Decode(welcome.front()).at("playerId").get<std::uint32_t>();
	}

	std::vector<OutboundFrame> Send(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		const Json& a_packet,
		double a_monotonic)
	{
		return a_host.HandleMessage(a_connectionId, Encode(a_packet), a_monotonic, a_monotonic + 1000.0);
	}

	Json TransformAt(double a_x)
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

	Json Place(std::uint64_t a_operationId, std::uint64_t a_revision, std::uint64_t a_zoneId, double a_x)
	{
		return Json{
			{ "type", "workshopOp" },
			{ "operationId", a_operationId },
			{ "expectedRevision", a_revision },
			{ "buildZoneId", a_zoneId },
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

	Json Hit(std::uint64_t a_zoneId, std::uint64_t a_objectId, const char* a_explosive)
	{
		return Json{ { "type", "structureHit" }, { "buildZoneId", a_zoneId }, { "objectId", a_objectId }, { "explosive", a_explosive } };
	}

	Json WeaponHit(std::uint64_t a_zoneId, std::uint64_t a_objectId, std::uint64_t a_weaponKind)
	{
		return Json{ { "type", "structureHit" }, { "buildZoneId", a_zoneId }, { "objectId", a_objectId }, { "weaponKind", a_weaponKind } };
	}

	Json Equipped(std::uint32_t a_rightHandFormId, std::uint32_t a_throwableFormId)
	{
		Json items = Json::array();
		if (a_rightHandFormId != 0) {
			items.push_back(Json{ { "slot", F4T::PlayerStateValidation::kRightHandSlot }, { "formId", std::format("{:08X}", a_rightHandFormId) } });
		}
		if (a_throwableFormId != 0) {
			items.push_back(Json{ { "slot", F4T::PlayerStateValidation::kThrowableSlot }, { "formId", std::format("{:08X}", a_throwableFormId) } });
		}
		return Json{ { "type", "playerState" }, { "equippedItems", items } };
	}

	void TestUnreadableRootStaysDisabled()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-root-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const auto zones = directory / "workshop-zones.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			std::ofstream output(zones, std::ios::binary);
			output << R"({"version":1,"zones":[{"buildZoneId":4,"space":{"plugin":"Fallout4.esm","localFormId":1},"centerX":0,"centerY":0,"centerZ":0,"radius":50,"maxScale":2,"maxObjects":8}]})";
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveWorkshop(Json::array());
			store.WaitForCommits();
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Unreadable Root" };
			assert(host.ConfigureWorkshop(&gate, zones) == 0);
			assert(!host.EnableWorkshopPersistence(&gate));
			Join(host, 90, "profile-g", 1.0);
			assert(Decode(*Find(Send(host, 90, Place(1, 0, 4, 1.0), 2.0), 90, "workshopReject")).at("status") == "unknownBuildZone");
		}
		std::filesystem::remove_all(directory);
	}

	void TestStructureHitNeedsReachAndCooldown()
	{
		ProtocolHost host{ "Workshop Structure Hit" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		Join(host, 71, "profile-outsider", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		assert(Find(Send(host, 70, TransformAt(0.0), 2.0), 70, "workshopSnapshot") != nullptr);
		Send(host, 70, Equipped(kSidearmWeaponFormId, kMiniNukeSourceFormId), 2.0);
		assert(Send(host, 71, Hit(9, 0, "mininuke"), 3.0).empty());
		assert(!Send(host, 70, Hit(9, 0, "mininuke"), 3.0).empty());
		assert(Send(host, 70, Hit(9, 0, "mininuke"), 3.5).empty());
		assert(!Send(host, 70, Hit(9, 0, "mininuke"), 4.5).empty());
	}

	void TestStructureDamageBudgetCapsEveryKind()
	{
		const auto directory = HeldWeaponTestSupport::MakeWorkshopDirectory("co-workshop-damage-budget-");
		ProtocolHost host{ "Workshop Damage Budget" };
		assert(host.ConfigureWorkshop(nullptr, directory / "workshop-zones.json") == 0);
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		Send(host, 70, HeldWeaponTestSupport::PluginListPacket(), 1.5);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		assert(Find(Send(host, 70, TransformAt(0.0), 2.0), 70, "workshopSnapshot") != nullptr);
		Send(host, 70, Equipped(kSidearmWeaponFormId, kMiniNukeSourceFormId), 2.0);
		assert(!Send(host, 70, Hit(9, 0, "mininuke"), 3.0).empty());
		assert(Send(host, 70, WeaponHit(9, 0, 0), 3.0).empty());
		assert(Send(host, 70, WeaponHit(9, 0, 1), 3.0).empty());
		assert(Send(host, 70, WeaponHit(9, 0, 2), 3.0).empty());
		assert(Send(host, 70, WeaponHit(9, 0, 3), 3.0).empty());
		const Json packets[] = { WeaponHit(9, 0, 1), Hit(9, 0, "mininuke") };
		const std::uint64_t damages[] = { 15, 600 };
		std::uint64_t accepted{};
		for (int step = 1; step <= 16; ++step) {
			for (std::size_t kind = 0; kind < 2; ++kind) {
				if (Find(Send(host, 70, packets[kind], 3.0 + step * 0.125), 70, "structureDamage")) {
					accepted += damages[kind];
				}
			}
		}
		assert(accepted <= 1200);
		std::filesystem::remove_all(directory);
	}

	void TestStructureDamageBudgetRefillsOverTime()
	{
		const auto directory = HeldWeaponTestSupport::MakeWorkshopDirectory("co-workshop-damage-refill-");
		ProtocolHost host{ "Workshop Damage Refill" };
		assert(host.ConfigureWorkshop(nullptr, directory / "workshop-zones.json") == 0);
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		Send(host, 70, HeldWeaponTestSupport::PluginListPacket(), 1.5);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		assert(Find(Send(host, 70, TransformAt(0.0), 2.0), 70, "workshopSnapshot") != nullptr);
		Send(host, 70, Equipped(kSidearmWeaponFormId, kMiniNukeSourceFormId), 2.0);
		assert(!Send(host, 70, Hit(9, 0, "mininuke"), 3.0).empty());
		assert(!Send(host, 70, WeaponHit(9, 0, 1), 3.125).empty());
		assert(!Send(host, 70, Hit(9, 0, "mininuke"), 4.25).empty());
		std::filesystem::remove_all(directory);
	}

	void TestWeaponHitsUseMaterialAndPerKindInterval()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-weapons-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto zones = directory / "workshop-zones.json";
		{
			std::ofstream output(zones, std::ios::binary);
			output << R"({"version":1,"zones":[]})";
		}
		{
			std::ofstream output(directory / "workshop-pieces.json", std::ios::binary);
			output << R"({"pieces":[{"plugin":"Fallout4.esm","localFormId":4660,"editorId":"TestScrapPiece","tier":"scrap"}]})";
		}
		HeldWeaponTestSupport::WriteWeaponClasses(directory);
		ProtocolHost host{ "Workshop Weapon Hits" };
		assert(host.ConfigureWorkshop(nullptr, zones) == 0);
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		assert(Find(Send(host, 70, TransformAt(0.0), 2.0), 70, "workshopSnapshot") != nullptr);
		Send(host, 70, Json{ { "type", "playerState" }, { "equippedItems", Json::array() } }, 2.0);
		const auto placed = Send(host, 70, Place(1, 0, 9, 1.0), 2.5);
		const auto objectId = Decode(*Find(placed, 70, "workshopCommit")).at("objectId").get<std::uint64_t>();
		const auto melee = Send(host, 70, WeaponHit(9, objectId, 0), 3.0);
		assert(Decode(*Find(melee, 70, "structureDamage")).at("health").get<double>() == 595.0);
		assert(Send(host, 70, WeaponHit(9, objectId, 0), 3.2).empty());
		Send(host, 70, HeldWeaponTestSupport::PluginListPacket(), 3.25);
		Send(host, 70, Equipped(kSidearmWeaponFormId, 0), 3.3);
		assert(!Send(host, 70, WeaponHit(9, objectId, 1), 3.4).empty());
		assert(Send(host, 70, WeaponHit(9, objectId, 0), 3.5).empty());
		assert(Send(host, 70, WeaponHit(9, objectId, 5), 4.0).empty());
		std::filesystem::remove_all(directory);
	}

	Json Material(const char* a_localFormId, std::uint64_t a_count)
	{
		return Json{ { "plugin", "fallout4.esm" }, { "localFormId", a_localFormId }, { "count", a_count } };
	}

	void TestStructureRepairCooldown()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-repair-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			AdmitProfile(store, "profile-owner");
			SeedInventory(store, "profile-owner", Json::array({ Material("000731A3", 100), Material("000731A4", 100), Material("00106D99", 100) }));
			ProtocolHost host{ "Workshop Repair Cooldown" };
			assert(host.EnableInventoryTransactions(gate));
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			FactionTestSupport::Join(host, 70, "profile-owner", 1.0, 1001.0, InventoryCapabilities());
			assert(VerifyStartingGrant(host, gate, 70, "profile-owner", 1.05, 1001.05));
			std::string failure;
			(void)host.SetSettlementOwner(9, "profile-owner", failure);
			assert(Find(Send(host, 70, TransformAt(0.0), 2.0), 70, "workshopSnapshot") != nullptr);
			const auto placed = Send(host, 70, Place(1, 0, 9, 1.0), 2.5);
			const auto objectId = Decode(*Find(placed, 70, "workshopCommit")).at("objectId").get<std::uint64_t>();
			Send(host, 70, Equipped(0, kFragGrenadeSourceFormId), 2.9);
			assert(!Send(host, 70, Hit(9, objectId, "grenade"), 3.0).empty());
			const auto repair = [&](double a_monotonic) {
				return Send(host, 70, Json{ { "type", "structureRepair" }, { "buildZoneId", 9 }, { "objectId", objectId } }, a_monotonic);
			};
			const auto acknowledge = [&](std::uint64_t a_operationId, double a_monotonic) {
				return Send(host, 70, Json{ { "type", "settlementChargeResult" }, { "operationId", a_operationId }, { "ok", true } }, a_monotonic);
			};
			const auto first = repair(34.0);
			const auto firstCharge = Find(first, 70, "settlementCharge");
			assert(firstCharge != nullptr);
			const auto firstOperation = Decode(*firstCharge).at("operationId").get<std::uint64_t>();
			(void)ConfirmChargeConsumed(host, 70, store, "profile-owner", Decode(*firstCharge), 34.05);
			assert(Find(acknowledge(firstOperation, 34.1), 70, "structureDamage") != nullptr);
			assert(acknowledge(firstOperation, 34.2).empty());
			Send(host, 70, Equipped(0, kFragGrenadeSourceFormId), 35.0);
			assert(!Send(host, 70, Hit(9, objectId, "grenade"), 35.5).empty());
			assert(repair(35.6).empty());
			const auto second = repair(40.0);
			const auto secondCharge = Find(second, 70, "settlementCharge");
			assert(secondCharge != nullptr);
			const auto secondOperation = Decode(*secondCharge).at("operationId").get<std::uint64_t>();
			const auto flushed = host.FlushWorkshopPersistence(1000.0, 46.0);
			const auto resent = Find(flushed, 70, "settlementCharge");
			assert(resent != nullptr && Decode(*resent).at("operationId").get<std::uint64_t>() == secondOperation);
			(void)ConfirmChargeConsumed(host, 70, store, "profile-owner", Decode(*secondCharge), 46.45);
			assert(Find(acknowledge(secondOperation, 66.0), 70, "structureDamage") != nullptr);
			store.WaitForCommits();
		}
		std::filesystem::remove_all(directory);
	}

	void TestStructureRepairWithoutMaterialsRefuses()
	{
		ProtocolHost host{ "Workshop Repair Materials" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(Find(Send(host, 70, TransformAt(0.0), 2.0), 70, "workshopSnapshot") != nullptr);
		const auto placed = Send(host, 70, Place(1, 0, 9, 1.0), 2.5);
		const auto objectId = Decode(*Find(placed, 70, "workshopCommit")).at("objectId").get<std::uint64_t>();
		Send(host, 70, Equipped(0, kFragGrenadeSourceFormId), 2.0);
		assert(!Send(host, 70, Hit(9, objectId, "grenade"), 3.0).empty());
		const auto refused = Send(host, 70, Json{ { "type", "structureRepair" }, { "buildZoneId", 9 }, { "objectId", objectId } }, 34.0);
		assert(Find(refused, 70, "settlementCharge") == nullptr);
		assert(Decode(*Find(refused, 70, "settlementAccessReject")).at("status") == "insufficientMaterials");
	}

	void TestStructureRepairRefusesNonOwnerAndUnderAttack()
	{
		ProtocolHost host{ "Workshop Repair Refusals" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		Join(host, 71, "profile-raider", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(Find(Send(host, 70, TransformAt(0.0), 2.0), 70, "workshopSnapshot") != nullptr);
		const auto placed = Send(host, 70, Place(1, 0, 9, 1.0), 2.5);
		const auto objectId = Decode(*Find(placed, 70, "workshopCommit")).at("objectId").get<std::uint64_t>();
		const auto repair = [&](std::uint32_t a_connectionId, double a_monotonic) {
			return Send(host, a_connectionId, Json{ { "type", "structureRepair" }, { "buildZoneId", 9 }, { "objectId", objectId } }, a_monotonic);
		};
		const auto notOwner = repair(71, 3.0);
		assert(Decode(*Find(notOwner, 71, "settlementAccessReject")).at("status") == "notAllowed");
		assert(Find(Send(host, 71, TransformAt(0.0), 3.5), 71, "workshopSnapshot") != nullptr);
		Send(host, 71, Equipped(0, kFragGrenadeSourceFormId), 3.9);
		assert(!Send(host, 71, Hit(9, objectId, "grenade"), 4.0).empty());
		const auto underAttack = repair(70, 4.5);
		assert(Decode(*Find(underAttack, 70, "settlementAccessReject")).at("status") == "underAttack");
		assert(Find(underAttack, 70, "settlementCharge") == nullptr);
	}

	void TestGuardsDieWithWorkbenchAndRecover()
	{
		StockedInventory stock{ "guards", 100 };
		ProtocolHost host{ "Workshop Guard Recover" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		FactionTestSupport::Join(host, 80, "profile-owner", 1.0, 1001.0, InventoryCapabilities());
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(Find(Send(host, 80, TransformAt(0.0), 2.0), 80, "workshopSnapshot") != nullptr);
		Send(host, 80, Equipped(kSidearmWeaponFormId, kMiniNukeSourceFormId), 2.0);
		const auto recruited = RecruitAcked(host, stock, 80, 9, 2.5);
		const auto settlerId = Decode(*Find(recruited, 80, "settlerState")).at("settlerId").get<std::uint64_t>();
		assert(!Send(host, 80, Json{ { "type", "settlerAssign" }, { "buildZoneId", 9 }, { "settlerId", settlerId }, { "assignment", "guard" } }, 2.6).empty());
		const auto recover = [&](double a_monotonic) {
			return Send(host, 80, Json{ { "type", "settlerRecover" }, { "buildZoneId", 9 }, { "settlerId", settlerId } }, a_monotonic);
		};
		assert(recover(2.7).empty());
		assert(!Send(host, 80, Hit(9, 0, "mininuke"), 3.0).empty());
		assert(!Send(host, 80, Hit(9, 0, "mininuke"), 4.5).empty());
		assert(!Send(host, 80, Hit(9, 0, "mininuke"), 6.0).empty());
		const auto broken = Send(host, 80, Hit(9, 0, "mininuke"), 7.5);
		assert(!broken.empty());
		const auto notice = Decode(*Find(broken, 80, "settlerState"));
		assert(notice.at("dead").get<bool>());
		assert(notice.at("settlerId").get<std::uint64_t>() == settlerId);
		const auto revived = recover(9.0);
		assert(Decode(*Find(revived, 80, "settlerState")).at("assignment") == "idle");
		const auto flushed = host.FlushWorkshopPersistence(1009.0);
		assert(Decode(*Find(flushed, 80, "defenderState")).at("defenseRating").get<double>() == 0.0);
	}

	void TestAttackNoticeReachesOwnerAndBuilderOutsideZone()
	{
		ProtocolHost host{ "Workshop Attack Notice" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 90, "profile-owner", 1.0);
		Join(host, 91, "profile-builder", 1.0);
		Join(host, 92, "profile-raider", 1.0);
		Join(host, 93, "profile-stranger", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		(void)Send(host, 90, Json{ { "type", "settlementAccessUpdate" }, { "buildZoneId", 9 }, { "op", "grant" }, { "targetProfileId", "profile-builder" } }, 1.5);
		(void)Send(host, 90, TransformAt(5000.0), 2.0);
		(void)Send(host, 91, TransformAt(5000.0), 2.0);
		(void)Send(host, 93, TransformAt(5000.0), 2.0);
		(void)Send(host, 92, TransformAt(0.0), 2.0);
		(void)host.HandleMessage(92, Encode(Equipped(0, kMiniNukeSourceFormId)), 2.9, 2000000.0);
		const auto hit = host.HandleMessage(92, Encode(Hit(9, 0, "mininuke")), 3.0, 2000000.0);
		assert(Find(hit, 92, "raidHitRefused") == nullptr);
		const auto* ownerNotice = Find(hit, 90, "zoneAttackState");
		assert(ownerNotice != nullptr && Decode(*ownerNotice).at("underAttack").get<bool>());
		assert(Find(hit, 91, "zoneAttackState") != nullptr);
		assert(Find(hit, 92, "zoneAttackState") != nullptr);
		assert(Find(hit, 93, "zoneAttackState") == nullptr);
		const auto cleared = host.FlushWorkshopPersistence(2000.0, 1000.0);
		const auto* ownerClear = Find(cleared, 90, "zoneAttackState");
		assert(ownerClear != nullptr && !Decode(*ownerClear).at("underAttack").get<bool>());
	}

	void TestTurretFiresOnlyAtRaidLegalAttacker()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-turret-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const auto zones = directory / "workshop-zones.json";
		{
			std::ofstream output(zones, std::ios::binary);
			output << R"({"version":1,"zones":[{"buildZoneId":9,"space":{"plugin":"Fallout4.esm","localFormId":1},"centerX":0,"centerY":0,"centerZ":0,"radius":10,"maxScale":2,"maxObjects":8}]})";
		}
		{
			std::ofstream output(directory / "workshop-pieces.json", std::ios::binary);
			output << R"({"pieces":[{"plugin":"Fallout4.esm","localFormId":4660,"tier":"wood","power":{"outputWatts":0,"drawWatts":0,"needsPower":false,"deviceClass":"turret"}}]})";
		}
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Turret Fire" };
			host.SetProfileFirstSeenLookup([](std::string_view a_profileId) -> std::optional<double> {
				if (a_profileId == "profile-newcomer") {
					return 1999000.0;
				}
				return std::nullopt;
			});
			assert(host.ConfigureWorkshop(&gate, zones) == 1);
			Join(host, 90, "profile-owner", 1.0);
			Join(host, 91, "profile-builder", 1.0);
			Join(host, 93, "profile-newcomer", 1.0);
			host.BindSessionProfileVerified(93, "profile-newcomer", false, "");
			const auto raiderId = Join(host, 92, "profile-raider", 1.0);
			std::string failure;
			(void)host.SetSettlementOwner(9, "profile-owner", failure);
			assert(failure.empty());
			(void)Send(host, 90, Json{ { "type", "settlementAccessUpdate" }, { "buildZoneId", 9 }, { "op", "grant" }, { "targetProfileId", "profile-builder" } }, 1.5);
			(void)Send(host, 90, TransformAt(6.0), 2.0);
			(void)Send(host, 91, TransformAt(5000.0), 2.0);
			(void)Send(host, 93, TransformAt(5.0), 2.0);
			(void)Send(host, 92, TransformAt(0.0), 2.0);
			assert(Find(Send(host, 90, Place(1, 0, 9, 6.0), 2.5), 90, "workshopCommit") != nullptr);
			(void)host.HandleMessage(92, Encode(Equipped(0, kMiniNukeSourceFormId)), 2.9, 2000000.0);
			(void)host.HandleMessage(92, Encode(Hit(9, 0, "mininuke")), 3.0, 2000000.0);
			const auto frames = host.FlushWorkshopPersistence(2000000.0, 3.5);
			std::size_t fired{};
			for (const auto& frame : frames) {
				if (frame.payload.empty() || frame.payload.front() != '{') {
					continue;
				}
				const auto packet = Decode(frame);
				if (packet.value("type", std::string{}) == "defenderEvent" && packet.value("kind", std::string{}) == "turretFire") {
					assert(packet.at("targetPlayerId").get<std::uint32_t>() == raiderId);
					++fired;
				}
			}
			assert(fired > 0);
			assert(Find(frames, 92, "defenderEvent") != nullptr);
		}
		std::filesystem::remove_all(directory);
	}

	F4T::WorkshopRaids::RaidConfig WindowedRaids()
	{
		F4T::WorkshopRaids::RaidConfig raidConfig;
		raidConfig.mode = F4T::WorkshopRaids::RaidMode::Windowed;
		return raidConfig;
	}

	Json RefusalOf(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		const auto* frame = Find(a_frames, a_connectionId, "settlementRefused");
		assert(frame != nullptr);
		return Decode(*frame);
	}

	void TestSettlementWindowWaitsForRaidDeclaration()
	{
		ProtocolHost host{ "Workshop Raid Window" };
		F4T::WorkshopRaids::RaidConfig raidConfig;
		raidConfig.mode = F4T::WorkshopRaids::RaidMode::Windowed;
		host.SetWorkshopRaidConfig(raidConfig);
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 80, "profile-owner", 1.0);
		Join(host, 81, "profile-raider", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		const auto window = Json{ { "type", "settlementWindowSet" }, { "buildZoneId", 9 }, { "windowStartHour", 0.0 }, { "windowDurationHours", 2.0 }, { "timezoneOffsetHours", 0.0 } };
		assert(!Send(host, 80, window, 2.0).empty());
		assert(!Send(host, 81, Json{ { "type", "raidDeclare" }, { "buildZoneId", 9 } }, 3.0).empty());
		assert(Send(host, 80, window, 4.0).empty());
		assert(!Send(host, 80, window, 10000.0).empty());
	}

	void TestWindowedDeclarationCoversFactionAtDeclareTime()
	{
		ProtocolHost host{ "Workshop Raid Faction" };
		assert(host.EnableFactions(nullptr));
		F4T::WorkshopRaids::RaidConfig raidConfig;
		raidConfig.mode = F4T::WorkshopRaids::RaidMode::Windowed;
		host.SetWorkshopRaidConfig(raidConfig);
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 80, "profile-owner", 1.0);
		Join(host, 82, "profile-leader", 1.0);
		const auto declarer = FactionTestSupport::Join(host, 81, "profile-declarer", 1.0, 1001.0);
		const auto late = FactionTestSupport::Join(host, 83, "profile-late", 1.0, 1001.0);
		Join(host, 84, "profile-outsider", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		FormFaction(host, 82, 81, declarer.playerId, "Raiders");
		const auto window = Json{ { "type", "settlementWindowSet" }, { "buildZoneId", 9 }, { "windowStartHour", 0.0 }, { "windowDurationHours", 2.0 }, { "timezoneOffsetHours", 0.0 } };
		assert(!Send(host, 80, window, 2.0).empty());
		assert(!Send(host, 81, Json{ { "type", "raidDeclare" }, { "buildZoneId", 9 } }, 3.0).empty());
		const auto invite = Send(host, 82, Invite(late.playerId), 4.0);
		AssertResult(invite, 82, "invite", true);
		AssertResult(Send(host, 83, Respond(InviteIdFrom(invite, 83), true), 4.1), 83, "inviteResponse", true);
		AssertResult(Send(host, 81, Leave(), 5.0), 81, "leave", true);
		Send(host, 81, TransformAt(0.0), 1899.0);
		Send(host, 82, TransformAt(0.0), 1899.0);
		Send(host, 83, TransformAt(0.0), 1899.0);
		Send(host, 84, TransformAt(0.0), 1899.0);
		Send(host, 81, Equipped(0, kMiniNukeSourceFormId), 1899.0);
		Send(host, 82, Equipped(0, kMiniNukeSourceFormId), 1899.0);
		Send(host, 83, Equipped(0, kMiniNukeSourceFormId), 1899.0);
		Send(host, 84, Equipped(0, kMiniNukeSourceFormId), 1899.0);
		assert(Find(Send(host, 84, Hit(9, 0, "mininuke"), 1900.0), 84, "raidHitRefused") != nullptr);
		assert(!Send(host, 81, Hit(9, 0, "mininuke"), 1900.0).empty());
		assert(!Send(host, 82, Hit(9, 0, "mininuke"), 1900.0).empty());
		assert(!Send(host, 83, Hit(9, 0, "mininuke"), 1900.0).empty());
	}

	void TestDeclarationCapIsSharedByFactionMembers()
	{
		ProtocolHost host{ "Workshop Declaration Cap Faction" };
		assert(host.EnableFactions(nullptr));
		host.SetWorkshopRaidConfig(WindowedRaids());
		for (const std::uint64_t zoneId : { 9, 10, 11, 12 }) {
			assert(host.RegisterWorkshopBuildZone(ZoneAt(zoneId)));
		}
		Join(host, 80, "profile-owner-a", 1.0);
		Join(host, 86, "profile-owner-b", 1.0);
		Join(host, 82, "profile-leader", 1.0);
		const auto declarer = FactionTestSupport::Join(host, 81, "profile-declarer", 1.0, 1001.0);
		const auto late = FactionTestSupport::Join(host, 83, "profile-late", 1.0, 1001.0);
		const auto second = FactionTestSupport::Join(host, 85, "profile-second", 1.0, 1001.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner-a", failure);
		(void)host.SetSettlementOwner(10, "profile-owner-a", failure);
		(void)host.SetSettlementOwner(11, "profile-owner-b", failure);
		(void)host.SetSettlementOwner(12, "profile-owner-b", failure);
		assert(failure.empty());
		FormFaction(host, 82, 81, declarer.playerId, "Raiders");
		const auto admit = [&](std::uint32_t a_connectionId, std::uint32_t a_playerId, double a_monotonic) {
			const auto invite = Send(host, 82, Invite(a_playerId), a_monotonic);
			AssertResult(invite, 82, "invite", true);
			AssertResult(Send(host, a_connectionId, Respond(InviteIdFrom(invite, a_connectionId), true), a_monotonic + 0.1), a_connectionId, "inviteResponse", true);
		};
		admit(83, late.playerId, 2.5);
		admit(85, second.playerId, 2.7);
		const auto declare = [&](std::uint32_t a_connectionId, std::uint64_t a_zoneId, double a_monotonic) {
			return Send(host, a_connectionId, Json{ { "type", "raidDeclare" }, { "buildZoneId", a_zoneId } }, a_monotonic);
		};
		assert(Find(declare(81, 9, 1100.0), 81, "raidDeclareState") != nullptr);
		assert(Find(declare(82, 10, 1200.0), 82, "raidDeclareState") != nullptr);
		assert(Find(declare(83, 11, 1300.0), 83, "raidDeclareState") != nullptr);
		assert(RefusalOf(declare(85, 12, 1400.0), 85).at("reason") == "dailyLimit");
	}

	void TestDeclarationCapIsPerProfileWithoutFaction()
	{
		ProtocolHost host{ "Workshop Declaration Cap Solo" };
		host.SetWorkshopRaidConfig(WindowedRaids());
		for (const std::uint64_t zoneId : { 9, 10, 11, 12 }) {
			assert(host.RegisterWorkshopBuildZone(ZoneAt(zoneId)));
		}
		Join(host, 80, "profile-owner-a", 1.0);
		Join(host, 84, "profile-solo", 1.0);
		Join(host, 85, "profile-other", 1.0);
		Join(host, 86, "profile-owner-b", 1.0);
		std::string failure;
		for (const std::uint64_t zoneId : { 9, 10, 11, 12 }) {
			const std::uint32_t owner = zoneId < 11 ? 80 : 86;
			(void)host.SetSettlementOwner(zoneId, owner == 80 ? "profile-owner-a" : "profile-owner-b", failure);
			assert(failure.empty());
			assert(!Send(host, owner, Json{ { "type", "settlementWindowSet" }, { "buildZoneId", zoneId }, { "windowStartHour", static_cast<double>(zoneId - 9) * 2.0 }, { "windowDurationHours", 1.0 }, { "timezoneOffsetHours", 0.0 } }, 2.0).empty());
		}
		const auto declare = [&](std::uint32_t a_connectionId, std::uint64_t a_zoneId, double a_monotonic) {
			return Send(host, a_connectionId, Json{ { "type", "raidDeclare" }, { "buildZoneId", a_zoneId } }, a_monotonic);
		};
		assert(Find(declare(84, 9, 100.0), 84, "raidDeclareState") != nullptr);
		assert(Find(declare(84, 10, 6000.0), 84, "raidDeclareState") != nullptr);
		assert(Find(declare(84, 11, 12000.0), 84, "raidDeclareState") != nullptr);
		assert(RefusalOf(declare(84, 12, 18000.0), 84).at("reason") == "dailyLimit");
		assert(Find(declare(85, 12, 18000.0), 85, "raidDeclareState") != nullptr);
	}

	void TestRegistryFirstSeenLiftsNewPlayerProtection()
	{
		ProtocolHost host{ "Workshop First Seen Registry" };
		host.SetProfileFirstSeenLookup([](std::string_view a_profileId) -> std::optional<double> {
			if (a_profileId == "profile-veteran") {
				return 1000000.0;
			}
			return std::nullopt;
		});
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 70, "profile-owner", 1.0);
		Join(host, 71, "profile-newcomer", 1.0);
		host.BindSessionProfileVerified(71, "profile-newcomer", false, "");
		Join(host, 72, "profile-veteran", 1.0);
		host.BindSessionProfileVerified(72, "profile-veteran", false, "");
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		Send(host, 71, TransformAt(0.0), 2.0);
		Send(host, 72, TransformAt(0.0), 2.0);
		Send(host, 71, Equipped(0, kMiniNukeSourceFormId), 2.0);
		Send(host, 72, Equipped(0, kMiniNukeSourceFormId), 2.0);
		const auto refusedHit = host.HandleMessage(71, Encode(Hit(9, 0, "mininuke")), 3.0, 2000000.0);
		const auto* refused = Find(refusedHit, 71, "raidHitRefused");
		assert(refused != nullptr && Decode(*refused).at("reason") == "newPlayerProtected");
		assert(Decode(*refused).contains("secondsLeft"));
		assert(host.HandleMessage(71, Encode(Hit(9, 0, "mininuke")), 4.0, 2000000.0).empty());
		assert(Find(host.HandleMessage(71, Encode(Hit(9, 0, "mininuke")), 14.0, 2000000.0), 71, "raidHitRefused") != nullptr);
		assert(!host.HandleMessage(72, Encode(Hit(9, 0, "mininuke")), 3.0, 2000000.0).empty());
	}

	void TestNewPlayerRaidProtectionIsPushedOnce()
	{
		ProtocolHost host{ "Workshop Player Protection Push" };
		host.SetProfileFirstSeenLookup([](std::string_view a_profileId) -> std::optional<double> {
			if (a_profileId == "profile-newcomer") {
				return 1999000.0;
			}
			return std::nullopt;
		});
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 71, "profile-newcomer", 1.0);
		host.BindSessionProfileVerified(71, "profile-newcomer", false, "");
		const auto first = host.PollPlayerRaidProtection(2000000.0);
		const auto* push = Find(first, 71, "playerRaidProtection");
		assert(push != nullptr && Decode(*push).at("secondsLeft") == 258200);
		assert(host.PollPlayerRaidProtection(2000000.0).empty());
	}

	void TestSettlementProtectedRefusalCarriesSecondsLeft()
	{
		ProtocolHost host{ "Workshop Settlement Protected Refusal" };
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		FactionTestSupport::Join(host, 70, "profile-owner", 1.0, 2000000.0);
		FactionTestSupport::Join(host, 71, "profile-raider", 1.0, 2000000.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure, 2000000.0, std::uint64_t{ 2000600 });
		assert(failure.empty());
		(void)host.HandleMessage(71, Encode(TransformAt(0.0)), 2.0, 2000000.0);
		(void)host.HandleMessage(71, Encode(Equipped(0, kMiniNukeSourceFormId)), 2.0, 2000000.0);
		const auto refused = host.HandleMessage(71, Encode(Hit(9, 0, "mininuke")), 4.0, 2000100.0);
		const auto* frame = Find(refused, 71, "raidHitRefused");
		assert(frame != nullptr && Decode(*frame).at("reason") == "settlementProtected");
		assert(Decode(*frame).at("secondsLeft") == 500);
	}

	void TestScriptOwnerChangeKillsGuardsForNewOwner()
	{
		StockedInventory stock{ "owner-change", 100 };
		ProtocolHost host{ "Workshop Script Owner Change" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		FactionTestSupport::Join(host, 80, "profile-owner", 1.0, 1001.0, InventoryCapabilities());
		Join(host, 81, "profile-new-owner", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure, 1000.0);
		assert(failure.empty());
		assert(Find(Send(host, 80, TransformAt(0.0), 2.0), 80, "workshopSnapshot") != nullptr);
		assert(Find(Send(host, 81, TransformAt(0.0), 2.0), 81, "workshopSnapshot") != nullptr);
		const auto recruited = RecruitAcked(host, stock, 80, 9, 2.5);
		const auto settlerId = Decode(*Find(recruited, 80, "settlerState")).at("settlerId").get<std::uint64_t>();
		assert(!Send(host, 80, Json{ { "type", "settlerAssign" }, { "buildZoneId", 9 }, { "settlerId", settlerId }, { "assignment", "guard" } }, 2.6).empty());
		const auto changed = host.SetSettlementOwner(9, "profile-new-owner", failure, 1100.0);
		assert(failure.empty());
		const auto killed = Decode(*Find(changed, 81, "settlerState"));
		assert(killed.at("settlerId").get<std::uint64_t>() == settlerId && killed.at("dead").get<bool>());
		assert(Find(changed, 80, "settlerState") == nullptr);
		const auto revived = Send(host, 81, Json{ { "type", "settlerRecover" }, { "buildZoneId", 9 }, { "settlerId", settlerId } }, 3.0);
		assert(Decode(*Find(revived, 81, "settlerState")).at("assignment") == "idle");
	}

	void TestFailedRootWriteRetriesUntilTheFaultClears()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-root-retry-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Root Retry" };
			assert(host.EnableWorkshopPersistence(&gate));
			std::filesystem::create_directory(backup);
			Join(host, 90, "profile-retry", 1.0);
			host.BindSessionProfileVerified(90, "profile-retry", false, "");
			(void)host.FlushWorkshopPersistence(1001.0);
			store.WaitForCommits();
			const auto firstFailure = store.TakePersistenceWarning();
			assert(firstFailure.has_value());
			(void)host.FlushWorkshopPersistence(1002.0);
			store.WaitForCommits();
			assert(!store.TakePersistenceWarning().has_value());
			(void)host.FlushWorkshopPersistence(1007.0);
			store.WaitForCommits();
			const auto retryFailure = store.TakePersistenceWarning();
			assert(retryFailure.has_value());
			(void)host.FlushWorkshopPersistence(1008.0);
			store.WaitForCommits();
			assert(!store.TakePersistenceWarning().has_value());
			std::filesystem::remove(backup);
			(void)host.FlushWorkshopPersistence(1012.0);
			store.WaitForCommits();
			const auto cleared = store.TakePersistenceWarning();
			assert(!cleared.has_value());
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			assert(store.GetWorkshop().at("profileFirstSeen").contains("profile-retry"));
		}
		std::filesystem::remove_all(directory);
	}

	void TestProfileFirstSeenEvidencePrecedence()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-first-seen-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			assert(store.SetCharacterSlotRecord("profile-veteran", 0, Json{ { "name", "Veteran" } }));
			assert(store.SetCharacterSlotRecord("profile-dated", 0, Json{ { "name", "Dated" }, { "createdAt", 500000.0 } }));
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop First Seen Evidence" };
			host.SetProfileFirstSeenLookup([](std::string_view a_profileId) -> std::optional<double> {
				if (a_profileId == "profile-registered") {
					return 1000000.0;
				}
				return std::nullopt;
			});
			assert(host.EnableWorkshopPersistence(&gate));
			assert(store.SetCharacterSlotRecord("profile-registered", 0, Json{ { "name", "Registered" }, { "createdAt", 500000.0 } }));
			assert(store.SetCharacterSlotRecord("profile-slotted", 0, Json{ { "name", "Slotted" }, { "createdAt", 1700000.0 } }));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			Join(host, 70, "profile-owner", 1.0);
			std::string failure;
			(void)host.SetSettlementOwner(9, "profile-owner", failure);
			assert(failure.empty());
			const auto bindPlayer = [&](std::uint32_t a_connectionId, const char* a_profile) {
				Join(host, a_connectionId, a_profile, 1.0);
				host.BindSessionProfileVerified(a_connectionId, a_profile, false, "");
				Send(host, a_connectionId, TransformAt(0.0), 2.0);
				Send(host, a_connectionId, Equipped(0, kMiniNukeSourceFormId), 2.0);
			};
			bindPlayer(71, "profile-newcomer");
			bindPlayer(72, "profile-veteran");
			bindPlayer(73, "profile-registered");
			bindPlayer(74, "profile-slotted");
			bindPlayer(75, "profile-dated");
			assert(Find(host.HandleMessage(71, Encode(Hit(9, 0, "mininuke")), 3.0, 2000000.0), 71, "raidHitRefused") != nullptr);
			assert(!host.HandleMessage(72, Encode(Hit(9, 0, "mininuke")), 13.0, 2000000.0).empty());
			assert(!host.HandleMessage(73, Encode(Hit(9, 0, "mininuke")), 23.0, 2000000.0).empty());
			Send(host, 74, Equipped(0, kMiniNukeSourceFormId), 32.0);
			assert(!host.HandleMessage(74, Encode(Hit(9, 0, "mininuke")), 33.0, 2000000.0).empty());
			Send(host, 75, Equipped(0, kMiniNukeSourceFormId), 42.0);
			assert(!host.HandleMessage(75, Encode(Hit(9, 0, "mininuke")), 43.0, 2000000.0).empty());
			(void)host.FlushWorkshopPersistence(2000000.0);
			store.WaitForCommits();
			const auto workshop = store.GetWorkshop();
			const auto& firstSeen = workshop.at("profileFirstSeen");
			assert(firstSeen.at("profile-veteran").get<std::uint64_t>() == std::uint64_t{ 1000000 } - 72 * 3600);
			assert(firstSeen.at("profile-dated").get<std::uint64_t>() == 500000);
			assert(firstSeen.at("profile-registered").get<std::uint64_t>() == 1000000);
			assert(firstSeen.at("profile-slotted").get<std::uint64_t>() == 1700000);
			assert(!workshop.at("profileFirstSeenPreexisting").contains("profile-veteran"));
		}
		std::filesystem::remove_all(directory);
	}

	void TestRaidDeclareRefusalsCarryReason()
	{
		ProtocolHost host{ "Workshop Raid Refusal" };
		host.SetWorkshopRaidConfig(WindowedRaids());
		assert(host.RegisterWorkshopBuildZone(Zone9()));
		Join(host, 80, "profile-owner", 1.0);
		Join(host, 81, "profile-raider", 1.0);
		std::string failure;
		(void)host.SetSettlementOwner(9, "profile-owner", failure);
		assert(failure.empty());
		const auto window = Json{ { "type", "settlementWindowSet" }, { "buildZoneId", 9 }, { "windowStartHour", 0.0 }, { "windowDurationHours", 2.0 }, { "timezoneOffsetHours", 0.0 } };
		assert(!Send(host, 80, window, 2.0).empty());
		const auto declare = Json{ { "type", "raidDeclare" }, { "buildZoneId", 9 } };
		const auto own = RefusalOf(Send(host, 80, declare, 3.0), 80);
		assert(own.at("reason") == "ownSettlement");
		assert(own.at("op") == "raidDeclare");
		assert(own.at("buildZoneId") == 9);
		assert(RefusalOf(host.HandleMessage(81, Encode(declare), 4.0, 43200.0), 81).at("reason") == "outsideRaidWindow");
		assert(Find(Send(host, 81, declare, 5.0), 81, "settlementRefused") == nullptr);
		assert(RefusalOf(Send(host, 81, declare, 1806.0), 81).at("reason") == "alreadyDeclared");
	}

	void TestSettlerRecruitCapsPerSettlementForOwner()
	{
		StockedInventory stock{ "settler-cap", 1000 };
		ProtocolHost host{ "Workshop Settler Cap Player" };
		assert(host.EnableInventoryTransactions(stock.gate));
		assert(host.RegisterWorkshopBuildZone(ZoneAt(9)));
		assert(host.RegisterWorkshopBuildZone(ZoneAt(10)));
		FactionTestSupport::Join(host, 80, "profile-owner", 1.0, 1001.0, InventoryCapabilities());
		std::string failure;
		for (const std::uint64_t zoneId : { 9, 10 }) {
			(void)host.SetSettlementOwner(zoneId, "profile-owner", failure);
		}
		assert(failure.empty());
		assert(Find(Send(host, 80, TransformAt(0.0), 2.0), 80, "workshopSnapshot") != nullptr);
		double monotonic = 3.0;
		const auto recruit = [&](std::uint64_t a_zoneId) {
			const auto frames = RecruitAcked(host, stock, 80, a_zoneId, monotonic);
			monotonic += 1.0;
			return frames;
		};
		for (const std::uint64_t zoneId : { 9, 10 }) {
			for (int count = 0; count < 10; ++count) {
				assert(Find(recruit(zoneId), 80, "settlerState") != nullptr);
			}
		}
		const auto refused = Send(host, 80, Json{ { "type", "settlerRecruit" }, { "buildZoneId", 9 } }, monotonic);
		assert(Find(refused, 80, "settlerState") == nullptr);
		const auto reason = RefusalOf(refused, 80);
		assert(reason.at("reason") == "settlerCapSettlement");
		assert(reason.at("op") == "settlerRecruit");
		assert(reason.at("buildZoneId") == 9);
	}

	void TestRaidDeclarationSurvivesRestartAndPrunesExpired()
	{
		const auto directory = std::filesystem::temp_directory_path() /
			("co-server-workshop-raid-restart-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		const auto path = directory / "durable-state.json";
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		double readyUnix{};
		double expiresUnix{};
		{
			DurableStateFileStore store(path, [] { return 1000.0; }, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Raid Restart" };
			host.SetWorkshopRaidConfig(WindowedRaids());
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			Join(host, 80, "profile-owner", 1.0);
			Join(host, 81, "profile-raider", 1.0);
			std::string failure;
			(void)host.SetSettlementOwner(9, "profile-owner", failure);
			assert(failure.empty());
			const auto window = Json{ { "type", "settlementWindowSet" }, { "buildZoneId", 9 }, { "windowStartHour", 0.0 }, { "windowDurationHours", 2.0 }, { "timezoneOffsetHours", 0.0 } };
			assert(!Send(host, 80, window, 2.0).empty());
			assert(!Send(host, 81, Json{ { "type", "raidDeclare" }, { "buildZoneId", 9 } }, 3.0).empty());
			(void)host.FlushWorkshopPersistence(1004.0);
			store.WaitForCommits();
			const auto stored = store.GetWorkshop().at("raidDeclarations").at("9");
			assert(stored.at("declarerProfileId") == "profile-raider");
			readyUnix = stored.at("readyUnix").get<double>();
			expiresUnix = stored.at("expiresUnix").get<double>();
		}
		{
			DurableStateFileStore store(path, [] { return 1000.0; }, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Raid Restart" };
			host.SetWorkshopRaidConfig(WindowedRaids());
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			Join(host, 82, "profile-visitor", 1.0);
			(void)host.FlushWorkshopPersistence(1004.0, 4.0);
			const auto entered = Send(host, 82, TransformAt(0.0), 5.0);
			const auto* state = Find(entered, 82, "raidDeclareState");
			assert(state != nullptr);
			assert(Decode(*state).at("declarerProfileId") == "profile-raider");
			assert(Decode(*state).at("readyMonotonic").get<double>() == 4.0 + readyUnix - 1004.0);
			assert(Decode(*state).at("expiresMonotonic").get<double>() == 4.0 + expiresUnix - 1004.0);
			assert(Decode(*state).at("readyUnix").get<double>() == readyUnix);
			assert(Decode(*state).at("expiresUnix").get<double>() == expiresUnix);
		}
		{
			DurableStateFileStore store(path, [expiresUnix] { return expiresUnix; }, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Workshop Raid Restart" };
			host.SetWorkshopRaidConfig(WindowedRaids());
			assert(host.EnableWorkshopPersistence(&gate));
			assert(host.RegisterWorkshopBuildZone(Zone9()));
			Join(host, 82, "profile-visitor", 1.0);
			(void)host.FlushWorkshopPersistence(expiresUnix, 1.0);
			store.WaitForCommits();
			assert(Find(Send(host, 82, TransformAt(0.0), 2.0), 82, "raidDeclareState") == nullptr);
			assert(!store.GetWorkshop().at("raidDeclarations").contains("9"));
		}
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestUnreadableRootStaysDisabled();
	TestStructureHitNeedsReachAndCooldown();
	TestStructureDamageBudgetCapsEveryKind();
	TestStructureDamageBudgetRefillsOverTime();
	TestWeaponHitsUseMaterialAndPerKindInterval();
	TestStructureRepairCooldown();
	TestStructureRepairWithoutMaterialsRefuses();
	TestStructureRepairRefusesNonOwnerAndUnderAttack();
	TestGuardsDieWithWorkbenchAndRecover();
	TestSettlementWindowWaitsForRaidDeclaration();
	TestWindowedDeclarationCoversFactionAtDeclareTime();
	TestRegistryFirstSeenLiftsNewPlayerProtection();
	TestDeclarationCapIsSharedByFactionMembers();
	TestDeclarationCapIsPerProfileWithoutFaction();
	TestScriptOwnerChangeKillsGuardsForNewOwner();
	TestFailedRootWriteRetriesUntilTheFaultClears();
	TestProfileFirstSeenEvidencePrecedence();
	TestNewPlayerRaidProtectionIsPushedOnce();
	TestSettlementProtectedRefusalCarriesSecondsLeft();
	TestAttackNoticeReachesOwnerAndBuilderOutsideZone();
	TestTurretFiresOnlyAtRaidLegalAttacker();
	TestRaidDeclareRefusalsCarryReason();
	TestSettlerRecruitCapsPerSettlementForOwner();
	TestRaidDeclarationSurvivesRestartAndPrunesExpired();
	return 0;
}
