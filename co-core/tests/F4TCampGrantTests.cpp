#include "F4TCampGrants.h"
#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "F4TWorkshopCosts.h"
#include "F4TWorkshopTransactions.h"
#include "StartingGrantFixture.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::CampGrants;
	using F4T::Trade::Line;

	const std::string keyOne = "test.esm:00000123";
	const std::string keyTwo = "test.esm:00000456";

	Json Funded()
	{
		auto root = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		F4T::Identity::TokenHash hash{};
		F4T::DurableState::SaveResumeCredential(root, "a", hash, std::nullopt, true, 5000.0, 1000.0);
		assert(F4T::DurableState::InitializeInventory(root, "a", Json::array({
			Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", 5u } } }), 1000.0));
		ReconcileStartingGrantFixture(root, "a");
		F4T::Drops::DropLedger::Validate(root);
		return root;
	}

	std::uint32_t Count(const Json& a_root, const std::string& a_key)
	{
		const auto& items = a_root.at("profiles").at("a").at("inventory").at("items");
		const auto found = items.find(a_key);
		return found == items.end() ? 0u : found->at("count").get<std::uint32_t>();
	}

	std::uint32_t Revision(const Json& a_root)
	{
		return a_root.at("profiles").at("a").at("inventory").at("revision").get<std::uint32_t>();
	}

	void TestCullAllRemovesEveryPiece()
	{
		using namespace F4T::Workshop;
		const FormKey space{ "Fallout4.esm", 1 };
		SettlementState state{ BuildZonePolicy{ 8, space, 0.0F, 0.0F, 0.0F, 100.0F, 2.0F, 2 }, 500 };
		Request place;
		place.playerId = 20;
		place.profileId = "profile-a";
		place.buildZoneId = 8;
		place.operationId = 1;
		place.kind = OperationKind::Place;
		place.form = { "Fallout4.esm", 0x1234 };
		place.space = space;
		place.transform = { 10.0F, 20.0F, 30.0F, 0.0F, 1.0F, 0.0F, 1.0F };
		assert(state.Apply(place).status == ApplyStatus::Committed);
		place.playerId = 21;
		place.profileId = "profile-b";
		place.expectedRevision = 1;
		assert(state.Apply(place).status == ApplyStatus::Committed);
		assert(state.ObjectCount() == 2);
		const auto revision = state.CurrentRevision();
		const auto culled = state.CullAll();
		assert(culled.size() == 2 && state.ObjectCount() == 0 && state.CurrentRevision() == revision + 1);
		const auto again = state.CullAll();
		assert(again.empty() && state.CurrentRevision() == revision + 1);
	}

	void TestHalfRefundRoundsEachComponentDown()
	{
		using namespace F4T::WorkshopCosts;
		Table table;
		assert(table.LoadFromJson(Json::parse(R"({"format":1,"costs":[
			{"plugin":"fallout4.esm","localFormId":4660,"components":[
				{"plugin":"fallout4.esm","localFormId":1,"count":1},
				{"plugin":"fallout4.esm","localFormId":2,"count":3},
				{"plugin":"fallout4.esm","localFormId":3,"count":4}]}]})")));
		std::vector<F4T::Workshop::ObjectState> culled(3);
		for (auto& object : culled) object.form = { "Fallout4.esm", 4660 };
		culled.push_back(F4T::Workshop::ObjectState{});
		culled.back().form = { "Fallout4.esm", 9999 };
		const auto refund = HalfRefund(table, culled);
		assert(refund.pricedPieces == 3 && refund.unpricedPieces == 1);
		assert(refund.lines.size() == 3);
		for (const auto& line : refund.lines) {
			if (line.itemKey == "fallout4.esm:00000001") assert(line.count == 1);
			else if (line.itemKey == "fallout4.esm:00000002") assert(line.count == 4);
			else if (line.itemKey == "fallout4.esm:00000003") assert(line.count == 6);
			else assert(false);
		}
		assert(HalfRefund(table, {}).lines.empty());
		Table empty;
		assert(!empty.LoadFromJson(Json::parse(R"({"costs":[{"plugin":"x","localFormId":1,"components":[{"plugin":"y","localFormId":1,"count":0}]}]})")));
	}

	void TestCommitCreditsOnceAndReplays()
	{
		auto root = Funded();
		const auto base = Revision(root);
		const auto commit = CommitMove(root, "a", 7, { Line{ keyOne, 2u }, Line{ keyTwo, 3u } });
		assert(commit.outcome == Outcome::Committed && commit.pending && commit.chunks.size() == 1);
		assert(commit.chunks[0].revision == base + 1);
		assert(Revision(root) == base + 1 && Count(root, keyTwo) == 3 && Count(root, keyOne) == 7);
		F4T::Drops::DropLedger::Validate(root);

		const auto before = root;
		const auto replay = CommitMove(root, "a", 7, { Line{ keyOne, 2u }, Line{ keyTwo, 3u } });
		assert(replay.outcome == Outcome::Replayed && replay.pending && replay.chunks == commit.chunks);
		assert(root == before);
		assert(Replay(root, "a", 7)->chunks == commit.chunks && !Replay(root, "a", 8));
	}

	void TestAckClearsPendingGrant()
	{
		auto root = Funded();
		(void)CommitMove(root, "a", 7, { Line{ keyTwo, 3u } });
		const auto pending = Pending(root, "a");
		assert(pending.size() == 1 && pending[0].operationId == 7);
		assert(!Ack(root, "a", 99));
		assert(Ack(root, "a", 7));
		assert(Pending(root, "a").empty() && !Ack(root, "a", 7));
		const auto replay = CommitMove(root, "a", 7, { Line{ keyTwo, 3u } });
		assert(replay.outcome == Outcome::Replayed && !replay.pending);
		assert(Count(root, keyTwo) == 3);
	}

	void TestRejectedCommitLeavesNothing()
	{
		auto root = Funded();
		const auto before = root;
		root["inventoryAuthorityBlocked"] = true;
		const auto blockedBefore = root;
		assert(CommitMove(root, "a", 7, { Line{ keyTwo, 3u } }).outcome == Outcome::Rejected && root == blockedBefore);
		root = before;
		assert(CommitMove(root, "missing", 7, { Line{ keyTwo, 3u } }).outcome == Outcome::Rejected && root == before);
		assert(CommitMove(root, "a", 0, { Line{ keyTwo, 3u } }).outcome == Outcome::Rejected && root == before);
		assert(CommitMove(root, "a", 7, { Line{ "not a key", 3u } }).outcome == Outcome::Rejected && root == before);
		root["profiles"]["a"]["startingGrant"]["granted"] = false;
		root["profiles"]["a"]["startingGrant"]["recovery"] = Json{ { "state", "unstarted" } };
		const auto pendingBefore = root;
		const auto pending = CommitMove(root, "a", 7, { Line{ keyTwo, 3u } });
		assert(pending.outcome == Outcome::Rejected && pending.reason == "startingInventoryPending" && root == pendingBefore);
		assert(Pending(root, "a").empty());
	}

	void TestEmptyRefundIsAckedRecordOnly()
	{
		auto root = Funded();
		const auto base = Revision(root);
		const auto commit = CommitMove(root, "a", 7, {});
		assert(commit.outcome == Outcome::Committed && commit.chunks.empty());
		assert(Revision(root) == base && Pending(root, "a").empty());
		assert(CommitMove(root, "a", 7, {}).outcome == Outcome::Replayed);
	}

	void TestLargeRefundIsChunked()
	{
		auto root = Funded();
		const auto base = Revision(root);
		std::vector<Line> lines;
		for (std::uint32_t index = 0; index < 20; ++index) {
			char key[32]{};
			std::snprintf(key, sizeof(key), "test.esm:%08X", 0x1000u + index);
			lines.push_back(Line{ key, index + 1 });
		}
		const auto commit = CommitMove(root, "a", 7, lines);
		assert(commit.outcome == Outcome::Committed && commit.chunks.size() == 2);
		assert(commit.chunks[0].revision == base + 1 && commit.chunks[1].revision == base + 2);
		assert(FlattenLines(commit.chunks).size() == 20 && Revision(root) == base + 2);
		F4T::Drops::DropLedger::Validate(root);
	}

	void TestAckedReplayWindowOutlivesPendingCap()
	{
		auto root = Funded();
		assert(CommitMove(root, "a", 1, { Line{ keyTwo, 1u } }).outcome == Outcome::Committed);
		assert(Ack(root, "a", 1));
		for (std::uint64_t operation = 2; operation <= 41; ++operation) {
			assert(CommitMove(root, "a", operation, {}).outcome == Outcome::Committed);
		}
		const auto before = root;
		const auto replay = CommitMove(root, "a", 1, { Line{ keyTwo, 1u } });
		assert(replay.outcome == Outcome::Replayed && !replay.pending && root == before);
		assert(Count(root, keyTwo) == 1);
	}

	void TestPendingCapRefusesNewGrant()
	{
		auto root = Funded();
		for (std::uint64_t operation = 1; operation <= 32; ++operation) {
			assert(CommitMove(root, "a", operation, { Line{ keyTwo, 1u } }).outcome == Outcome::Committed);
		}
		const auto before = root;
		assert(CommitMove(root, "a", 33, { Line{ keyTwo, 1u } }).outcome == Outcome::Rejected && root == before);
	}

	void TestPacketsRoundTripAndRejectMalformed()
	{
		const std::vector<Chunk> chunks{ Chunk{ 4, { Line{ keyOne, 2u } } } };
		const auto grant = ParseGrantPacket(MakeGrantPacket(9, chunks));
		assert(grant && grant->operationId == 9 && grant->chunks == chunks);
		assert(ParseGrantAck(MakeGrantAck(9)) == std::optional<std::uint64_t>{ 9 });
		auto bad = MakeGrantPacket(9, chunks);
		bad["extra"] = 1;
		assert(!ParseGrantPacket(bad));
		assert(!ParseGrantPacket(MakeGrantPacket(0, chunks)) && !ParseGrantPacket(MakeGrantPacket(9, {})));
		assert(!ParseGrantAck(Json{ { "type", "campGrantAck" }, { "operationId", 0 } }));
	}

	Counts StartCounts()
	{
		return Counts{ { keyOne, 5 }, { keyTwo, 0 } };
	}

	Counts GrantedCounts()
	{
		return Counts{ { keyOne, 7 }, { keyTwo, 3 } };
	}

	Grant CampGrant(std::uint64_t a_operationId)
	{
		return Grant{ a_operationId, { Chunk{ 4, { Line{ keyOne, 2u }, Line{ keyTwo, 3u } } } } };
	}

	struct Device
	{
		Counts inventory{ { keyOne, 5 }, { keyTwo, 0 } };
	};

	ReceiptState StateOf(const std::vector<Receipt>& a_receipts, std::uint64_t a_operationId)
	{
		const auto found = std::find_if(a_receipts.begin(), a_receipts.end(), [a_operationId](const Receipt& a_receipt) { return a_receipt.operationId == a_operationId; });
		return found == a_receipts.end() ? ReceiptState::Pending : found->state;
	}

	ApplyOutcome Pass(const Grant& a_grant, Device& a_device, std::vector<Receipt>& a_receipts, bool a_addLands = true)
	{
		const Counts current = a_device.inventory;
		return ApplyOnce(a_grant, current, a_receipts,
			[&](const std::vector<Line>& a_lines) {
				if (a_addLands) {
					for (const auto& line : a_lines) a_device.inventory[line.itemKey] += line.count;
				}
				return true;
			},
			[&]() { return std::optional<Counts>{ a_device.inventory }; });
	}

	std::string Save(const std::vector<Receipt>& a_receipts)
	{
		return FormatWorlds(Worlds{ { "world", a_receipts } });
	}

	std::vector<Receipt> Restore(const std::string& a_cosave)
	{
		const auto worlds = ParseWorlds(a_cosave);
		return worlds && worlds->contains("world") ? worlds->at("world") : std::vector<Receipt>{};
	}

	void TestAckWaitsForLandedSave()
	{
		Device device;
		std::vector<Receipt> receipts;
		assert(Pass(CampGrant(7), device, receipts) == ApplyOutcome::Hold);
		assert(device.inventory == GrantedCounts() && StateOf(receipts, 7) == ReceiptState::Applied);
		const auto landing = AppliedIds(receipts);
		assert(landing == std::vector<std::uint64_t>{ 7 });
		assert(Pass(CampGrant(7), device, receipts) == ApplyOutcome::Hold);
		assert(MarkLanded(receipts, landing) == std::vector<std::uint64_t>{ 7 });
		assert(StateOf(receipts, 7) == ReceiptState::Acked);
		assert(Pass(CampGrant(7), device, receipts) == ApplyOutcome::Acknowledge);
		assert(device.inventory == GrantedCounts());
	}

	void TestSaveLostBeforeAckKeepsGrantAndAcksAfterNextLanding()
	{
		Device device;
		std::vector<Receipt> receipts;
		assert(Pass(CampGrant(7), device, receipts) == ApplyOutcome::Hold);
		auto restarted = Restore(Save(receipts));
		assert(StateOf(restarted, 7) == ReceiptState::Applied);
		assert(Pass(CampGrant(7), device, restarted) == ApplyOutcome::Hold);
		assert(device.inventory == GrantedCounts());
		assert(MarkLanded(restarted, AppliedIds(restarted)) == std::vector<std::uint64_t>{ 7 });
		assert(Pass(CampGrant(7), device, restarted) == ApplyOutcome::Acknowledge);
		assert(device.inventory == GrantedCounts());
	}

	void TestSilentAddFailureStaysPendingThenGrantsOnce()
	{
		Device device;
		std::vector<Receipt> receipts;
		assert(Pass(CampGrant(7), device, receipts, false) == ApplyOutcome::Retry);
		assert(device.inventory == StartCounts() && StateOf(receipts, 7) == ReceiptState::Pending);
		assert(Pass(CampGrant(7), device, receipts) == ApplyOutcome::Hold);
		assert(device.inventory == GrantedCounts() && StateOf(receipts, 7) == ReceiptState::Applied);
	}

	void TestPendingReceiptReconcilesAgainstSavedItems()
	{
		Device device;
		device.inventory = GrantedCounts();
		auto restarted = Restore(Save(std::vector<Receipt>{ Receipt{ 7, ReceiptState::Pending, StartCounts() } }));
		assert(Pass(CampGrant(7), device, restarted) == ApplyOutcome::Hold);
		assert(device.inventory == GrantedCounts() && StateOf(restarted, 7) == ReceiptState::Applied);
	}

	void TestReplayGrantsOnlyTheMissingPart()
	{
		Device device;
		device.inventory = Counts{ { keyOne, 7 }, { keyTwo, 0 } };
		auto restarted = Restore(Save(std::vector<Receipt>{ Receipt{ 7, ReceiptState::Pending, StartCounts() } }));
		assert(Pass(CampGrant(7), device, restarted) == ApplyOutcome::Hold);
		assert(device.inventory == GrantedCounts());
	}

	void TestSoldAfterAddIsNotGrantedTwice()
	{
		Device device;
		std::vector<Receipt> receipts;
		assert(Pass(CampGrant(7), device, receipts) == ApplyOutcome::Hold);
		device.inventory[keyTwo] = 0;
		assert(Pass(CampGrant(7), device, receipts) == ApplyOutcome::Hold);
		assert(device.inventory[keyTwo] == 0);
	}

	void TestOldIdFileAdoptsAsAckedAndIsNotRegranted()
	{
		Device device;
		const std::vector<std::uint64_t> expected{ 7, 9 };
		const auto ids = ParseAckedIds("7\n9\n");
		assert(ids && *ids == expected);
		std::vector<Receipt> receipts;
		AddAcked(receipts, *ids);
		assert(receipts.size() == 2 && StateOf(receipts, 7) == ReceiptState::Acked);
		assert(Pass(CampGrant(7), device, receipts) == ApplyOutcome::Acknowledge);
		assert(device.inventory == StartCounts());
		assert(!ParseAckedIds("{\"7\":1}") && !ParseAckedIds("7 x") && !ParseAckedIds("0"));
	}

	void TestWorldsKeepEveryWorldAndBoundAckedTotal()
	{
		std::vector<Receipt> alpha;
		std::vector<Receipt> beta;
		for (std::uint64_t operation = 1; operation <= 300; ++operation) alpha.push_back(Receipt{ operation, ReceiptState::Acked, {} });
		for (std::uint64_t operation = 301; operation <= 600; ++operation) beta.push_back(Receipt{ operation, ReceiptState::Acked, {} });
		beta.push_back(Receipt{ 700, ReceiptState::Pending, StartCounts() });
		beta.push_back(Receipt{ 701, ReceiptState::Applied, GrantedCounts() });
		const Worlds worlds{ { "alpha", alpha }, { "beta", beta } };
		const auto loaded = ParseWorlds(FormatWorlds(worlds));
		assert(loaded && loaded->size() == 2);
		assert(loaded->at("alpha").size() == kMaximumAckedRecords - 300 && loaded->at("alpha").front().operationId == 89);
		assert(loaded->at("beta") == beta);
		assert(AppliedIds(loaded->at("beta")) == std::vector<std::uint64_t>{ 701 });
	}

	void TestVersionOneRecordStillLoads()
	{
		const auto snapshot = ParseSnapshot(R"({"worldId":"world","receipts":[{"operationId":7,"state":"applied","baseline":{"test.esm:00000123":5}},{"operationId":8,"state":"acked"}]})");
		const std::vector<Receipt> expected{ Receipt{ 7, ReceiptState::Applied, Counts{ { keyOne, 5 } } }, Receipt{ 8, ReceiptState::Acked, {} } };
		assert(snapshot && snapshot->worldId == "world" && snapshot->receipts == expected);
	}

	void TestOverLimitRecordDropsAckedThenOtherWorldsThenOldestOpen()
	{
		Worlds worlds{ { "world", {} } };
		for (std::uint64_t operation = 1; operation <= 200; ++operation) worlds["world"].push_back(Receipt{ operation, ReceiptState::Acked, {} });
		const Receipt pending{ 900, ReceiptState::Pending, StartCounts() };
		worlds["world"].push_back(pending);
		const auto limit = FormatWorlds(Worlds{ { "world", { pending } } }).size() + 1024;
		assert(!FitWorlds(worlds, "world", limit));
		assert(FormatWorlds(worlds).size() <= limit);
		assert(worlds["world"].back() == pending && worlds["world"].front().state == ReceiptState::Acked && worlds["world"].front().operationId > 1);
		assert(worlds["world"][worlds["world"].size() - 2].operationId == 200);
	}

	void TestOverLimitOpenReceiptsKeepTheNewestOfTheCurrentWorld()
	{
		const Receipt newest{ 3, ReceiptState::Pending, StartCounts() };
		const auto limit = FormatWorlds(Worlds{ { "world", { newest } } }).size();
		Worlds worlds{ { "other", { Receipt{ 1, ReceiptState::Pending, StartCounts() } } }, { "world", { Receipt{ 2, ReceiptState::Pending, StartCounts() }, newest } } };
		assert(FitWorlds(worlds, "world", limit));
		const Worlds expected{ { "world", { newest } } };
		assert(worlds == expected);
	}

	void TestCorruptSnapshotLoadsNothing()
	{
		const auto text = Save(std::vector<Receipt>{ Receipt{ 7, ReceiptState::Applied, StartCounts() } });
		assert(ParseWorlds(text) && ParseWorlds(R"({"worlds":[]})") && ParseWorlds(R"({"worlds":[{"worldId":"world","receipts":[]}]})"));
		assert(!ParseWorlds(text.substr(0, text.size() - 1)));
		assert(!ParseWorlds("7\n9\n"));
		assert(!ParseWorlds(R"({"worlds":[{"worldId":"world","receipts":[{"operationId":7,"state":"bogus","baseline":{}}]}]})"));
		assert(!ParseWorlds(R"({"worlds":[{"worldId":"world","receipts":[{"operationId":7,"state":"applied"}]}]})"));
		assert(!ParseWorlds(R"({"worlds":[{"worldId":"world","receipts":[{"operationId":7,"state":"pending","baseline":{}},{"operationId":8}]}]})"));
		assert(!ParseWorlds(R"({"worlds":[{"worldId":"world","receipts":[]},{"worldId":"world","receipts":[]}]})"));
		assert(!ParseWorlds(R"({"worlds":[{"worldId":"world","receipts":[],"extra":1}]})"));
		assert(!ParseWorlds(R"({"worlds":[],"extra":1})"));
		assert(!ParseWorlds(R"({"receipts":[]})"));
		assert(!ParseSnapshot(R"({"worldId":"world","receipts":[{"operationId":7,"state":"bogus","baseline":{}}]})"));
		assert(!ParseSnapshot(R"({"worldId":"world","receipts":[],"extra":1})"));
	}
}

int main()
{
	TestCullAllRemovesEveryPiece();
	TestHalfRefundRoundsEachComponentDown();
	TestCommitCreditsOnceAndReplays();
	TestAckClearsPendingGrant();
	TestRejectedCommitLeavesNothing();
	TestEmptyRefundIsAckedRecordOnly();
	TestLargeRefundIsChunked();
	TestAckedReplayWindowOutlivesPendingCap();
	TestPendingCapRefusesNewGrant();
	TestPacketsRoundTripAndRejectMalformed();
	TestAckWaitsForLandedSave();
	TestSaveLostBeforeAckKeepsGrantAndAcksAfterNextLanding();
	TestSilentAddFailureStaysPendingThenGrantsOnce();
	TestPendingReceiptReconcilesAgainstSavedItems();
	TestReplayGrantsOnlyTheMissingPart();
	TestSoldAfterAddIsNotGrantedTwice();
	TestOldIdFileAdoptsAsAckedAndIsNotRegranted();
	TestWorldsKeepEveryWorldAndBoundAckedTotal();
	TestVersionOneRecordStillLoads();
	TestOverLimitRecordDropsAckedThenOtherWorldsThenOldestOpen();
	TestOverLimitOpenReceiptsKeepTheNewestOfTheCurrentWorld();
	TestCorruptSnapshotLoadsNothing();
	return 0;
}
