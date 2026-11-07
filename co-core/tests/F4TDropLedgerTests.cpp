#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TInventorySnapshot.h"
#include "F4TInventoryWire.h"
#include "F4TInventoryContract.h"
#include "F4TSnapshotDeltaWire.h"
#include "F4TSnapshotReceiver.h"
#include "StartingGrantFixture.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::Drops;
	const InventoryPosition position{ "CAFE", "F00D", 1.0F, 2.0F, 3.0F };
	const std::string key = "test.esm:00000123";

	Json Request(std::string a_operation, std::uint32_t a_revision, std::uint32_t a_count = 1u)
	{
		return Json{ { "type", "inventoryDropRequest" }, { "operationId", a_operation },
			{ "expectedRevision", a_revision }, { "itemKey", key }, { "count", a_count } };
	}

	Json Pickup(std::string a_operation, std::uint32_t a_revision, EntityId a_entity)
	{
		return Json{ { "type", "inventoryPickupRequest" }, { "operationId", a_operation },
			{ "expectedRevision", a_revision }, { "entityId", a_entity } };
	}

	Json ContainerRequest(std::string a_operation, std::uint32_t a_revision, std::string a_direction, std::uint32_t a_count = 1u, std::string a_cell = "0000CAFE")
	{
		return Json{ { "type", "inventoryContainerRequest" }, { "operationId", a_operation }, { "expectedRevision", a_revision },
			{ "direction", a_direction }, { "container", Json{ { "cellId", a_cell }, { "refId", "0000BEEF" } } }, { "itemKey", key }, { "count", a_count } };
	}

	Json Funded(std::uint32_t a_count = 5u)
	{
		auto root = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		F4T::Identity::TokenHash hash{};
		for (const std::string profile : { "a", "b", "c" }) {
			F4T::DurableState::SaveResumeCredential(root, profile, hash, std::nullopt, true, 5000.0, 1000.0);
			assert(F4T::DurableState::InitializeInventory(root, profile, profile == "a" ? Json::array({
				Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", a_count } } }) : Json::array(), 1000.0));
		}
		ReconcileStartingGrantFixture(root, "a");
		F4T::DurableState::ValidateRoot(root);
		return root;
	}

	void Validate(const Json& a_root)
	{
		F4T::DurableState::ValidateRoot(a_root);
		std::uint32_t total = 0;
		for (const auto& profile : a_root.at("profiles")) {
			const auto& items = profile.at("inventory").at("items");
			if (items.contains(key)) total += items.at(key).at("count").get<std::uint32_t>();
		}
		for (const auto& drop : a_root.at("drops").at("live")) total += drop.at("item").at("count").get<std::uint32_t>();
		for (const auto& container : a_root.at("containers")) {
			if (container.at("items").contains(key)) total += container.at("items").at(key).at("count").get<std::uint32_t>();
		}
		assert(total == 5);
	}

	void Invalid(const Json& a_root)
	{
		bool threw = false;
		try { F4T::DurableState::ValidateRoot(a_root); }
		catch (const std::exception&) { threw = true; }
		assert(threw);
	}
	void TestStableDropProjection()
	{
		using namespace F4T::Replication;
		StableDroppedObject object{ Json{ { "plugin", std::string(128, 'a') }, { "localFormId", "00ABCDEF" }, { "count", 65535u } }, position };
		const auto bytes = EncodeStableDroppedObject(object);
		assert(bytes && bytes->size() == 156);
		const auto decoded = DecodeStableDroppedObject(*bytes);
		assert(decoded && decoded->item == object.item && decoded->position.cellId == "0000CAFE" && decoded->position.worldspaceId == "0000F00D");
		assert(decoded->position.x == position.x && decoded->position.y == position.y && decoded->position.z == position.z);
		assert(EncodeStableDroppedObject(*decoded) == bytes);
		for (std::size_t size = 0; size < bytes->size(); ++size) assert(!DecodeStableDroppedObject(std::span{ *bytes }.first(size)));
		auto malformed = *bytes;
		malformed.push_back(0);
		assert(!DecodeStableDroppedObject(malformed));
		for (const std::size_t offset : { 0u, 1u, 2u }) {
			malformed = *bytes;
			malformed[offset] = offset == 2 ? '/' : 0;
			assert(!DecodeStableDroppedObject(malformed));
		}
		malformed = *bytes;
		malformed[2] = 'A';
		assert(!DecodeStableDroppedObject(malformed));
		for (const auto [offset, length] : { std::pair{ 130u, 4u }, std::pair{ 134u, 2u }, std::pair{ 136u, 4u } }) {
			malformed = *bytes;
			std::fill_n(malformed.begin() + offset, length, 0);
			assert(!DecodeStableDroppedObject(malformed));
		}
		malformed = *bytes;
		std::fill_n(malformed.end() - 12, 4, 0xFF);
		assert(!DecodeStableDroppedObject(malformed));
		object.position.worldspaceId.clear();
		const auto interior = DecodeStableDroppedObject(*EncodeStableDroppedObject(object));
		assert(interior && interior->position.worldspaceId.empty());
		object.item["plugin"] = std::string(129, 'a');
		assert(!EncodeStableDroppedObject(object));
		object.item["plugin"] = "test.esm";
		object.item["count"] = 65536u;
		assert(!EncodeStableDroppedObject(object));
		object.item["count"] = 1u;
		object.position.x = (std::numeric_limits<float>::infinity)();
		assert(!EncodeStableDroppedObject(object));
		auto root = Funded(65535);
		const auto large = Json{ { "plugin", std::string(128, 'a') }, { "localFormId", "00000123" }, { "count", 65535u } };
		const auto medium = Json{ { "plugin", std::string(13, 'b') }, { "localFormId", "00000123" }, { "count", 5u } };
		const auto small = Json{ { "plugin", "c" }, { "localFormId", "00000123" }, { "count", 5u } };
		root["profiles"]["a"]["inventory"]["items"] = F4T::DurableState::NormalizeStartingInventory(Json::array({ large }));
		root["profiles"]["b"]["inventory"]["items"] = F4T::DurableState::NormalizeStartingInventory(Json::array({ large, medium, small }));
		const auto largeKey = std::string(128, 'a') + ":00000123";
		for (std::uint32_t index = 0; index < 399; ++index) {
			const auto profile = index < 256 ? "a" : "b";
			const auto revision = index < 256 ? index : index - 256;
			auto request = Request("max-" + std::to_string(index), revision);
			request["itemKey"] = largeKey;
			assert(DropLedger::Drop(root, profile, request, position).outcome == Outcome::Committed);
		}
		for (const auto [revision, itemKey] : { std::pair{ 143u, std::string(13, 'b') + ":00000123" }, std::pair{ 144u, std::string{ "c:00000123" } } }) {
			auto request = Request("boundary-" + std::to_string(revision), revision);
			request["itemKey"] = itemKey;
			assert(DropLedger::Drop(root, "b", request, position).outcome == Outcome::Committed);
		}
		const auto projection = BuildStableDropSnapshot(root.at("drops").at("live"));
		assert(projection && projection->size() == 401);
		const auto baseline = EncodeBaseline(*projection, 1, Channel::Reliable);
		assert(baseline && baseline->size() == kMaximumDropBaselineBytes);
		DecodedFrame frame;
		assert(DecodeFrame(*baseline, frame, [](Family a_family) { return a_family == Family::StableDroppedItem; }) == DecodeStatus::Valid);
		SnapshotReceiver lateJoin;
		assert(lateJoin.AcceptBaseline(frame.baseline, frame.snapshotSequence));
		assert(lateJoin.CurrentView() == *projection);
		for (const auto& [component, data] : frame.baseline) assert(component.family == Family::StableDroppedItem && DecodeStableDroppedObject(data));
		const auto before = root;
		auto next = Request("over-byte-limit", 145);
		next["itemKey"] = "c:00000123";
		const auto overCapacity = DropLedger::Drop(root, "b", next, position);
		if (overCapacity.outcome != Outcome::Rejected) {
			std::fprintf(stderr, "Oversized funded drop committed: baseline=%zu limit=%zu rootUnchanged=%d\n",
				EncodeBaseline(*BuildStableDropSnapshot(root.at("drops").at("live")), 1, Channel::Reliable)->size(), kMaximumDropBaselineBytes, root == before);
		}
		assert(overCapacity.outcome == Outcome::Rejected && overCapacity.packet.at("reason") == "capacityExceeded");
		assert(root == before);
		auto replayRequest = Request("max-398", 142);
		replayRequest["itemKey"] = largeKey;
		assert(DropLedger::Drop(root, "b", replayRequest, position).outcome == Outcome::Replayed && root == before);
		root = F4T::DurableState::LoadAndMigrate(Json::parse(root.dump()), 1001.0).root;
		assert(BuildStableDropSnapshot(root.at("drops").at("live")) == projection);
		assert(DropLedger::Pickup(root, "b", Pickup("free-capacity", 145, kFirstDroppedEntityId), position).outcome == Outcome::Committed);
		next["expectedRevision"] = 146u;
		assert(DropLedger::Drop(root, "b", next, position).outcome == Outcome::Committed);
		F4T::DurableState::ValidateRoot(root);
		assert(EncodeBaseline(*BuildStableDropSnapshot(root.at("drops").at("live")), 2, Channel::Reliable)->size() < kMaximumDropBaselineBytes);
	}

	void TestCampKitMarker()
	{
		const auto id = std::to_string(kFirstDroppedEntityId);
		auto placed = Funded();
		assert(DropLedger::PlaceOwnedDrop(placed, "a", key, position));
		F4T::DurableState::ValidateRoot(placed);
		DropLedger::Validate(placed);
		assert(placed.at("drops").at("live").at(id).at(kCampKitSourceField) == kCampKitSource);
		assert(placed.at("profiles").at("a").at("inventoryOperations").empty());
		const auto projection = BuildStableDropSnapshot(placed.at("drops").at("live"));
		assert(projection && projection->size() == 1);

		auto unmarked = placed;
		unmarked["drops"]["live"][id].erase(std::string{ kCampKitSourceField });
		Invalid(unmarked);

		auto unknownKind = placed;
		unknownKind["drops"]["live"][id][kCampKitSourceField] = "playerDrop";
		Invalid(unknownKind);

		auto ownerless = placed;
		ownerless["drops"]["live"][id]["ownerProfileId"] = "missing";
		Invalid(ownerless);

		auto picked = placed;
		const auto pickup = DropLedger::Pickup(picked, "a", Pickup("camp-kit-pickup", 0, kFirstDroppedEntityId), position);
		assert(pickup.outcome == Outcome::Committed && pickup.packet.at("drop").at(kCampKitSourceField) == kCampKitSource);
		F4T::DurableState::ValidateRoot(picked);
		assert(picked.at("drops").at("live").empty());

		auto legacy = Funded();
		assert(DropLedger::Drop(legacy, "a", Request("legacy-drop", 0, 5), position).outcome == Outcome::Committed);
		const auto reloaded = F4T::DurableState::LoadAndMigrate(Json::parse(legacy.dump()), 1001.0).root;
		F4T::DurableState::ValidateRoot(reloaded);
		assert(!reloaded.at("drops").at("live").at(id).contains(kCampKitSourceField));

		auto markedCreation = legacy;
		markedCreation["profiles"]["a"]["inventoryOperations"][0]["result"]["drop"][kCampKitSourceField] = kCampKitSource;
		markedCreation["drops"]["live"][id][kCampKitSourceField] = kCampKitSource;
		Invalid(markedCreation);
	}
}

int main()
{
	TestStableDropProjection();
	TestCampKitMarker();
	auto root = Funded();
	auto pending = root;
	pending["profiles"]["a"]["startingGrant"]["granted"] = false;
		pending["profiles"]["a"]["startingGrant"]["recovery"] = Json{ { "state", "unstarted" } };
	const auto pendingBefore = pending;
	for (const auto& result : { DropLedger::Drop(pending, "a", Request("pending-drop", 0), position),
		DropLedger::Pickup(pending, "a", Pickup("pending-pickup", 0, kFirstDroppedEntityId), position),
		DropLedger::ContainerTransfer(pending, "a", ContainerRequest("pending-put", 0, "put"), position) }) {
		assert(result.outcome == Outcome::Rejected && result.packet.at("reason") == "startingInventoryPending" && !result.packet.contains("revision"));
	}
	assert(pending == pendingBefore);
	const auto pendingPickup = DropLedger::Pickup(pending, "a", Pickup("pending-pickup", 0, kFirstDroppedEntityId), position);
	const auto parsedPickup = F4T::InventoryContract::ParseOwnedResult(pendingPickup.packet, F4T::InventoryContract::OwnedOperation::Pickup);
	assert(parsedPickup && parsedPickup->reason == F4T::InventoryContract::OwnedReason::StartingInventoryPending && !parsedPickup->revision);
	const auto pendingContainer = DropLedger::ContainerTransfer(pending, "a", ContainerRequest("pending-put", 0, "put"), position);
	assert(F4T::InventoryWire::NormalizeContainerResult(pendingContainer.packet) == pendingContainer.packet);
	auto schema3 = root;
	schema3["schemaVersion"] = 3;
	schema3.erase("drops");
	for (auto& profile : schema3["profiles"]) profile.erase("inventoryOperations");
	schema3["inventoryAuthorityBlocked"] = true;
	const auto migrated = F4T::DurableState::LoadAndMigrate(schema3, 1001.0);
	assert(migrated.migrated && migrated.root.at("schemaVersion") == F4T::DurableState::CurrentSchemaVersion && migrated.root.at("inventoryAuthorityBlocked") == true);
	assert(migrated.root.at("profiles").at("a").at("inventory") == root.at("profiles").at("a").at("inventory"));
	assert(migrated.root.at("profiles").at("b").at("inventory").at("items").empty());
	assert(migrated.root.at("drops").at("live").empty());
	const auto before = root;
	const auto unowned = DropLedger::Drop(root, "missing", Request("unowned", 0), position);
	assert(unowned.outcome == Outcome::Rejected && root == before);
	assert(DropLedger::Drop(root, "b", Request("unowned", 0), position).packet.at("reason") == "insufficientItems");
	assert(root == before);
	const auto firstRequest = Request("drop-1", 0, 5);
	const auto first = DropLedger::Drop(root, "a", firstRequest, position);
	assert(first.outcome == Outcome::Committed && first.packet.at("revision") == 1);
	const auto entity = first.packet.at("entityId").get<EntityId>();
	assert(entity == kFirstDroppedEntityId);
	assert(root.at("profiles").at("a").at("inventory").at("items").empty());
	assert(!F4T::DurableState::InitializeInventory(root, "a", Json::array(), 1001.0));
	Validate(root);
	const auto committed = root;
	const auto replay = DropLedger::Drop(root, "a", firstRequest, position);
	assert(replay.outcome == Outcome::Replayed && replay.packet == first.packet && root == committed);
	assert(DropLedger::Drop(root, "a", Request("drop-1", 0, 1), position).packet.at("reason") == "operationMismatch");
	assert(DropLedger::Drop(root, "a", Request("drop-2", 0), position).packet.at("reason") == "staleRevision");
	assert(DropLedger::Drop(root, "a", Request("drop-2", 1), position).packet.at("reason") == "insufficientItems");
	assert(root == committed);
	auto wrongCell = position;
	wrongCell.cellId = "BEEF";
	const auto pickupRequest = Pickup("pickup-1", 0, entity);
	assert(DropLedger::Pickup(root, "b", pickupRequest, wrongCell).packet.at("reason") == "outOfScope");
	auto above = position;
	above.z += 257.0F;
	assert(DropLedger::Pickup(root, "b", pickupRequest, above).packet.at("reason") == "outOfScope");
	assert(root == committed);
	const auto pickup = DropLedger::Pickup(root, "b", pickupRequest, position);
	assert(pickup.outcome == Outcome::Committed && pickup.packet.at("revision") == 1);
	assert(root.at("drops").at("live").empty());
	assert(root.at("profiles").at("b").at("inventory").at("items").at(key).at("count") == 5);
	Validate(root);
	const auto picked = root;
	auto tampered = picked;
	tampered["profiles"]["a"]["inventory"]["revision"] = 0u;
	tampered["profiles"]["a"]["inventoryOperations"] = Json::array();
	tampered["profiles"]["b"]["inventory"]["revision"] = 0u;
	tampered["profiles"]["b"]["inventoryOperations"] = Json::array();
	tampered["drops"]["live"][std::to_string(entity)] = first.packet.at("drop");
	Invalid(tampered);
	tampered = committed;
	tampered["drops"]["nextEntityId"] = entity + 2u;
	tampered["drops"]["live"][std::to_string(entity + 1u)] = first.packet.at("drop");
	Invalid(tampered);
	tampered = picked;
	tampered["profiles"]["b"]["inventoryOperations"][0]["result"]["drop"]["item"]["count"] = 4u;
	Invalid(tampered);
	tampered = picked;
	tampered["profiles"]["a"]["inventoryOperations"] = Json::array();
	Invalid(tampered);
	tampered = picked;
	tampered["profiles"]["a"]["inventoryOperations"][0]["result"]["drop"]["ownerProfileId"] = "b";
	Invalid(tampered);
	tampered = picked;
	tampered["drops"]["live"][std::to_string(entity)] = first.packet.at("drop");
	Invalid(tampered);
	tampered = committed;
	tampered["drops"]["live"][std::to_string(entity)]["item"]["count"] = 4u;
	Invalid(tampered);
	tampered = before;
	tampered["profiles"]["a"]["inventoryOperations"] = Json::object();
	bool threw = false;
	try { (void)DropLedger::Drop(tampered, "a", firstRequest, position); }
	catch (const std::exception&) { threw = true; }
	assert(threw);
	assert(DropLedger::Pickup(root, "b", pickupRequest, wrongCell).packet == pickup.packet);
	assert(root == picked);
	assert(DropLedger::Pickup(root, "c", Pickup("race", 0, entity), position).packet.at("reason") == "unknownEntity");
	assert(DropLedger::Drop(root, "b", Request("pickup-1", 0), position).packet.at("reason") == "operationMismatch");
	assert(root == picked);
	auto restored = F4T::DurableState::LoadAndMigrate(Json::parse(root.dump()), 1002.0).root;
	assert(DropLedger::Drop(restored, "a", firstRequest, position).packet == first.packet);
	assert(DropLedger::Pickup(restored, "b", pickupRequest, position).packet == pickup.packet);
	Validate(restored);
	const auto next = DropLedger::Drop(restored, "b", Request("second", 1, 5), position);
	assert(next.packet.at("entityId") == entity + 1);
	Validate(restored);
	const auto nextBefore = restored;
	auto forged = Request("forged", 1);
	forged["profileId"] = "b";
	assert(DropLedger::Drop(restored, "a", forged, position).packet.at("reason") == "invalidRequest");
	forged = Request("forged", 1);
	forged["type"] = 9;
	assert(DropLedger::Drop(restored, "a", forged, position).packet.at("reason") == "invalidRequest");
	assert(restored == nextBefore);
	restored["inventoryAuthorityBlocked"] = true;
	const auto blocked = restored;
	assert(DropLedger::Drop(restored, "a", firstRequest, position).packet.at("reason") == "inventoryAuthorityUnavailable");
	assert(restored == blocked);

	auto bounded = Funded(65535);
	for (std::uint32_t index = 0; index < kMaximumLiveDropsPerPlayer; ++index) {
		assert(DropLedger::Drop(bounded, "a", Request("op-" + std::to_string(index), index), position).outcome == Outcome::Committed);
	}
	F4T::DurableState::ValidateRoot(bounded);
	assert(bounded.at("profiles").at("a").at("inventoryOperations").size() == kMaximumOperationHistory);
	const auto fullBefore = bounded;
	assert(DropLedger::Drop(bounded, "a", Request("full", 256), position).packet.at("reason") == "capacityExceeded");
	assert(bounded == fullBefore);
	assert(DropLedger::Drop(bounded, "a", Request("op-0", 0), position).packet.at("reason") == "staleRevision");
	assert(DropLedger::Drop(bounded, "a", Request("op-255", 255), position).outcome == Outcome::Replayed);
	auto exhausted = Funded();
	exhausted["drops"]["nextEntityId"] = 0xFFFFFFFFu;
	const auto exhaustedBefore = exhausted;
	assert(DropLedger::Drop(exhausted, "a", Request("exhausted", 0), position).packet.at("reason") == "capacityExceeded");
	assert(exhausted == exhaustedBefore);
	auto overflowing = Funded();
	overflowing["profiles"]["b"]["inventory"]["items"] = overflowing.at("profiles").at("a").at("inventory").at("items");
	overflowing["profiles"]["b"]["inventory"]["items"][key]["count"] = (std::numeric_limits<std::uint32_t>::max)();
	const auto dropped = DropLedger::Drop(overflowing, "a", Request("overflow-drop", 0), position);
	const auto overflowBefore = overflowing;
	assert(DropLedger::Pickup(overflowing, "b", Pickup("overflow", 0, dropped.packet.at("entityId").get<EntityId>()), position).packet.at("reason") == "capacityExceeded");
	assert(overflowing == overflowBefore);
	auto mixed = Funded(5);
	const auto mixedDrop = DropLedger::Drop(mixed, "a", Request("mixed-drop", 0, 2), position);
	assert(mixedDrop.outcome == Outcome::Committed);
	const auto snapshot = F4T::InventorySnapshot::NormalizeRequest("mixed-snap", 1,
		Json::array({ Json{ { "plugin", "test.esm" }, { "localFormId", "123" }, { "count", 4u } } }));
	assert(snapshot && F4T::InventorySnapshot::Reconcile(mixed, "a", *snapshot).changed);
	DropLedger::Validate(mixed);
	F4T::DurableState::ValidateRoot(mixed);
	const auto mixedPickup = DropLedger::Pickup(mixed, "b", Pickup("mixed-pickup", 0, mixedDrop.packet.at("entityId").get<EntityId>()), position);
	assert(mixedPickup.outcome == Outcome::Committed);
	DropLedger::Validate(mixed);
	assert(mixed.at("profiles").at("a").at("inventoryOperations").size() == 2);
	auto screened = Funded();
	const auto screenedBefore = screened;
	assert(!DropLedger::Precheck(screened, "a", Request("screen", 0), true));
	assert(DropLedger::Precheck(screened, "a", Request("screen", 1), true)->packet.at("reason") == "staleRevision");
	assert(DropLedger::Precheck(screened, "missing", Request("screen", 0), true)->packet.at("reason") == "inventoryAuthorityUnavailable");
	assert(DropLedger::Precheck(screened, "a", Request("screen", 0, 1), false)->packet.at("reason") == "invalidRequest");
	assert(screened == screenedBefore);
	const auto screenedDrop = DropLedger::Drop(screened, "a", Request("screen", 0, 5), position);
	const auto replayed = DropLedger::Precheck(screened, "a", Request("screen", 0, 5), true);
	assert(screenedDrop.outcome == Outcome::Committed && replayed && replayed->outcome == Outcome::Replayed && replayed->packet == screenedDrop.packet);
	assert(DropLedger::Precheck(screened, "a", Request("screen", 0, 1), true)->packet.at("reason") == "operationMismatch");
	assert(!DropLedger::Precheck(screened, "b", Request("fresh", 0), true));
	screened["inventoryAuthorityBlocked"] = true;
	assert(DropLedger::Precheck(screened, "b", Request("fresh", 0), true)->packet.at("reason") == "inventoryAuthorityUnavailable");

	auto contained = Funded();
	const auto stored = DropLedger::ContainerTransfer(contained, "a", ContainerRequest("put-1", 0, "put", 3), position);
	assert(stored.outcome == Outcome::Committed && stored.packet.at("revision") == 1 && stored.packet.at("containerRevision") == 1);
	assert(contained.at("containers").at("0000CAFE:0000BEEF").at("items").at(key).at("count") == 3);
	assert(contained.at("profiles").at("a").at("inventory").at("items").at(key).at("count") == 2);
	Validate(contained);
	const auto afterPut = contained;
	auto schema5WithContainer = contained;
	schema5WithContainer["schemaVersion"] = 5;
	const auto migratedContainer = F4T::DurableState::LoadAndMigrate(schema5WithContainer, 1002.0);
	assert(migratedContainer.migrated && migratedContainer.root == contained);
	assert(DropLedger::ContainerTransfer(contained, "a", ContainerRequest("put-1", 0, "put", 3), position).outcome == Outcome::Replayed);
	assert(DropLedger::ContainerTransfer(contained, "a", ContainerRequest("put-2", 1, "put", 3), position).packet.at("reason") == "insufficientItems");
	assert(DropLedger::ContainerTransfer(contained, "b", ContainerRequest("take-x", 0, "take", 1, "0000DEAD"), position).packet.at("reason") == "outOfScope");
	assert(DropLedger::ContainerTransfer(contained, "b", ContainerRequest("take-1", 0, "take", 4), position).packet.at("reason") == "insufficientItems");
	assert(contained == afterPut);
	const auto taken = DropLedger::ContainerTransfer(contained, "b", ContainerRequest("take-1", 0, "take", 2), position);
	assert(taken.outcome == Outcome::Committed && taken.packet.at("revision") == 1 && taken.packet.at("containerRevision") == 2);
	assert(contained.at("profiles").at("b").at("inventory").at("items").at(key).at("count") == 2);
	assert(contained.at("containers").at("0000CAFE:0000BEEF").at("items").at(key).at("count") == 1);
	Validate(contained);
	assert(DropLedger::ContainerTransfer(contained, "b", ContainerRequest("take-1", 0, "take", 2), position).outcome == Outcome::Replayed);
	assert(DropLedger::ContainerTransfer(contained, "b", ContainerRequest("take-1", 0, "take", 1), position).packet.at("reason") == "operationMismatch");
	assert(DropLedger::ContainerTransfer(contained, "b", ContainerRequest("take-2", 0, "take", 1), position).packet.at("reason") == "staleRevision");
	assert(DropLedger::ContainerTransfer(contained, "b", ContainerRequest("take-2", 1, "take", 2), position).packet.at("reason") == "insufficientItems");
	const auto emptied = DropLedger::ContainerTransfer(contained, "b", ContainerRequest("take-2", 1, "take", 1), position);
	assert(emptied.outcome == Outcome::Committed && emptied.packet.at("containerRevision") == 3);
	assert(contained.at("containers").at("0000CAFE:0000BEEF").at("items").empty());
	Validate(contained);
	assert(F4T::DurableState::LoadAndMigrate(Json::parse(contained.dump()), 1003.0).root == contained);
	auto unlinked = contained;
	unlinked["profiles"]["a"]["inventoryOperations"][0]["result"]["containerRevision"] = 9u;
	Invalid(unlinked);
	unlinked = contained;
	unlinked["containers"]["0000CAFE:0000BEEF"]["revision"] = 2u;
	Invalid(unlinked);
	unlinked = contained;
	unlinked["containers"] = Json::object();
	Invalid(unlinked);
	unlinked = contained;
	unlinked["containers"]["0000CAFE:0000BEEF"]["items"]["bad"] = Json{ { "plugin", "test.esm" }, { "localFormId", "123" }, { "count", 1u } };
	Invalid(unlinked);
	auto blockedContainer = contained;
	blockedContainer["inventoryAuthorityBlocked"] = true;
	assert(DropLedger::ContainerTransfer(blockedContainer, "a", ContainerRequest("blocked", 1, "put", 1), position).packet.at("reason") == "inventoryAuthorityUnavailable");
	auto malformedLive = contained;
	malformedLive["drops"]["live"]["123x"] = first.packet.at("drop");
	Invalid(malformedLive);
	malformedLive = contained;
	malformedLive["drops"]["live"]["99999999999999"] = first.packet.at("drop");
	Invalid(malformedLive);
}
