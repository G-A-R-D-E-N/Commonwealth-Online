#include "F4TWorkshopProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>
#include <string>

int main()
{
	using Json = nlohmann::json;
	using namespace F4T::Workshop;
	using namespace F4T::WorkshopProtocol;

	const Json form{ { "plugin", "Fallout4.esm" }, { "localFormId", 0x1234 } };
	const Json space{ { "plugin", "Fallout4.esm" }, { "localFormId", 1 } };
	const Json transform{
		{ "x", 10.0 },
		{ "y", 20.0 },
		{ "z", 30.0 },
		{ "pitch", 0.0 },
		{ "yaw", 1.0 },
		{ "roll", 0.0 },
		{ "scale", 1.0 }
	};
	const Json place{
		{ "type", "workshopOp" },
		{ "operationId", 10 },
		{ "expectedRevision", 0 },
		{ "buildZoneId", 7 },
		{ "kind", "place" },
		{ "form", form },
		{ "space", space },
		{ "transform", transform }
	};

	const auto normalized = NormalizeOperation(place, 22);
	assert(normalized.has_value());
	assert(normalized->playerId == 22);
	assert(normalized->buildZoneId == 7);
	assert(normalized->operationId == 10);
	assert(normalized->kind == OperationKind::Place);
	assert(normalized->form.plugin == "Fallout4.esm");
	assert(normalized->form.localFormId == 0x1234);
	assert(normalized->transform.x == 10.0F);

	const auto operationPacket = MakeOperation(*normalized);
	assert(operationPacket.has_value());
	assert(operationPacket->at("type") == "workshopOp");
	assert(!operationPacket->contains("playerId"));
	assert(operationPacket->at("operationId") == 10);
	assert(NormalizeOperation(*operationPacket, 99).has_value());

	assert(!NormalizeOperation(place, 0).has_value());

	Json wrongType = place;
	wrongType["type"] = 42;
	assert(!NormalizeOperation(wrongType, 22).has_value());

	Json bad = place;
	bad["operationId"] = 0;
	assert(!NormalizeOperation(bad, 22).has_value());

	bad = place;
	bad["buildZoneId"] = -1;
	assert(!NormalizeOperation(bad, 22).has_value());

	bad = place;
	bad["kind"] = "wire";
	assert(!NormalizeOperation(bad, 22).has_value());

	bad = place;
	bad["form"]["plugin"] = std::string(MaximumPluginNameBytes + 1, 'x');
	assert(!NormalizeOperation(bad, 22).has_value());

	bad = place;
	bad["transform"]["x"] = std::numeric_limits<double>::infinity();
	assert(!NormalizeOperation(bad, 22).has_value());

	Json move{
		{ "type", "workshopOp" },
		{ "operationId", 11 },
		{ "expectedRevision", 1 },
		{ "buildZoneId", 7 },
		{ "kind", "move" },
		{ "objectId", 44 },
		{ "space", space },
		{ "transform", transform }
	};
	const auto normalizedMove = NormalizeOperation(move, 22);
	assert(normalizedMove.has_value());
	assert(normalizedMove->kind == OperationKind::Move);
	assert(normalizedMove->objectId == 44);

	Json scrap{
		{ "type", "workshopOp" },
		{ "operationId", 12 },
		{ "expectedRevision", 2 },
		{ "buildZoneId", 7 },
		{ "kind", "scrap" },
		{ "objectId", 44 }
	};
	const auto normalizedScrap = NormalizeOperation(scrap, 22);
	assert(normalizedScrap.has_value());
	assert(normalizedScrap->kind == OperationKind::Scrap);

	const ObjectState object{
		44,
		7,
		{ "Fallout4.esm", 0x1234 },
		{ "Fallout4.esm", 1 },
		{ 10.0F, 20.0F, 30.0F, 0.0F, 1.0F, 0.0F, 1.0F }
	};
	const Result committed{ ApplyStatus::Committed, 3, 44, false, object };
	const auto commit = MakeCommit(*normalizedMove, committed);
	assert(commit.has_value());
	assert(commit->at("type") == "workshopCommit");
	assert(commit->at("playerId") == 22);
	assert(commit->at("revision") == 3);
	assert(commit->at("objectId") == 44);
	assert(commit->at("form").at("localFormId") == 0x1234);
	assert(commit->at("transform").at("x") == 10.0F);

	const auto parsedCommit = NormalizeCommit(*commit);
	assert(parsedCommit.has_value());
	assert(parsedCommit->playerId == 22);
	assert(parsedCommit->buildZoneId == 7);
	assert(parsedCommit->revision == 3);
	assert(parsedCommit->object.has_value());
	assert(parsedCommit->object->transform.x == 10.0F);

	Json malformedCommit = *commit;
	malformedCommit["transform"]["x"] = "bad";
	assert(!NormalizeCommit(malformedCommit).has_value());

	const Result replayed{ ApplyStatus::Committed, 3, 44, true, object };
	const auto replayCommit = MakeCommit(*normalizedMove, replayed);
	assert(replayCommit.has_value());
	assert(*replayCommit == *commit);

	const Result rejected{ ApplyStatus::OutsideBuildZone, 3, 44, false, std::nullopt };
	const auto reject = MakeReject(*normalizedMove, rejected);
	assert(reject.at("type") == "workshopReject");
	assert(reject.at("status") == "outsideBuildZone");
	assert(reject.at("revision") == 3);

	const auto parsedReject = NormalizeReject(reject);
	assert(parsedReject.has_value());
	assert(parsedReject->buildZoneId == 7);
	assert(parsedReject->operationId == 11);
	assert(parsedReject->revision == 3);
	assert(parsedReject->status == ApplyStatus::OutsideBuildZone);

	Json badReject = reject;
	badReject["status"] = "unknown";
	assert(!NormalizeReject(badReject).has_value());

	const Result missingState{ ApplyStatus::Committed, 3, 44, false, std::nullopt };
	assert(!MakeCommit(*normalizedMove, missingState).has_value());

	const Result scrapCommit{ ApplyStatus::Committed, 4, 44, false, std::nullopt };
	const auto scrapPacket = MakeCommit(*normalizedScrap, scrapCommit);
	assert(scrapPacket.has_value());
	assert(!scrapPacket->contains("transform"));

	const auto parsedScrap = NormalizeCommit(*scrapPacket);
	assert(parsedScrap.has_value());
	assert(parsedScrap->kind == OperationKind::Scrap);
	assert(!parsedScrap->object.has_value());

	Json badScrap = *scrapPacket;
	badScrap["transform"] = transform;
	assert(!NormalizeCommit(badScrap).has_value());

	Json worldScrap{
		{ "type", "workshopOp" },
		{ "operationId", 13 },
		{ "expectedRevision", 4 },
		{ "buildZoneId", 7 },
		{ "kind", "scrapWorld" },
		{ "form", form },
		{ "space", space },
		{ "transform", transform }
	};
	const auto normalizedWorldScrap = NormalizeOperation(worldScrap, 22);
	assert(normalizedWorldScrap.has_value());
	assert(normalizedWorldScrap->kind == OperationKind::ScrapWorld);
	assert(normalizedWorldScrap->form.localFormId == 0x1234);
	const auto worldScrapPacket = MakeOperation(*normalizedWorldScrap);
	assert(worldScrapPacket.has_value());
	assert(worldScrapPacket->at("kind") == "scrapWorld");
	assert(worldScrapPacket->at("form").at("localFormId") == 0x1234);

	Json worldScrapWithObject = worldScrap;
	worldScrapWithObject["objectId"] = 44;
	assert(NormalizeOperation(worldScrapWithObject, 22).has_value());

	const ObjectState removed{
		45,
		7,
		{ "Fallout4.esm", 0x1234 },
		{ "Fallout4.esm", 1 },
		{ 10.0F, 20.0F, 30.0F, 0.0F, 1.0F, 0.0F, 1.0F }
	};
	const Result worldCommitResult{ ApplyStatus::Committed, 5, 45, false, removed };
	const auto worldCommit = MakeCommit(*normalizedWorldScrap, worldCommitResult);
	assert(worldCommit.has_value());
	assert(worldCommit->at("kind") == "scrapWorld");
	assert(worldCommit->at("objectId") == 45);
	const auto parsedWorldCommit = NormalizeCommit(*worldCommit);
	assert(parsedWorldCommit.has_value());
	assert(parsedWorldCommit->kind == OperationKind::ScrapWorld);
	assert(parsedWorldCommit->object.has_value());
	assert(parsedWorldCommit->object->form.localFormId == 0x1234);

	const Result denied{ ApplyStatus::NotAuthorized, 5, 44, false, std::nullopt };
	const auto deniedReject = MakeReject(*normalizedMove, denied);
	assert(deniedReject.at("status") == "notAuthorized");
	assert(NormalizeReject(deniedReject)->status == ApplyStatus::NotAuthorized);
	const Result alreadyRemoved{ ApplyStatus::AlreadyRemoved, 5, 0, false, std::nullopt };
	const auto removedReject = MakeReject(*normalizedWorldScrap, alreadyRemoved);
	assert(NormalizeReject(removedReject)->status == ApplyStatus::AlreadyRemoved);
	const Result unsaved{ ApplyStatus::PersistenceUnavailable, 5, 0, false, std::nullopt };
	const auto unsavedReject = MakeReject(*normalizedWorldScrap, unsaved);
	assert(unsavedReject.at("status") == "persistenceUnavailable");
	assert(NormalizeReject(unsavedReject)->status == ApplyStatus::PersistenceUnavailable);

	std::vector<ObjectState> objects;
	for (std::uint64_t id = 1; id <= MaximumSnapshotEntriesPerFrame + 8; ++id) {
		objects.push_back(ObjectState{
			id,
			7,
			{ "Fallout4.esm", 0x1234 },
			{ "Fallout4.esm", 1 },
			{ 10.0F, 20.0F, 30.0F, 0.0F, 1.0F, 0.0F, 1.0F }
		});
	}
	const std::vector<ObjectState> tombstones{
		ObjectState{ 41, 7, { "Fallout4.esm", 0xABCD }, { "Fallout4.esm", 1 }, { 5.0F, 5.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F } }
	};
	const auto frames = MakeSnapshotPackets(7, 6, objects, tombstones);
	assert(frames.size() == 2);
	assert(frames.front().at("objects").size() == MaximumSnapshotEntriesPerFrame);
	assert(frames.front().at("tombstones").empty());
	assert(frames.front().at("complete") == false);
	assert(frames.back().at("objects").size() == 8);
	assert(frames.back().at("tombstones").size() == 1);
	assert(frames.back().at("complete") == true);

	const auto parsedFrame = NormalizeSnapshot(frames.back());
	assert(parsedFrame.has_value());
	assert(parsedFrame->buildZoneId == 7);
	assert(parsedFrame->revision == 6);
	assert(parsedFrame->objects.size() == 8);
	assert(parsedFrame->objects.front().buildZoneId == 7);
	assert(parsedFrame->tombstones.size() == 1);
	assert(parsedFrame->tombstones.front().form.localFormId == 0xABCD);
	assert(parsedFrame->complete);

	const auto empty = MakeSnapshotPackets(7, 0, {}, {});
	assert(empty.size() == 1);
	assert(empty.front().at("complete") == true);
	assert(NormalizeSnapshot(empty.front())->objects.empty());

	Json oversized = frames.front();
	oversized["tombstones"].push_back(oversized["objects"][0]);
	assert(!NormalizeSnapshot(oversized).has_value());

	Json wrongSnapshotType = frames.back();
	wrongSnapshotType["type"] = "workshopCommit";
	assert(!NormalizeSnapshot(wrongSnapshotType).has_value());

	Json badSnapshotEntry = frames.back();
	badSnapshotEntry["objects"][0]["transform"]["x"] = "bad";
	assert(!NormalizeSnapshot(badSnapshotEntry).has_value());

	Json unchangedPacket = MakeSnapshotPackets(7, 6, {}, {}).front();
	unchangedPacket["complete"] = false;
	unchangedPacket["unchanged"] = true;
	const auto unchangedSnapshot = NormalizeSnapshot(unchangedPacket);
	assert(unchangedSnapshot && unchangedSnapshot->unchanged && !unchangedSnapshot->complete);
	unchangedPacket["unchanged"] = "yes";
	assert(!NormalizeSnapshot(unchangedPacket).has_value());

	{
		const BuildZonePolicy zonePolicy{ 5, FormKey{ "Fallout4.esm", 1 }, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 8 };
		SettlementState source{ zonePolicy, 40 };
		Request placeRequest;
		placeRequest.playerId = 1;
		placeRequest.profileId = "profile-a";
		placeRequest.buildZoneId = 5;
		placeRequest.operationId = 1;
		placeRequest.kind = OperationKind::Place;
		placeRequest.form = { "Fallout4.esm", 0x1234 };
		placeRequest.space = zonePolicy.space;
		placeRequest.transform = { 1.0F, 2.0F, 3.0F, 0.0F, 0.0F, 0.0F, 1.0F };
		assert(source.Apply(placeRequest).status == ApplyStatus::Committed);
		Request scrapRequest = placeRequest;
		scrapRequest.operationId = 2;
		scrapRequest.expectedRevision = 1;
		scrapRequest.profileId = "profile-b";
		scrapRequest.kind = OperationKind::ScrapWorld;
		scrapRequest.form = { "Fallout4.esm", 0xABCD };
		assert(source.Apply(scrapRequest).status == ApplyStatus::Committed);

		const auto stored = MakeZoneState(source);
		assert(stored.at("tombstones").at(0).at("profileId") == "profile-b");
		SettlementState restored{ zonePolicy };
		assert(RestoreZoneState(restored, Json::parse(stored.dump())));
		assert(restored.CurrentRevision() == 2);
		assert(restored.NextObjectId() == 42);
		assert(restored.Objects().size() == 1 && restored.Tombstones().size() == 1);
		assert(restored.StoredTombstones().front().profileId == "profile-b");

		SettlementState other{ BuildZonePolicy{ 6, zonePolicy.space, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 8 } };
		assert(!RestoreZoneState(other, Json{ { "revision", 1 } }));
		Json noOwner = stored;
		noOwner["tombstones"][0].erase("profileId");
		SettlementState untouched{ zonePolicy };
		assert(!RestoreZoneState(untouched, noOwner));
		assert(untouched.CurrentRevision() == 0);
		Json lowCounter = stored;
		lowCounter["nextObjectId"] = 40;
		assert(!RestoreZoneState(untouched, lowCounter));
	}

	{
		const Json zoneEntry{
			{ "buildZoneId", 3 },
			{ "space", space },
			{ "centerX", 1.5 },
			{ "centerY", 2.5 },
			{ "centerZ", 3.5 },
			{ "radius", 400.0 },
			{ "maxScale", 2.0 },
			{ "maxObjects", 64 }
		};
		const auto zonesOf = [](const Json& a_entry) {
			return Json{ { "version", 1 }, { "zones", Json::array({ a_entry }) } };
		};
		const auto parsed = ParseBuildZones(zonesOf(zoneEntry));
		assert(parsed && parsed->size() == 1);
		assert(parsed->front().buildZoneId == 3 && parsed->front().radius == 400.0F && parsed->front().maxObjects == 64);
		assert(ParseBuildZones(Json{ { "version", 1 }, { "zones", Json::array() } })->empty());
		assert(!ParseBuildZones(Json{ { "version", 1 }, { "zones", Json::array({ zoneEntry, zoneEntry }) } }));
		Json wrongVersion = zonesOf(zoneEntry);
		wrongVersion["version"] = 2;
		assert(!ParseBuildZones(wrongVersion));
		Json noVersion = zonesOf(zoneEntry);
		noVersion.erase("version");
		assert(!ParseBuildZones(noVersion));
		assert(!ParseBuildZones(Json::array()));
		for (const char* field : { "buildZoneId", "space", "centerX", "radius", "maxScale", "maxObjects" }) {
			Json broken = zoneEntry;
			broken.erase(field);
			assert(!ParseBuildZones(zonesOf(broken)));
		}
		const std::vector<std::pair<std::string, Json>> invalidValues{
			{ "radius", 0.0 }, { "maxObjects", 0 }, { "buildZoneId", 0 }, { "maxScale", -1.0 }
		};
		for (const auto& [field, value] : invalidValues) {
			Json broken = zoneEntry;
			broken[field] = value;
			assert(!ParseBuildZones(zonesOf(broken)));
		}
	}

	{
		const Json explosiveHit{ { "type", "structureHit" }, { "buildZoneId", 7 }, { "objectId", 9 }, { "explosive", "missile" } };
		const auto hit = NormalizeStructureHit(explosiveHit);
		assert(hit && hit->buildZoneId == 7 && hit->objectId == 9 && hit->explosive == F4T::WorkshopRaids::ExplosiveKind::Missile);
		const auto encodedHit = MakeStructureHit(*hit);
		assert(encodedHit.at("type") == "structureHit");
		assert(encodedHit.at("explosive") == "missile");
		const auto decodedHit = NormalizeStructureHit(encodedHit);
		assert(decodedHit && decodedHit->buildZoneId == 7 && decodedHit->objectId == 9 && decodedHit->explosive == F4T::WorkshopRaids::ExplosiveKind::Missile);
		Json rejected = explosiveHit;
		rejected["explosive"] = "nuke";
		assert(!NormalizeStructureHit(rejected));
		rejected.erase("explosive");
		assert(!NormalizeStructureHit(rejected));
		rejected = explosiveHit;
		rejected["explosive"] = 150;
		assert(!NormalizeStructureHit(rejected));
		rejected = explosiveHit;
		rejected["buildZoneId"] = 0;
		assert(!NormalizeStructureHit(rejected));
		assert(hit->weaponKind == F4T::WorkshopMaterials::WeaponKind::Explosive);
		const Json meleeHit{ { "type", "structureHit" }, { "buildZoneId", 7 }, { "objectId", 9 }, { "weaponKind", 0 } };
		const auto melee = NormalizeStructureHit(meleeHit);
		assert(melee && melee->weaponKind == F4T::WorkshopMaterials::WeaponKind::Melee);
		const auto encodedMelee = MakeStructureHit(*melee);
		assert(encodedMelee.at("weaponKind") == 0);
		const auto decodedMelee = NormalizeStructureHit(encodedMelee);
		assert(decodedMelee && decodedMelee->weaponKind == F4T::WorkshopMaterials::WeaponKind::Melee);
		const Json rifleHit{ { "type", "structureHit" }, { "buildZoneId", 7 }, { "objectId", 9 }, { "weaponKind", 2 } };
		const auto rifle = NormalizeStructureHit(rifleHit);
		assert(rifle && rifle->weaponKind == F4T::WorkshopMaterials::WeaponKind::Rifle);
		const auto decodedRifle = NormalizeStructureHit(MakeStructureHit(*rifle));
		assert(decodedRifle && decodedRifle->weaponKind == F4T::WorkshopMaterials::WeaponKind::Rifle);
		Json weaponRejected = meleeHit;
		weaponRejected["weaponKind"] = 5;
		assert(!NormalizeStructureHit(weaponRejected));
		weaponRejected["weaponKind"] = -1;
		assert(!NormalizeStructureHit(weaponRejected));
		weaponRejected["weaponKind"] = "2";
		assert(!NormalizeStructureHit(weaponRejected));
		weaponRejected = explosiveHit;
		weaponRejected.erase("explosive");
		weaponRejected["weaponKind"] = 4;
		assert(!NormalizeStructureHit(weaponRejected));
	}
	{
		const Json windowPacket{ { "type", "settlementWindowSet" }, { "buildZoneId", 7 }, { "windowStartHour", 23.0F }, { "windowDurationHours", 24.0F }, { "timezoneOffsetHours", -5.0F } };
		const auto parsed = NormalizeSettlementWindowSet(windowPacket);
		assert(parsed && parsed->buildZoneId == 7 && parsed->windowDurationHours == 24.0F && parsed->timezoneOffsetHours == -5.0F);
		Json tooLong = windowPacket;
		tooLong["windowDurationHours"] = 24.5F;
		assert(!NormalizeSettlementWindowSet(tooLong));
			Json tooShort = windowPacket;
			tooShort["windowDurationHours"] = 0.5F;
			assert(!NormalizeSettlementWindowSet(tooShort));
			tooShort["windowDurationHours"] = 1.0F;
			assert(NormalizeSettlementWindowSet(tooShort));
		Json badOffset = windowPacket;
		badOffset["timezoneOffsetHours"] = 15.0F;
		assert(!NormalizeSettlementWindowSet(badOffset));
		badOffset["timezoneOffsetHours"] = "5";
		assert(!NormalizeSettlementWindowSet(badOffset));
		Json noOffset = windowPacket;
		noOffset.erase("timezoneOffsetHours");
		assert(NormalizeSettlementWindowSet(noOffset) && NormalizeSettlementWindowSet(noOffset)->timezoneOffsetHours == 0.0F);
	}

	{
		using F4T::Workshop::ApplyStatus;
		assert(std::string{ F4T::WorkshopProtocol::RejectNotice(ApplyStatus::OutsideBuildZone) }.find("build area") != std::string::npos);
		assert(std::string{ F4T::WorkshopProtocol::RejectNotice(ApplyStatus::StaleRevision) }.find("Place it again") != std::string::npos);
		assert(std::string{ F4T::WorkshopProtocol::RejectNotice(ApplyStatus::BudgetExceeded) }.find("limit") != std::string::npos);
		assert(std::string{ F4T::WorkshopProtocol::RejectNotice(ApplyStatus::PersistenceUnavailable) }.find("could not save") != std::string::npos);
		assert(std::string{ F4T::WorkshopProtocol::RejectNotice(ApplyStatus::RevisionExhausted) } == "Build refused.");
		assert(F4T::WorkshopProtocol::StatusName(ApplyStatus::StaleRevision) == "staleRevision");
	}

	{
		using F4T::WorkshopRaids::HitRefusal;
		const auto refused = F4T::WorkshopProtocol::MakeRaidHitRefused(7, HitRefusal::SettlementProtected, 3600);
		assert(refused.at("type") == "raidHitRefused");
		assert(refused.at("buildZoneId") == 7);
		assert(refused.at("reason") == "settlementProtected");
		assert(refused.at("secondsLeft") == 3600);
		const auto window = F4T::WorkshopProtocol::MakeRaidHitRefused(7, HitRefusal::RaidWindowClosed, 0);
		assert(window.at("reason") == "raidWindowClosed");
		assert(!window.contains("secondsLeft"));
		assert(F4T::WorkshopRaids::HitRefusalName(HitRefusal::SettlementProtected) == "settlementProtected");
		assert(F4T::WorkshopRaids::HitRefusalName(HitRefusal::FactionLeaveBlocked) == "factionLeaveBlocked");
		const auto declared = F4T::WorkshopProtocol::MakeRaidDeclareState(9, "profile-raider", 1803.0, 9003.0, 2803.0, 10003.0);
		assert(declared.at("type") == "raidDeclareState");
		assert(declared.at("readyMonotonic") == 1803.0);
		assert(declared.at("expiresMonotonic") == 9003.0);
		assert(declared.at("readyUnix") == 2803.0);
		assert(declared.at("expiresUnix") == 10003.0);
	}

	{
		const auto power = F4T::WorkshopProtocol::MakePowerState(7, { 11, 0, 12 });
		const auto parsedPower = F4T::WorkshopProtocol::ParsePowerState(power);
		assert(parsedPower && parsedPower->buildZoneId == 7 && parsedPower->poweredObjectIds == std::vector<std::uint64_t>({ 11, 12 }));
		assert(F4T::WorkshopProtocol::ParsePowerState(F4T::WorkshopProtocol::MakePowerState(7, {})));
		Json badPower = power;
		badPower["buildZoneId"] = 0;
		assert(!F4T::WorkshopProtocol::ParsePowerState(badPower));
		badPower = power;
		badPower["powered"] = 11;
		assert(!F4T::WorkshopProtocol::ParsePowerState(badPower));
		badPower["powered"] = Json::array({ "11" });
		assert(!F4T::WorkshopProtocol::ParsePowerState(badPower));
		badPower["powered"] = Json::array({ 0 });
		assert(!F4T::WorkshopProtocol::ParsePowerState(badPower));
		badPower.erase("powered");
		assert(!F4T::WorkshopProtocol::ParsePowerState(badPower));
		badPower = power;
		badPower["type"] = "workshopCommit";
		assert(!F4T::WorkshopProtocol::ParsePowerState(badPower));

		const auto wired = F4T::WorkshopProtocol::MakePowerState(7, { 11 }, { { 30, 11, 12 }, { 0, 13, 14 } });
		const auto parsedWired = F4T::WorkshopProtocol::ParsePowerState(wired);
		assert(parsedWired && parsedWired->hasWires && parsedWired->wires.size() == 1);
		assert(parsedWired->wires[0].wireId == 30 && parsedWired->wires[0].fromId == 11 && parsedWired->wires[0].toId == 12);
		Json legacyPower = wired;
		legacyPower.erase("wires");
		const auto parsedLegacy = F4T::WorkshopProtocol::ParsePowerState(legacyPower);
		assert(parsedLegacy && !parsedLegacy->hasWires && parsedLegacy->wires.empty());
		Json badWires = wired;
		badWires["wires"] = 5;
		assert(!F4T::WorkshopProtocol::ParsePowerState(badWires));
		badWires["wires"] = Json::array({ Json{ { "wireId", 0 }, { "fromId", 11 }, { "toId", 12 } } });
		assert(!F4T::WorkshopProtocol::ParsePowerState(badWires));
		badWires["wires"] = Json::array({ Json{ { "wireId", 30 }, { "fromId", 11 } } });
		assert(!F4T::WorkshopProtocol::ParsePowerState(badWires));

		const auto upkeep = F4T::WorkshopProtocol::MakeUpkeepState(7, 25, 0, true);
		const auto parsedUpkeep = F4T::WorkshopProtocol::ParseUpkeepState(upkeep);
		assert(parsedUpkeep && parsedUpkeep->buildZoneId == 7 && parsedUpkeep->hourlyCost == 25 && parsedUpkeep->hoursCovered == 0 && parsedUpkeep->decaying);
		const auto paid = F4T::WorkshopProtocol::ParseUpkeepState(F4T::WorkshopProtocol::MakeUpkeepState(7, 0, 48, false));
		assert(paid && paid->hourlyCost == 0 && paid->hoursCovered == 48 && !paid->decaying);
		Json badUpkeep = upkeep;
		badUpkeep["buildZoneId"] = 0;
		assert(!F4T::WorkshopProtocol::ParseUpkeepState(badUpkeep));
		badUpkeep = upkeep;
		badUpkeep["hourlyCost"] = "25";
		assert(!F4T::WorkshopProtocol::ParseUpkeepState(badUpkeep));
		badUpkeep = upkeep;
		badUpkeep["decaying"] = 1;
		assert(!F4T::WorkshopProtocol::ParseUpkeepState(badUpkeep));
		badUpkeep = upkeep;
		badUpkeep.erase("hoursCovered");
		assert(!F4T::WorkshopProtocol::ParseUpkeepState(badUpkeep));
		badUpkeep["type"] = "powerState";
		assert(!F4T::WorkshopProtocol::ParseUpkeepState(badUpkeep));
	}

	{
		Json connect = Json{ { "type", "wireOp" }, { "buildZoneId", 7 }, { "operationId", 5 }, { "op", "connect" }, { "fromId", 11 }, { "toId", 12 }, { "wireId", 30 } };
		const auto parsedConnect = F4T::WorkshopProtocol::NormalizeWireOp(connect);
		assert(parsedConnect && parsedConnect->kind == F4T::WorkshopProtocol::WireOpKind::Connect && parsedConnect->wireId == 30 && parsedConnect->fromId == 11 && parsedConnect->toId == 12);
		Json unbound = connect;
		unbound.erase("wireId");
		const auto parsedUnbound = F4T::WorkshopProtocol::NormalizeWireOp(unbound);
		assert(parsedUnbound && parsedUnbound->wireId == 0);
		Json zeroWire = connect;
		zeroWire["wireId"] = 0;
		assert(!F4T::WorkshopProtocol::NormalizeWireOp(zeroWire));
	}

	return 0;
}
