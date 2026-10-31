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

	std::vector<ObjectState> objects;
	for (std::uint64_t id = 1; id <= 40; ++id) {
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

	return 0;
}
