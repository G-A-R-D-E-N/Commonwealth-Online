#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TSnapshotDeltaWire.h"
#include "F4TSnapshotReceiver.h"

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

	Json Funded(std::uint32_t a_count = 5u)
	{
		auto root = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		F4T::Identity::TokenHash hash{};
		for (const std::string profile : { "a", "b", "c" }) {
			F4T::DurableState::SaveResumeCredential(root, profile, hash, std::nullopt, true, 5000.0, 1000.0);
			assert(F4T::DurableState::InitializeInventory(root, profile, profile == "a" ? Json::array({
				Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", a_count } } }) : Json::array(), 1000.0));
		}
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
		assert(total == 5);
	}

	void Invalid(const Json& a_root)
	{
		bool threw = false;
		try { F4T::DurableState::ValidateRoot(a_root); }
		catch (const std::exception&) { threw = true; }
		assert(threw);
	}
	%:include "F4TStableDropProjectionTests.inc"
}

int main()
{
	TestStableDropProjection();
	auto root = Funded();
	auto schema3 = root;
	schema3["schemaVersion"] = 3;
	schema3.erase("drops");
	for (auto& profile : schema3["profiles"]) profile.erase("inventoryOperations");
	schema3["inventoryAuthorityBlocked"] = true;
	const auto migrated = F4T::DurableState::LoadAndMigrate(schema3, 1001.0);
	assert(migrated.migrated && migrated.root.at("schemaVersion") == 4 && migrated.root.at("inventoryAuthorityBlocked") == true);
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
	overflowing["profiles"]["b"]["inventory"]["items"][key]["count"] = 65535u;
	const auto dropped = DropLedger::Drop(overflowing, "a", Request("overflow-drop", 0), position);
	const auto overflowBefore = overflowing;
	assert(DropLedger::Pickup(overflowing, "b", Pickup("overflow", 0, dropped.packet.at("entityId").get<EntityId>()), position).packet.at("reason") == "capacityExceeded");
	assert(overflowing == overflowBefore);
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
}
