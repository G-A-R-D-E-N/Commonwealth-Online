#include "F4TParkedSuit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <format>
#include <string>
#include <vector>
// TODO-verify: concept-level wire and table checks, engine facts unverified; source:CodeNotes-GARDEN power-armor-sync.

namespace
{
	using Json = nlohmann::json;
	using Pick = F4T::ParkedSuit::Breach::Pick;

	bool Contains(const std::vector<Pick>& a_picks, const Pick& a_pick)
	{
		return std::any_of(a_picks.begin(), a_picks.end(), [&](const Pick& a_existing) {
			return a_existing.row == a_pick.row && a_existing.col == a_pick.col;
		});
	}

	std::vector<Pick> NextCandidates(const std::vector<Pick>& a_picks)
	{
		const auto index = a_picks.size();
		const auto line = index == 0 ? std::size_t{ 0 } : (index % 2 == 1 ? a_picks.back().col : a_picks.back().row);
		std::vector<Pick> candidates;
		for (std::size_t i = 0; i < F4T::ParkedSuit::Breach::kGridSize; ++i) {
			const auto pick = index % 2 == 0 ? Pick{ line, i } : Pick{ i, line };
			if (!Contains(a_picks, pick)) {
				candidates.push_back(pick);
			}
		}
		return candidates;
	}

	bool FindWinningPicks(const F4T::ParkedSuit::Breach::Grid& a_grid, std::size_t a_length, std::vector<Pick>& a_picks)
	{
		if (F4T::ParkedSuit::Breach::Solves(a_grid, a_length, a_picks)) {
			return true;
		}
		if (a_picks.size() > a_length) {
			return false;
		}
		for (const auto& candidate : NextCandidates(a_picks)) {
			a_picks.push_back(candidate);
			if (FindWinningPicks(a_grid, a_length, a_picks)) {
				return true;
			}
			a_picks.pop_back();
		}
		return false;
	}

	Json PicksJson(const std::vector<Pick>& a_picks)
	{
		Json picks = Json::array();
		for (const auto& pick : a_picks) {
			picks.push_back(Json::array({ pick.row, pick.col }));
		}
		return picks;
	}
}

