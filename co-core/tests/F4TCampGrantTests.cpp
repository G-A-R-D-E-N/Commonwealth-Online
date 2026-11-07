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

	void TestApplyOnceReplayAppliesNothingAndAcks()
	{
		const std::vector<std::uint64_t> applied{ 7 };
		int applies = 0;
		int remembers = 0;
		const auto outcome = ApplyOnce(7, [&](std::uint64_t a_id) { return std::find(applied.begin(), applied.end(), a_id) != applied.end(); },
			[&] { ++applies; return true; }, [&](std::uint64_t) { ++remembers; });
		assert(outcome == ApplyOutcome::Acknowledge && applies == 0 && remembers == 0);
	}

	void TestApplyOnceNewIdAppliesOnce()
	{
		std::vector<std::uint64_t> applied;
		int applies = 0;
		const auto wasApplied = [&](std::uint64_t a_id) { return std::find(applied.begin(), applied.end(), a_id) != applied.end(); };
		const auto apply = [&] { ++applies; return true; };
		const auto remember = [&](std::uint64_t a_id) { RememberAppliedId(applied, a_id); };
		assert(ApplyOnce(9, wasApplied, apply, remember) == ApplyOutcome::Acknowledge);
		assert(ApplyOnce(9, wasApplied, apply, remember) == ApplyOutcome::Acknowledge);
		assert(applies == 1 && applied == std::vector<std::uint64_t>{ 9 });
		assert(ApplyOnce(10, wasApplied, [] { return false; }, remember) == ApplyOutcome::Retry);
		assert(applied == std::vector<std::uint64_t>{ 9 });
	}

	void TestAppliedIdsSurviveReload()
	{
		std::vector<std::uint64_t> ids;
		for (std::uint64_t operation = 1; operation <= 600; ++operation) RememberAppliedId(ids, operation);
		assert(ids.size() == kMaximumAckedRecords && ids.front() == 89 && ids.back() == 600);
		const auto reloaded = ParseAppliedIds(FormatAppliedIds(ids));
		assert(reloaded == ids && ParseAppliedIds("").empty());
		int applies = 0;
		const auto outcome = ApplyOnce(600, [&](std::uint64_t a_id) { return std::find(reloaded.begin(), reloaded.end(), a_id) != reloaded.end(); },
			[&] { ++applies; return true; }, [](std::uint64_t) {});
		assert(outcome == ApplyOutcome::Acknowledge && applies == 0);
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
	TestApplyOnceReplayAppliesNothingAndAcks();
	TestApplyOnceNewIdAppliesOnce();
	TestAppliedIdsSurviveReload();
	return 0;
}
