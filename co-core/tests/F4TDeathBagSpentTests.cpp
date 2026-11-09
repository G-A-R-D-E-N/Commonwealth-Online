#include <cstdio>
#include <cstdlib>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "CHECK %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__); std::abort(); } } while (0)


#include "F4TCharacterSlots.h"
#include "F4TDeathBag.h"
#include "F4TDeathBagUnplaced.h"
#include "StartingGrantFixture.h"

#include <cassert>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

int main()
{
	using Json = nlohmann::json;
	const std::string character = "quest-11111111111111111111111111111111";
	const std::string item = "test.esm:00000002";
	const Json stacks = Json::array({ Json{ { "itemKey", item }, { "count", 2u }, { "metadata", nullptr } } });
	const auto recordFor = [&character](const Json& a_root) {
		return Json{ { "version", 1u }, { "profileId", "profile-a#0" }, { "characterId", character },
			{ "inventoryRevision", a_root.at("profiles").at("profile-a#0").at("inventory").at("revision") },
			{ "cellKey", "test.esm:00000001" }, { "worldspaceKey", nullptr }, { "position", Json::array({ 1.0, 2.0, 3.0 }) },
			{ "state", "dead" }, { "deadlineUnix", nullptr }, { "locationSource", "client-reported" } };
	};
	const auto makeRoot = [&](std::string& a_deathId) {
		auto root = F4T::DurableState::NewRoot("0123456789abcdef0123456789abcdef", 1000.0);
		F4T::Identity::TokenHash hash{};
		F4T::DurableState::SaveResumeCredential(root, "profile-a", hash, std::nullopt, true, 5000.0, 1000.0);
		CHECK(F4T::CharacterSlots::SetSlotRecord(root, "profile-a", 0, Json{ { "name", "Test" }, { "questProfile", character } }));
		Json catalog = Json::array({
			Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } },
			Json{ { "plugin", "test.esm" }, { "localFormId", "00000002" }, { "count", 2u } } });
		CHECK(F4T::DurableState::InitializeInventory(root, "profile-a#0", catalog, 1000.0));
		ReconcileStartingGrantFixture(root, "profile-a#0");
		const auto allocation = F4T::DeathIntent::Allocate(root, recordFor(root));
		CHECK(allocation.outcome == F4T::DeathIntent::Outcome::Created);
		a_deathId = allocation.deathId;
		return root;
	};
	const auto revisionOf = [](const Json& a_root) { return a_root.at("profiles").at("profile-a#0").at("inventory").at("revision").get<std::uint32_t>(); };
	const auto placedRoot = [&](std::string& a_deathId) {
		auto placed = makeRoot(a_deathId);
		CHECK(F4T::DeathBag::CreateEmpty(placed, a_deathId, 1000.0));
		CHECK(F4T::DeathBag::Deposit(placed, a_deathId, stacks));
		CHECK(F4T::DeathBag::Place(placed, a_deathId, "profile-a#0", character));
		return placed;
	};
	const auto spentRoot = [&](std::string& a_deathId) {
		auto spent = placedRoot(a_deathId);
		CHECK(F4T::DeathBag::Withdraw(spent, a_deathId, "profile-a#0", character, false, item, 2, revisionOf(spent), "last", 1500.0));
		return spent;
	};
	const auto rejects = [](const Json& a_root) {
		try {
			F4T::DeathIntent::Validate(a_root);
		} catch (const std::invalid_argument&) {
			return true;
		}
		return false;
	};

	std::string lastId;
	auto lastRoot = placedRoot(lastId);
	CHECK(F4T::DeathBag::Withdraw(lastRoot, lastId, "profile-a#0", character, false, item, 2, revisionOf(lastRoot), "last", 1500.0));
	CHECK(!lastRoot.at("deathBags").contains(lastId));
	CHECK(lastRoot.at("deathGenerations").at(character).at("bagSpent") == true);
	CHECK(lastRoot.at("deathGenerations").at(character).at("activeId") == lastId);
	CHECK(lastRoot.at("profiles").at("profile-a#0").at("inventory").at("items").at(item).at("count") == 2u);

	std::string partialId;
	auto partialRoot = placedRoot(partialId);
	CHECK(F4T::DeathBag::Withdraw(partialRoot, partialId, "profile-a#0", character, false, item, 1, revisionOf(partialRoot), "part", 1500.0));
	CHECK(partialRoot.at("deathBags").at(partialId).at("items").at(item).at("count") == 1u);
	CHECK(!partialRoot.at("deathGenerations").at(character).contains("bagSpent"));

	std::string replayId;
	auto replayRoot = placedRoot(replayId);
	const auto replayRevision = revisionOf(replayRoot);
	Json replayMetadata;
	CHECK(F4T::DeathBag::Withdraw(replayRoot, replayId, "profile-a#0", character, false, item, 2, replayRevision, "once", 1500.0, &replayMetadata));
	const auto replayed = replayRoot;
	Json replayAgain;
	CHECK(F4T::DeathBag::Withdraw(replayRoot, replayId, "profile-a#0", character, false, item, 2, replayRevision, "once", 1500.0, &replayAgain));
	CHECK(replayRoot == replayed && replayAgain == replayMetadata);
	CHECK(!F4T::DeathBag::Withdraw(replayRoot, replayId, "profile-a#0", character, false, item, 1, replayRevision, "once", 1500.0));

	std::string ackId;
	auto ackRoot = spentRoot(ackId);
	const auto beforeAck = ackRoot;
	CHECK(F4T::DeathBag::Place(ackRoot, ackId, "profile-a#0", character));
	CHECK(ackRoot == beforeAck);
	const auto ackVisible = F4T::DeathBag::Discovery(ackRoot, "profile-a#0", character, true);
	CHECK(ackVisible && ackVisible->empty());
	CHECK(!F4T::DeathBag::CreateEmpty(ackRoot, ackId, 1600.0));
	CHECK(ackRoot == beforeAck);

	const auto restarted = Json::parse(ackRoot.dump());
	F4T::DurableState::ValidateRoot(restarted);
	F4T::DeathBag::Validate(restarted);
	CHECK(restarted == ackRoot && restarted.at("deathGenerations").at(character).at("bagSpent") == true);
	auto nullActive = ackRoot;
	nullActive["deathGenerations"][character]["activeId"] = nullptr;
	CHECK(rejects(nullActive));

	auto newRoot = ackRoot;
	CHECK(F4T::DeathIntent::RetireUntransferred(newRoot, character, ackId));
	CHECK(!newRoot.at("deathGenerations").at(character).contains("bagSpent") && newRoot.at("deathGenerations").at(character).at("activeId").is_null());
	const auto fresh = F4T::DeathIntent::Allocate(newRoot, recordFor(newRoot));
	CHECK(fresh.outcome == F4T::DeathIntent::Outcome::Created && fresh.deathId != ackId && fresh.generation == 2u);
	const auto freshId = fresh.deathId;
	CHECK(F4T::DeathBag::CreateEmpty(newRoot, freshId, 1600.0));
	CHECK(F4T::DeathBag::Deposit(newRoot, freshId, stacks));
	CHECK(F4T::DeathBag::Place(newRoot, freshId, "profile-a#0", character));
	CHECK(F4T::DeathBag::Withdraw(newRoot, freshId, "profile-a#0", character, false, item, 2, revisionOf(newRoot), "next", 1700.0));
	CHECK(!newRoot.at("deathBags").contains(freshId) && newRoot.at("deathGenerations").at(character).at("bagSpent") == true);

	std::string legacyId;
	auto legacyRoot = placedRoot(legacyId);
	legacyRoot["deathBags"][legacyId]["items"] = Json::object();
	legacyRoot["deathBags"][legacyId]["stacks"] = Json::array();
	F4T::DeathBag::Validate(legacyRoot);
	CHECK(F4T::DeathBag::HasExpiryWork(legacyRoot, 1000.0));
	CHECK(F4T::DeathBag::SweepEmptied(legacyRoot) == 1);
	CHECK(!legacyRoot.at("deathBags").contains(legacyId) && legacyRoot.at("deathGenerations").at(character).at("bagSpent") == true);
	CHECK(!F4T::DeathBag::HasExpiryWork(legacyRoot, 1000.0));
	std::string e2eId;
	auto e2eRoot = makeRoot(e2eId);
	CHECK(F4T::DeathBag::CreateEmpty(e2eRoot, e2eId, 1000.0));
	CHECK(F4T::DeathBag::Deposit(e2eRoot, e2eId, stacks));
	CHECK(F4T::DeathBag::RequestPlacement(e2eRoot, e2eId, 1001.0));
	CHECK(F4T::DeathBag::Place(e2eRoot, e2eId, "profile-a#0", character));
	CHECK(e2eRoot.at("deathBags").at(e2eId).at("placed") == true && !e2eRoot.at("deathBags").at(e2eId).contains("placeRequestedAt"));
	CHECK(F4T::DeathBag::Withdraw(e2eRoot, e2eId, "profile-a#0", character, false, item, 2, revisionOf(e2eRoot), "e2e-last", 1500.0));
	CHECK(!e2eRoot.at("deathBags").contains(e2eId) && e2eRoot.at("deathGenerations").at(character).at("bagSpent") == true);
	CHECK(e2eRoot.at("profiles").at("profile-a#0").at("inventory").at("items").at(item).at("count") == 2u);
	const auto e2eSpent = e2eRoot;
	CHECK(F4T::DeathBag::Place(e2eRoot, e2eId, "profile-a#0", character));
	CHECK(e2eRoot == e2eSpent);
	CHECK(F4T::DeathBag::ReportUnplaced(e2eRoot, e2eId, "profile-a#0", character, "unplace-e2e") == F4T::DeathBag::UnplacedOutcome::Absent);
	CHECK(e2eRoot == e2eSpent);
	CHECK(F4T::DeathIntent::RetireUntransferred(e2eRoot, character, e2eId));
	const auto e2eNext = F4T::DeathIntent::Allocate(e2eRoot, recordFor(e2eRoot));
	CHECK(e2eNext.outcome == F4T::DeathIntent::Outcome::Created && e2eNext.deathId != e2eId && e2eNext.generation == 2u);
	CHECK(F4T::DeathBag::CreateEmpty(e2eRoot, e2eNext.deathId, 1600.0));
	CHECK(F4T::DeathBag::Deposit(e2eRoot, e2eNext.deathId, stacks));
	CHECK(F4T::DeathBag::Place(e2eRoot, e2eNext.deathId, "profile-a#0", character));
	CHECK(F4T::DeathBag::Withdraw(e2eRoot, e2eNext.deathId, "profile-a#0", character, false, item, 1, revisionOf(e2eRoot), "e2e-next", 1700.0));
	CHECK(e2eRoot.at("deathBags").at(e2eNext.deathId).at("items").at(item).at("count") == 1u);
	CHECK(F4T::DeathBag::ReportUnplaced(e2eRoot, e2eId, "profile-a#0", character, "unplace-e2e") == F4T::DeathBag::UnplacedOutcome::Absent);
	F4T::DeathBag::Validate(e2eRoot);

	CHECK(F4T::DeathIntent::ReclaimOrphans(replayRoot) == 0);
	CHECK(!F4T::DeathBag::Withdraw(replayRoot, replayId, "profile-a#0", character, false, item, 1, replayRevision, "once", 1500.0));

	std::string reclaimId;
	auto reclaimRoot = spentRoot(reclaimId);
	CHECK(F4T::DeathIntent::ReclaimOrphans(reclaimRoot) == 0);
	CHECK(reclaimRoot.at("deathGenerations").contains(character) && reclaimRoot.at("deathIntents").contains(reclaimId));
	CHECK(F4T::CharacterSlots::EraseSlotRecord(reclaimRoot, "profile-a", 0));
	CHECK(F4T::DeathIntent::ReclaimOrphans(reclaimRoot) == 1);
	CHECK(!reclaimRoot.at("deathGenerations").contains(character) && !reclaimRoot.at("deathIntents").contains(reclaimId));
	F4T::DeathIntent::Validate(reclaimRoot);

	std::string heldId;
	auto heldRoot = placedRoot(heldId);
	CHECK(F4T::CharacterSlots::EraseSlotRecord(heldRoot, "profile-a", 0));
	CHECK(F4T::DeathIntent::ReclaimOrphans(heldRoot) == 0);
	CHECK(heldRoot.at("deathBags").contains(heldId) && heldRoot.at("deathGenerations").contains(character) && heldRoot.at("deathIntents").contains(heldId));

	const auto orphanName = [](std::size_t a_index) {
		char text[33];
		std::snprintf(text, sizeof(text), "%032zx", a_index + 1);
		return std::string{ "quest-" } + text;
	};
	const std::string newcomer = "quest-22222222222222222222222222222222";
	const std::string newcomerTwo = "quest-44444444444444444444444444444444";
	const auto addNewcomer = [&](Json& a_root, const std::string& a_account, const std::string& a_quest) {
		F4T::Identity::TokenHash hash{};
		F4T::DurableState::SaveResumeCredential(a_root, a_account, hash, std::nullopt, true, 5000.0, 1000.0);
		CHECK(F4T::CharacterSlots::SetSlotRecord(a_root, a_account, 0, Json{ { "name", "B" }, { "questProfile", a_quest } }));
		const auto profile = a_account + "#0";
		CHECK(F4T::DurableState::InitializeInventory(a_root, profile, Json::array({ Json{ { "plugin", "fallout4.esm" }, { "localFormId", "00021B3B" }, { "count", 1u } } }), 1000.0));
		auto record = recordFor(a_root);
		record["profileId"] = profile;
		record["characterId"] = a_quest;
		record["inventoryRevision"] = a_root.at("profiles").at(profile).at("inventory").at("revision");
		return record;
	};
	const auto fillOrphans = [&](Json& a_root, std::size_t a_first, std::size_t a_count, bool a_withIntents) {
		const auto server = a_root.at("serverInstanceId").get<std::string>();
		auto base = recordFor(a_root);
		base["state"] = "downed";
		base["deadlineUnix"] = 5.0;
		for (std::size_t i = a_first; i < a_first + a_count; ++i) {
			const auto name = orphanName(i);
			if (!a_withIntents) {
				a_root["deathGenerations"][name] = Json{ { "number", 1u }, { "activeId", nullptr } };
				continue;
			}
			const auto id = F4T::DeathIntent::DerivedId(server, name, 1);
			base["characterId"] = name;
			a_root["deathIntents"][id] = base;
			a_root["deathGenerations"][name] = Json{ { "number", 1u }, { "activeId", id } };
		}
	};
	for (const bool withIntents : { false, true }) {
		std::string churnId;
		auto churnRoot = makeRoot(churnId);
		const auto newcomerRecord = addNewcomer(churnRoot, "profile-b", newcomer);
		std::set<std::string> issued{ churnId };
		if (!withIntents) CHECK(F4T::DeathIntent::RetireUntransferred(churnRoot, character, churnId));
		const auto existing = churnRoot.at("deathGenerations").size();
		fillOrphans(churnRoot, 0, F4T::DeathIntent::MaximumRecords - existing, withIntents);
		CHECK(churnRoot.at("deathGenerations").size() == F4T::DeathIntent::MaximumRecords);
		F4T::DeathIntent::Validate(churnRoot);
		CHECK(F4T::DeathIntent::AtCapacity(churnRoot));
		const auto beforeFull = churnRoot;
		auto blocked = churnRoot;
		blocked["characterSlots"]["profile-b"]["0"]["questProfile"] = "quest-33333333333333333333333333333333";
		CHECK(F4T::DeathIntent::Allocate(blocked, newcomerRecord).outcome == F4T::DeathIntent::Outcome::Rejected);
		const auto created = F4T::DeathIntent::Allocate(churnRoot, newcomerRecord);
		CHECK(created.outcome == F4T::DeathIntent::Outcome::Created && created.generation == 1u && issued.insert(created.deathId).second);
		CHECK(churnRoot.at("deathGenerations").size() == 2 && churnRoot.at("deathGenerations").contains(character) && churnRoot.at("deathGenerations").contains(newcomer));
		CHECK(!F4T::DeathIntent::AtCapacity(churnRoot) && churnRoot != beforeFull);
		F4T::DeathIntent::Validate(churnRoot);

		CHECK(F4T::CharacterSlots::EraseSlotRecord(churnRoot, "profile-b", 0));
		CHECK(F4T::DeathIntent::ReclaimOrphans(churnRoot) == 1);
		CHECK(!churnRoot.at("deathGenerations").contains(newcomer) && !churnRoot.at("deathIntents").contains(created.deathId));
		fillOrphans(churnRoot, F4T::DeathIntent::MaximumRecords, F4T::DeathIntent::MaximumRecords - 1, withIntents);
		CHECK(F4T::DeathIntent::AtCapacity(churnRoot));
		const auto secondRecord = addNewcomer(churnRoot, "profile-c", newcomerTwo);
		const auto again = F4T::DeathIntent::Allocate(churnRoot, secondRecord);
		CHECK(again.outcome == F4T::DeathIntent::Outcome::Created && issued.insert(again.deathId).second);
		CHECK(churnRoot.at("deathGenerations").size() == 2 && churnRoot.at("deathGenerations").contains(newcomerTwo));

		if (!withIntents) {
			const auto next =F4T::DeathIntent::Allocate(churnRoot, recordFor(churnRoot));
			CHECK(next.outcome == F4T::DeathIntent::Outcome::Created && next.generation == 2u && issued.insert(next.deathId).second);
			CHECK(!F4T::DeathIntent::RetireUntransferred(churnRoot, character, churnId));
		}
		const auto restarted = Json::parse(churnRoot.dump());
		F4T::DeathIntent::Validate(restarted);
		CHECK(restarted == churnRoot);
	}
}
