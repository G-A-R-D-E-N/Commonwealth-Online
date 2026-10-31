// source:CO-BETA/docs/protocol/packets.md
#include "F4TNpcCoopValidation.h"
#include "F4TNpcLootSync.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::NpcLootSync;
	using F4T::NpcCoopValidation::NormalizeNpcLoot;
	using F4T::NpcCoopValidation::NormalizeNpcLootTake;

	std::string Hex(std::uint32_t a_value)
	{
		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", a_value);
		return buffer;
	}

	Json LootPacket(const std::vector<LootItem>& a_items, std::uint32_t a_revision = 1)
	{
		Json items = Json::array();
		for (const auto& item : a_items) {
			items.push_back(Json{ { "formId", Hex(item.formId) }, { "count", item.count } });
		}
		return Json{
			{ "type", "npcLoot" },
			{ "npcId", 42 },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "sourceFormId", "1234" },
			{ "authorityEpoch", 2 },
			{ "revision", a_revision },
			{ "playerId", 1 },
			{ "items", items }
		};
	}

	Json TakePacket(const std::string& a_operationId, std::uint32_t a_formId, std::uint32_t a_count)
	{
		return Json{
			{ "type", "npcLootTake" },
			{ "operationId", a_operationId },
			{ "npcId", 42 },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "sourceFormId", "1234" },
			{ "authorityEpoch", 2 },
			{ "formId", Hex(a_formId) },
			{ "count", a_count },
			{ "playerId", 2 }
		};
	}

	Json ResultPacket(const char* a_status, std::uint32_t a_revision)
	{
		return Json{
			{ "type", "npcLootResult" },
			{ "operationId", "t2-1" },
			{ "status", a_status },
			{ "formId", "0001F669" },
			{ "count", 3 },
			{ "revision", a_revision },
			{ "takerPlayerId", 2 }
		};
	}

	TakeResult Accepted()
	{
		return TakeResult{ .operationId = "t2-1", .accepted = true, .reason = "", .formId = 0x0001F669, .count = 3, .revision = 4, .takerPlayerId = 2 };
	}
}

