#include <cstdio>
#include <cstdlib>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::fprintf(stderr, "CHECK %s:%d %s\n", __FILE__, __LINE__, #__VA_ARGS__); std::abort(); } } while (0)


#include "F4TCharacterSlots.h"
#include "F4TDeathBag.h"
#include "F4TDeathBagReceipts.h"
#include "StartingGrantFixture.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

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

	std::string refundId;
	auto refundRoot = makeRoot(refundId);
	const auto refundDeathId = std::string(32, 'c');
	CHECK(F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1", item, 1, nullptr));
	CHECK(refundRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + refundDeathId + ":ticket-1"]["claimRequired"] == true);
	CHECK(refundRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 3u);
	CHECK(F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1", item, 1, nullptr));
	CHECK(refundRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 3u);
	CHECK(!F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1", item, 2, nullptr));
	CHECK(!F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-2", item, 0, nullptr));
	CHECK(!F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#1", character, refundDeathId, "ticket-3", item, 1, nullptr));
	CHECK(!F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", "quest-22222222222222222222222222222222", refundDeathId, "ticket-4", item, 1, nullptr));
	CHECK(!F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-5", "bogus", 1, nullptr));
	CHECK(!F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, std::string(31, 'a'), "ticket-6", item, 1, nullptr));
	CHECK(F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-7", item, 2, nullptr));
	CHECK(refundRoot["profiles"]["profile-a#0"]["inventory"]["items"][item]["count"] == 5u);
	const auto refundGrants = F4T::DeathBag::ReconcileGrants(refundRoot, "profile-a#0", character);
	CHECK(refundGrants && refundGrants->empty());
	const auto refundCursorGrants = F4T::DeathBag::ReconcileGrants(refundRoot, "profile-a#0", character, std::string{});
	CHECK(refundCursorGrants && refundCursorGrants->size() == 2 && refundCursorGrants->at(0).at("claimRequired") == true);
	using F4T::DeathBagReceipts::GrantDisposition;
	using F4T::DeathBagReceipts::ReceiptStatus;
	const std::string legacySaveHash(64, 'a');
	const auto refundOperation = refundDeathId + ":ticket-1";
	std::map<std::string, std::set<std::string>> preparedSaveProofs;
	CHECK(F4T::DeathBagReceipts::DecideGrant(ReceiptStatus::Unknown, false, false) == GrantDisposition::Hold);
	CHECK(F4T::DeathBagReceipts::AddSaveProof(preparedSaveProofs[refundOperation], legacySaveHash));
	const auto recoveredProofs = Json::parse(Json(preparedSaveProofs).dump()).get<std::map<std::string, std::set<std::string>>>();
	CHECK(F4T::DeathBagReceipts::HasSaveProof(recoveredProofs, refundOperation, legacySaveHash));
	CHECK(!F4T::DeathBagReceipts::HasSaveProof(recoveredProofs, refundDeathId + ":other-ticket", legacySaveHash));
	CHECK(F4T::DeathBagReceipts::DecideGrant(ReceiptStatus::Unknown, false, F4T::DeathBagReceipts::HasSaveProof(recoveredProofs, refundOperation, legacySaveHash)) == GrantDisposition::Hold);
	CHECK(!F4T::DeathBag::MarkGrantAttempted(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1", 1999.0));
	auto conflictingMarker = refundRoot;
	conflictingMarker["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + refundDeathId + ":ticket-1"]["freshProof"] = true;
	bool conflictingMarkerRejected = false;
	try { F4T::DeathBag::ValidateRecipientInstances(conflictingMarker); } catch (const std::invalid_argument&) { conflictingMarkerRejected = true; }
	CHECK(conflictingMarkerRejected);
	auto attemptedMarker = refundRoot;
	attemptedMarker["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + refundDeathId + ":ticket-1"]["state"] = "attempted";
	attemptedMarker["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + refundDeathId + ":ticket-1"]["attemptedAt"] = 2000.0;
	bool attemptedMarkerRejected = false;
	try { F4T::DeathBag::ValidateRecipientInstances(attemptedMarker); } catch (const std::invalid_argument&) { attemptedMarkerRejected = true; }
	CHECK(attemptedMarkerRejected);
	CHECK(!F4T::DeathBag::ClaimQueuedGrant(refundRoot, "profile-a#1", character, refundDeathId, "ticket-1"));
	CHECK(F4T::DeathBag::ClaimQueuedGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1"));
	CHECK(F4T::DeathBagReceipts::DecideGrant(ReceiptStatus::Unknown,
		refundRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + refundDeathId + ":ticket-1"].value("freshProof", false),
		F4T::DeathBagReceipts::HasSaveProof(recoveredProofs, refundOperation, legacySaveHash)) == GrantDisposition::Apply);
	CHECK(F4T::DeathBagReceipts::DecideGrant(ReceiptStatus::Unknown, true, F4T::DeathBagReceipts::HasSaveProof(recoveredProofs, refundOperation, std::string(64, 'b'))) == GrantDisposition::Hold);
	CHECK(!recoveredProofs.contains(refundDeathId + ":other-ticket"));
	F4T::DeathBagReceipts::ReceiptSet savedRefund(false);
	CHECK(savedRefund.Insert(refundDeathId + ":ticket-1"));
	const auto reloadedRefund = F4T::DeathBagReceipts::ReceiptSet::Decode(savedRefund.Encode());
	CHECK(reloadedRefund && reloadedRefund->Contains(refundDeathId + ":ticket-1"));
	CHECK(F4T::DeathBagReceipts::DecideGrant(ReceiptStatus::Present, true, true) == GrantDisposition::Duplicate);
	CHECK(F4T::DeathBag::ClaimQueuedGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1"));
	CHECK(!refundRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + refundDeathId + ":ticket-1"].contains("claimRequired"));
	CHECK(refundRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + refundDeathId + ":ticket-1"]["claimManaged"] == true);
	const auto claimedLegacy = F4T::DeathBag::ReconcileGrants(refundRoot, "profile-a#0", character);
	CHECK(claimedLegacy && claimedLegacy->empty());
	const auto claimedCursor = F4T::DeathBag::ReconcileGrants(refundRoot, "profile-a#0", character, std::string{});
	CHECK(claimedCursor && claimedCursor->size() == 2 && !claimedCursor->at(0).contains("claimManaged"));
	std::string fullRefundId;
	auto fullRefundRoot = makeRoot(fullRefundId);
	const auto fullDeathId = std::string(32, 'd');
	CHECK(F4T::DeathBag::QueueRefundGrant(fullRefundRoot, "profile-a#0", character, fullDeathId, "retry", item, 1, nullptr));
	auto filler = fullRefundRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + fullDeathId + ":retry"];
	for (std::size_t i = 1; i < 4096; ++i) {
		const auto operation = "fill" + std::to_string(i);
		filler["operationId"] = operation;
		fullRefundRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + fullDeathId + ":" + operation] = filler;
	}
	CHECK(F4T::DeathBag::QueueRefundGrant(fullRefundRoot, "profile-a#0", character, fullDeathId, "retry", item, 1, nullptr));
	CHECK(F4T::DeathBag::MarkGrantAttempted(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1", 2000.0));
	const auto refundRemaining = F4T::DeathBag::ReconcileGrants(refundRoot, "profile-a#0", character);
	CHECK(refundRemaining && refundRemaining->empty());
	const auto attemptedCursor = F4T::DeathBag::ReconcileGrants(refundRoot, "profile-a#0", character, std::string{});
	CHECK(attemptedCursor && attemptedCursor->size() == 2 && attemptedCursor->at(0).at("state") == "attempted" &&
		!attemptedCursor->at(0).contains("claimManaged"));
	const auto refundKey = "bag:" + refundDeathId + ":ticket-1";
	CHECK(refundRoot.at("deathBagGrantHistory").at("profile-a#0").contains(refundKey));
	CHECK(!refundRoot.at("profiles").at("profile-a#0").at("deathBagInstances").contains(refundKey));
	CHECK(F4T::DeathBag::ClaimQueuedGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1"));
	CHECK(F4T::DeathBag::MarkGrantAttempted(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1", 2500.0));
	CHECK(refundRoot.at("deathBagGrantHistory").at("profile-a#0").at(refundKey).at("attemptedAt") == 2000.0);
	CHECK(F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1", item, 1, nullptr));
	CHECK(refundRoot.at("profiles").at("profile-a#0").at("inventory").at("items").at(item).at("count") == 5u);
	CHECK(!F4T::DeathBag::QueueRefundGrant(refundRoot, "profile-a#0", character, refundDeathId, "ticket-1", item, 2, nullptr));
	CHECK(!refundRoot.at("profiles").at("profile-a#0").at("deathBagInstances").contains(refundKey));

	std::string pageId;
	auto pageRoot = makeRoot(pageId);
	const auto pageOperation = [](std::size_t a_index) {
		char name[8];
		std::snprintf(name, sizeof(name), "g%03zu", a_index);
		return std::string(name);
	};
	for (std::size_t i = 0; i < 100; ++i) {
		Json record{ { "deathId", pageId }, { "operationId", pageOperation(i) }, { "characterId", character }, { "itemKey", item },
			{ "count", 1u }, { "metadata", nullptr }, { "state", "pending" } };
		const auto key = "bag:" + pageId + ":" + pageOperation(i);
		if (i % 3 == 0) {
			record["state"] = "attempted";
			record["attemptedAt"] = 1000.0 + static_cast<double>(i);
			pageRoot["deathBagGrantHistory"]["profile-a#0"][key] = std::move(record);
		} else {
			pageRoot["profiles"]["profile-a#0"]["deathBagInstances"][key] = std::move(record);
		}
	}
	const std::string otherCharacter = "quest-22222222222222222222222222222222";
	pageRoot["deathBagGrantHistory"]["profile-a#0"]["bag:" + pageId + ":g050x"] = Json{ { "deathId", pageId }, { "operationId", "g050x" },
		{ "characterId", otherCharacter }, { "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "attempted" }, { "attemptedAt", 1.0 } };
	F4T::DeathBag::ValidateRecipientInstances(pageRoot);
	std::vector<std::string> seen;
	std::optional<std::string> pageCursor = std::string{};
	std::size_t pages = 0;
	while (pageCursor) {
		std::optional<std::string> following;
		const auto page = F4T::DeathBag::ReconcileGrants(pageRoot, "profile-a#0", character, *pageCursor, &following);
		CHECK(page && page->size() <= F4T::DeathBag::MaximumPendingGrants);
		for (const auto& grant : *page) {
			CHECK(!grant.contains("attemptedAt") && grant.at("characterId") == character);
			const auto index = std::stoul(grant.at("operationId").get<std::string>().substr(1));
			CHECK(grant.at("state") == (index % 3 == 0 ? "attempted" : "pending"));
			seen.push_back(grant.at("operationId").get<std::string>());
		}
		if (following) CHECK(page->size() == F4T::DeathBag::MaximumPendingGrants && *following == "bag:" + pageId + ":" + seen.back());
		pageCursor = following;
		++pages;
	}
	CHECK(pages == 2 && seen.size() == 100 && std::is_sorted(seen.begin(), seen.end()) && std::adjacent_find(seen.begin(), seen.end()) == seen.end());
	CHECK(!F4T::DeathBag::ReconcileGrants(pageRoot, "profile-a#0", character, std::string(129, 'z')));
	std::optional<std::string> tailCursor = std::string{ "stale" };
	const auto tail = F4T::DeathBag::ReconcileGrants(pageRoot, "profile-a#0", character, "bag:" + pageId + ":" + pageOperation(98), &tailCursor);
	CHECK(tail && tail->size() == 1 && tail->at(0).at("operationId") == pageOperation(99) && !tailCursor);

	std::string bulkId;
	auto bulkRoot = makeRoot(bulkId);
	auto& bulkHistory = bulkRoot["deathBagGrantHistory"]["profile-a#0"];
	for (std::size_t i = 0; i < 4096; ++i) {
		const auto operation = "h" + std::to_string(i);
		bulkHistory["bag:" + bulkId + ":" + operation] = Json{ { "deathId", bulkId }, { "operationId", operation }, { "characterId", character },
			{ "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "attempted" }, { "attemptedAt", 1000.0 } };
	}
	bulkRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + bulkId + ":live"] = Json{ { "deathId", bulkId }, { "operationId", "live" },
		{ "characterId", character }, { "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "pending" } };
	CHECK(F4T::DeathBag::MarkGrantAttempted(bulkRoot, "profile-a#0", character, bulkId, "live", 2000.0));
	CHECK(bulkRoot.at("deathBagGrantHistory").at("profile-a#0").size() == 4097);
	CHECK(bulkRoot.at("profiles").at("profile-a#0").at("deathBagInstances").empty());
	CHECK(F4T::DeathBag::QueueRefundGrant(bulkRoot, "profile-a#0", character, bulkId, "after", item, 1, nullptr));
	CHECK(F4T::DeathBag::QueueRefundGrant(bulkRoot, "profile-a#0", character, bulkId, "h17", item, 1, nullptr));
	CHECK(!bulkRoot.at("profiles").at("profile-a#0").at("deathBagInstances").contains("bag:" + bulkId + ":h17"));
	CHECK(!F4T::DeathBag::ClaimQueuedGrant(bulkRoot, "profile-a#0", character, bulkId, "h17"));
	CHECK(F4T::DeathBag::ClaimQueuedGrant(bulkRoot, "profile-a#0", character, bulkId, "after"));
	CHECK(F4T::DeathBag::MarkGrantAttempted(bulkRoot, "profile-a#0", character, bulkId, "after", 2100.0));
	CHECK(bulkRoot.at("deathBagGrantHistory").at("profile-a#0").size() == 4098);
	const auto bulkReplay = Json::parse(bulkRoot.dump());
	F4T::DeathBag::ValidateRecipientInstances(bulkReplay);
	CHECK(bulkReplay == bulkRoot);
	auto duplicateLive = bulkRoot;
	duplicateLive["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + bulkId + ":h17"] = Json{ { "deathId", bulkId }, { "operationId", "h17" },
		{ "characterId", character }, { "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "pending" } };
	bool duplicateRejected = false;
	try { F4T::DeathBag::ValidateRecipientInstances(duplicateLive); } catch (const std::invalid_argument&) { duplicateRejected = true; }
	CHECK(duplicateRejected);
	std::string tieId;
	auto tieRoot = makeRoot(tieId);
	auto& tieHistory = tieRoot["deathBagGrantHistory"]["profile-a#0"];
	for (const auto* operation : { "t3", "t1", "t2" }) {
		tieHistory["bag:" + tieId + ":" + operation] = Json{ { "deathId", tieId }, { "operationId", operation }, { "characterId", character },
			{ "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "attempted" }, { "attemptedAt", 1000.0 } };
	}
	tieRoot["profiles"]["profile-a#0"]["deathBagInstances"]["bag:" + tieId + ":t0"] = Json{ { "deathId", tieId }, { "operationId", "t0" }, { "characterId", character },
		{ "itemKey", item }, { "count", 1u }, { "metadata", nullptr }, { "state", "attempted" }, { "attemptedAt", 1000.0 } };
	const auto tied = F4T::DeathBag::ReconcileGrants(tieRoot, "profile-a#0", character);
	CHECK(tied);
	std::vector<std::string> tieOrder;
	for (const auto& grant : *tied) {
		if (grant.at("deathId") == tieId) tieOrder.push_back(grant.at("operationId"));
	}
	CHECK(tieOrder == std::vector<std::string>({ "t0", "t1", "t2", "t3" }));
}
