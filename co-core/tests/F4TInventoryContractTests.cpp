#include "F4TInventoryContract.h"
#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TFormIdentity.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::InventoryContract;
	using F4T::Drops::DropLedger;
	using F4T::Drops::InventoryPosition;
	using F4T::Drops::Outcome;
	using F4T::FormIdentity::Kind;
	using F4T::FormIdentity::SplitFormId;

	const InventoryPosition position{ "CAFE", "F00D", 1.0F, 2.0F, 3.0F };
	const std::string key = "test.esm:00000123";

	std::string Expected(const std::string& a_plugin, const std::string& a_form)
	{
		return F4T::DurableState::NormalizeStartingInventory(Json::array({ Json{ { "plugin", a_plugin }, { "localFormId", a_form }, { "count", 1u } } })).begin().key();
	}

	std::string ItemKey()
	{
		const auto itemKey = MakeItemKey("Test.esm", 0x123, Kind::Regular);
		assert(itemKey);
		return *itemKey;
	}

	std::string Op(std::uint32_t a_counter)
	{
		const auto operation = MakeOperationId("session", a_counter);
		assert(operation);
		return *operation;
	}

	Json Funded(std::uint32_t a_count)
	{
		auto root = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		F4T::Identity::TokenHash hash{};
		for (const std::string profile : { "a", "b" }) {
			F4T::DurableState::SaveResumeCredential(root, profile, hash, std::nullopt, true, 5000.0, 1000.0);
			assert(F4T::DurableState::InitializeInventory(root, profile, profile == "a" ? Json::array({
				Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", a_count } } }) : Json::array(), 1000.0));
		}
		F4T::DurableState::ValidateRoot(root);
		return root;
	}

	void TestItemKeys()
	{
		assert(MakeItemKey("Test.esm", 0x123, Kind::Regular) == Expected("Test.esm", "123"));
		assert(MakeItemKey("TEST.ESM", 0x123, Kind::Regular) == key);
		assert(MakeItemKey("Fallout4.esm", 0x0001F4A6U, Kind::Regular) == Expected("Fallout4.esm", "0001F4A6"));
		const auto light = SplitFormId(0xFE000ABCU);
		assert(light.kind == Kind::Light);
		assert(MakeItemKey("CommonwealthOnline.esp", light.localId, light.kind) == Expected("CommonwealthOnline.esp", "00000ABC"));
		assert(!MakeItemKey("Test:esm", 0x123, Kind::Regular));
		assert(!MakeItemKey("", 0x123, Kind::Regular));
		assert(!MakeItemKey("Test.esm", 0, Kind::Regular));
		assert(!MakeItemKey("CommonwealthOnline.esp", 0x1000U, Kind::Light));
		assert(SplitFormId(0xFF000800U).kind == Kind::Runtime);
		assert(!MakeItemKey("Test.esm", 0x800U, Kind::Runtime));
		const auto parsed = ParseItemKey(key);
		assert(parsed && parsed->plugin == "test.esm" && parsed->localId == 0x123U);
		assert(MakeItemKey(parsed->plugin, parsed->localId, Kind::Regular) == key);
		assert(!ParseItemKey("Test.esm:00000123"));
		assert(!ParseItemKey("test.esm:123"));
		assert(!ParseItemKey("test.esm:0000000g"));
	}

	void TestBuiltRequestsCommit()
	{
		auto root = Funded(5);
		const auto drop = BuildDropRequest(Op(1), 0, ItemKey(), 5);
		assert(drop);
		const auto dropped = DropLedger::Drop(root, "a", *drop, position);
		assert(dropped.outcome == Outcome::Committed);
		const auto dropResult = ParseOwnedResult(dropped.packet, OwnedOperation::Drop);
		assert(dropResult && dropResult->committed && dropResult->revision == 1U && dropResult->entityId == F4T::Drops::kFirstDroppedEntityId);
		const auto pickup = BuildPickupRequest(Op(2), 0, *dropResult->entityId);
		assert(pickup);
		const auto picked = DropLedger::Pickup(root, "b", *pickup, position);
		assert(picked.outcome == Outcome::Committed);
		assert(ParseOwnedResult(picked.packet, OwnedOperation::Pickup) && ParseOwnedResult(picked.packet, OwnedOperation::Pickup)->committed);
		F4T::DurableState::ValidateRoot(root);
	}

	void TestServerInventory()
	{
		const auto root = Funded(5);
		const auto inventory = F4T::DurableState::GetInventory(root, "a");
		assert(inventory);
		const auto view = ParseServerInventory(*inventory);
		assert(view && view->revision == 0U && view->items.size() == 1 && view->items.at(key).plugin == "test.esm" &&
			view->items.at(key).localFormId == "00000123" && view->items.at(key).count == 5U);
		Json bad = *inventory;
		bad["items"][key]["count"] = 0U;
		assert(!ParseServerInventory(bad));
		bad["items"][key]["count"] = 65536U;
		assert(!ParseServerInventory(bad));
		bad = *inventory;
		bad["items"]["other.esm:00000123"] = bad["items"][key];
		bad["items"].erase(key);
		assert(!ParseServerInventory(bad));
		bad = *inventory;
		bad["initialized"] = false;
		assert(!ParseServerInventory(bad));
		for (const std::uint32_t entries : { 256U, 257U }) {
			bad = *inventory;
			bad["items"] = Json::object();
			for (std::uint32_t index = 1; index <= entries; ++index) {
				char form[9]{};
				std::snprintf(form, sizeof(form), "%08X", index);
				bad["items"][std::string{ "test.esm:" } + form] = Json{ { "plugin", "test.esm" }, { "localFormId", form }, { "count", 1U } };
			}
			assert(ParseServerInventory(bad).has_value() == (entries == 256U));
		}
	}

	void TestOwnedResults()
	{
		auto root = Funded(5);
		const auto firstRequest = *BuildDropRequest(Op(1), 0, ItemKey(), 5);
		const auto first = DropLedger::Drop(root, "a", firstRequest, position);
		const auto committed = ParseOwnedResult(first.packet, OwnedOperation::Drop);
		assert(committed && committed->committed && committed->revision == 1U && committed->entityId == F4T::Drops::kFirstDroppedEntityId &&
			committed->reason == OwnedReason::None && committed->drop.at("itemKey") == key);
		const auto replay = DropLedger::Drop(root, "a", firstRequest, position);
		assert(replay.outcome == Outcome::Replayed && ParseOwnedResult(replay.packet, OwnedOperation::Drop) && ParseOwnedResult(replay.packet, OwnedOperation::Drop)->committed);
		const auto stale = DropLedger::Drop(root, "a", *BuildDropRequest(Op(2), 0, ItemKey(), 1), position);
		const auto staleResult = ParseOwnedResult(stale.packet, OwnedOperation::Drop);
		assert(staleResult && !staleResult->committed && staleResult->reason == OwnedReason::StaleRevision && staleResult->revision == 1U);
		const auto insufficient = DropLedger::Drop(root, "a", *BuildDropRequest(Op(3), 1, ItemKey(), 1), position);
		const auto insufficientResult = ParseOwnedResult(insufficient.packet, OwnedOperation::Drop);
		assert(insufficientResult && insufficientResult->reason == OwnedReason::InsufficientItems);
		const Json rateLimited{ { "type", "inventoryDropResult" }, { "operationId", Op(4) }, { "status", "rejected" }, { "reason", "rateLimited" } };
		const auto limited = ParseOwnedResult(rateLimited, OwnedOperation::Drop);
		assert(limited && !limited->revision && limited->reason == OwnedReason::RateLimited);
		Json futureReason = rateLimited;
		futureReason["reason"] = "someFutureReason";
		const auto unknown = ParseOwnedResult(futureReason, OwnedOperation::Drop);
		assert(unknown && unknown->reason == OwnedReason::Unknown);
		Json highEntity = first.packet;
		highEntity["entityId"] = 0x7FFFFFFFU;
		assert(!ParseOwnedResult(highEntity, OwnedOperation::Drop));
		Json pending = rateLimited;
		pending["status"] = "pending";
		assert(!ParseOwnedResult(pending, OwnedOperation::Drop));
		assert(!ParseOwnedResult(first.packet, OwnedOperation::Pickup));
		Json noRevision = first.packet;
		noRevision.erase("revision");
		assert(!ParseOwnedResult(noRevision, OwnedOperation::Drop));
		Json extra = rateLimited;
		extra["unexpected"] = 1U;
		assert(!ParseOwnedResult(extra, OwnedOperation::Drop));
		const Json pickupStub{ { "type", "inventoryPickupResult" }, { "operationId", Op(5) }, { "entityId", F4T::Drops::kFirstDroppedEntityId },
			{ "status", "rejected" }, { "reason", "inventoryAuthorityUnavailable" } };
		const auto stub = ParseOwnedResult(pickupStub, OwnedOperation::Pickup);
		assert(stub && !stub->committed && stub->reason == OwnedReason::InventoryAuthorityUnavailable && stub->entityId == F4T::Drops::kFirstDroppedEntityId);
	}

	void TestOperationIdBounds()
	{
		const std::string maximum(64, 'a');
		const std::string overlong(65, 'a');
		assert(BuildDropRequest(maximum, 0, key, 1) && !BuildDropRequest(overlong, 0, key, 1) && !BuildDropRequest("", 0, key, 1));
		assert(BuildPickupRequest(maximum, 0, F4T::Drops::kFirstDroppedEntityId) && !BuildPickupRequest(overlong, 0, F4T::Drops::kFirstDroppedEntityId));
		assert(!BuildPickupRequest(maximum, 0, F4T::Drops::kFirstDroppedEntityId - 1U));
		assert(!BuildDropRequest(maximum, 0, key, 0) && !BuildDropRequest(maximum, 0, key, 65536U));
		assert(!BuildDropRequest(maximum, 0, "Test.esm:00000123", 1));
		assert(MakeOperationId("session", 1) == std::string{ "session:1" });
		assert(MakeOperationId(std::string(62, 'n'), 1) && MakeOperationId(std::string(62, 'n'), 1)->size() == 64);
		assert(!MakeOperationId(std::string(63, 'n'), 1) && !MakeOperationId("", 1));
	}
}

int main()
{
	TestItemKeys();
	TestBuiltRequestsCommit();
	TestServerInventory();
	TestOwnedResults();
	TestOperationIdBounds();
}