int main()
{
	using namespace F4T::ParkedSuit;

	const std::string ownerA(64, 'a');
	const std::string ownerB(64, 'b');
	const std::string kLocal = "00001A2B";
	const std::string kSuit = "00000001";

	const auto suited = Json{
		{ "inFrame", true },
		{ "frameBaseFormId", "00005678" },
		{ "pieces", Json::array({ Json{ { "slot", "body" }, { "formId", "00001234" } } }) }
	};
	const auto where = Json{ { "cellId", "0000ABCD" }, { "worldspaceId", "" }, { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 } };
	const auto makeRecord = [&](const std::string& a_owner, const std::string& a_localId) {
		Record record;
		record.ownerKey = a_owner;
		record.localOwnerKey = a_owner;
		record.localId = a_localId;
		record.powerArmor = suited;
		record.position = *F4T::ObjectWire::ParsePosition(where);
		return record;
	};

	assert(NormalizeSuitId(Json("1a2b")) == std::optional<std::string>{ kLocal });
	assert(NormalizeSuitId(Json("0")) == std::nullopt);
	assert(NormalizeSuitId(Json("")) == std::nullopt);
	assert(NormalizeSuitId(Json("123456789")) == std::nullopt);
	assert(NormalizeSuitId(Json(1234)) == std::nullopt);

	Table table;
	assert(table.Park(makeRecord(ownerA, kLocal)) == ParkOutcome::Stored);
	assert(table.Park(makeRecord(ownerB, kLocal)) == ParkOutcome::Stored);
	assert(table.Entries().size() == 2);
	assert(table.Entries()[0].suitId != table.Entries()[1].suitId);
	assert(!table.Entries()[0].suitId.empty() && !table.Entries()[1].suitId.empty());
	const auto suitA = table.Entries()[0].suitId;
	const auto suitB = table.Entries()[1].suitId;
	assert(table.FindByLocal(ownerA, kLocal)->suitId == suitA);
	assert(table.FindByLocal(ownerB, kLocal)->suitId == suitB);
	assert(table.Find(suitA)->ownerKey == ownerA);

	assert(table.Park(makeRecord(ownerA, kLocal)) == ParkOutcome::Replaced);
	assert(table.Entries().size() == 2);
	assert(table.Find(suitA) != nullptr);

	assert(table.Reassign(suitA, ownerB) == HackOutcome::Hacked);
	assert(table.Find(suitA)->ownerKey == ownerB);
	assert(table.Find(suitA)->localOwnerKey == ownerA);
	assert(table.Reassign(suitA, ownerB) == HackOutcome::SameOwner);
	assert(table.Reassign("0000FFFF", ownerB) == HackOutcome::Missing);
	assert(!table.Remove(ownerA, kLocal).has_value());
	assert(table.Remove(ownerB, kLocal) == std::optional<std::string>{ suitB });
	assert(table.Find(suitA) != nullptr);
	assert(table.Park(makeRecord(ownerA, kLocal)) == ParkOutcome::NotOwner);
	assert(table.Find(suitA)->ownerKey == ownerB);
	assert(table.Find(suitA)->localOwnerKey == ownerA);
	assert(table.Entries().size() == 1);

	Table adopting;
	assert(adopting.Park(makeRecord(ownerA, kLocal)) == ParkOutcome::Stored);
	const auto adoptSuit = adopting.Entries()[0].suitId;
	assert(adopting.Reassign(adoptSuit, ownerB) == HackOutcome::Hacked);
	const std::string kAdoptLocal = "00001C3D";
	const std::string kOtherLocal = "00001C3E";
	std::string previousOwner;
	assert(adopting.Adopt(adoptSuit, ownerA, kAdoptLocal, &previousOwner) == AdoptOutcome::NotOwner);
	assert(adopting.Adopt("0000FFFF", ownerB, kAdoptLocal, &previousOwner) == AdoptOutcome::Missing);
	assert(adopting.Adopt(adoptSuit, ownerB, kAdoptLocal, &previousOwner) == AdoptOutcome::Adopted);
	assert(previousOwner == ownerA);
	assert(adopting.Find(adoptSuit)->localOwnerKey == ownerB && adopting.Find(adoptSuit)->localId == kAdoptLocal);
	assert(adopting.Adopt(adoptSuit, ownerB, kAdoptLocal, &previousOwner) == AdoptOutcome::Adopted);
	assert(previousOwner == ownerB);
	assert(adopting.Park(makeRecord(ownerB, kAdoptLocal)) == ParkOutcome::Replaced);
	assert(adopting.Entries().size() == 1 && adopting.Find(adoptSuit)->suitId == adoptSuit);
	assert(adopting.Park(makeRecord(ownerA, kLocal)) == ParkOutcome::Stored);
	assert(adopting.Entries().size() == 2);
	assert(adopting.FindByLocal(ownerA, kLocal)->suitId != adoptSuit);
	assert(adopting.Find(adoptSuit)->ownerKey == ownerB && adopting.Find(adoptSuit)->localOwnerKey == ownerB);
	assert(adopting.Park(makeRecord(ownerB, kOtherLocal)) == ParkOutcome::Stored);
	assert(adopting.Adopt(adoptSuit, ownerB, kOtherLocal, &previousOwner) == AdoptOutcome::Duplicate);
	assert(adopting.Remove(ownerB, kAdoptLocal) == std::optional<std::string>{ adoptSuit });
	assert(adopting.Entries().size() == 2);

	Table limited;
	for (std::size_t i = 0; i < kMaximumPerOwner; ++i) {
		assert(limited.Park(makeRecord(ownerA, std::format("{:08X}", 0x100 + i))) == ParkOutcome::Stored);
	}
	assert(limited.Park(makeRecord(ownerA, "00000999")) == ParkOutcome::OwnerLimit);
	assert(limited.Park(makeRecord(ownerB, "00000999")) == ParkOutcome::Stored);
	assert(limited.Reassign(limited.Entries().back().suitId, ownerA) == HackOutcome::OwnerLimit);

	const auto grid = Breach::Generate(123456789u, 4);
	assert(grid.target.size() == 4);
	assert(Breach::Generate(123456789u, 4).target == grid.target);
	assert(grid.cells[0] == (std::array<std::string, Breach::kGridSize>{ "1C", "E9", "1C", "55", "FF", "C3" }));
	assert(grid.target == (std::vector<std::string>{ "1C", "C3", "7A", "C3" }));
	std::vector<Pick> picks;
	assert(FindWinningPicks(grid, 4, picks));
	assert(Breach::Solves(grid, 4, picks));
	assert(picks.size() <= Breach::kMaximumPicks);

	auto afterSuccess = picks;
	afterSuccess.push_back(Pick{ 0, 0 });
	assert(!Breach::Solves(grid, 4, afterSuccess));

	auto repeated = picks;
	repeated.push_back(picks.front());
	assert(!Breach::Solves(grid, 4, repeated));

	auto offLine = picks;
	offLine[0].row = (offLine[0].row + 1) % Breach::kGridSize;
	assert(!Breach::Solves(grid, 4, offLine));

	std::vector<Pick> tooMany(Breach::kMaximumPicks + 1, Pick{ 0, 0 });
	assert(!Breach::Solves(grid, 4, tooMany));
	assert(!Breach::Solves(grid, 4, {}));

	const auto hack = NormalizeHackRequest(Json{ { "type", "parkedSuit" }, { "action", "hack" }, { "suitId", "1" }, { "picks", PicksJson(picks) } });
	assert(hack && hack->suitId == kSuit && hack->picks.size() == picks.size());
	assert(!NormalizeHackRequest(Json{ { "type", "parkedSuit" }, { "action", "hack" }, { "suitId", "1" }, { "picks", Json::array({ Json::array({ 6, 0 }) }) } }));
	assert(!NormalizeHackRequest(Json{ { "type", "parkedSuit" }, { "action", "hack" }, { "suitId", "1" } }));
	assert(NormalizeBreachStartRequest(Json{ { "type", "parkedSuit" }, { "action", "breachStart" }, { "suitId", "1" } }) == std::optional<std::string>{ kSuit });
	assert(NormalizeNonce(Json(7)) == std::optional<std::uint32_t>{ 7 });
	assert(NormalizeNonce(Json(0)) == std::optional<std::uint32_t>{ 0 } && NormalizeNonce(Json(4294967295LL)) == std::optional<std::uint32_t>{ 4294967295u });
	assert(!NormalizeNonce(Json(-1)) && !NormalizeNonce(Json("7")) && !NormalizeNonce(Json(4294967296ULL)) && !NormalizeNonce(Json(4294967296LL)));
	assert(NormalizeRemoveRequest(Json{ { "type", "parkedSuit" }, { "action", "remove" }, { "localId", "1a2b" }, { "nonce", 7 } }) == std::optional<std::string>{ kLocal });
	assert(!NormalizeRemoveRequest(Json{ { "type", "parkedSuit" }, { "action", "remove" }, { "localId", "1a2b" } }));
	assert(!NormalizeRemoveRequest(Json{ { "type", "parkedSuit" }, { "action", "remove" }, { "suitId", "1a2b" } }));
	const auto adopt = NormalizeAdoptRequest(Json{ { "type", "parkedSuit" }, { "action", "adopt" }, { "suitId", "1" }, { "localId", "1a2b" }, { "nonce", 7 } });
	assert(adopt && adopt->suitId == kSuit && adopt->localId == kLocal);
	assert(!NormalizeAdoptRequest(Json{ { "type", "parkedSuit" }, { "action", "adopt" }, { "suitId", "1" } }));
	assert(!NormalizeAdoptRequest(Json{ { "type", "parkedSuit" }, { "action", "adopt" }, { "suitId", "0" }, { "localId", "1a2b" }, { "nonce", 7 } }));
	assert(IsWriteRequest(Json{ { "type", "parkedSuit" }, { "action", "adopt" }, { "suitId", "1" }, { "localId", "1a2b" }, { "nonce", 7 } }));
	assert(!IsWriteRequest(Json{ { "type", "parkedSuit" }, { "action", "breachStart" }, { "suitId", "1" } }));

	const auto saved = [&](const std::string& a_suit, const std::string& a_local) {
		return Json{ { "ownerKey", ownerA }, { "suitId", a_suit }, { "localId", a_local }, { "localOwnerKey", ownerA }, { "powerArmor", suited }, { "position", where }, { "angleZ", 0.0 } };
	};
	const auto parkPacket = [&](const Json& a_pieceKeys) {
		Json packet{ { "type", "parkedSuit" }, { "action", "park" }, { "localId", kLocal }, { "powerArmor", suited }, { "position", where }, { "angleZ", 0.0 }, { "nonce", 7 } };
		if (!a_pieceKeys.is_null()) {
			packet["pieceKeys"] = a_pieceKeys;
		}
		return packet;
	};
	const std::string kItemA = "fallout4.esm:00001234";
	const std::string kItemB = "fallout4.esm:00001235";
	const auto unkeyed = NormalizeParkRequest(parkPacket(Json()));
	assert(unkeyed && unkeyed->pieceKeys.empty());
	const auto deduped = NormalizeParkRequest(parkPacket(Json::array({ kItemA, kItemA, kItemB })));
	assert(deduped && deduped->pieceKeys == (std::vector<std::string>{ kItemA, kItemB }));
	assert(!NormalizeParkRequest(parkPacket(Json::array({ "fallout4.esm:1234" }))));
	assert(!NormalizeParkRequest(parkPacket(Json::array({ 5 }))));
	assert(!NormalizeParkRequest(parkPacket(Json(kItemA))));
	std::vector<std::string> nineKeys;
	for (std::size_t index = 1; index <= kMaximumPieceKeys + 1; ++index) {
		nineKeys.push_back(std::format("fallout4.esm:{:08X}", index));
	}
	assert(!NormalizeParkRequest(parkPacket(Json(nineKeys))));
	nineKeys.pop_back();
	const auto eightKeys = NormalizeParkRequest(parkPacket(Json(nineKeys)));
	assert(eightKeys && eightKeys->pieceKeys.size() == kMaximumPieceKeys);
	assert(unkeyed && !unkeyed->console);
	auto consolePacket = parkPacket(Json());
	consolePacket["console"] = true;
	const auto consoleSpawn = NormalizeParkRequest(consolePacket);
	assert(consoleSpawn && consoleSpawn->console);
	consolePacket["console"] = "yes";
	assert(!NormalizeParkRequest(consolePacket));

	auto withKeys = saved("00000030", kLocal);
	withKeys["pieceKeys"] = Json::array({ kItemA });
	withKeys["hacked"] = true;
	Table keyed;
	keyed.LoadFromJson(Json::array({ withKeys }));
	const auto* keyedRecord = keyed.Find("00000030");
	assert(keyedRecord && keyedRecord->pieceKeys == (std::vector<std::string>{ kItemA }) && keyedRecord->hacked);
	Table keyedRestart;
	keyedRestart.LoadFromJson(keyed.ToJson());
	const auto* restartedKeyed = keyedRestart.Find("00000030");
	assert(restartedKeyed && restartedKeyed->pieceKeys == (std::vector<std::string>{ kItemA }) && restartedKeyed->hacked);

	Table reassigned;
	reassigned.LoadFromJson(Json::array({ saved("00000031", kLocal) }));
	assert(reassigned.Find("00000031") && !reassigned.Find("00000031")->hacked);
	assert(reassigned.Reassign("00000031", ownerB) == HackOutcome::Hacked);
	assert(reassigned.Find("00000031")->hacked);
	Table reassignedRestart;
	reassignedRestart.LoadFromJson(reassigned.ToJson());
	assert(reassignedRestart.Find("00000031") && reassignedRestart.Find("00000031")->hacked);

	Table legacy;
	legacy.LoadFromJson(Json::array({ saved("00000032", kLocal) }));
	assert(legacy.Find("00000032") && legacy.Find("00000032")->pieceKeys.empty() && !legacy.Find("00000032")->hacked);

	const Json heldBoth{ { "items", Json{ { kItemA, Json{ { "count", 1u } } }, { kItemB, Json{ { "count", 2u } } } } } };
	const Json heldA{ { "items", Json{ { kItemA, Json{ { "count", 1u } } } } } };
	const Json zeroB{ { "items", Json{ { kItemA, Json{ { "count", 1u } } }, { kItemB, Json{ { "count", 0u } } } } } };
	const Json stringB{ { "items", Json{ { kItemA, Json{ { "count", 1u } } }, { kItemB, Json{ { "count", "2" } } } } } };
	const std::vector<std::string> onlyB{ kItemB };
	const auto allHeld = EvaluatePieceAudit({ kItemA, kItemB }, heldBoth, 1.0, 15.0);
	assert(allHeld.allPresent && !allHeld.expired && allHeld.missing.empty());
	assert(EvaluatePieceAudit({ kItemA, kItemB }, heldBoth, 99.0, 15.0).allPresent);
	const auto earlyMiss = EvaluatePieceAudit({ kItemA, kItemB }, heldA, 14.9, 15.0);
	assert(!earlyMiss.allPresent && !earlyMiss.expired && earlyMiss.missing == onlyB);
	const auto lateMiss = EvaluatePieceAudit({ kItemA, kItemB }, heldA, 15.0, 15.0);
	assert(!lateMiss.allPresent && lateMiss.expired && lateMiss.missing == onlyB);
	const auto zeroCount = EvaluatePieceAudit({ kItemA, kItemB }, zeroB, 15.0, 15.0);
	assert(zeroCount.expired && zeroCount.missing == onlyB);
	const auto textCount = EvaluatePieceAudit({ kItemA, kItemB }, stringB, 15.0, 15.0);
	assert(textCount.expired && textCount.missing == onlyB);
	const auto noItems = EvaluatePieceAudit({ kItemA }, Json::object(), 1.0, 15.0);
	assert(!noItems.allPresent && !noItems.expired && noItems.missing == std::vector<std::string>{ kItemA });
	assert(EvaluatePieceAudit({ kItemA }, Json::object(), 15.0, 15.0).expired);
	const auto noKeys = EvaluatePieceAudit({}, Json::object(), 15.0, 15.0);
	assert(noKeys.allPresent && !noKeys.expired && noKeys.missing.empty());

	Table loaded;
	loaded.LoadFromJson(Json::array({ saved("00000007", "00000101"), saved("00000007", "00000102"), saved("00000008", "00000101") }));
	assert(loaded.Entries().size() == 1);
	assert(loaded.Find("00000007") != nullptr && loaded.Find("00000007")->localId == "00000101");
	assert(loaded.Park(makeRecord(ownerB, "00000103")) == ParkOutcome::Stored);
	assert(loaded.Find("00000009") != nullptr);
	Table roundTrip;
	roundTrip.LoadFromJson(loaded.ToJson());
	assert(roundTrip.Entries().size() == loaded.Entries().size());

	Table handoff;
	handoff.LoadFromJson(Json::array({ saved("00000020", kLocal) }));
	assert(handoff.Reassign("00000020", ownerB) == HackOutcome::Hacked);
	assert(handoff.Adopt("00000020", ownerB, kAdoptLocal, &previousOwner) == AdoptOutcome::Adopted);
	assert(previousOwner == ownerA && handoff.FindByLocal(ownerA, kLocal) == nullptr);
	Table restarted;
	restarted.LoadFromJson(handoff.ToJson());
	const auto* restored = restarted.Find("00000020");
	assert(restored && restored->ownerKey == ownerB && restored->localOwnerKey == ownerB && restored->localId == kAdoptLocal);
	assert(restarted.FindByLocal(ownerA, kLocal) == nullptr);
	assert(restarted.Park(makeRecord(ownerA, kLocal)) == ParkOutcome::Stored);
	assert(restarted.Entries().size() == 2 && restarted.Find("00000020")->localId == kAdoptLocal);

	Table offline;
	offline.LoadFromJson(handoff.ToJson());
	assert(offline.ToJson().at("releases").size() == 1);
	const auto owed = offline.TakeReleases(ownerA);
	assert(owed.size() == 1 && owed[0].suitId == "00000020" && owed[0].localId == kLocal);
	assert(offline.TakeReleases(ownerA).empty());
	Table delivered;
	delivered.LoadFromJson(offline.ToJson());
	assert(delivered.TakeReleases(ownerA).empty() && delivered.Entries().size() == 1);

	Table cleared;
	cleared.LoadFromJson(handoff.ToJson());
	assert(!cleared.ClearRelease(ownerB, kLocal));
	assert(cleared.ClearRelease(ownerA, kLocal));
	assert(!cleared.ClearRelease(ownerA, kLocal) && cleared.TakeReleases(ownerA).empty());

	const auto ack = BuildAckPacket(Json{ { "action", "park" }, { "localId", kLocal }, { "nonce", 7 } }, false);
	assert(ack.at("op") == "park" && ack.at("ok") == false && ack.at("localId") == kLocal && ack.at("nonce") == 7);
	assert(!ack.contains("reason"));
	const auto reasoned = BuildAckPacket(Json{ { "action", "park" }, { "localId", kLocal }, { "nonce", 7 } }, false, "outOfReach");
	assert(reasoned.at("ok") == false && reasoned.at("reason") == "outOfReach");
	const auto adoptAck = BuildAckPacket(Json{ { "action", "adopt" }, { "localId", kLocal }, { "nonce", 8 } }, true);
	assert(adoptAck.at("op") == "adopt" && adoptAck.at("ok") == true && adoptAck.at("localId") == kLocal && adoptAck.at("nonce") == 8);
	const auto release = BuildReleasePacket(kSuit, kLocal);
	assert(release.at("type") == "parkedSuit" && release.at("action") == "release" && release.at("suitId") == kSuit && release.at("localId") == kLocal && release.size() == 4);
	assert(BuildBreachChallengePacket(kSuit, 123456789u, 4).at("length") == 4);

	Table pending;
	const auto release20 = Json{ { "playerKey", ownerA }, { "suitId", "00000020" }, { "localId", kLocal } };
	pending.LoadFromJson(Json{ { "records", Json::array() }, { "releases", Json::array({ release20, release20 }) } });
	assert(pending.ToJson().at("releases").size() == 1);
	Json flood = Json::array();
	for (std::size_t index = 1; index <= kMaximumPendingReleases + 1; ++index) {
		flood.push_back(Json{ { "playerKey", ownerA }, { "suitId", "00000020" }, { "localId", std::format("{:08X}", index) } });
	}
	Table capped;
	capped.LoadFromJson(Json{ { "records", Json::array() }, { "releases", flood } });
	const auto kept = capped.ToJson().at("releases");
	assert(kept.size() == kMaximumPendingReleases);
	assert(kept.front().at("localId") == std::format("{:08X}", 2));
	assert(kept.back().at("localId") == std::format("{:08X}", kMaximumPendingReleases + 1));

	const double kDay = 24.0 * 60.0 * 60.0;
	const double kRetention = 14.0 * kDay;
	const auto ownerKey = [](std::size_t a_index) { return std::format("{:064x}", a_index); };
	const std::set<std::string> onlineA{ ownerA };
	const std::set<std::string> nobody{};

	Table aging;
	assert(aging.Park(makeRecord(ownerA, "00000A01")) == ParkOutcome::Stored);
	assert(aging.Park(makeRecord(ownerB, "00000A02")) == ParkOutcome::Stored);
	const auto agingA = aging.FindByLocal(ownerA, "00000A01")->suitId;
	const auto agingB = aging.FindByLocal(ownerB, "00000A02")->suitId;
	assert(aging.SweepOffline(1000.0, kRetention, onlineA).empty());
	assert(aging.Find(agingA)->ownerLastSeenUnix == 1000.0 && aging.Find(agingB)->ownerLastSeenUnix == 1000.0);
	auto expired = aging.SweepOffline(1000.0 + kRetention + 1.0, kRetention, onlineA);
	assert(expired.size() == 1 && expired[0].suitId == agingB && aging.Find(agingB) == nullptr);
	assert(aging.Find(agingA) != nullptr && aging.Find(agingA)->ownerLastSeenUnix == 1000.0 + kRetention + 1.0);
	assert(aging.SweepOffline(1000.0 + kRetention + 1.0 + kDay, kRetention, nobody).empty());
	assert(aging.Find(agingA) != nullptr);
	expired = aging.SweepOffline(1000.0 + 2.0 * kRetention + 2.0, kRetention, nobody);
	assert(expired.size() == 1 && expired[0].suitId == agingA && aging.Entries().empty());

	Table stamped;
	assert(stamped.Park(makeRecord(ownerA, "00000B01")) == ParkOutcome::Stored);
	assert(stamped.Park(makeRecord(ownerB, "00000B02")) == ParkOutcome::Stored);
	assert(stamped.SweepOffline(1000.0, kRetention, onlineA).empty());
	assert(stamped.SweepOffline(2000.0, kRetention, onlineA).empty());
	Table stampedRestart;
	stampedRestart.LoadFromJson(stamped.ToJson());
	assert(stampedRestart.Entries().size() == 2);
	const auto* stampedA = stampedRestart.FindByLocal(ownerA, "00000B01");
	const auto* stampedB = stampedRestart.FindByLocal(ownerB, "00000B02");
	assert(stampedA && stampedB && stampedA->ownerLastSeenUnix == 2000.0 && stampedB->ownerLastSeenUnix == 1000.0);
	assert(stampedB->powerArmor == suited && stampedB->position.cellId == "0000ABCD" && stampedB->position.z == 3.0);

	Table released;
	assert(released.Park(makeRecord(ownerA, kLocal)) == ParkOutcome::Stored);
	const auto releasedSuit = released.Entries()[0].suitId;
	assert(released.Reassign(releasedSuit, ownerB) == HackOutcome::Hacked);
	std::string releasedPrevious;
	assert(released.Adopt(releasedSuit, ownerB, kAdoptLocal, &releasedPrevious) == AdoptOutcome::Adopted && releasedPrevious == ownerA);
	assert(released.ToJson().at("releases").size() == 1);
	assert(released.SweepOffline(1000.0, kRetention, nobody).empty());
	assert(released.SweepOffline(1000.0 + kRetention + 1.0, kRetention, nobody).size() == 1);
	assert(released.ToJson().at("releases").empty() && released.Entries().empty());

	Table full;
	for (std::size_t owner = 1; owner <= kMaximumTotal / kMaximumPerOwner; ++owner) {
		for (std::size_t slot = 0; slot < kMaximumPerOwner; ++slot) {
			assert(full.Park(makeRecord(ownerKey(owner), std::format("{:08X}", 0x200 + slot))) == ParkOutcome::Stored);
		}
	}
	assert(full.Entries().size() == kMaximumTotal);
	const auto onlineKey = ownerKey(1000);
	assert(full.Park(makeRecord(onlineKey, kLocal)) == ParkOutcome::TableFull);
	assert(full.SweepOffline(1000.0, kRetention, nobody).empty());
	assert(full.SweepOffline(2000.0, kRetention, std::set<std::string>{ ownerKey(1) }).empty());
	const std::set<std::string> onlineNewcomer{ onlineKey };
	const auto oldest = full.EvictOldestOffline(onlineNewcomer);
	assert(oldest && oldest->ownerKey == ownerKey(2) && oldest->localId == "00000200" && oldest->ownerLastSeenUnix == 1000.0);
	assert(full.Entries().size() == kMaximumTotal - 1);
	assert(full.Park(makeRecord(onlineKey, kLocal)) == ParkOutcome::Stored);
	assert(full.Entries().size() == kMaximumTotal);
	std::set<std::string> everyone{ onlineKey };
	for (std::size_t owner = 1; owner <= kMaximumTotal / kMaximumPerOwner; ++owner) {
		everyone.insert(ownerKey(owner));
	}
	assert(!full.EvictOldestOffline(everyone).has_value());
	Table reloaded;
	reloaded.LoadFromJson(full.ToJson());
	assert(reloaded.Entries().size() == kMaximumTotal && reloaded.Entries().front().ownerLastSeenUnix == full.Entries().front().ownerLastSeenUnix);

	return 0;
}
