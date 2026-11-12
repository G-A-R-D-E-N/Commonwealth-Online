#include <cstdio>
#include <cstdlib>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "CHECK %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__); std::abort(); } } while (0)


#include "F4TCharacterSlots.h"
#include "F4TDeathBag.h"
#include "F4TDeathBagUnplaced.h"
#include "StartingGrantFixture.h"

#include <cassert>
#include <optional>
#include <string>

int main()
{
	using Json = nlohmann::json;
	const std::string character = "quest-11111111111111111111111111111111";
	const std::string item = "test.esm:00000002";
	const std::string pipboy = "fallout4.esm:00021B3B";
	const Json stacks = Json::array({ Json{ { "itemKey", item }, { "count", 2u }, { "metadata", nullptr } } });
	const auto makeRootWith = [&](std::string& a_deathId, const Json& a_extra) {
		auto root = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		F4T::Identity::TokenHash hash{};
		F4T::DurableState::SaveResumeCredential(root, "profile-a", hash, std::nullopt, true, 5000.0, 1000.0);
		CHECK(F4T::CharacterSlots::SetSlotRecord(root, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", character } }));
		Json catalog = Json::array({
			Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } });
		for (const auto& extra : a_extra) catalog.push_back(extra);
		CHECK(F4T::DurableState::InitializeInventory(root, "profile-a#0", catalog, 1000.0));
		ReconcileStartingGrantFixture(root, "profile-a#0");
		const Json record{ { "version", 1u }, { "profileId", "profile-a#0" }, { "characterId", character }, { "inventoryRevision", 0u },
			{ "cellKey", "test.esm:00000001" }, { "worldspaceKey", nullptr }, { "position", Json::array({ 1.0, 2.0, 3.0 }) },
			{ "state", "dead" }, { "deadlineUnix", nullptr }, { "locationSource", "client-reported" } };
		const auto allocation = F4T::DeathIntent::Allocate(root, record);
		CHECK(allocation.outcome == F4T::DeathIntent::Outcome::Created);
		a_deathId = allocation.deathId;
		return root;
	};
	const auto makeRoot = [&](std::string& a_deathId) { return makeRootWith(a_deathId, Json::array()); };

	std::string id;
	auto root = makeRoot(id);
	CHECK(F4T::DeathBag::CreateEmpty(root, id, 1000.0));
	CHECK(!F4T::DeathBag::CreateEmpty(root, id, 1000.0));
	CHECK(!F4T::DeathBag::Place(root, id, "profile-a#0", character));
	CHECK(!F4T::DeathBag::OwnedBy(root, id, "profile-a#0", character));
	CHECK(F4T::DeathBag::Deposit(root, id, stacks));
	CHECK(F4T::DeathBag::OwnedBy(root, id, "profile-a#0", character));
	CHECK(!F4T::DeathBag::OwnedBy(root, id, "profile-a#1", character));
	CHECK(!F4T::DeathBag::OwnedBy(root, id, "profile-a#0", "quest-22222222222222222222222222222222"));
	CHECK(!F4T::DeathBag::Deposit(root, id, stacks));
	CHECK(root["profiles"]["profile-a#0"]["inventory"]["items"].contains(pipboy));
	CHECK(!root["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));
	CHECK(root["deathBags"][id]["items"][item]["count"] == 2u);
	const auto unplaced = F4T::DeathBag::Discovery(root, "profile-a#0", character, false);
	CHECK(unplaced && unplaced->empty());
	CHECK(!F4T::DeathBag::Place(root, id, "profile-a#0", "quest-22222222222222222222222222222222"));
	CHECK(F4T::DeathBag::Place(root, id, "profile-a#0", character));
	CHECK(!F4T::DeathBag::Place(root, id, "profile-a#0", character));
	CHECK(F4T::DeathBag::ExpirePending(root, 2000.0).empty());
	const auto visible = F4T::DeathBag::Discovery(root, "profile-a#0", character, false);
	CHECK(visible && visible->size() == 1 && visible->at(0).at("canTake") == true);

	std::string sweepId;
	auto sweepRoot = makeRoot(sweepId);
	CHECK(F4T::DeathBag::CreateEmpty(sweepRoot, sweepId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(sweepRoot, sweepId, stacks));
	CHECK(F4T::DeathBag::Place(sweepRoot, sweepId, "profile-a#0", character));
	const auto sweepRevision = sweepRoot["profiles"]["profile-a#0"]["inventory"]["revision"].get<std::uint32_t>();
	CHECK(F4T::DeathBag::Withdraw(sweepRoot, sweepId, "profile-a#0", character, false, item, 2, sweepRevision, "all", 1500.0));
	CHECK(!sweepRoot["deathBags"].contains(sweepId) && sweepRoot.at("deathGenerations").at(character).at("bagSpent") == true);
	CHECK(F4T::DeathBag::SweepEmptied(sweepRoot) == 0);
	CHECK(F4T::DeathIntent::RetireUntransferred(sweepRoot, character, sweepId));
	CHECK(F4T::DeathBag::SweepEmptied(sweepRoot) == 0);
	CHECK(!sweepRoot["deathBags"].contains(sweepId) && !sweepRoot.at("deathGenerations").at(character).contains("bagSpent"));

	std::string voidId;
	auto voidRoot = makeRoot(voidId);
	CHECK(F4T::DeathBag::CreateEmpty(voidRoot, voidId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(voidRoot, voidId, stacks));
	Json restored;
	CHECK(F4T::DeathBag::Void(voidRoot, voidId, &restored));
	CHECK(restored == stacks);
	CHECK(!voidRoot["deathBags"].contains(voidId));
	CHECK(voidRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);
	CHECK(voidRoot["profiles"]["profile-a#0"]["inventory"]["items"][pipboy]["count"] == 1u);

	std::string expireId;
	auto expireRoot = makeRoot(expireId);
	CHECK(F4T::DeathBag::CreateEmpty(expireRoot, expireId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(expireRoot, expireId, stacks));
	CHECK(F4T::DeathBag::ExpirePending(expireRoot, 1119.0).empty());
	CHECK(!F4T::DeathBag::HasExpiryWork(expireRoot, 1119.0));
	CHECK(!F4T::DeathBag::MarkDeparted(expireRoot, "quest-99999999999999999999999999999999", false, 1000.0));
	CHECK(F4T::DeathBag::MarkDeparted(expireRoot, character, false, 1000.0));
	CHECK(!F4T::DeathBag::MarkDeparted(expireRoot, character, true, 1001.0));
	CHECK(expireRoot["deathBags"][expireId]["departure"] == "crash" && expireRoot["deathBags"][expireId]["departedAt"] == 1000.0);
	CHECK(F4T::DeathBag::ExpirePending(expireRoot, 1119.0).empty());
	CHECK(F4T::DeathBag::HasExpiryWork(expireRoot, 1120.0));
	const auto expired = F4T::DeathBag::ExpirePending(expireRoot, 1120.0);
	CHECK(expired.size() == 1 && expired.at(0).deathId == expireId && expired.at(0).characterId == character && expired.at(0).stacks == stacks);
	CHECK(!expireRoot["deathBags"].contains(expireId));
	CHECK(expireRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);

	const Json pipboyWire{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } };
	const Json itemWire{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 1u } };
	const auto placedRoot = [&](std::string& a_deathId) {
		auto placed = makeRoot(a_deathId);
		CHECK(F4T::DeathBag::CreateEmpty(placed, a_deathId, 1000.0));
		CHECK(F4T::DeathBag::Deposit(placed, a_deathId, stacks));
		CHECK(F4T::DeathBag::Place(placed, a_deathId, "profile-a#0", character));
		return placed;
	};
	const auto snapshotOf = [](const Json& a_root, const std::string& a_operationId, const Json& a_wireItems) {
		const auto revision = a_root.at("profiles").at("profile-a#0").at("inventory").at("revision").get<std::uint32_t>();
		return *F4T::InventorySnapshot::NormalizeRequest(a_operationId, revision, a_wireItems);
	};
	std::string gainId;
	auto gainRoot = placedRoot(gainId);
	const auto gain = snapshotOf(gainRoot, "gain-1", Json::array({ pipboyWire, itemWire }));
	CHECK(F4T::InventorySnapshot::Reconcile(gainRoot, "profile-a#0", gain).changed);
	CHECK(gainRoot["deathBags"][gainId]["items"][item]["count"] == 2u);
	CHECK(gainRoot["deathBags"][gainId]["stacks"] == stacks);
	CHECK(gainRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 1u);
	const Json boughtWire{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 5u } };
	const auto bought = snapshotOf(gainRoot, "gain-2", Json::array({ pipboyWire, boughtWire }));
	CHECK(F4T::InventorySnapshot::Reconcile(gainRoot, "profile-a#0", bought).changed);
	CHECK(gainRoot["deathBags"][gainId]["items"][item]["count"] == 2u);
	CHECK(gainRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 5u);
	std::string otherId;
	auto otherRoot = placedRoot(otherId);
	const Json otherWire{ { "plugin", "test.esm" }, { "localFormId", "00000003" }, { "count", 1u } };
	const auto other = snapshotOf(otherRoot, "other-1", Json::array({ pipboyWire, otherWire }));
	CHECK(F4T::InventorySnapshot::Reconcile(otherRoot, "profile-a#0", other).changed);
	CHECK(otherRoot["deathBags"][otherId]["items"][item]["count"] == 2u);
	std::string pendingId;
	auto pendingRoot = makeRoot(pendingId);
	CHECK(F4T::DeathBag::CreateEmpty(pendingRoot, pendingId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pendingRoot, pendingId, stacks));
	const auto pending = snapshotOf(pendingRoot, "pending-1", Json::array({ pipboyWire, itemWire }));
	CHECK(F4T::InventorySnapshot::Reconcile(pendingRoot, "profile-a#0", pending).changed);
	CHECK(pendingRoot["deathBags"][pendingId]["items"][item]["count"] == 2u);

	const std::string otherCharacter = "quest-22222222222222222222222222222222";
	std::string hiddenId;
	auto hiddenRoot = placedRoot(hiddenId);
	CHECK(hiddenRoot["deathBags"][hiddenId]["items"][item]["count"] == 2u);
	CHECK(F4T::CharacterSlots::SetSlotRecord(hiddenRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", otherCharacter } }));
	const auto hidden = F4T::DeathBag::Discovery(hiddenRoot, "profile-a#0", otherCharacter, false);
	CHECK(hidden && hidden->empty());
	const auto shared = F4T::DeathBag::Discovery(hiddenRoot, "profile-a#0", otherCharacter, true);
	CHECK(shared && shared->size() == 1 && shared->at(0).at("deathId") == hiddenId && shared->at(0).at("canTake") == true && shared->at(0).size() == 4);
	CHECK(F4T::CharacterSlots::SetSlotRecord(hiddenRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", character } }));
	const auto ownView = F4T::DeathBag::Discovery(hiddenRoot, "profile-a#0", character, false);
	CHECK(ownView && ownView->size() == 1 && ownView->at(0).size() == 4 && ownView->at(0).at("stacks") == stacks);
	CHECK(F4T::CharacterSlots::SetSlotRecord(hiddenRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", otherCharacter } }));

	std::string guardId;
	auto guardRoot = makeRoot(guardId);
	CHECK(F4T::DeathBag::CreateEmpty(guardRoot, guardId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(guardRoot, guardId, stacks));
	CHECK(F4T::CharacterSlots::SetSlotRecord(guardRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", otherCharacter } }));
	CHECK(!F4T::DeathBag::Void(guardRoot, guardId));
	CHECK(guardRoot["deathBags"].contains(guardId));
	CHECK(F4T::CharacterSlots::SetSlotRecord(guardRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", character } }));
	CHECK(F4T::DeathBag::Void(guardRoot, guardId));

	std::string discardId;
	auto discardRoot = makeRoot(discardId);
	CHECK(F4T::DeathBag::CreateEmpty(discardRoot, discardId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(discardRoot, discardId, stacks));
	CHECK(!discardRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));
	Json discarded;
	CHECK(F4T::DeathBag::Discard(discardRoot, discardId, &discarded));
	CHECK(discarded == stacks && !discardRoot["deathBags"].contains(discardId));
	CHECK(!discardRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));
	std::string placedDiscardId;
	auto placedDiscardRoot = placedRoot(placedDiscardId);
	CHECK(!F4T::DeathBag::Discard(placedDiscardRoot, placedDiscardId));
	CHECK(placedDiscardRoot["deathBags"].contains(placedDiscardId));

	CHECK(F4T::DeathBag::CrashRefundSeconds == 120.0);
	std::string ttlId;
	auto ttlRoot = placedRoot(ttlId);
	const double lostAt = 1000.0 + 1800.0;
	CHECK(!F4T::DeathBag::HasExpiryWork(ttlRoot, lostAt - 1.0));
	CHECK(F4T::DeathBag::ExpirePending(ttlRoot, lostAt - 1.0).empty());
	CHECK(ttlRoot["deathBags"].contains(ttlId));
	CHECK(ttlRoot["deathBags"][ttlId]["items"][item]["count"] == 2u);
	CHECK(!F4T::DeathBag::HasExpiryWork(ttlRoot, lostAt));
	CHECK(F4T::DeathBag::ExpirePending(ttlRoot, lostAt).empty());
	CHECK(ttlRoot["deathBags"].contains(ttlId));
	CHECK(ttlRoot["deathBags"][ttlId]["placed"] == true);
	CHECK(ttlRoot["deathBags"][ttlId]["items"][item]["count"] == 2u);
	CHECK(!ttlRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));
	CHECK(!F4T::DeathBag::HasExpiryWork(ttlRoot, lostAt + 90000.0));
	CHECK(F4T::DeathBag::ExpirePending(ttlRoot, lostAt + 90000.0).empty());
	CHECK(ttlRoot["deathBags"].contains(ttlId));

	std::string quitId;
	auto quitRoot = makeRoot(quitId);
	CHECK(F4T::DeathBag::CreateEmpty(quitRoot, quitId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(quitRoot, quitId, stacks));
	CHECK(F4T::DeathBag::MarkDeparted(quitRoot, character, true, 1000.0));
	CHECK(F4T::DeathBag::ExpirePending(quitRoot, 1119.0).empty());
	CHECK(F4T::DeathBag::HasExpiryWork(quitRoot, 1120.0));
	const auto refundedQuit = F4T::DeathBag::ExpirePending(quitRoot, 1120.0);
	CHECK(refundedQuit.size() == 1 && refundedQuit.at(0).deathId == quitId && refundedQuit.at(0).stacks == stacks);
	CHECK(!quitRoot["deathBags"].contains(quitId));
	CHECK(quitRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);
	CHECK(!F4T::DeathBag::HasExpiryWork(quitRoot, 5000.0));

	std::string connectedId;
	auto connectedRoot = makeRoot(connectedId);
	CHECK(F4T::DeathBag::CreateEmpty(connectedRoot, connectedId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(connectedRoot, connectedId, stacks));
	CHECK(F4T::DeathBag::ExpirePending(connectedRoot, 1119.0).empty());
	CHECK(F4T::DeathBag::HasExpiryWork(connectedRoot, 1120.0));
	const auto refundedConnected = F4T::DeathBag::ExpirePending(connectedRoot, 1120.0);
	CHECK(refundedConnected.size() == 1 && refundedConnected.at(0).deathId == connectedId && refundedConnected.at(0).stacks == stacks);
	CHECK(!connectedRoot["deathBags"].contains(connectedId));
	CHECK(connectedRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);

	std::string workId;
	auto workRoot = makeRoot(workId);
	CHECK(F4T::DeathBag::CreateEmpty(workRoot, workId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(workRoot, workId, stacks));
	CHECK(!F4T::DeathBag::HasExpiryWork(workRoot, 1119.0));
	CHECK(F4T::DeathBag::HasExpiryWork(workRoot, 1120.0));
	CHECK(!F4T::DeathBag::HasExpiryWork(Json::object(), 1120.0));
	CHECK(F4T::DeathBag::HasExpiryWork(Json{ { "deathBags", Json::array() } }, 1120.0));

	std::string grantId;
	auto grantRoot = placedRoot(grantId);
	const auto grantRevision = grantRoot["profiles"]["profile-a#0"]["inventory"]["revision"].get<std::uint32_t>();
	CHECK(F4T::DeathBag::Withdraw(grantRoot, grantId, "profile-a#0", character, false, item, 1, grantRevision, "lost", 1500.0));
	const auto owed = F4T::DeathBag::ReconcileGrants(grantRoot, "profile-a#0", character);
	CHECK(owed && owed->size() == 1 && owed->at(0).at("operationId") == "lost");
	auto pagingRoot = grantRoot;
	const auto pagingRevision = pagingRoot["profiles"]["profile-a#0"]["inventory"]["revision"].get<std::uint32_t>();
	CHECK(F4T::DeathBag::Withdraw(pagingRoot, grantId, "profile-a#0", character, false, item, 1, pagingRevision, "next", 1501.0));
	std::optional<std::string> nextCursor;
	const auto nextPage = F4T::DeathBag::ReconcileGrants(pagingRoot, "profile-a#0", character, "bag:" + grantId + ":lost", &nextCursor);
	CHECK(nextPage && nextPage->size() == 1 && nextPage->at(0).at("operationId") == "next" && !nextCursor);
	const auto spendRevision = grantRoot["profiles"]["profile-a#0"]["inventory"]["revision"].get<std::uint32_t>();
	const auto spent = *F4T::InventorySnapshot::NormalizeRequest("spent", spendRevision, Json::array({ pipboyWire }));
	CHECK(F4T::InventorySnapshot::Reconcile(grantRoot, "profile-a#0", spent).changed);
	const auto dropped = F4T::DeathBag::ReconcileGrants(grantRoot, "profile-a#0", character);
	CHECK(dropped && dropped->size() == 1 && dropped->at(0).at("operationId") == "lost");
	CHECK(grantRoot["profiles"]["profile-a#0"]["deathBagInstances"].size() == 1);
	CHECK(F4T::DeathBag::MarkGrantAttempted(grantRoot, "profile-a#0", character, grantId, "lost", 2000.0));
	const auto attemptedKept = F4T::DeathBag::ReconcileGrants(grantRoot, "profile-a#0", character);
	CHECK(attemptedKept && attemptedKept->size() == 1 && attemptedKept->at(0).at("state") == "attempted" && !attemptedKept->at(0).contains("attemptedAt"));
	const auto deliveredGrant = attemptedKept ? F4T::DeathBagWire::DecodeGrantRecord(attemptedKept->at(0)) : std::nullopt;
	CHECK(deliveredGrant && deliveredGrant->delivered);
	const auto lostKey = "bag:" + grantId + ":lost";
	const auto& lostHistory = grantRoot.at("deathBagGrantHistory").at("profile-a#0");
	CHECK(!grantRoot.at("profiles").at("profile-a#0").at("deathBagInstances").contains(lostKey));
	CHECK(lostHistory.at(lostKey).at("attemptedAt") == 2000.0 && lostHistory.at(lostKey).at("state") == "attempted");
	CHECK(F4T::DeathBag::MarkGrantAttempted(grantRoot, "profile-a#0", character, grantId, "lost", 3000.0));
	CHECK(grantRoot.at("deathBagGrantHistory").at("profile-a#0").at(lostKey).at("attemptedAt") == 2000.0);
	const auto replayRevisionOf = [](const Json& a_root) { return a_root.at("profiles").at("profile-a#0").at("inventory").at("revision").get<std::uint32_t>(); };
	const auto revisionBeforeReplay = replayRevisionOf(grantRoot);
	CHECK(F4T::DeathBag::Withdraw(grantRoot, grantId, "profile-a#0", character, false, item, 1, revisionBeforeReplay, "lost", 3000.0));
	CHECK(replayRevisionOf(grantRoot) == revisionBeforeReplay && !grantRoot.at("profiles").at("profile-a#0").at("deathBagInstances").contains(lostKey));
	CHECK(!F4T::DeathBag::Withdraw(grantRoot, grantId, "profile-a#0", character, false, item, 2, revisionBeforeReplay, "lost", 3000.0));
	CHECK(grantRoot.at("profiles").at("profile-a#0").at("deathBagInstances").empty() && grantRoot.at("deathBagGrantHistory").at("profile-a#0").size() == 1);
	auto twinHistory = grantRoot;
	twinHistory["profiles"]["profile-a#0"]["deathBagInstances"][lostKey] = grantRoot.at("deathBagGrantHistory").at("profile-a#0").at(lostKey);
	bool twinRejected = false;
	try { F4T::DeathBag::ValidateRecipientInstances(twinHistory); } catch (const std::invalid_argument&) { twinRejected = true; }
	CHECK(twinRejected);

	std::string wedgeId;
	auto wedgeRoot = placedRoot(wedgeId);
	const auto attemptedKey = std::string{ "bag:" } + wedgeId + ":health";
	wedgeRoot["profiles"]["profile-a#0"]["deathBagInstances"][attemptedKey] = Json{ { "deathId", wedgeId }, { "operationId", "health" },
		{ "characterId", character }, { "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "attempted" } };
	F4T::DeathBag::ValidateRecipientInstances(wedgeRoot);
	const auto staleKey = std::string{ "bag:" } + wedgeId + ":stale";
	wedgeRoot["profiles"]["profile-a#0"]["deathBagInstances"][staleKey] = Json{ { "deathId", wedgeId }, { "operationId", "stale" },
		{ "characterId", character }, { "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "unknown" } };
	bool threw = false;
	try {
		F4T::DeathBag::ValidateRecipientInstances(wedgeRoot);
	} catch (const std::invalid_argument&) {
		threw = true;
	}
	CHECK(threw);
	wedgeRoot["profiles"]["profile-a#0"]["deathBagInstances"].erase(staleKey);
	wedgeRoot["profiles"]["profile-a#0"]["deathBagInstances"][attemptedKey]["attemptedAt"] = 1500.0;
	F4T::DeathBag::ValidateRecipientInstances(wedgeRoot);
	wedgeRoot["profiles"]["profile-a#0"]["deathBagInstances"][attemptedKey]["state"] = "pending";
	bool pendingStampThrew = false;
	try {
		F4T::DeathBag::ValidateRecipientInstances(wedgeRoot);
	} catch (const std::invalid_argument&) {
		pendingStampThrew = true;
	}
	CHECK(pendingStampThrew);

	auto downedRoot = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
	F4T::Identity::TokenHash downedHash{};
	F4T::DurableState::SaveResumeCredential(downedRoot, "profile-a", downedHash, std::nullopt, true, 5000.0, 1000.0);
	CHECK(F4T::CharacterSlots::SetSlotRecord(downedRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", character } }));
	CHECK(F4T::DurableState::InitializeInventory(downedRoot, "profile-a#0", Json::array({
		Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } } }), 1000.0));
	Json downedRecord{ { "version", 1u }, { "profileId", "profile-a#0" }, { "characterId", character }, { "inventoryRevision", 0u },
		{ "cellKey", "test.esm:00000001" }, { "worldspaceKey", nullptr }, { "position", Json::array({ 1.0, 2.0, 3.0 }) },
		{ "state", "downed" }, { "deadlineUnix", 2000.0 }, { "locationSource", "client-reported" } };
	const auto downed = F4T::DeathIntent::Allocate(downedRoot, downedRecord);
	CHECK(downed.outcome == F4T::DeathIntent::Outcome::Created);
	downedRoot["profiles"]["profile-a#0"]["inventory"]["revision"] = 7u;
	CHECK(!F4T::DeathIntent::HasDueExpiry(downedRoot, 1999.0, {}));
	CHECK(F4T::DeathIntent::HasDueExpiry(downedRoot, 2000.0, {}));
	CHECK(!F4T::DeathIntent::HasDueExpiry(downedRoot, 2000.0, { character }));
	CHECK(!F4T::DeathIntent::HasDueExpiry(Json::object(), 2000.0, {}));
	CHECK(F4T::DeathIntent::MarkDead(downedRoot, character, downed.deathId));
	CHECK(downedRoot["deathIntents"][downed.deathId]["state"] == "dead");
	CHECK(downedRoot["deathIntents"][downed.deathId]["inventoryRevision"] == 7u);

	std::string offlineVoidId;
	auto offlineVoidRoot = makeRoot(offlineVoidId);
	CHECK(F4T::DeathBag::CreateEmpty(offlineVoidRoot, offlineVoidId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(offlineVoidRoot, offlineVoidId, stacks));
	CHECK(F4T::DeathBag::MarkDeparted(offlineVoidRoot, character, false, 1000.0));
	const auto offlineVoided = F4T::DeathBag::ExpirePending(offlineVoidRoot, 1120.0);
	CHECK(offlineVoided.size() == 1 && offlineVoided.at(0).stacks == stacks);
	CHECK(!offlineVoidRoot["deathBags"].contains(offlineVoidId));

	std::string bigId;
	auto bigRoot = placedRoot(bigId);
	Json bigStacks = Json::array();
	for (int i = 0; i < 129; ++i) bigStacks.push_back(Json{ { "itemKey", item }, { "count", 1u }, { "metadata", nullptr } });
	bigRoot["deathBags"][bigId]["stacks"] = bigStacks;
	bigRoot["deathBags"][bigId]["items"][item]["count"] = 129u;
	CHECK(F4T::CharacterSlots::SetSlotRecord(bigRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", otherCharacter } }));
	const auto bigOther = F4T::DeathBag::Discovery(bigRoot, "profile-a#0", otherCharacter, true);
	CHECK(bigOther && bigOther->empty());
	CHECK(F4T::CharacterSlots::SetSlotRecord(bigRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", character } }));
	const auto bigOwn = F4T::DeathBag::Discovery(bigRoot, "profile-a#0", character, false);
	CHECK(bigOwn && bigOwn->size() == 1 && bigOwn->at(0).at("stacks").size() == 129);

	std::string clearId;
	auto clearRoot = makeRoot(clearId);
	CHECK(F4T::DeathBag::CreateEmpty(clearRoot, clearId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(clearRoot, clearId, stacks));
	CHECK(F4T::DeathBag::MarkDeparted(clearRoot, character, true, 1000.0));
	CHECK(F4T::DeathBag::ClearDeparted(clearRoot, character));
	CHECK(clearRoot["deathBags"][clearId]["departure"] == "none" && clearRoot["deathBags"][clearId]["departedAt"] == 0.0);
	CHECK(!F4T::DeathBag::ClearDeparted(clearRoot, character));
	CHECK(F4T::DeathBag::ExpirePending(clearRoot, 1119.0).empty());
	CHECK(F4T::DeathBag::MarkDeparted(clearRoot, character, false, 1030.0));
	CHECK(F4T::DeathBag::ExpirePending(clearRoot, 1149.0).empty());
	const auto clearedExpired = F4T::DeathBag::ExpirePending(clearRoot, 1150.0);
	CHECK(clearedExpired.size() == 1 && clearedExpired.at(0).stacks == stacks);
	CHECK(!clearRoot["deathBags"].contains(clearId));
	CHECK(clearRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);

	std::string legacyId;
	auto legacyRoot = makeRoot(legacyId);
	CHECK(F4T::DeathBag::CreateEmpty(legacyRoot, legacyId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(legacyRoot, legacyId, stacks));
	legacyRoot["deathBags"][legacyId].erase("departure");
	legacyRoot["deathBags"][legacyId].erase("departedAt");
	CHECK(legacyRoot["deathBags"][legacyId].size() == 8);
	F4T::DeathBag::Validate(legacyRoot);
	F4T::DeathBag::MigrateLoaded(legacyRoot, 1500.0);
	CHECK(legacyRoot["deathBags"][legacyId].size() == 10);
	CHECK(legacyRoot["deathBags"][legacyId]["departure"] == "crash" && legacyRoot["deathBags"][legacyId]["departedAt"] == 1500.0);
	F4T::DeathBag::Validate(legacyRoot);
	CHECK(F4T::DeathBag::ExpirePending(legacyRoot, 1619.0).empty());
	CHECK(F4T::DeathBag::ExpirePending(legacyRoot, 1620.0).size() == 1);
	CHECK(legacyRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);

	std::string legacyPlacedId;
	auto legacyPlacedRoot = placedRoot(legacyPlacedId);
	legacyPlacedRoot["deathBags"][legacyPlacedId].erase("departure");
	legacyPlacedRoot["deathBags"][legacyPlacedId].erase("departedAt");
	F4T::DeathBag::Validate(legacyPlacedRoot);
	F4T::DeathBag::MigrateLoaded(legacyPlacedRoot, 1500.0);
	CHECK(legacyPlacedRoot["deathBags"][legacyPlacedId]["departure"] == "none");
	CHECK(F4T::DeathBag::ExpirePending(legacyPlacedRoot, 1500.0).empty());

	const auto revisionOf = [](const Json& a_root) { return a_root.at("profiles").at("profile-a#0").at("inventory").at("revision").get<std::uint32_t>(); };
	std::string deniedId;
	auto deniedRoot = placedRoot(deniedId);
	CHECK(F4T::CharacterSlots::SetSlotRecord(deniedRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", otherCharacter } }));
	CHECK(!F4T::DeathBag::Withdraw(deniedRoot, deniedId, "profile-a#0", otherCharacter, false, item, 1, revisionOf(deniedRoot), "denied", 1500.0));
	CHECK(deniedRoot["deathBags"][deniedId]["items"][item]["count"] == 2u);
	CHECK(F4T::DeathBag::Withdraw(deniedRoot, deniedId, "profile-a#0", otherCharacter, true, item, 1, revisionOf(deniedRoot), "loot", 1500.0));
	CHECK(deniedRoot["deathBags"][deniedId]["items"][item]["count"] == 1u);
	CHECK(deniedRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + deniedId + ":loot"]["state"] == "pending");

	std::string expiredId;
	auto expiredRoot = placedRoot(expiredId);
	CHECK(F4T::DeathBag::Withdraw(expiredRoot, expiredId, "profile-a#0", character, false, item, 1, revisionOf(expiredRoot), "late", 901000.0));
	CHECK(expiredRoot["deathBags"][expiredId]["items"][item]["count"] == 1u);
	CHECK(F4T::DeathBag::Withdraw(expiredRoot, expiredId, "profile-a#0", character, false, item, 1, revisionOf(expiredRoot), "later", 901001.0));
	CHECK(!expiredRoot["deathBags"].contains(expiredId));

	std::string replayId;
	auto replayRoot = placedRoot(replayId);
	Json replayMetadata;
	const auto replayRevision = revisionOf(replayRoot);
	CHECK(F4T::DeathBag::Withdraw(replayRoot, replayId, "profile-a#0", character, false, item, 1, replayRevision, "once", 1500.0, &replayMetadata));
	const auto replayed = replayRoot;
	CHECK(F4T::DeathBag::Withdraw(replayRoot, replayId, "profile-a#0", character, false, item, 1, replayRevision, "once", 1500.0, &replayMetadata));
	CHECK(replayRoot == replayed);
	CHECK(!F4T::DeathBag::Withdraw(replayRoot, replayId, "profile-a#0", character, false, item, 2, revisionOf(replayRoot), "once", 1500.0));
	CHECK(replayRoot["deathBags"][replayId]["items"][item]["count"] == 1u);

	std::string raceId;
	auto raceRoot = placedRoot(raceId);
	CHECK(F4T::DeathBag::Withdraw(raceRoot, raceId, "profile-a#0", character, true, item, 1, revisionOf(raceRoot), "owner-1", 1500.0));
	CHECK(F4T::CharacterSlots::SetSlotRecord(raceRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", otherCharacter } }));
	CHECK(!F4T::DeathBag::Withdraw(raceRoot, raceId, "profile-a#0", otherCharacter, true, item, 2, revisionOf(raceRoot), "loot-2", 1500.0));
	CHECK(F4T::DeathBag::Withdraw(raceRoot, raceId, "profile-a#0", otherCharacter, true, item, 1, revisionOf(raceRoot), "loot-1", 1500.0));
	CHECK(F4T::CharacterSlots::SetSlotRecord(raceRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", character } }));
	CHECK(!F4T::DeathBag::Withdraw(raceRoot, raceId, "profile-a#0", character, true, item, 1, revisionOf(raceRoot), "owner-2", 1500.0));
	CHECK(raceRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);
	CHECK(!raceRoot["deathBags"].contains(raceId));

	std::string capId;
	auto capRoot = placedRoot(capId);
	auto& capRecords = capRoot["profiles"]["profile-a#0"]["deathBagInstances"];
	for (int i = 0; i < 64; ++i) {
		const auto operation = "pending" + std::to_string(i);
		capRecords["bag:" + capId + ":" + operation] = Json{ { "deathId", capId }, { "operationId", operation }, { "characterId", character },
			{ "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "pending" } };
	}
	CHECK(!F4T::DeathBag::Withdraw(capRoot, capId, "profile-a#0", character, false, item, 1, revisionOf(capRoot), "over-cap", 1500.0));
	capRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + capId + ":pending0"]["state"] = "attempted";
	CHECK(F4T::DeathBag::Withdraw(capRoot, capId, "profile-a#0", character, false, item, 1, revisionOf(capRoot), "under-cap", 1500.0));
	CHECK(capRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + capId + ":under-cap"]["state"] == "pending");

	std::string pruneId;
	auto pruneRoot = placedRoot(pruneId);
	const std::string goneId = "ffffffffffffffffffffffffffffffff";
	const auto goneKey = "bag:" + goneId + ":gone";
	const Json goneRecord{ { "deathId", goneId }, { "operationId", "gone" }, { "characterId", character }, { "itemKey", item },
		{ "count", 1u }, { "metadata", nullptr }, { "state", "attempted" } };
	pruneRoot["profiles"]["profile-a#0"]["deathBagInstances"][goneKey] = goneRecord;
	CHECK(F4T::DeathBag::Withdraw(pruneRoot, pruneId, "profile-a#0", character, false, item, 1, revisionOf(pruneRoot), "fresh", 1500.0));
	CHECK(pruneRoot.at("profiles").at("profile-a#0").at("deathBagInstances").at(goneKey) == goneRecord);
	CHECK(F4T::DeathBag::MigrateLoaded(pruneRoot, 1600.0));
	CHECK(!pruneRoot.at("profiles").at("profile-a#0").at("deathBagInstances").contains(goneKey));
	CHECK(pruneRoot.at("profiles").at("profile-a#0").at("deathBagInstances").contains("bag:" + pruneId + ":fresh"));
	auto goneMigrated = goneRecord;
	goneMigrated["attemptedAt"] = 1600.0;
	CHECK(pruneRoot.at("deathBagGrantHistory").at("profile-a#0").at(goneKey) == goneMigrated);
	const auto migratedOnce = pruneRoot;
	CHECK(!F4T::DeathBag::MigrateLoaded(pruneRoot, 1700.0) && pruneRoot == migratedOnce);
	F4T::DeathBag::ValidateRecipientInstances(pruneRoot);
	const auto goneOffer = F4T::DeathBag::ReconcileGrants(pruneRoot, "profile-a#0", character);
	CHECK(goneOffer && goneOffer->size() == 2 && goneOffer->at(1).at("operationId") == "gone" && !goneOffer->at(1).contains("attemptedAt"));

	std::string evictId;
	auto evictRoot = placedRoot(evictId);
	auto& evictRecords = evictRoot["profiles"]["profile-a#0"]["deathBagInstances"];
	for (std::size_t i = 0; i < F4T::DeathBag::MaximumPendingGrants; ++i) {
		const auto operation = "p" + std::to_string(i);
		evictRecords["bag:" + evictId + ":" + operation] = Json{ { "deathId", evictId }, { "operationId", operation }, { "characterId", character },
			{ "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "pending" } };
	}
	const std::size_t legacyAttempted = 256;
	for (std::size_t i = 0; i <= legacyAttempted; ++i) {
		const auto operation = "a" + std::to_string(i);
		evictRecords["bag:" + evictId + ":" + operation] = Json{ { "deathId", evictId }, { "operationId", operation }, { "characterId", character },
			{ "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "attempted" }, { "attemptedAt", 1000.0 + static_cast<double>(i) } };
	}
	const auto evictOffer = F4T::DeathBag::ReconcileGrants(evictRoot, "profile-a#0", character);
	CHECK(evictOffer && evictOffer->size() == F4T::DeathBag::MaximumPendingGrants);
	CHECK(evictOffer && std::all_of(evictOffer->begin(), evictOffer->end(), [](const Json& a_grant) { return a_grant.at("state") == "pending"; }));
	const auto& evictedRecords = evictRoot["profiles"]["profile-a#0"]["deathBagInstances"];
	CHECK(evictedRecords.size() == F4T::DeathBag::MaximumPendingGrants + legacyAttempted + 1);
	CHECK(F4T::DeathBag::PendingGrantCount(evictedRecords) == F4T::DeathBag::MaximumPendingGrants);
	CHECK(evictedRecords.contains("bag:" + evictId + ":a0") && evictedRecords.contains("bag:" + evictId + ":a1"));
	for (std::size_t i = 2; i < F4T::DeathBag::MaximumPendingGrants; ++i) evictRecords.erase("bag:" + evictId + ":p" + std::to_string(i));
	const auto mixedOffer = F4T::DeathBag::ReconcileGrants(evictRoot, "profile-a#0", character);
	CHECK(mixedOffer && mixedOffer->size() == F4T::DeathBag::MaximumPendingGrants );
	CHECK(mixedOffer && mixedOffer->at(0).at("state") == "pending" && mixedOffer->at(1).at("state") == "pending");
	CHECK(mixedOffer && mixedOffer->at(2).at("operationId") == "a" + std::to_string(legacyAttempted));
	CHECK(mixedOffer && mixedOffer->at(3).at("operationId") == "a" + std::to_string(legacyAttempted - 1));
	CHECK(evictedRecords.contains("bag:" + evictId + ":a0") && std::none_of(mixedOffer->begin(), mixedOffer->end(), [](const Json& a_grant) { return a_grant.at("operationId") == "a0"; }));
	CHECK(F4T::DeathBag::MigrateLoaded(evictRoot, 5000.0));
	CHECK(evictRoot.at("profiles").at("profile-a#0").at("deathBagInstances").size() == 2);
	CHECK(evictRoot.at("deathBagGrantHistory").at("profile-a#0").size() == legacyAttempted + 1);
	CHECK(evictRoot.at("deathBagGrantHistory").at("profile-a#0").at("bag:" + evictId + ":a0").at("attemptedAt") == 1000.0);
	CHECK(F4T::DeathBag::ReconcileGrants(evictRoot, "profile-a#0", character) == mixedOffer);

	std::string capBagsId;
	auto capBagsRoot = placedRoot(capBagsId);
	const Json bagTemplate = capBagsRoot["deathBags"][capBagsId];
	for (int i = 1; i <= 128; ++i) {
		const auto digits = std::to_string(i);
		capBagsRoot["deathBags"][std::string(32 - digits.size(), '0') + digits] = bagTemplate;
	}
	const auto capped = F4T::DeathBag::Discovery(capBagsRoot, "profile-a#0", character, false);
	CHECK(capped && capped->size() == 128);

	std::string crashId;
	auto crashRoot = makeRoot(crashId);
	CHECK(F4T::DeathBag::CreateEmpty(crashRoot, crashId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(crashRoot, crashId, stacks));
	CHECK(F4T::DeathBag::MarkDeparted(crashRoot, character, false, 1000.0));
	CHECK(F4T::CharacterSlots::SetSlotRecord(crashRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", "quest-22222222222222222222222222222222" } }));
	CHECK(!F4T::DeathBag::HasExpiryWork(crashRoot, 1120.0));
	CHECK(F4T::DeathBag::ExpirePending(crashRoot, 1120.0).empty());
	CHECK(F4T::CharacterSlots::SetSlotRecord(crashRoot, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", character } }));
	CHECK(F4T::DeathBag::HasExpiryWork(crashRoot, 1120.0));
	CHECK(F4T::DeathBag::ExpirePending(crashRoot, 1120.0).size() == 1);

	std::string migrateId;
	auto migrateRoot = makeRoot(migrateId);
	CHECK(F4T::DeathBag::CreateEmpty(migrateRoot, migrateId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(migrateRoot, migrateId, stacks));
	CHECK(F4T::DeathBag::MigrateLoaded(migrateRoot, 1500.0));
	CHECK(migrateRoot["deathBags"][migrateId]["departure"] == "crash");
	CHECK(!F4T::DeathBag::MigrateLoaded(migrateRoot, 1500.0));

	std::string budgetId;
	auto budgetRoot = placedRoot(budgetId);
	Json fullStacks = Json::array();
	for (int i = 0; i < 200; ++i) fullStacks.push_back(Json{ { "itemKey", item }, { "count", 1u }, { "metadata", nullptr } });
	budgetRoot["deathBags"][budgetId]["stacks"] = fullStacks;
	budgetRoot["deathBags"][budgetId]["items"][item]["count"] = 200u;
	const Json fullBag = budgetRoot["deathBags"][budgetId];
	for (int i = 1; i <= 6; ++i) {
		const auto digits = std::to_string(i);
		budgetRoot["deathBags"][std::string(32 - digits.size(), '0') + digits] = fullBag;
	}
	const auto budgeted = F4T::DeathBag::Discovery(budgetRoot, "profile-a#0", character, false);
	CHECK(budgeted && budgeted->size() == 5);
	std::size_t budgetBytes = F4T::DeathBagWire::DiscoveryFrameBytes();
	for (const auto& entry : *budgeted) budgetBytes += F4T::DeathBagWire::DiscoveryEntryBytes(entry);
	CHECK(Json{ { "type", "deathBagDiscovery" }, { "bags", *budgeted } }.dump().size() <= F4T::DeathBagWire::MaximumDiscoveryBytes);
	CHECK(budgetBytes <= F4T::DeathBagWire::MaximumDiscoveryBytes);
	const std::string foreignKey = "ffffffffffffffffffffffffffffffff";
	std::string foreignId;
	auto foreignRoot = placedRoot(foreignId);
	Json foreignBag = foreignRoot["deathBags"][foreignId];
	foreignBag["characterId"] = "quest-22222222222222222222222222222222";
	budgetRoot["deathBags"][foreignKey] = foreignBag;
	const auto budgetShared = F4T::DeathBag::Discovery(budgetRoot, "profile-a#0", character, true);
	CHECK(budgetShared && budgetShared->size() == 6 && budgetShared->at(5).at("deathId") == foreignKey);

	std::string ownerCapId;
	auto ownerCapRoot = placedRoot(ownerCapId);
	Json ownerStacks = Json::array();
	for (int i = 0; i < 1025; ++i) ownerStacks.push_back(Json{ { "itemKey", item }, { "count", 1u }, { "metadata", nullptr } });
	ownerCapRoot["deathBags"][ownerCapId]["stacks"] = ownerStacks;
	ownerCapRoot["deathBags"][ownerCapId]["items"][item]["count"] = 1025u;
	const auto overOwnerCap = F4T::DeathBag::Discovery(ownerCapRoot, "profile-a#0", character, false);
	CHECK(overOwnerCap && overOwnerCap->empty());
	ownerStacks.erase(ownerStacks.begin());
	ownerCapRoot["deathBags"][ownerCapId]["stacks"] = ownerStacks;
	ownerCapRoot["deathBags"][ownerCapId]["items"][item]["count"] = 1024u;
	const auto atOwnerCap = F4T::DeathBag::Discovery(ownerCapRoot, "profile-a#0", character, false);
	CHECK(atOwnerCap && atOwnerCap->size() == 1 && atOwnerCap->at(0).at("stacks").size() == 1024);

	std::string depositId;
	auto depositRoot = makeRoot(depositId);
	CHECK(F4T::DeathBag::CreateEmpty(depositRoot, depositId, 1000.0));
	Json depositStacks = Json::array();
	for (int i = 0; i < 1025; ++i) depositStacks.push_back(Json{ { "itemKey", item }, { "count", 1u }, { "metadata", nullptr } });
	depositRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] = 1025u;
	const auto beforeDeposit = depositRoot;
	CHECK(!F4T::DeathBag::Deposit(depositRoot, depositId, depositStacks));
	CHECK(depositRoot == beforeDeposit);
	depositStacks.erase(depositStacks.begin());
	depositRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] = 1024u;
	CHECK(F4T::DeathBag::Deposit(depositRoot, depositId, depositStacks));
	CHECK(depositRoot["deathBags"][depositId]["stacks"].size() == 1024);
	CHECK(F4T::DeathBag::Place(depositRoot, depositId, "profile-a#0", character));
	const auto depositDiscovered = F4T::DeathBag::Discovery(depositRoot, "profile-a#0", character, false);
	CHECK(depositDiscovered && depositDiscovered->size() == 1 && depositDiscovered->at(0).at("stacks").size() == 1024);

	const std::string dusty = "fallout4.esm:0006D7B8";
	const std::string vault81 = "fallout4.esm:001B9B87";
	const std::string neighbour = "fallout4.esm:00021B3C";
	const Json extraPipboy = Json::array({
		Json{ { "plugin", "fallout4.esm" }, { "localFormId", "0006D7B8" }, { "count", 1u } },
		Json{ { "plugin", "fallout4.esm" }, { "localFormId", "001B9B87" }, { "count", 1u } },
		Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3C" }, { "count", 1u } } });
	std::string pipboyGuardId;
	auto pipboyGuardRoot = makeRootWith(pipboyGuardId, extraPipboy);
	CHECK(F4T::DeathBag::CreateEmpty(pipboyGuardRoot, pipboyGuardId, 1000.0));
	const Json pipboyGuardStacks = Json::array({ Json{ { "itemKey", item }, { "count", 2u }, { "metadata", nullptr } },
		Json{ { "itemKey", neighbour }, { "count", 1u }, { "metadata", nullptr } } });
	const Json pipboyGuardForged = Json::array({ Json{ { "itemKey", item }, { "count", 2u }, { "metadata", nullptr } },
		Json{ { "itemKey", dusty }, { "count", 1u }, { "metadata", nullptr } } });
	CHECK(!F4T::DeathBag::Deposit(pipboyGuardRoot, pipboyGuardId, pipboyGuardForged));
	CHECK(F4T::DeathBag::Deposit(pipboyGuardRoot, pipboyGuardId, pipboyGuardStacks));
	const auto& pipboyGuardItems = pipboyGuardRoot["profiles"]["profile-a#0"]["inventory"]["items"];
	CHECK(pipboyGuardItems.contains(pipboy) && pipboyGuardItems.contains(dusty) && pipboyGuardItems.contains(vault81));
	CHECK(!pipboyGuardItems.contains(neighbour) && !pipboyGuardItems.contains(item));
	const auto& pipboyGuardBagItems = pipboyGuardRoot["deathBags"][pipboyGuardId]["items"];
	CHECK(pipboyGuardBagItems.contains(neighbour) && pipboyGuardBagItems.contains(item));
	CHECK(!pipboyGuardBagItems.contains(pipboy) && !pipboyGuardBagItems.contains(dusty) && !pipboyGuardBagItems.contains(vault81));

	const auto healthMeta = F4T::ItemHealthSnapshot::Encode(0.75f);
	CHECK(healthMeta);
	std::string healthId;
	auto healthRoot = makeRoot(healthId);
	CHECK(F4T::DeathBag::CreateEmpty(healthRoot, healthId, 1000.0));
	Json healthStacks = Json::array();
	for (int i = 0; i < 256; ++i) healthStacks.push_back(Json{ { "itemKey", item }, { "count", 1u }, { "metadata", *healthMeta } });
	healthRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] = 256u;
	CHECK(F4T::DeathBag::Deposit(healthRoot, healthId, healthStacks));
	CHECK(healthRoot["deathBags"][healthId]["stacks"].size() == 256);

	std::string heavyId;
	auto heavyRoot = makeRoot(heavyId);
	CHECK(F4T::DeathBag::CreateEmpty(heavyRoot, heavyId, 1000.0));
	Json heavyStacks = Json::array();
	for (int i = 0; i < 1024; ++i) heavyStacks.push_back(Json{ { "itemKey", item }, { "count", 1u }, { "metadata", *healthMeta } });
	heavyRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] = 1024u;
	const auto beforeHeavy = heavyRoot;
	CHECK(!F4T::DeathBag::Deposit(heavyRoot, heavyId, heavyStacks));
	CHECK(heavyRoot == beforeHeavy);
	std::string archiveId;
	auto archiveRoot = placedRoot(archiveId);
	CHECK(F4T::DeathBag::MoveToArchive(archiveRoot, archiveId, "placeFailed"));
	CHECK(!F4T::DeathBag::MoveToArchive(archiveRoot, archiveId, "placeFailed"));
	CHECK(!archiveRoot["deathBags"].contains(archiveId) && archiveRoot["deathBagArchive"][archiveId]["reason"] == "placeFailed");
	CHECK(archiveRoot["deathBagArchive"][archiveId]["bag"]["items"][item]["count"] == 2u);
	CHECK(!F4T::DeathBag::CreateEmpty(archiveRoot, archiveId, 1000.0));
	CHECK(!archiveRoot["deathBags"].contains(archiveId) && archiveRoot["deathBagArchive"].contains(archiveId));
	const auto archiveSummary = F4T::DeathBag::ArchiveSummary(archiveRoot, 0);
	CHECK(archiveSummary.at("total") == 1u && archiveSummary.at("bags").size() == 1 && archiveSummary.at("bags").at(0).at("deathId") == archiveId && archiveSummary.at("bags").at(0).at("reason") == "placeFailed" && archiveSummary.at("bags").at(0).at("stacks") == stacks);
	const auto archivedEntry = archiveRoot["deathBagArchive"][archiveId];
	CHECK(F4T::DeathBag::RecoverArchived(archiveRoot, archiveId));
	CHECK(!archiveRoot["deathBagArchive"].contains(archiveId));
	CHECK(archiveRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + archiveId + ":" + F4T::DeathBag::RecoveryOperation(archiveId, 1, 0)]["count"] == 2u);
	CHECK(archiveRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);
	CHECK(!F4T::DeathBag::RecoverArchived(archiveRoot, archiveId));
	archiveRoot["deathBagArchive"][archiveId] = archivedEntry;
	CHECK(!F4T::DeathBag::RecoverArchived(archiveRoot, archiveId) && archiveRoot["deathBagArchive"].contains(archiveId));
	CHECK(archiveRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);

	std::string malformedId;
	auto malformedRoot = placedRoot(malformedId);
	CHECK(F4T::DeathBag::MoveToArchive(malformedRoot, malformedId, "evicted"));
	malformedRoot["deathBagArchive"][malformedId]["reason"] = "lost";
	bool archiveRejected = false;
	try {
		F4T::DeathBag::Validate(malformedRoot);
	} catch (const std::invalid_argument&) {
		archiveRejected = true;
	}
	CHECK(archiveRejected);
	CHECK(!F4T::DeathBag::RecoverArchived(malformedRoot, malformedId) && malformedRoot["deathBagArchive"].contains(malformedId));

	std::string onlyId;
	auto onlyRoot = placedRoot(onlyId);
	CHECK(!F4T::DeathBag::EvictForCapacity(onlyRoot));
	CHECK(onlyRoot["deathBags"].contains(onlyId) && !onlyRoot.contains("deathBagArchive"));
	const Json sampleBag = onlyRoot["deathBags"][onlyId];
	const auto paddedId = [](std::size_t a_index) {
		const auto digits = std::to_string(a_index);
		return std::string(32 - digits.size(), '0') + digits;
	};

	std::string protectedId;
	auto protectedRoot = placedRoot(protectedId);
	for (std::size_t i = 1; i < F4T::DeathBag::MaximumBags; ++i) {
		protectedRoot["deathBags"][paddedId(i)] = sampleBag;
		protectedRoot["deathBags"][paddedId(i)]["createdAt"] = i == 1 ? 1500.0 : 2000.0;
	}
	CHECK(protectedRoot["deathBags"].size() == F4T::DeathBag::MaximumBags);
	CHECK(F4T::DeathBag::EvictForCapacity(protectedRoot));
	CHECK(protectedRoot["deathBags"].contains(protectedId) && !protectedRoot["deathBags"].contains(paddedId(1)));
	CHECK(protectedRoot["deathBagArchive"][paddedId(1)]["reason"] == "evicted");

	std::string overflowId;
	auto overflowRoot = makeRoot(overflowId);
	for (std::size_t i = 1; i <= F4T::DeathBag::MaximumBags; ++i) overflowRoot["deathBags"][paddedId(i)] = sampleBag;
	overflowRoot["deathBags"][paddedId(1)]["createdAt"] = 900.0;
	CHECK(overflowRoot["deathBags"].size() == F4T::DeathBag::MaximumBags);
	CHECK(F4T::DeathBag::CreateEmpty(overflowRoot, overflowId, 1000.0));
	CHECK(overflowRoot["deathBags"].contains(overflowId) && overflowRoot["deathBags"].size() == F4T::DeathBag::MaximumBags);
	CHECK(!overflowRoot["deathBags"].contains(paddedId(1)) && overflowRoot["deathBagArchive"][paddedId(1)]["reason"] == "evicted");

	std::string fullArchiveId;
	auto fullArchiveRoot = placedRoot(fullArchiveId);
	for (std::size_t i = 1; i <= F4T::DeathBag::MaximumArchivedBags; ++i) fullArchiveRoot["deathBagArchive"][paddedId(i)] = Json{ { "reason", "evicted" }, { "bag", sampleBag } };
	CHECK(!F4T::DeathBag::MoveToArchive(fullArchiveRoot, fullArchiveId, "placeFailed"));
	CHECK(fullArchiveRoot["deathBags"].contains(fullArchiveId) && !fullArchiveRoot["deathBagArchive"].contains(fullArchiveId));

	std::string capArchiveId;
	auto capArchiveRoot = placedRoot(capArchiveId);
	CHECK(F4T::DeathBag::MoveToArchive(capArchiveRoot, capArchiveId, "placeFailed"));
	for (int i = 0; i < 64; ++i) {
		const auto operation = "pending" + std::to_string(i);
		capArchiveRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + capArchiveId + ":" + operation] = Json{ { "deathId", capArchiveId }, { "operationId", operation },
			{ "characterId", character }, { "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "pending" } };
	}
	CHECK(!F4T::DeathBag::RecoverArchived(capArchiveRoot, capArchiveId));
	CHECK(capArchiveRoot["deathBagArchive"].contains(capArchiveId) && capArchiveRoot["profiles"]["profile-a#0"]["deathBagInstances"].size() == 64);
	capArchiveRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + capArchiveId + ":pending0"]["state"] = "attempted";
	CHECK(F4T::DeathBag::RecoverArchived(capArchiveRoot, capArchiveId));
	CHECK(capArchiveRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + capArchiveId + ":" + F4T::DeathBag::RecoveryOperation(capArchiveId, 1, 0)]["state"] == "pending");

	std::string twinId;
	auto twinRoot = placedRoot(twinId);
	const std::string twinOtherId = "fedcba9876543210fedcba9876543210";
	const Json twinBag = twinRoot["deathBags"][twinId];
	twinRoot["deathBags"][twinOtherId] = twinBag;
	CHECK(F4T::DeathBag::MoveToArchive(twinRoot, twinId, "placeFailed"));
	CHECK(F4T::DeathBag::MoveToArchive(twinRoot, twinOtherId, "placeFailed"));
	CHECK(F4T::DeathBag::RecoverArchived(twinRoot, twinId));
	CHECK(F4T::DeathBag::RecoverArchived(twinRoot, twinOtherId));
	CHECK(!twinRoot["deathBagArchive"].contains(twinId) && !twinRoot["deathBagArchive"].contains(twinOtherId));
	CHECK(twinRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 4u);
	CHECK(twinRoot["profiles"]["profile-a#0"]["deathBagInstances"].size() == 2);
	CHECK(twinRoot["profiles"]["profile-a#0"]["deathBagInstances"].contains("bag:" + twinOtherId + ":" + F4T::DeathBag::RecoveryOperation(twinOtherId, 1, 0)));

	std::string bigArchiveId;
	auto bigArchiveRoot = placedRoot(bigArchiveId);
	Json chunkStacks = Json::array();
	for (int i = 0; i < 70; ++i) chunkStacks.push_back(Json{ { "itemKey", item }, { "count", 1u }, { "metadata", nullptr } });
	bigArchiveRoot["deathBags"][bigArchiveId]["stacks"] = chunkStacks;
	bigArchiveRoot["deathBags"][bigArchiveId]["items"][item]["count"] = 70u;
	CHECK(F4T::DeathBag::MoveToArchive(bigArchiveRoot, bigArchiveId, "placeFailed"));
	const Json bigEntry = bigArchiveRoot["deathBagArchive"][bigArchiveId];
	CHECK(F4T::DeathBag::RecoverArchived(bigArchiveRoot, bigArchiveId));
	CHECK(bigArchiveRoot["deathBagArchive"][bigArchiveId]["bag"]["stacks"].size() == 6);
	CHECK(bigArchiveRoot["deathBagArchive"][bigArchiveId]["bag"]["items"][item]["count"] == 6u);
	CHECK(bigArchiveRoot["profiles"]["profile-a#0"]["deathBagInstances"].size() == 64);
	CHECK(bigArchiveRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 64u);
	CHECK(!F4T::DeathBag::RecoverArchived(bigArchiveRoot, bigArchiveId));
	for (int i = 0; i < 6; ++i) CHECK(F4T::DeathBag::ClaimQueuedGrant(bigArchiveRoot, "profile-a#0", character, bigArchiveId, F4T::DeathBag::RecoveryOperation(bigArchiveId, 70, i)));
	for (int i = 0; i < 6; ++i) CHECK(F4T::DeathBag::MarkGrantAttempted(bigArchiveRoot, "profile-a#0", character, bigArchiveId, F4T::DeathBag::RecoveryOperation(bigArchiveId, 70, i), 1000.0));
	CHECK(F4T::DeathBag::RecoverArchived(bigArchiveRoot, bigArchiveId));
	CHECK(!bigArchiveRoot["deathBagArchive"].contains(bigArchiveId));
	CHECK(bigArchiveRoot["profiles"]["profile-a#0"]["deathBagInstances"].size() == 64 && bigArchiveRoot["deathBagGrantHistory"]["profile-a#0"].size() == 6);
	CHECK(bigArchiveRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 70u);
	bigArchiveRoot["deathBagArchive"][bigArchiveId] = bigEntry;
	CHECK(!F4T::DeathBag::RecoverArchived(bigArchiveRoot, bigArchiveId) && bigArchiveRoot["deathBagArchive"].contains(bigArchiveId));

	std::string holdId;
	auto holdRoot = placedRoot(holdId);
	CHECK(F4T::DeathBag::Hold(holdRoot, holdId) == F4T::DeathBag::HoldOutcome::Rejected);
	CHECK(holdRoot["deathBags"].contains(holdId) && !holdRoot.contains("deathBagArchive"));

	std::string holdArchivedId;
	auto holdArchivedRoot = makeRoot(holdArchivedId);
	CHECK(F4T::DeathBag::CreateEmpty(holdArchivedRoot, holdArchivedId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(holdArchivedRoot, holdArchivedId, stacks));
	CHECK(F4T::DeathBag::Hold(holdArchivedRoot, holdArchivedId) == F4T::DeathBag::HoldOutcome::Archived);
	CHECK(!holdArchivedRoot["deathBags"].contains(holdArchivedId) && holdArchivedRoot["deathBagArchive"][holdArchivedId]["reason"] == "placeFailed");

	std::string parkId;
	auto parkRoot = makeRoot(parkId);
	CHECK(F4T::DeathBag::CreateEmpty(parkRoot, parkId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(parkRoot, parkId, stacks));
	for (std::size_t i = 1; i <= F4T::DeathBag::MaximumArchivedBags; ++i) parkRoot["deathBagArchive"][paddedId(i)] = Json{ { "reason", "evicted" }, { "bag", sampleBag } };
	CHECK(F4T::DeathBag::Hold(parkRoot, parkId) == F4T::DeathBag::HoldOutcome::Parked);
	CHECK(parkRoot["deathBags"][parkId]["deposited"] == false && parkRoot["deathBags"][parkId]["placed"] == false);
	CHECK(!F4T::DeathBag::HasExpiryWork(parkRoot, 5000.0));
	CHECK(F4T::DeathBag::ExpirePending(parkRoot, 5000.0).empty());
	CHECK(parkRoot["deathBags"][parkId]["items"][item]["count"] == 2u);
	parkRoot["deathBags"][parkId]["createdAt"] = 2000.0;
	const auto parkedListing = F4T::DeathBag::ArchiveSummary(parkRoot, 0);
	CHECK(parkedListing.at("total") == 4097u && parkedListing.at("bags").at(0).at("deathId") == parkId && parkedListing.at("bags").at(0).at("reason") == "parked" && parkedListing.at("bags").at(0).at("stacks") == stacks);
	CHECK(F4T::DeathBag::RecoverArchived(parkRoot, parkId));
	CHECK(!parkRoot["deathBags"].contains(parkId));
	CHECK(parkRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + parkId + ":" + F4T::DeathBag::RecoveryOperation(parkId, 1, 0)]["count"] == 2u);
	CHECK(parkRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);
	CHECK(F4T::DeathBag::ArchiveSummary(parkRoot, 0).at("total") == 4096u);
	CHECK(!F4T::DeathBag::RecoverArchived(parkRoot, parkId));

	std::string parkBigId;
	auto parkBigRoot = placedRoot(parkBigId);
	Json parkBigStacks = Json::array();
	for (int i = 0; i < 70; ++i) parkBigStacks.push_back(Json{ { "itemKey", item }, { "count", 1u }, { "metadata", nullptr } });
	parkBigRoot["deathBags"][parkBigId]["stacks"] = parkBigStacks;
	parkBigRoot["deathBags"][parkBigId]["items"][item]["count"] = 70u;
	parkBigRoot["deathBags"][parkBigId]["placed"] = false;
	parkBigRoot["deathBags"][parkBigId]["deposited"] = false;
	CHECK(F4T::DeathBag::RecoverArchived(parkBigRoot, parkBigId));
	CHECK(parkBigRoot["deathBags"][parkBigId]["stacks"].size() == 6 && parkBigRoot["deathBags"][parkBigId]["items"][item]["count"] == 6u && parkBigRoot["deathBags"][parkBigId]["deposited"] == false);
	CHECK(!F4T::DeathBag::RecoverArchived(parkBigRoot, parkBigId));
	for (int i = 0; i < 6; ++i) CHECK(F4T::DeathBag::ClaimQueuedGrant(parkBigRoot, "profile-a#0", character, parkBigId, F4T::DeathBag::RecoveryOperation(parkBigId, 70, i)));
	for (int i = 0; i < 6; ++i) CHECK(F4T::DeathBag::MarkGrantAttempted(parkBigRoot, "profile-a#0", character, parkBigId, F4T::DeathBag::RecoveryOperation(parkBigId, 70, i), 1000.0));
	CHECK(F4T::DeathBag::RecoverArchived(parkBigRoot, parkBigId));
	CHECK(!parkBigRoot["deathBags"].contains(parkBigId));
	CHECK(parkBigRoot["profiles"]["profile-a#0"]["deathBagInstances"].size() == 64 && parkBigRoot["deathBagGrantHistory"]["profile-a#0"].size() == 6);
	CHECK(parkBigRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 70u);

	Json pageRoot = Json::object();
	for (std::size_t i = 1; i <= 52; ++i) {
		auto pageBag = sampleBag;
		pageBag["createdAt"] = static_cast<double>(i);
		pageRoot["deathBagArchive"][paddedId(i)] = Json{ { "reason", "placeFailed" }, { "bag", pageBag } };
	}
	const auto firstPage = F4T::DeathBag::ArchiveSummary(pageRoot, 0);
	CHECK(firstPage.at("total") == 52u && firstPage.at("bags").size() == 50 && firstPage.at("bags").at(0).at("deathId") == paddedId(52) && firstPage.at("bags").at(49).at("deathId") == paddedId(3));
	const auto secondPage = F4T::DeathBag::ArchiveSummary(pageRoot, 50);
	CHECK(secondPage.at("total") == 52u && secondPage.at("bags").size() == 2 && secondPage.at("bags").at(0).at("deathId") == paddedId(2) && secondPage.at("bags").at(1).at("deathId") == paddedId(1));
	CHECK(F4T::DeathBag::ArchiveSummary(pageRoot, 52).at("bags").empty());

	std::string pfId;
	auto pfRoot = makeRoot(pfId);
	CHECK(F4T::DeathBag::CreateEmpty(pfRoot, pfId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pfRoot, pfId, stacks));
	const Json pfOver = Json::array({ Json{ { "itemKey", item }, { "count", 3u }, { "metadata", nullptr } } });
	const Json pfWrongMeta = Json::array({ Json{ { "itemKey", item }, { "count", 2u }, { "metadata", *F4T::ItemHealthSnapshot::Encode(0.5f) } } });
	const Json pfUnknown = Json::array({ Json{ { "itemKey", "test.esm:00000009" }, { "count", 1u }, { "metadata", nullptr } } });
	const Json pfTrimmed = Json::array({ Json{ { "itemKey", item }, { "count", 1u }, { "metadata", nullptr } } });
	const auto pfBefore = pfRoot;
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", character, false, pfOver) == F4T::DeathBag::PlaceFailedOutcome::Rejected);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", character, false, pfWrongMeta) == F4T::DeathBag::PlaceFailedOutcome::Rejected);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", character, false, pfUnknown) == F4T::DeathBag::PlaceFailedOutcome::Rejected);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", character, true, stacks) == F4T::DeathBag::PlaceFailedOutcome::Rejected);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-b#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-b#0", character, false, pfOver) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", "quest-22222222222222222222222222222222", false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(pfRoot == pfBefore);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Archived);
	CHECK(!pfRoot["deathBags"].contains(pfId) && pfRoot["deathBagArchive"][pfId]["reason"] == "placeFailed");
	CHECK(pfRoot["deathBagArchive"][pfId]["bag"]["stacks"] == pfTrimmed && pfRoot["deathBagArchive"][pfId]["bag"]["items"][item]["count"] == 1u);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", character, false, stacks) == F4T::DeathBag::PlaceFailedOutcome::Rejected);
	CHECK(F4T::DeathBag::RecoverArchived(pfRoot, pfId));
	CHECK(!pfRoot["deathBagArchive"].contains(pfId));
	CHECK(pfRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + pfId + ":" + F4T::DeathBag::RecoveryOperation(pfId, 1, 0)]["count"] == 1u);
	CHECK(pfRoot["profiles"]["profile-a#0"]["deathBagInstances"].size() == 1);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfRoot, pfId, "profile-a#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);

	std::string pfUnkId;
	auto pfUnkRoot = makeRoot(pfUnkId);
	CHECK(F4T::DeathBag::CreateEmpty(pfUnkRoot, pfUnkId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pfUnkRoot, pfUnkId, stacks));
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfUnkRoot, pfUnkId, "profile-a#0", character, false, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Archived);
	CHECK(pfUnkRoot["deathBagArchive"][pfUnkId]["reason"] == "placeFailedUnverified" && pfUnkRoot["deathBagArchive"][pfUnkId]["bag"]["stacks"] == stacks);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfUnkRoot, pfUnkId, "profile-a#0", character, false, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfUnkRoot, pfUnkId, "profile-a#0", character, false, pfOver) == F4T::DeathBag::PlaceFailedOutcome::Rejected);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfUnkRoot, pfUnkId, "profile-b#0", character, true, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(pfUnkRoot["deathBagArchive"].contains(pfUnkId));
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfUnkRoot, pfUnkId, "profile-a#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Archived);
	CHECK(pfUnkRoot["deathBagArchive"][pfUnkId]["bag"]["stacks"] == pfTrimmed && pfUnkRoot["deathBagArchive"][pfUnkId]["bag"]["items"][item]["count"] == 1u);
	CHECK(pfUnkRoot["deathBagArchive"][pfUnkId]["reason"] == "placeFailed");
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfUnkRoot, pfUnkId, "profile-a#0", character, true, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Archived);
	CHECK(!pfUnkRoot["deathBagArchive"].contains(pfUnkId) && !pfUnkRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));

	std::string pfResolveId;
	auto pfResolveRoot = makeRoot(pfResolveId);
	CHECK(F4T::DeathBag::CreateEmpty(pfResolveRoot, pfResolveId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pfResolveRoot, pfResolveId, stacks));
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfResolveRoot, pfResolveId, "profile-a#0", character, false, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Archived);
	CHECK(F4T::DeathBag::Unverified(pfResolveRoot, pfResolveId) && F4T::DeathBag::ArchiveSummary(pfResolveRoot, 0).at("bags").at(0).at("reason") == "placeFailedUnverified");
	CHECK(!F4T::DeathBag::RecoverArchived(pfResolveRoot, pfResolveId) && pfResolveRoot["deathBagArchive"].contains(pfResolveId));
	auto pfResolveTrim = pfResolveRoot;
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfResolveTrim, pfResolveId, "profile-a#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Archived);
	CHECK(pfResolveTrim["deathBagArchive"][pfResolveId]["reason"] == "placeFailed" && !F4T::DeathBag::Unverified(pfResolveTrim, pfResolveId));
	auto pfResolveNone = pfResolveRoot;
	CHECK(F4T::DeathBag::ResolveUnverified(pfResolveNone, pfResolveId, F4T::DeathBag::Resolution::None) && !pfResolveNone["deathBagArchive"].contains(pfResolveId));
	CHECK(!F4T::DeathBag::ResolveUnverified(pfResolveNone, pfResolveId, F4T::DeathBag::Resolution::Full));
	CHECK(F4T::DeathBag::ResolveUnverified(pfResolveRoot, pfResolveId, F4T::DeathBag::Resolution::Full) && pfResolveRoot["deathBagArchive"][pfResolveId]["reason"] == "placeFailed");
	CHECK(!F4T::DeathBag::ResolveUnverified(pfResolveRoot, pfResolveId, F4T::DeathBag::Resolution::None) && pfResolveRoot["deathBagArchive"].contains(pfResolveId));
	CHECK(F4T::DeathBag::RecoverArchived(pfResolveRoot, pfResolveId) && !pfResolveRoot["deathBagArchive"].contains(pfResolveId));

	std::string pfParkId;
	auto pfParkRoot = makeRoot(pfParkId);
	CHECK(F4T::DeathBag::CreateEmpty(pfParkRoot, pfParkId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pfParkRoot, pfParkId, stacks));
	for (std::size_t i = 1; i <= F4T::DeathBag::MaximumArchivedBags; ++i) pfParkRoot["deathBagArchive"][paddedId(i)] = Json{ { "reason", "evicted" }, { "bag", sampleBag } };
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfParkRoot, pfParkId, "profile-a#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Parked);
	CHECK(pfParkRoot["deathBags"][pfParkId]["deposited"] == false && pfParkRoot["deathBags"][pfParkId]["items"][item]["count"] == 1u);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfParkRoot, pfParkId, "profile-a#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfParkRoot, pfParkId, "profile-a#0", character, true, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfParkRoot, pfParkId, "profile-a#0", character, false, stacks) == F4T::DeathBag::PlaceFailedOutcome::Rejected);
	CHECK(pfParkRoot["deathBags"].contains(pfParkId) && !pfParkRoot["deathBagArchive"].contains(pfParkId));
	CHECK(!pfParkRoot["deathBags"][pfParkId].contains("unverified"));

	std::string pfParkUnkId;
	auto pfParkUnkRoot = makeRoot(pfParkUnkId);
	CHECK(F4T::DeathBag::CreateEmpty(pfParkUnkRoot, pfParkUnkId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pfParkUnkRoot, pfParkUnkId, stacks));
	for (std::size_t i = 1; i <= F4T::DeathBag::MaximumArchivedBags; ++i) pfParkUnkRoot["deathBagArchive"][paddedId(i)] = Json{ { "reason", "evicted" }, { "bag", sampleBag } };
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfParkUnkRoot, pfParkUnkId, "profile-a#0", character, false, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Parked);
	CHECK(pfParkUnkRoot["deathBags"][pfParkUnkId]["unverified"] == true && F4T::DeathBag::IsParked(pfParkUnkRoot["deathBags"][pfParkUnkId]) && F4T::DeathBag::Unverified(pfParkUnkRoot, pfParkUnkId));
	pfParkUnkRoot["deathBags"][pfParkUnkId]["createdAt"] = 2000.0;
	CHECK(F4T::DeathBag::ArchiveSummary(pfParkUnkRoot, 0).at("bags").at(0).at("reason") == "parkedUnverified");
	CHECK(!F4T::DeathBag::RecoverArchived(pfParkUnkRoot, pfParkUnkId) && pfParkUnkRoot["deathBags"].contains(pfParkUnkId));
	auto pfParkUnkBad = pfParkUnkRoot;
	pfParkUnkBad["deathBags"][pfParkUnkId]["unverified"] = false;
	CHECK(!F4T::DeathBag::ResolveUnverified(pfParkUnkBad, pfParkUnkId, F4T::DeathBag::Resolution::Full));
	auto pfParkUnkNone = pfParkUnkRoot;
	CHECK(F4T::DeathBag::ResolveUnverified(pfParkUnkNone, pfParkUnkId, F4T::DeathBag::Resolution::None) && !pfParkUnkNone["deathBags"].contains(pfParkUnkId));
	CHECK(F4T::DeathBag::ResolveUnverified(pfParkUnkRoot, pfParkUnkId, F4T::DeathBag::Resolution::Full) && !pfParkUnkRoot["deathBags"][pfParkUnkId].contains("unverified"));
	CHECK(F4T::DeathBag::ArchiveSummary(pfParkUnkRoot, 0).at("bags").at(0).at("reason") == "parked");
	CHECK(F4T::DeathBag::RecoverArchived(pfParkUnkRoot, pfParkUnkId) && !pfParkUnkRoot["deathBags"].contains(pfParkUnkId));

	std::string pfDiscardId;
	auto pfDiscardRoot = makeRoot(pfDiscardId);
	CHECK(F4T::DeathBag::CreateEmpty(pfDiscardRoot, pfDiscardId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pfDiscardRoot, pfDiscardId, stacks));
	Json pfDiscarded;
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfDiscardRoot, pfDiscardId, "profile-a#0", character, true, Json::array(), &pfDiscarded) == F4T::DeathBag::PlaceFailedOutcome::Discarded);
	CHECK(!pfDiscardRoot["deathBags"].contains(pfDiscardId) && pfDiscarded == stacks);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfDiscardRoot, pfDiscardId, "profile-a#0", character, true, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(!pfDiscardRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));

	std::string pfPlacedId;
	auto pfPlacedRoot = placedRoot(pfPlacedId);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfPlacedRoot, pfPlacedId, "profile-a#0", character, false, pfTrimmed) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfPlacedRoot, pfPlacedId, "profile-a#0", character, false, pfOver) == F4T::DeathBag::PlaceFailedOutcome::Rejected);
	CHECK(F4T::DeathBag::ApplyPlaceFailed(pfPlacedRoot, pfPlacedId, "profile-a#0", character, true, Json::array()) == F4T::DeathBag::PlaceFailedOutcome::Unchanged);
	CHECK(pfPlacedRoot["deathBags"][pfPlacedId]["placed"] == true && pfPlacedRoot["deathBags"][pfPlacedId]["items"][item]["count"] == 2u);

	std::string pfMarkId;
	auto pfMarkRoot = makeRoot(pfMarkId);
	CHECK(F4T::DeathBag::CreateEmpty(pfMarkRoot, pfMarkId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pfMarkRoot, pfMarkId, stacks));
	CHECK(F4T::DeathBag::RequestPlacement(pfMarkRoot, pfMarkId, 1000.0));
	CHECK(pfMarkRoot["deathBags"][pfMarkId]["placeRequestedAt"] == 1000.0);
	CHECK(!F4T::DeathBag::Void(pfMarkRoot, pfMarkId));
	CHECK(!F4T::DeathBag::HasExpiryWork(pfMarkRoot, 1120.0));
	CHECK(F4T::DeathBag::ExpirePending(pfMarkRoot, 1120.0).empty());
	CHECK(!F4T::DeathBag::HasExpiryWork(pfMarkRoot, 1000.0 + F4T::DeathBag::PlaceResultBoundSeconds - 1.0));
	CHECK(F4T::DeathBag::ArchiveOverdueClaims(pfMarkRoot, 1000.0 + F4T::DeathBag::PlaceResultBoundSeconds - 1.0) == 0);
	CHECK(pfMarkRoot["deathBags"].contains(pfMarkId));
	CHECK(F4T::DeathBag::HasExpiryWork(pfMarkRoot, 1000.0 + F4T::DeathBag::PlaceResultBoundSeconds));
	CHECK(F4T::DeathBag::ExpirePending(pfMarkRoot, 1000.0 + F4T::DeathBag::PlaceResultBoundSeconds).empty());
	CHECK(F4T::DeathBag::ArchiveOverdueClaims(pfMarkRoot, 1000.0 + F4T::DeathBag::PlaceResultBoundSeconds) == 1);
	CHECK(!pfMarkRoot["deathBags"].contains(pfMarkId) && pfMarkRoot["deathBagArchive"][pfMarkId]["reason"] == "placeFailedUnverified");
	CHECK(!F4T::DeathBag::RecoverArchived(pfMarkRoot, pfMarkId));
	CHECK(!pfMarkRoot["deathBagArchive"][pfMarkId]["bag"].contains("placeRequestedAt") && !pfMarkRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));

	std::string pfPlaceId;
	auto pfPlaceRoot = makeRoot(pfPlaceId);
	CHECK(F4T::DeathBag::CreateEmpty(pfPlaceRoot, pfPlaceId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(pfPlaceRoot, pfPlaceId, stacks));
	CHECK(F4T::DeathBag::RequestPlacement(pfPlaceRoot, pfPlaceId, 1000.0));
	CHECK(F4T::DeathBag::Place(pfPlaceRoot, pfPlaceId, "profile-a#0", character));
	CHECK(pfPlaceRoot["deathBags"][pfPlaceId]["placed"] == true && !pfPlaceRoot["deathBags"][pfPlaceId].contains("placeRequestedAt"));

	std::string unplacedId;
	auto unplacedRoot = makeRoot(unplacedId);
	CHECK(F4T::DeathBag::CreateEmpty(unplacedRoot, unplacedId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(unplacedRoot, unplacedId, stacks));
	CHECK(F4T::DeathBag::RequestPlacement(unplacedRoot, unplacedId, 1000.0));
	CHECK(F4T::DeathBag::ReportUnplaced(unplacedRoot, unplacedId, "profile-a#0", character, "u0123456789abcde") == F4T::DeathBag::UnplacedOutcome::Refunded);
	CHECK(!unplacedRoot["deathBags"].contains(unplacedId) && unplacedRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);
	CHECK(F4T::DeathBag::ReportUnplaced(unplacedRoot, unplacedId, "profile-a#0", character, "u0123456789abcde") == F4T::DeathBag::UnplacedOutcome::Absent);
	CHECK(unplacedRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 2u);

	std::string unplacedPlacedId;
	auto unplacedPlacedRoot = makeRoot(unplacedPlacedId);
	CHECK(F4T::DeathBag::CreateEmpty(unplacedPlacedRoot, unplacedPlacedId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(unplacedPlacedRoot, unplacedPlacedId, stacks));
	CHECK(F4T::DeathBag::RequestPlacement(unplacedPlacedRoot, unplacedPlacedId, 1000.0));
	CHECK(F4T::DeathBag::Place(unplacedPlacedRoot, unplacedPlacedId, "profile-a#0", character));
	CHECK(F4T::DeathBag::ReportUnplaced(unplacedPlacedRoot, unplacedPlacedId, "profile-a#0", character, "u0123456789abcde") == F4T::DeathBag::UnplacedOutcome::Refused);
	CHECK(unplacedPlacedRoot["deathBags"][unplacedPlacedId]["placed"] == true && !unplacedPlacedRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));

	std::string unplacedNotRequestedId;
	auto unplacedNotRequestedRoot = makeRoot(unplacedNotRequestedId);
	CHECK(F4T::DeathBag::CreateEmpty(unplacedNotRequestedRoot, unplacedNotRequestedId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(unplacedNotRequestedRoot, unplacedNotRequestedId, stacks));
	CHECK(F4T::DeathBag::ReportUnplaced(unplacedNotRequestedRoot, unplacedNotRequestedId, "profile-a#0", character, "u0123456789abcde") == F4T::DeathBag::UnplacedOutcome::Refused);
	CHECK(unplacedNotRequestedRoot["deathBags"].contains(unplacedNotRequestedId) && !unplacedNotRequestedRoot["profiles"]["profile-a#0"]["inventory"]["items"].contains(item));
	CHECK(F4T::DeathBag::ReportUnplaced(unplacedNotRequestedRoot, unplacedNotRequestedId, "profile-a#0", "quest-22222222222222222222222222222222", "u0123456789abcde") == F4T::DeathBag::UnplacedOutcome::Refused);
	CHECK(F4T::DeathBag::ReportUnplaced(unplacedNotRequestedRoot, unplacedNotRequestedId, "profile-a#0", character, "") == F4T::DeathBag::UnplacedOutcome::Refused);
}