int main()
{
	{
		const std::vector<LootItem> raw{
			{ 0x0001F669, 3 },
			{ 0x0001F669, 2 },
			{ 0xFF000001, 2 },
			{ 0x00000000, 1 },
			{ 0x00000F00, 0 },
			{ 0x00000F00, 4 }
		};
		const auto publish = BuildPublishList(raw);
		assert(!publish.truncated);
		assert((publish.items == std::vector<LootItem>{ { 0x00000F00, 4 }, { 0x0001F669, 5 } }));
		assert(NormalizeNpcLoot(LootPacket(publish.items)).has_value());

		const std::vector<LootItem> clampRaw{ { 0x0001F669, 40000 }, { 0x0001F669, 40000 } };
		const auto clamped = BuildPublishList(clampRaw);
		assert((clamped.items == std::vector<LootItem>{ { 0x0001F669, 65535 } }));
		assert(NormalizeNpcLoot(LootPacket(clamped.items)).has_value());

		std::vector<LootItem> sixtyFour;
		for (std::uint32_t id = 1; id <= 64; ++id) {
			sixtyFour.push_back({ 0x1000 + id, id });
		}
		const auto atCap = BuildPublishList(sixtyFour);
		assert(!atCap.truncated);
		assert(atCap.items.size() == 64);
		assert(NormalizeNpcLoot(LootPacket(atCap.items)).has_value());

		std::vector<LootItem> sixtyFive = sixtyFour;
		sixtyFive.push_back({ 0x1000 + 65, 1 });
		const auto overCap = BuildPublishList(sixtyFive);
		assert(overCap.truncated);
		assert(overCap.items.empty());

		std::vector<LootItem> runtimeNoise = sixtyFour;
		for (std::uint32_t id = 0; id < 10; ++id) {
			runtimeNoise.push_back({ 0xFF000001 + id, 1 });
		}
		const auto filteredCap = BuildPublishList(runtimeNoise);
		assert(!filteredCap.truncated);
		assert(filteredCap.items.size() == 64);

		const auto empty = BuildPublishList(std::vector<LootItem>{});
		assert(!empty.truncated);
		assert(empty.items.empty());
		assert(NormalizeNpcLoot(LootPacket(empty.items)).has_value());

		const std::vector<LootItem> onlyRuntime{ { 0xFF000000, 1 }, { 0xFFFFFFFF, 1 } };
		const auto runtimeOnly = BuildPublishList(onlyRuntime);
		assert(!runtimeOnly.truncated);
		assert(runtimeOnly.items.empty());
	}

	{
		const auto loot = ParseRemoteLoot(LootPacket({ { 0x0001F669, 3 }, { 0x0001F669, 2 }, { 0x00000F00, 1 } }, 7));
		assert(loot.has_value());
		assert((loot->identity == LootIdentity{ .npcId = 42, .sourceFormId = 0x1234, .cellId = 0xCAFE, .worldspaceId = 0xF00D, .authorityEpoch = 2 }));
		assert(loot->revision == 7);
		assert(loot->authorityPlayerId == 1);
		assert((loot->items == std::vector<LootItem>{ { 0x00000F00, 1 }, { 0x0001F669, 5 } }));

		Json noWorldspace = LootPacket({});
		noWorldspace["worldspaceId"] = "";
		const auto unscoped = ParseRemoteLoot(noWorldspace);
		assert(unscoped.has_value());
		assert(unscoped->identity.worldspaceId == 0);
		assert(unscoped->items.empty());

		const auto roundTrip = BuildPublishList(std::vector<LootItem>{ { 0x00000F00, 1 }, { 0x0001F669, 65535 } });
		const auto parsed = ParseRemoteLoot(LootPacket(roundTrip.items));
		assert(parsed.has_value());
		assert(parsed->items == roundTrip.items);

		Json missingPlayer = LootPacket({});
		missingPlayer.erase("playerId");
		assert(!ParseRemoteLoot(missingPlayer).has_value());

		Json zeroPlayer = LootPacket({});
		zeroPlayer["playerId"] = 0;
		assert(!ParseRemoteLoot(zeroPlayer).has_value());

		Json stringPlayer = LootPacket({});
		stringPlayer["playerId"] = "1";
		assert(!ParseRemoteLoot(stringPlayer).has_value());

		Json zeroRevision = LootPacket({});
		zeroRevision["revision"] = 0;
		assert(!ParseRemoteLoot(zeroRevision).has_value());

		assert(!ParseRemoteLoot(LootPacket({ { 0xFF000001, 1 } })).has_value());
	}

	{
		const auto request = ParseTakeRequest(TakePacket("t2-1", 0x0001F669, 3));
		assert(request.has_value());
		assert((request->identity == LootIdentity{ .npcId = 42, .sourceFormId = 0x1234, .cellId = 0xCAFE, .worldspaceId = 0xF00D, .authorityEpoch = 2 }));
		assert(request->takerPlayerId == 2);
		assert(request->operationId == "t2-1");
		assert(request->formId == 0x0001F669);
		assert(request->count == 3);

		Json missingPlayer = TakePacket("t2-1", 0x0001F669, 3);
		missingPlayer.erase("playerId");
		assert(!ParseTakeRequest(missingPlayer).has_value());
		assert(!ParseTakeRequest(TakePacket("t2-1", 0x0001F669, 0)).has_value());
		assert(!ParseTakeRequest(TakePacket("t2-1", 0xFF000000, 1)).has_value());
		assert(!ParseTakeRequest(TakePacket("", 0x0001F669, 1)).has_value());
	}

	{
		const auto accepted = ParseTakeResult(ResultPacket("accepted", 4));
		assert(accepted.has_value());
		assert(accepted->accepted);
		assert(accepted->operationId == "t2-1");
		assert(accepted->reason.empty());
		assert(accepted->formId == 0x0001F669);
		assert(accepted->count == 3);
		assert(accepted->revision == 4);
		assert(accepted->takerPlayerId == 2);

		Json rejectedPacket = ResultPacket("rejected", 0);
		rejectedPacket["reason"] = "takerAbsent";
		const auto rejected = ParseTakeResult(rejectedPacket);
		assert(rejected.has_value());
		assert(!rejected->accepted);
		assert(rejected->reason == "takerAbsent");
		assert(rejected->revision == 0);

		assert(!ParseTakeResult(ResultPacket("maybe", 4)).has_value());
		assert(!ParseTakeResult(ResultPacket("accepted", 0)).has_value());

		Json zeroTaker = ResultPacket("accepted", 4);
		zeroTaker["takerPlayerId"] = 0;
		assert(!ParseTakeResult(zeroTaker).has_value());

		Json longReason = ResultPacket("rejected", 0);
		longReason["reason"] = std::string(65, 'x');
		assert(!ParseTakeResult(longReason).has_value());
	}

	{
		const auto surplus = ComputeMirrorDiff(std::vector<LootItem>{ { 1, 3 } }, std::vector<LootItem>{ { 1, 1 } });
		assert((surplus.removals == std::vector<LootItem>{ { 1, 2 } }));
		assert(surplus.additions.empty());

		const auto missing = ComputeMirrorDiff(std::vector<LootItem>{}, std::vector<LootItem>{ { 2, 4 } });
		assert(missing.removals.empty());
		assert((missing.additions == std::vector<LootItem>{ { 2, 4 } }));

		const auto equal = ComputeMirrorDiff(std::vector<LootItem>{ { 1, 3 }, { 2, 1 } }, std::vector<LootItem>{ { 2, 1 }, { 1, 3 } });
		assert(equal.removals.empty());
		assert(equal.additions.empty());

		const auto bothEmpty = ComputeMirrorDiff(std::vector<LootItem>{}, std::vector<LootItem>{});
		assert(bothEmpty.removals.empty());
		assert(bothEmpty.additions.empty());

		const auto removeAll = ComputeMirrorDiff(std::vector<LootItem>{ { 1, 3 }, { 1, 2 } }, std::vector<LootItem>{});
		assert((removeAll.removals == std::vector<LootItem>{ { 1, 5 } }));
		assert(removeAll.additions.empty());

		const auto split = ComputeMirrorDiff(std::vector<LootItem>{ { 1, 3 }, { 1, 2 } }, std::vector<LootItem>{ { 1, 5 } });
		assert(split.removals.empty());
		assert(split.additions.empty());

		const auto mixed = ComputeMirrorDiff(std::vector<LootItem>{ { 5, 1 }, { 3, 2 } }, std::vector<LootItem>{ { 4, 1 }, { 3, 5 } });
		assert((mixed.removals == std::vector<LootItem>{ { 5, 1 } }));
		assert((mixed.additions == std::vector<LootItem>{ { 3, 3 }, { 4, 1 } }));
	}

	assert(ClampToAvailable(0, 5) == 0);
	assert(ClampToAvailable(5, 5) == 5);
	assert(ClampToAvailable(9, 5) == 5);
	assert(ClampToAvailable(3, 0) == 0);

	assert(NextRevision(0) == 1);
	assert(NextRevision(41) == 42);
	assert(NextRevision(0xFFFFFFFE) == 0xFFFFFFFF);
	assert(NextRevision(0xFFFFFFFF) == 1);

	{
		RevisionCounter revisions;
		assert(revisions.Next(42) == 1);
		assert(revisions.Next(42) == 2);
		assert(revisions.Next(43) == 1);
		assert(revisions.Next(42) == 3);
		revisions.Clear();
		assert(revisions.Next(42) == 1);
	}

	assert(MakeOperationId(1, 1) == "t1-1");
	assert(MakeOperationId(1, 2) != MakeOperationId(2, 1));
	{
		const auto longest = MakeOperationId(0xFFFFFFFF, 0xFFFFFFFF);
		assert(!longest.empty() && longest.size() <= 64);
		assert(NormalizeNpcLootTake(TakePacket(longest, 0x0001F669, 1)).has_value());
	}

	{
		TakeCoalescer sums;
		assert(sums.Add(42, 1, 2));
		assert(sums.Add(42, 1, 3));
		assert(!sums.Add(42, 9, 0));
		assert((sums.Drain(8) == std::vector<CoalescedTake>{ { 42, 1, 5 } }));
		assert(sums.Drain(8).empty());

		TakeCoalescer order;
		assert(order.Add(42, 2, 1));
		assert(order.Add(42, 1, 1));
		assert(order.Add(43, 1, 1));
		assert((order.Drain(8) == std::vector<CoalescedTake>{ { 42, 2, 1 }, { 42, 1, 1 }, { 43, 1, 1 } }));

		TakeCoalescer pacing;
		for (std::uint32_t id = 1; id <= 20; ++id) {
			assert(pacing.Add(42, id, 1));
		}
		assert(pacing.Drain(8).size() == 8);
		assert(pacing.Drain(8).size() == 8);
		const auto tail = pacing.Drain(8);
		assert(tail.size() == 4);
		assert(tail.front().formId == 17);
		assert(tail.back().formId == 20);
		assert(pacing.Drain(8).empty());

		TakeCoalescer bounded;
		for (std::uint32_t id = 1; id <= 64; ++id) {
			assert(bounded.Add(42, id, 1));
		}
		assert(!bounded.Add(42, 65, 1));
		assert(bounded.Add(42, 1, 1));
		const auto full = bounded.Drain(64);
		assert(full.size() == 64);
		assert(full.front().count == 2);
		assert(bounded.Drain(64).empty());

		TakeCoalescer saturate;
		assert(saturate.Add(1, 1, 60000));
		assert(saturate.Add(1, 1, 60000));
		assert(saturate.Drain(1).front().count == 65535);

		TakeCoalescer zero;
		assert(zero.Add(1, 1, 1));
		assert(zero.Drain(0).empty());
		assert(zero.Drain(1).size() == 1);
	}

	{
		EchoLedger ledger;
		ledger.Expect(7, 0x0001F669, 3, 1000);
		assert(!ledger.Consume(8, 0x0001F669, 3, 1100));
		assert(!ledger.Consume(7, 0x00000F00, 3, 1100));
		assert(ledger.Consume(7, 0x0001F669, 3, 1100));
		assert(!ledger.Consume(7, 0x0001F669, 3, 1100));

		EchoLedger partial;
		partial.Expect(7, 0x0001F669, 5, 1000);
		assert(partial.Consume(7, 0x0001F669, 3, 1000));
		assert(!partial.Consume(7, 0x0001F669, 3, 1000));
		assert(!partial.Consume(7, 0x0001F669, 1, 1000));

		EchoLedger overshoot;
		overshoot.Expect(7, 0x0001F669, 2, 1000);
		assert(!overshoot.Consume(7, 0x0001F669, 5, 1000));
		assert(!overshoot.Consume(7, 0x0001F669, 1, 1000));

		EchoLedger lifetime;
		lifetime.Expect(7, 0x0001F669, 1, 1000);
		assert(lifetime.Consume(7, 0x0001F669, 1, 3000));
		lifetime.Expect(7, 0x0001F669, 1, 1000);
		assert(!lifetime.Consume(7, 0x0001F669, 1, 3001));

		EchoLedger expiry;
		expiry.Expect(7, 0x0001F669, 1, 0);
		expiry.Expire(2001);
		assert(!expiry.Consume(7, 0x0001F669, 1, 2001));

		EchoLedger ignored;
		ignored.Expect(7, 0x0001F669, 0, 0);
		assert(!ignored.Consume(7, 0x0001F669, 1, 0));
		assert(!ignored.Consume(7, 0x0001F669, 0, 0));

		EchoLedger full;
		for (std::uint32_t id = 1; id <= 256; ++id) {
			full.Expect(7, id, 1, 0);
		}
		full.Expect(7, 1000, 1, 0);
		assert(!full.Consume(7, 1, 1, 0));
		assert(full.Consume(7, 2, 1, 0));
		assert(full.Consume(7, 1000, 1, 0));
	}

	{
		OperationLru lru;
		lru.Remember(2, "t2-1", Accepted());
		assert((lru.Find(2, "t2-1") == std::optional<TakeResult>{ Accepted() }));
		assert(!lru.Find(3, "t2-1").has_value());
		assert(!lru.Find(2, "t2-2").has_value());

		TakeResult rejected = Accepted();
		rejected.accepted = false;
		rejected.reason = "unavailable";
		lru.Remember(2, "t2-1", rejected);
		assert((lru.Find(2, "t2-1") == std::optional<TakeResult>{ rejected }));

		OperationLru evict;
		for (int index = 0; index < 64; ++index) {
			evict.Remember(1, "op" + std::to_string(index), Accepted());
		}
		assert(evict.Find(1, "op0").has_value());
		evict.Remember(1, "op64", Accepted());
		assert(!evict.Find(1, "op1").has_value());
		assert(evict.Find(1, "op0").has_value());
		assert(evict.Find(1, "op64").has_value());

		OperationLru duplicates;
		for (int index = 0; index < 100; ++index) {
			duplicates.Remember(1, "dup", Accepted());
		}
		for (int index = 0; index < 63; ++index) {
			duplicates.Remember(1, "other" + std::to_string(index), Accepted());
		}
		assert(duplicates.Find(1, "dup").has_value());
		assert(duplicates.Find(1, "other0").has_value());
	}

	{
		const RemoteLoot loot{
			LootIdentity{ 42, 0x00012345, 0x0001F4A6, 0x0000003C, 2 },
			4,
			1,
			std::vector<LootItem>{ { 0x00000F00, 1 }, { 0x0001F669, 3 } }
		};
		const auto lootRoundTrip = ParseRemoteLoot(RemoteLootToJson(loot));
		assert(lootRoundTrip.has_value());
		assert(*lootRoundTrip == loot);

		const RemoteLoot emptyLoot{ LootIdentity{ 42, 0x00012345, 0x0001F4A6, 0, 2 }, 1, 1, {} };
		const auto emptyRoundTrip = ParseRemoteLoot(RemoteLootToJson(emptyLoot));
		assert(emptyRoundTrip.has_value());
		assert(*emptyRoundTrip == emptyLoot);
		assert(RemoteLootToJson(emptyLoot).at("worldspaceId").get<std::string>().empty());
		assert(RemoteLootToJson(loot).at("worldspaceId").get<std::string>() == Hex(0x0000003C));
		const auto normalizedEmpty = NormalizeNpcLoot(RemoteLootToJson(emptyLoot));
		assert(normalizedEmpty.has_value());
		assert(normalizedEmpty->at("worldspaceId").get<std::string>().empty());
		const auto normalizedExterior = NormalizeNpcLoot(RemoteLootToJson(loot));
		assert(normalizedExterior.has_value());
		assert(normalizedExterior->at("worldspaceId").get<std::string>() == Hex(0x0000003C));
		const TakeRequest emptyTake{ LootIdentity{ 42, 0x00012345, 0x0001F4A6, 0, 2 }, 2, "t2-9", 0x0001F669, 3 };
		const auto emptyTakeRoundTrip = ParseTakeRequest(TakeRequestToJson(emptyTake));
		assert(emptyTakeRoundTrip.has_value());
		assert(*emptyTakeRoundTrip == emptyTake);
		assert(TakeRequestToJson(emptyTake).at("worldspaceId").get<std::string>().empty());

		const TakeRequest request{ LootIdentity{ 42, 0x00012345, 0x0001F4A6, 0x0000003C, 2 }, 2, "t2-7", 0x0001F669, 3 };
		const auto requestRoundTrip = ParseTakeRequest(TakeRequestToJson(request));
		assert(requestRoundTrip.has_value());
		assert(*requestRoundTrip == request);

		const TakeResult accepted{ "t2-7", true, "", 0x0001F669, 3, 4, 2 };
		const auto acceptedRoundTrip = ParseTakeResult(TakeResultToJson(accepted));
		assert(acceptedRoundTrip.has_value());
		assert(*acceptedRoundTrip == accepted);

		const TakeResult rejected{ "t2-8", false, "unavailable", 0x0001F669, 1, 0, 2 };
		const auto rejectedRoundTrip = ParseTakeResult(TakeResultToJson(rejected));
		assert(rejectedRoundTrip.has_value());
		assert(*rejectedRoundTrip == rejected);

		assert(ParseTakeRequest(TakeRequestToJson(request))->operationId == "t2-7");
	}

	return 0;
}
