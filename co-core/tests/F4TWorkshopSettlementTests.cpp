#include "F4TWorkshopCamps.h"
#include "F4TWorkshopMaterials.h"
#include "F4TWorkshopPieces.h"
#include "F4TWorkshopPower.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

int main()
{
	using namespace F4T;

	const Workshop::FormKey commonwealth{ "Fallout4.esm", 0x1234 };
	const std::vector<Workshop::BuildZonePolicy> noSettlements;

	{
		WorkshopCamps::CampRecord stored;
		stored.buildZoneId = WorkshopCamps::MakeCampZoneId(1);
		stored.ownerProfileId = "owner-a";
		stored.space = commonwealth;
		stored.stored = true;
		const std::vector<WorkshopCamps::CampRecord> camps{ stored };

		WorkshopCamps::FoundRequest request;
		request.ownerProfileId = "owner-a";
		request.operationId = 1;
		request.space = commonwealth;
		request.x = 1000.0F;
		request.y = 1000.0F;
		request.probe.navmeshHit = true;
		request.nowUnix = 100;
		const auto sameOwner = WorkshopCamps::ValidateFound(request, camps, noSettlements, false);
		request.ownerProfileId = "owner-b";
		const auto otherOwner = WorkshopCamps::ValidateFound(request, camps, noSettlements, false);
		assert(sameOwner == WorkshopCamps::FoundStatus::LimitProfile);
		assert(otherOwner == WorkshopCamps::FoundStatus::Ok);
	}

	{
		std::vector<WorkshopCamps::CampRecord> full;
		for (std::uint64_t index = 1; index <= WorkshopCamps::MaximumCampRecords; ++index) {
			WorkshopCamps::CampRecord record;
			record.buildZoneId = WorkshopCamps::MakeCampZoneId(index);
			record.ownerProfileId = "owner-" + std::to_string(index);
			record.space = commonwealth;
			record.stored = true;
			full.push_back(record);
		}
		WorkshopCamps::FoundRequest request;
		request.ownerProfileId = "owner-new";
		request.operationId = 2;
		request.space = commonwealth;
		request.x = 1000.0F;
		request.y = 1000.0F;
		request.probe.navmeshHit = true;
		request.nowUnix = 100;
		const auto atLimit = WorkshopCamps::ValidateFound(request, full, noSettlements, false);
		full.pop_back();
		const auto underLimit = WorkshopCamps::ValidateFound(request, full, noSettlements, false);
		assert(atLimit == WorkshopCamps::FoundStatus::Limit);
		assert(underLimit == WorkshopCamps::FoundStatus::Ok);
	}

	{
		WorkshopCamps::CampRecord moving;
		moving.buildZoneId = WorkshopCamps::MakeCampZoneId(1);
		moving.ownerProfileId = "owner-a";
		moving.space = commonwealth;
		moving.stored = true;
		WorkshopCamps::CampRecord owned;
		owned.buildZoneId = WorkshopCamps::MakeCampZoneId(2);
		owned.ownerProfileId = "owner-a";
		owned.space = commonwealth;
		owned.centerX = 1000.0F;
		owned.centerY = 1000.0F;
		const std::vector<WorkshopCamps::CampRecord> camps{ moving, owned };

		WorkshopCamps::MoveRequest move;
		move.ownerProfileId = "owner-a";
		move.operationId = 3;
		move.buildZoneId = moving.buildZoneId;
		move.space = commonwealth;
		move.x = -20000.0F;
		move.y = -20000.0F;
		move.probe.navmeshHit = true;
		move.nowUnix = 10000;
		assert(WorkshopCamps::ValidateMove(move, moving, camps, noSettlements, false, false) == WorkshopCamps::FoundStatus::NotFound);
		WorkshopCamps::FoundRequest reactivate;
		reactivate.ownerProfileId = "owner-a";
		reactivate.operationId = move.operationId;
		reactivate.space = commonwealth;
		reactivate.x = move.x;
		reactivate.y = move.y;
		reactivate.probe = move.probe;
		reactivate.nowUnix = move.nowUnix;
		assert(WorkshopCamps::ValidateFound(reactivate, camps, noSettlements, false, moving.buildZoneId) == WorkshopCamps::FoundStatus::LimitProfile);
	}

	{
		WorkshopCamps::CampRecord camp;
		camp.buildZoneId = WorkshopCamps::MakeCampZoneId(3);
		camp.ownerProfileId = "owner-a";
		camp.space = commonwealth;
		camp.centerX = 1000.0F;
		camp.centerY = 1000.0F;
		camp.lastMovedAtUnix = std::numeric_limits<std::uint64_t>::max() - 1;
		WorkshopCamps::MoveRequest move;
		move.ownerProfileId = "owner-a";
		move.operationId = 4;
		move.buildZoneId = camp.buildZoneId;
		move.space = commonwealth;
		move.x = -1000.0F;
		move.y = -1000.0F;
		move.probe.navmeshHit = true;
		move.nowUnix = 100;
		const std::vector<WorkshopCamps::CampRecord> camps{ camp };
		const auto overflow = WorkshopCamps::ValidateMove(move, camp, camps, noSettlements, false, false);
		camp.lastMovedAtUnix = 1000;
		const std::vector<WorkshopCamps::CampRecord> settled{ camp };
		move.nowUnix = 1000 + WorkshopCamps::MoveCooldownSeconds;
		const auto elapsed = WorkshopCamps::ValidateMove(move, camp, settled, noSettlements, false, false);
		assert(overflow == WorkshopCamps::FoundStatus::Cooldown);
		assert(elapsed == WorkshopCamps::FoundStatus::Ok);
	}

	{
		WorkshopCamps::PlayerLocation player;
		player.worldspaceId = "0x0000003C";
		player.cellId = "0x00000001";
		player.x = 100.0;
		player.y = 200.0;
		const std::vector<Workshop::FormKey> known{ { "Fallout4.esm", 0x3C } };
		const auto placed = WorkshopCamps::ResolvePlacement(player, "Fallout4.esm", known, 240.0F, 200.0F, 0.0F);
		const auto tooFar = WorkshopCamps::ResolvePlacement(player, "Fallout4.esm", known, 800.0F, 200.0F, 0.0F);
		const auto notANumber = WorkshopCamps::ResolvePlacement(player, "Fallout4.esm", known, std::numeric_limits<float>::quiet_NaN(), 200.0F, 0.0F);
		const auto spoofed = WorkshopCamps::ResolvePlacement(player, "Evil.esp", known, 103.0F, 200.0F, 0.0F);
		const std::vector<Workshop::FormKey> contested{ { "Fallout4.esm", 0x3C }, { "Other.esp", 0x3C } };
		const auto ambiguous = WorkshopCamps::ResolvePlacement(player, "Fallout4.esm", contested, 103.0F, 200.0F, 0.0F);
		const auto unknownWorld = WorkshopCamps::ResolvePlacement(player, "Evil.esp", {}, 103.0F, 200.0F, 0.0F);
		WorkshopCamps::PlayerLocation outOfRange = player;
		outOfRange.worldspaceId = "0x01000000";
		const auto badWorldspace = WorkshopCamps::ResolvePlacement(outOfRange, "Fallout4.esm", known, 103.0F, 200.0F, 0.0F);
		assert(placed.has_value());
		assert(placed->space.plugin == "Fallout4.esm");
		assert(placed->space.localFormId == 0x3C);
		assert(placed->cell.localFormId == 1);
		assert(!tooFar.has_value());
		assert(!notANumber.has_value());
		assert(!spoofed.has_value());
		assert(!ambiguous.has_value());
		assert(unknownWorld.has_value() && unknownWorld->space.plugin == "Evil.esp");
		assert(!badWorldspace.has_value());
	}

	{
		WorkshopPower::PowerGraph grid;
		WorkshopPower::Node generator;
		generator.objectId = 1;
		generator.outputWatts = 10;
		WorkshopPower::Node lamp;
		lamp.objectId = 2;
		lamp.drawWatts = 5;
		lamp.needsPower = true;
		lamp.deviceClass = WorkshopPower::DeviceClass::Light;
		const bool added = grid.AddNode(generator) && grid.AddNode(lamp);
		const bool connected = grid.Connect(1, 2);
		grid.Solve();
		const bool lit = grid.Powered().contains(1) && grid.Powered().contains(2);
		const bool removed = grid.RemoveNode(1);
		grid.Solve();
		const bool dark = !grid.Powered().contains(2);
		assert(added && connected && lit);
		assert(removed && dark);
		assert(grid.Nodes().size() == 1);
	}

	{
		const nlohmann::json valid = nlohmann::json::parse(R"({"pieces":[{"plugin":"Fallout4.esm","localFormId":1,"tier":"concrete"}]})");
		const nlohmann::json invalid = nlohmann::json::parse(R"({"pieces":[{"plugin":"Fallout4.esm","localFormId":2,"tier":"glass"}]})");
		WorkshopPieces::Table table;
		const bool loaded = table.LoadFromJson(valid);
		const auto concrete = table.Find(Workshop::FormKey{ "Fallout4.esm", 1 }).tier;
		const bool rejected = !table.LoadFromJson(invalid);
		const auto kept = table.Find(Workshop::FormKey{ "Fallout4.esm", 1 }).tier;
		const auto unknown = table.Find(Workshop::FormKey{ "Fallout4.esm", 999 }).tier;
		assert(loaded && concrete == WorkshopMaterials::MaterialTier::Concrete);
		assert(rejected && table.Size() == 1);
		assert(kept == WorkshopMaterials::MaterialTier::Concrete);
		assert(unknown == WorkshopMaterials::MaterialTier::Wood);
		const auto lowered = table.Find(Workshop::FormKey{ "fallout4.esm", 1 }).tier;
		assert(lowered == WorkshopMaterials::MaterialTier::Concrete);
	}

	{
		const nlohmann::json packet = nlohmann::json::parse(R"({
			"type":"campFound","operationId":7,
			"space":{"plugin":"Fallout4.esm","localFormId":60},
			"x":100.0,"y":200.0,"z":0.0,
			"probe":{"height":0.0,"slopeDegrees":0.0,"navmeshHit":true},
			"dropEntityId":2147483649
		})");
		auto withoutDrop = packet;
		withoutDrop.erase("dropEntityId");
		auto lowDrop = packet;
		lowDrop["dropEntityId"] = 5;
		const auto parsed = WorkshopCamps::NormalizeCampFound(packet, "owner-a");
		assert(parsed.has_value() && parsed->dropEntityId == 0x80000001U);
		assert(!WorkshopCamps::NormalizeCampFound(withoutDrop, "owner-a").has_value());
		assert(!WorkshopCamps::NormalizeCampFound(lowDrop, "owner-a").has_value());

		WorkshopCamps::FoundRequest request;
		request.ownerProfileId = "owner-a";
		request.operationId = 1;
		request.space = commonwealth;
		request.x = 1000.0F;
		request.y = 1000.0F;
		request.probe.navmeshHit = true;
		request.nowUnix = 100;
		assert(WorkshopCamps::ValidateFound(request, {}, noSettlements, false) == WorkshopCamps::FoundStatus::Ok);
	}

	{
		nlohmann::json kit = nlohmann::json::parse(R"({
			"ownerProfileId":"owner-a",
			"itemKey":"commonwealthonline.esp:0000997F",
			"item":{"plugin":"commonwealthonline.esp","localFormId":"0000997F","count":1},
			"position":{"cellId":"0000003C","worldspaceId":"0025DA15","x":1.0,"y":2.0,"z":3.0}
		})");
		assert(WorkshopCamps::CheckCampKitDrop(kit, "owner-a") == WorkshopCamps::FoundStatus::Ok);
		assert(WorkshopCamps::CheckCampKitDrop(kit, "owner-b") == WorkshopCamps::FoundStatus::NotFound);
		nlohmann::json otherItem = kit;
		otherItem["itemKey"] = "Fallout4.esm:00000F1F";
		assert(WorkshopCamps::CheckCampKitDrop(otherItem, "owner-a") == WorkshopCamps::FoundStatus::NotFound);
		nlohmann::json stack = kit;
		stack["item"]["count"] = 2;
		assert(WorkshopCamps::CheckCampKitDrop(stack, "owner-a") == WorkshopCamps::FoundStatus::NotFound);
		assert(WorkshopCamps::CheckCampKitDrop(kit, "") == WorkshopCamps::FoundStatus::NotFound);
		nlohmann::json noWorld = kit;
		noWorld["position"].erase("worldspaceId");
		assert(WorkshopCamps::CheckCampKitDrop(noWorld, "owner-a") == WorkshopCamps::FoundStatus::NotFound);
		assert(WorkshopCamps::CheckCampKitDrop(nlohmann::json(nullptr), "owner-a") == WorkshopCamps::FoundStatus::NotFound);
	}

	{
		WorkshopCamps::PlayerLocation origin;
		origin.worldspaceId = "0x0000003C";
		origin.cellId = "0x00000001";
		const std::vector<Workshop::FormKey> known{ { "Fallout4.esm", 0x3C } };
		const auto reach = WorkshopCamps::ResolvePlacement(origin, "Fallout4.esm", known, WorkshopCamps::MaximumPlacementOffsetUnits, 0.0F, 0.0F);
		const auto beyond = WorkshopCamps::ResolvePlacement(origin, "Fallout4.esm", known, 9.0F * WorkshopCamps::UnitsPerMeter, 0.0F, 0.0F);
		assert(reach.has_value());
		assert(!beyond.has_value());
	}

	{
		WorkshopCamps::CampRecord existing;
		existing.buildZoneId = WorkshopCamps::MakeCampZoneId(5);
		existing.ownerProfileId = "owner-a";
		existing.space = commonwealth;
		const std::vector<WorkshopCamps::CampRecord> camps{ existing };
		WorkshopCamps::FoundRequest request;
		request.ownerProfileId = "owner-b";
		request.operationId = 8;
		request.space = commonwealth;
		request.probe.navmeshHit = true;
		request.nowUnix = 100;
		request.x = 100.0F * WorkshopCamps::UnitsPerMeter;
		WorkshopCamps::SeparationDetail detail;
		const auto tooClose = WorkshopCamps::ValidateFound(request, camps, noSettlements, false, 0, &detail);
		request.x = 160.0F * WorkshopCamps::UnitsPerMeter;
		const auto separated = WorkshopCamps::ValidateFound(request, camps, noSettlements, false);
		assert(tooClose == WorkshopCamps::FoundStatus::TooCloseToCamp);
		assert(separated == WorkshopCamps::FoundStatus::Ok);
		assert(detail.distanceMeters == 100 && detail.requiredMeters == 150 && detail.subjectName.empty());
		const auto packet = WorkshopCamps::MakeCampRejected(8, tooClose, detail);
		assert(packet.at("status") == "tooCloseCamp" && packet.at("distanceMeters") == 100 && packet.at("requiredMeters") == 150);
		assert(!packet.contains("subjectName"));
	}

	{
		Workshop::BuildZonePolicy settlement;
		settlement.buildZoneId = 7;
		settlement.space = commonwealth;
		settlement.radius = 10.0F;
		settlement.name = "Sanctuary Hills";
		const std::vector<Workshop::BuildZonePolicy> settlements{ settlement };
		WorkshopCamps::FoundRequest request;
		request.ownerProfileId = "owner-a";
		request.operationId = 9;
		request.space = commonwealth;
		request.x = 220.0F * WorkshopCamps::UnitsPerMeter;
		request.probe.navmeshHit = true;
		request.nowUnix = 100;
		WorkshopCamps::SeparationDetail detail;
		const auto nearSettlement = WorkshopCamps::ValidateFound(request, {}, settlements, false, 0, &detail);
		assert(nearSettlement == WorkshopCamps::FoundStatus::TooCloseToSettlement);
		assert(detail.distanceMeters == 220 && detail.requiredMeters == 300 && detail.subjectName == "Sanctuary Hills");
		const auto packet = WorkshopCamps::MakeCampRejected(9, nearSettlement, detail);
		assert(packet.at("status") == "tooCloseSettlement");
		assert(packet.at("distanceMeters") == 220 && packet.at("requiredMeters") == 300);
		assert(packet.at("subjectName") == "Sanctuary Hills");
		const auto plain = WorkshopCamps::MakeCampRejected(9, WorkshopCamps::FoundStatus::Limit);
		assert(plain.at("status") == "limit" && !plain.contains("distanceMeters") && !plain.contains("subjectName"));
		assert(WorkshopCamps::FoundStatusName(WorkshopCamps::FoundStatus::LimitArea) == "limitArea");
	}

	{
		WorkshopCamps::CampRecord camp;
		camp.buildZoneId = WorkshopCamps::MakeCampZoneId(6);
		camp.ownerProfileId = "owner-a";
		camp.space = commonwealth;
		nlohmann::json record = WorkshopCamps::CampRecordJson(camp);
		record["radius"] = 60.0;
		const auto migrated = WorkshopCamps::ReadCampRecord(record);
		assert(migrated.has_value() && migrated->radius == WorkshopCamps::DefaultCampRadiusUnits);
		const auto campList = WorkshopCamps::MakeCampList({ camp });
		assert(campList.at("camps").at(0).at("space").at("plugin") == "Fallout4.esm");
		assert(campList.at("camps").at(0).at("space").at("localFormId") == 0x1234);
	}

	return 0;
}
