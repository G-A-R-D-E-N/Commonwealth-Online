#include "F4TTradeLedger.h"
#include "F4TDropLedger.h"
#include "F4TDurableState.h"
#include "StartingGrantFixture.h"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::Trade;

	const std::string keyOne = "test.esm:00000123";
	const std::string keyTwo = "test.esm:00000456";

	Json Funded(std::uint32_t a_countA, std::uint32_t a_countB)
	{
		auto root = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		F4T::Identity::TokenHash hash{};
		for (const std::string profile : { "a", "b" }) {
			F4T::DurableState::SaveResumeCredential(root, profile, hash, std::nullopt, true, 5000.0, 1000.0);
		}
		assert(F4T::DurableState::InitializeInventory(root, "a", Json::array({
			Json{ { "plugin", "Test.esm" }, { "localFormId", "123" }, { "count", a_countA } } }), 1000.0));
		assert(F4T::DurableState::InitializeInventory(root, "b", Json::array({
			Json{ { "plugin", "Test.esm" }, { "localFormId", "456" }, { "count", a_countB } } }), 1000.0));
		ReconcileStartingGrantFixture(root, "a");
		ReconcileStartingGrantFixture(root, "b");
		F4T::Drops::DropLedger::Validate(root);
		return root;
	}

	CommitRequest MakeCommit(std::uint32_t a_revisionA, std::uint32_t a_revisionB, std::vector<Line> a_giveA,
		std::vector<Line> a_giveB, const std::string& a_operationId = "trade-1000-1")
	{
		CommitRequest request;
		request.operationId = a_operationId;
		request.first = CommitParty{ "a", a_revisionA, std::move(a_giveA) };
		request.second = CommitParty{ "b", a_revisionB, std::move(a_giveB) };
		return request;
	}

	void TestPendingGrantBlocksEitherParty()
	{
		for (const auto* profile : { "a", "b" }) {
			auto root = Funded(5, 5);
			root["profiles"][profile]["startingGrant"]["granted"] = false;
			root["profiles"][profile]["startingGrant"]["recovery"] = Json{ { "state", "unstarted" } };
			const auto before = root;
			const auto result = Commit(root, MakeCommit(0, 0, { Line{ keyOne, 1u } }, {}));
			assert(result.outcome == F4T::Drops::Outcome::Rejected && result.reason == "startingInventoryPending" && root == before);
		}
	}

	std::uint32_t Count(const Json& a_root, const char* a_profile, const std::string& a_key)
	{
		const auto& items = a_root.at("profiles").at(a_profile).at("inventory").at("items");
		const auto found = items.find(a_key);
		return found == items.end() ? 0u : found->at("count").get<std::uint32_t>();
	}

	std::uint32_t Revision(const Json& a_root, const char* a_profile)
	{
		return a_root.at("profiles").at(a_profile).at("inventory").at("revision").get<std::uint32_t>();
	}

	void TestIntentParsing()
	{
		const auto request = ParseClientIntent(Json{ { "type", "tradeRequest" }, { "targetPlayerId", 7u } });
		assert(request && request->kind == IntentKind::Request && request->targetPlayerId == 7u);
		assert(!ParseClientIntent(Json{ { "type", "tradeRequest" }, { "targetPlayerId", 0u } }));
		assert(!ParseClientIntent(Json{ { "type", "tradeRequest" }, { "targetPlayerId", 7u }, { "extra", true } }));

		const auto respond = ParseClientIntent(Json{ { "type", "tradeRespond" }, { "tradeId", "1000-1" }, { "accept", true } });
		assert(respond && respond->kind == IntentKind::Respond && respond->accept && respond->tradeId == "1000-1");
		assert(!ParseClientIntent(Json{ { "type", "tradeRespond" }, { "tradeId", "1000-1" }, { "accept", "yes" } }));

		const auto offer = ParseClientIntent(Json{ { "type", "tradeOffer" }, { "tradeId", "1000-1" },
			{ "lines", Json::array({ Json{ { "itemKey", keyOne }, { "count", 2u } } }) } });
		assert(offer && offer->kind == IntentKind::Offer && offer->lines.size() == 1 && offer->lines[0].count == 2u);
		assert(!ParseClientIntent(Json{ { "type", "tradeOffer" }, { "tradeId", "1000-1" },
			{ "lines", Json::array({ Json{ { "itemKey", keyOne }, { "count", 0u } } }) } }));
		assert(!ParseClientIntent(Json{ { "type", "tradeOffer" }, { "tradeId", "1000-1" },
			{ "lines", Json::array({ Json{ { "itemKey", keyOne }, { "count", 65536u } } }) } }));
		assert(!ParseClientIntent(Json{ { "type", "tradeOffer" }, { "tradeId", "1000-1" },
			{ "lines", Json::array({ Json{ { "itemKey", "nope" }, { "count", 1u } } }) } }));
		assert(!ParseClientIntent(Json{ { "type", "tradeOffer" }, { "tradeId", "1000-1" },
			{ "lines", Json::array({ Json{ { "itemKey", keyOne }, { "count", 1u } }, Json{ { "itemKey", keyOne }, { "count", 2u } } }) } }));

		Json tooMany = Json::array();
		for (std::uint32_t index = 0; index < 17; ++index) {
			tooMany.push_back(Json{ { "itemKey", "test.esm:0000" + std::to_string(1000 + index) }, { "count", 1u } });
		}
		assert(!ParseClientIntent(Json{ { "type", "tradeOffer" }, { "tradeId", "1000-1" }, { "lines", tooMany } }));

		const auto confirm = ParseClientIntent(Json{ { "type", "tradeConfirm" }, { "tradeId", "1000-1" }, { "offerVersion", 2u } });
		assert(confirm && confirm->kind == IntentKind::Confirm && confirm->offerVersion == 2u);
		assert(!ParseClientIntent(Json{ { "type", "tradeConfirm" }, { "tradeId", "1000-1" } }));
		assert(!ParseClientIntent(Json{ { "type", "tradeConfirm" }, { "tradeId", "1000-1" }, { "offerVersion", -1 } }));
		assert(ParseClientIntent(Json{ { "type", "tradeCancel" }, { "tradeId", "1000-1" } })->kind == IntentKind::Cancel);
		assert(!ParseClientIntent(Json{ { "type", "tradeCancel" }, { "tradeId", "" } }));
		assert(!ParseClientIntent(Json{ { "type", "tradeCancel" }, { "tradeId", std::string(65, 'x') } }));
		assert(!ParseClientIntent(Json{ { "type", "inventoryTradeRequest" }, { "vendor", "x" } }));
	}

	void TestBookLifecycle()
	{
		Book book{ 1000 };
		assert(book.Request(1u, 1u, 0.0).reason == "selfTrade");
		assert(book.Request(0u, 2u, 0.0).reason == "invalidPlayer");
		assert(book.Request(1u, 2u, 0.0).accepted);
		assert(book.Request(3u, 2u, 0.0).reason == "busy");
		assert(book.ActiveFor(1u) != nullptr && book.ActiveFor(1u)->tradeId == "1000-1");

		assert(book.SetOffer("1000-1", 1u, { Line{ keyOne, 1u } }, 0.0).reason == "notNegotiating");
		assert(book.Respond("1000-1", 1u, true, 0.0).reason == "notTarget");
		assert(book.Respond("1000-1", 2u, true, 0.0).accepted);
		assert(book.Confirm("1000-1", 1u, 3u, 0u, 0.0).reason == "emptyTrade");
		assert(book.SetOffer("1000-1", 1u, { Line{ keyOne, 1u } }, 0.0).accepted);
		assert(book.Find("1000-1")->offerVersion == 1u);
		assert(book.Confirm("1000-1", 1u, 3u, 0u, 0.0).reason == "offerChanged");
		assert(book.Confirm("1000-1", 1u, 3u, 1u, 0.0).accepted);
		assert(!book.Ready("1000-1"));
		assert(book.Confirm("1000-1", 2u, 4u, 1u, 0.0).accepted);
		assert(book.Ready("1000-1"));
		assert(book.BuildCommit("1000-1", "a", "b").has_value());
		assert(book.BuildCommit("1000-1", "a", "b")->first.expectedRevision == 3u);
		assert(book.BuildCommit("1000-1", "a", "b")->second.expectedRevision == 4u);

		assert(book.SetOffer("1000-1", 2u, { Line{ keyTwo, 1u } }, 0.0).accepted);
		assert(book.Find("1000-1")->offerVersion == 2u);
		assert(!book.Ready("1000-1"));
		assert(!book.Find("1000-1")->initiator.confirmed && !book.Find("1000-1")->target.confirmed);
		assert(book.Confirm("1000-1", 1u, 3u, 1u, 0.0).reason == "offerChanged");
		assert(book.Cancel("1000-1", 3u, 0.0).reason == "notParticipant");
		assert(book.Cancel("1000-1", 1u, 0.0).accepted);
		const auto ended = book.TakeEnded();
		assert(ended.size() == 1 && ended[0].reason == "cancelled" && book.ActiveFor(1u) == nullptr);
		assert(book.TakeEnded().empty());
	}

	void TestBookDeclineExpiryAndEnds()
	{
		Book book{ 1000 };
		assert(book.Request(4u, 5u, 10.0).accepted);
		assert(book.Respond("1000-1", 5u, false, 10.0).accepted);
		auto ended = book.TakeEnded();
		assert(ended.size() == 1 && ended[0].reason == "declined" && book.ActiveFor(4u) == nullptr);
		assert(book.Request(5u, 4u, 39.5).reason == "cooldown");
		assert(book.Request(4u, 5u, 40.5).accepted);

		book.Expire(55.0);
		assert(book.ActiveFor(4u) != nullptr);
		book.Expire(56.0);
		ended = book.TakeEnded();
		assert(ended.size() == 1 && ended[0].reason == "expired" && book.ActiveFor(5u) == nullptr);
		assert(book.Request(4u, 5u, 60.0).reason == "cooldown");

		assert(book.Request(6u, 7u, 100.0).accepted);
		const auto acceptedId = book.ActiveFor(6u)->tradeId;
		assert(book.Respond(acceptedId, 7u, true, 100.0).accepted);
		book.Expire(159.0);
		assert(book.ActiveFor(6u) != nullptr);
		book.Expire(161.0);
		ended = book.TakeEnded();
		assert(ended.size() == 1 && ended[0].reason == "expired" && book.ActiveFor(6u) == nullptr);
		assert(book.Request(7u, 6u, 170.0).reason == "cooldown");

		assert(book.Request(8u, 9u, 200.0).accepted);
		book.EndFor(9u, "downed");
		ended = book.TakeEnded();
		assert(ended.size() == 1 && ended[0].reason == "downed" && ended[0].initiatorPlayerId == 8u && ended[0].targetPlayerId == 9u);
		assert(book.Request(8u, 9u, 201.0).accepted);
		const auto id = book.ActiveFor(8u)->tradeId;
		book.Finish(id, "committed");
		ended = book.TakeEnded();
		assert(ended.size() == 1 && ended[0].reason == "committed" && book.ActiveFor(9u) == nullptr);
	}

	void TestSettleOutcomesEndTheTrade()
	{
		Book book{ 1000 };
		assert(book.Request(1u, 2u, 0.0).accepted);
		assert(book.Respond("1000-1", 2u, true, 0.0).accepted);
		assert(book.SetOffer("1000-1", 1u, { Line{ keyOne, 1u } }, 0.0).accepted);
		assert(book.Confirm("1000-1", 1u, 3u, 1u, 0.0).accepted);
		assert(book.Confirm("1000-1", 2u, 4u, 1u, 0.0).accepted);
		book.Unconfirm("1000-1");
		assert(!book.Ready("1000-1") && !book.Find("1000-1")->initiator.confirmed && !book.Find("1000-1")->target.confirmed);
		assert(book.ActiveFor(1u) != nullptr && book.TakeEnded().empty());
		assert(book.Confirm("1000-1", 1u, 3u, 1u, 0.0).accepted);
		assert(book.Confirm("1000-1", 2u, 4u, 1u, 0.0).accepted);
		assert(book.Ready("1000-1"));
		book.Finish("1000-1", "partnerUnavailable");
		auto ended = book.TakeEnded();
		assert(ended.size() == 1 && ended[0].reason == "partnerUnavailable" && book.ActiveFor(1u) == nullptr && book.ActiveFor(2u) == nullptr);
		assert(book.Request(1u, 2u, 40.0).accepted);
		const auto downedId = book.ActiveFor(1u)->tradeId;
		book.Finish(downedId, "downed");
		ended = book.TakeEnded();
		assert(ended.size() == 1 && ended[0].reason == "downed" && book.ActiveFor(2u) == nullptr);
	}

	void TestCommitConservesItems()
	{
		auto root = Funded(5u, 2u);
		const auto revisionA = Revision(root, "a");
		const auto revisionB = Revision(root, "b");
		const auto request = MakeCommit(revisionA, revisionB, { Line{ keyOne, 2u } }, { Line{ keyTwo, 1u } });
		const auto before = root;

		const auto result = Commit(root, request);
		assert(result.outcome == F4T::Drops::Outcome::Committed);
		assert(result.firstRevision == revisionA + 1 && result.secondRevision == revisionB + 1);
		assert(Count(root, "a", keyOne) == 3u && Count(root, "a", keyTwo) == 1u);
		assert(Count(root, "b", keyOne) == 2u && Count(root, "b", keyTwo) == 1u);
		assert(Count(root, "a", keyOne) + Count(root, "b", keyOne) == Count(before, "a", keyOne) + Count(before, "b", keyOne));
		assert(Count(root, "a", keyTwo) + Count(root, "b", keyTwo) == Count(before, "a", keyTwo) + Count(before, "b", keyTwo));
		F4T::Drops::DropLedger::Validate(root);

		const auto packet = CommitResultPacket("1000-1", request, result, true);
		assert(packet.at("status") == "committed" && packet.at("revision") == revisionA + 1);
		assert(packet.at("give")[0].at("itemKey") == keyOne && packet.at("receive")[0].at("itemKey") == keyTwo);
		const auto secondPacket = CommitResultPacket("1000-1", request, result, false);
		assert(secondPacket.at("give")[0].at("itemKey") == keyTwo && secondPacket.at("receive")[0].at("itemKey") == keyOne);

		const auto replay = Commit(root, request);
		assert(replay.outcome == F4T::Drops::Outcome::Replayed && replay.firstRevision == revisionA + 1);
		assert(Commit(root, MakeCommit(revisionA, revisionB, { Line{ keyOne, 1u } }, { Line{ keyTwo, 1u } })).reason == "operationMismatch");

		const auto snapshot = root;
		assert(Commit(root, MakeCommit(revisionA, revisionB, { Line{ keyOne, 1u } }, {}, "trade-1000-2")).reason == "staleRevision");
		assert(root == snapshot);
		assert(Commit(root, MakeCommit(revisionA + 1, revisionB + 1, { Line{ keyOne, 99u } }, {}, "trade-1000-3")).reason == "insufficientItems");
		assert(root == snapshot);
		auto selfRequest = MakeCommit(revisionA + 1, revisionB + 1, { Line{ keyOne, 1u } }, {}, "trade-1000-5");
		selfRequest.second.profileId = "a";
		assert(Commit(root, selfRequest).reason == "selfTrade");
		assert(root == snapshot);

		const auto both = Commit(root, MakeCommit(revisionA + 1, revisionB + 1, { Line{ keyOne, 3u } }, { Line{ keyOne, 2u } }, "trade-1000-4"));
		assert(both.outcome == F4T::Drops::Outcome::Committed && both.firstRevision == revisionA + 2 && both.secondRevision == revisionB + 2);
		assert(Count(root, "a", keyOne) == 2u && Count(root, "b", keyOne) == 3u);
		assert(Count(root, "a", keyOne) + Count(root, "b", keyOne) == 5u);
		F4T::Drops::DropLedger::Validate(root);
	}

	void TestCommitRefusesCountOverflow()
	{
		auto root = Funded(5u, 2u);
		auto overflowing = root.at("profiles").at("a").at("inventory").at("items").at(keyOne);
		overflowing["count"] = 65535u;
		root["profiles"]["b"]["inventory"]["items"][keyOne] = overflowing;
		const auto snapshot = root;
		const auto result = Commit(root, MakeCommit(Revision(root, "a"), Revision(root, "b"), { Line{ keyOne, 1u } }, {}, "trade-1000-20"));
		assert(result.outcome == F4T::Drops::Outcome::Rejected && result.reason == "capacityExceeded");
		assert(root == snapshot);
	}

	void TestHistoryCapHoldsThroughValidate()
	{
		auto root = Funded(5u, 2u);
		for (std::uint32_t index = 0; index < 65; ++index) {
			const auto forward = index % 2 == 0;
			const auto result = Commit(root, MakeCommit(Revision(root, "a"), Revision(root, "b"),
				forward ? std::vector<Line>{ Line{ keyOne, 1u } } : std::vector<Line>{},
				forward ? std::vector<Line>{} : std::vector<Line>{ Line{ keyOne, 1u } }, "trade-1000-" + std::to_string(100 + index)));
			assert(result.outcome == F4T::Drops::Outcome::Committed);
		}
		auto& history = root.at("profiles").at("a").at("inventoryOperations");
		assert(history.size() == 64u && Revision(root, "a") == 65u);
		F4T::Drops::DropLedger::Validate(root);
		const auto copy = history.back();
		history.push_back(copy);
		auto refused = false;
		try {
			F4T::Drops::DropLedger::Validate(root);
		} catch (const std::invalid_argument&) {
			refused = true;
		}
		assert(refused);
	}

	void TestPacketShapes()
	{
		Trade trade;
		trade.tradeId = "1000-1";
		trade.initiator.playerId = 1u;
		trade.target.playerId = 2u;
		trade.initiator.offer = { Line{ keyOne, 1u } };
		trade.offerVersion = 3u;
		const auto state = StatePacket(trade, 2u);
		assert(state.at("type") == "tradeState" && state.at("phase") == "requested" && state.at("role") == "target");
		assert(state.at("partnerPlayerId") == 1u && state.at("partnerOffer").size() == 1 && state.at("ownOffer").empty());
		assert(state.at("offerVersion") == 3u);
		trade.accepted = true;
		assert(StatePacket(trade, 1u).at("phase") == "negotiating" && StatePacket(trade, 1u).at("role") == "initiator");
		const auto ended = EndedPacket(Ended{ "1000-1", 1u, 2u, "declined" }, 2u);
		assert(ended.at("phase") == "ended" && ended.at("partnerPlayerId") == 1u && ended.at("reason") == "declined");
		assert(RefusalPacket("", "notFound").at("status") == "rejected" && !RefusalPacket("", "notFound").contains("tradeId"));
		assert(RefusalPacket("1000-1", "busy").at("tradeId") == "1000-1");
	}
}

int main()
{
	TestPendingGrantBlocksEitherParty();
	TestIntentParsing();
	TestBookLifecycle();
	TestBookDeclineExpiryAndEnds();
	TestSettleOutcomesEndTheTrade();
	TestCommitConservesItems();
	TestCommitRefusesCountOverflow();
	TestHistoryCapHoldsThroughValidate();
	TestPacketShapes();
	return 0;
}
