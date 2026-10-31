#include "F4TQuestWire.h"

#include <cassert>
#include <string>

// TODO-verify: wire field names and quest key shape follow the #343 design brief; see E:/GardenEngine/re-requests/2026-10-18-quest-sync-runtimes.md

namespace
{
	using F4T::QuestWire::Json;

	const std::string kQuest = "00ABCD:commonwealthonline.esp";
	const std::string kOther = "001234:fallout4.esm";

	Json StagePacket()
	{
		return Json{
			{ "type", "questStage" },
			{ "quest", kQuest },
			{ "stage", 20 },
			{ "fromPlayerId", 7 },
		};
	}

	Json SyncPacket()
	{
		return Json{
			{ "type", "questSync" },
			{ "stages", Json::array({ Json{ { "quest", kQuest }, { "stage", 30 } }, Json{ { "quest", kQuest }, { "stage", 10 } } }) },
			{ "completed", Json::array({ kOther }) },
		};
	}

	Json NotReachablePacket()
	{
		return Json{
			{ "type", "questNotReachable" },
			{ "reason", "prerequisite_missing" },
			{ "quest", kQuest },
			{ "missing", kOther },
			{ "previousLabel", "Out of Time" },
		};
	}

	Json PolicyPacket()
	{
		return Json{
			{ "type", "questPolicy" },
			{ "mode", "faction-war" },
			{ "version", 3 },
			{ "soloQuests", Json::array({ kOther }) },
			{ "partyQuests", Json::array({ kQuest }) },
			{ "gates", Json::array({ Json{ { "quest", kQuest }, { "stage", 40 } } }) },
		};
	}

	void TestReportEncoding()
	{
		const auto packet = F4T::QuestWire::EncodeReport({ kQuest, 42, false });
		assert(packet.at("type").get<std::string>() == "questReport");
		assert(packet.at("quest").get<std::string>() == kQuest);
		assert(packet.at("stage").get<std::uint32_t>() == 42);
		assert(packet.at("completed").get<bool>() == false);
		assert(packet.size() == 4);

		const auto completed = F4T::QuestWire::EncodeReport({ kQuest, 42, true });
		assert(completed.at("completed").get<bool>() == true);
	}

	void TestScopeEncoding()
	{
		const auto solo = F4T::QuestWire::EncodeScope({ kQuest, F4T::QuestLogic::Scope::Solo });
		assert(solo.at("type").get<std::string>() == "questScope");
		assert(solo.at("quest").get<std::string>() == kQuest);
		assert(solo.at("scope").get<std::string>() == "solo");
		assert(solo.size() == 3);

		const auto party = F4T::QuestWire::EncodeScope({ kQuest, F4T::QuestLogic::Scope::Party });
		assert(party.at("scope").get<std::string>() == "party");
		assert(party.size() == 3);

		const auto shared = F4T::QuestWire::EncodeScope({ kQuest, F4T::QuestLogic::Scope::Shared });
		assert(shared.at("scope").get<std::string>() == "shared");
	}

	void TestStageDecode()
	{
		const auto decoded = F4T::QuestWire::DecodeStage(StagePacket());
		assert(decoded && decoded->formKey == kQuest && decoded->stage == 20 && decoded->fromPlayerId == 7);
		assert(!decoded->completed);

		auto completed = StagePacket();
		completed["completed"] = true;
		const auto withCompletion = F4T::QuestWire::DecodeStage(completed);
		assert(withCompletion && withCompletion->completed && withCompletion->stage == 20);

		auto lowercase = StagePacket();
		lowercase["quest"] = "00abcd:CommonwealthOnline.esp";
		const auto canonical = F4T::QuestWire::DecodeStage(lowercase);
		assert(canonical && canonical->formKey == kQuest);
	}

	void TestStageDecodeRejects()
	{
		assert(!F4T::QuestWire::DecodeStage(SyncPacket()));
		assert(!F4T::QuestWire::DecodeStage(Json::array()));

		auto missingQuest = StagePacket();
		missingQuest.erase("quest");
		assert(!F4T::QuestWire::DecodeStage(missingQuest));

		auto missingPlayer = StagePacket();
		missingPlayer.erase("fromPlayerId");
		assert(!F4T::QuestWire::DecodeStage(missingPlayer));

		auto badKey = StagePacket();
		badKey["quest"] = "ABCDE:CommonwealthOnline.esp";
		assert(!F4T::QuestWire::DecodeStage(badKey));

		auto badStage = StagePacket();
		badStage["stage"] = "20";
		assert(!F4T::QuestWire::DecodeStage(badStage));

		auto negative = StagePacket();
		negative["stage"] = -1;
		assert(!F4T::QuestWire::DecodeStage(negative));

		auto tooBig = StagePacket();
		tooBig["stage"] = 70000;
		assert(!F4T::QuestWire::DecodeStage(tooBig));

		auto badCompleted = StagePacket();
		badCompleted["completed"] = "yes";
		assert(!F4T::QuestWire::DecodeStage(badCompleted));
	}

	void TestSyncDecode()
	{
		const auto decoded = F4T::QuestWire::DecodeSync(SyncPacket());
		assert(decoded && decoded->stages.size() == 2);
		assert(decoded->stages[0].quest == kQuest && decoded->stages[0].stage == 30);
		assert(decoded->stages[1].quest == kQuest && decoded->stages[1].stage == 10);
		assert(decoded->completed.size() == 1 && decoded->completed.front() == kOther);

		auto empty = SyncPacket();
		empty.erase("completed");
		const auto withoutCompleted = F4T::QuestWire::DecodeSync(empty);
		assert(withoutCompleted && withoutCompleted->stages.size() == 2 && withoutCompleted->completed.empty());

		auto emptyStages = SyncPacket();
		emptyStages["stages"] = Json::array();
		const auto noStages = F4T::QuestWire::DecodeSync(emptyStages);
		assert(noStages && noStages->stages.empty());
	}

	void TestSyncDecodeRejects()
	{
		assert(!F4T::QuestWire::DecodeSync(StagePacket()));

		auto missingStages = SyncPacket();
		missingStages.erase("stages");
		assert(!F4T::QuestWire::DecodeSync(missingStages));

		auto badEntry = SyncPacket();
		badEntry["stages"] = Json::array({ Json{ { "quest", kQuest } } });
		assert(!F4T::QuestWire::DecodeSync(badEntry));

		auto badCompleted = SyncPacket();
		badCompleted["completed"] = Json::array({ "not-a-key" });
		assert(!F4T::QuestWire::DecodeSync(badCompleted));

		auto tooMany = SyncPacket();
		tooMany["stages"] = Json::array();
		for (std::size_t index = 0; index <= F4T::QuestWire::kMaximumStages; ++index) {
			tooMany["stages"].push_back(Json{ { "quest", kQuest }, { "stage", 10 } });
		}
		assert(!F4T::QuestWire::DecodeSync(tooMany));

		auto tooManyCompleted = SyncPacket();
		tooManyCompleted["completed"] = Json::array();
		for (std::size_t index = 0; index <= F4T::QuestWire::kMaximumCompleted; ++index) {
			tooManyCompleted["completed"].push_back(kQuest);
		}
		assert(!F4T::QuestWire::DecodeSync(tooManyCompleted));
	}

	void TestNotReachableDecode()
	{
		const auto decoded = F4T::QuestWire::DecodeNotReachable(NotReachablePacket());
		assert(decoded && decoded->reason == "prerequisite_missing");
		assert(decoded->quest == kQuest);
		assert(decoded->missing == kOther);
		assert(decoded->previousLabel == "Out of Time");

		auto bare = NotReachablePacket();
		bare.erase("quest");
		bare.erase("missing");
		bare.erase("previousLabel");
		const auto minimal = F4T::QuestWire::DecodeNotReachable(bare);
		assert(minimal && minimal->reason == "prerequisite_missing");
		assert(minimal->quest.empty() && minimal->missing.empty() && minimal->previousLabel.empty());

		auto lowercase = NotReachablePacket();
		lowercase["quest"] = "00abcd:CommonwealthOnline.esp";
		lowercase["missing"] = "001234:Fallout4.esm";
		const auto canonical = F4T::QuestWire::DecodeNotReachable(lowercase);
		assert(canonical && canonical->quest == kQuest && canonical->missing == kOther);
	}

	void TestNotReachableDecodeRejects()
	{
		assert(!F4T::QuestWire::DecodeNotReachable(StagePacket()));
		assert(!F4T::QuestWire::DecodeNotReachable(Json::array()));

		auto missingReason = NotReachablePacket();
		missingReason.erase("reason");
		assert(!F4T::QuestWire::DecodeNotReachable(missingReason));

		auto emptyReason = NotReachablePacket();
		emptyReason["reason"] = "";
		assert(!F4T::QuestWire::DecodeNotReachable(emptyReason));

		auto badQuest = NotReachablePacket();
		badQuest["quest"] = "not-a-key";
		assert(!F4T::QuestWire::DecodeNotReachable(badQuest));

		auto badMissing = NotReachablePacket();
		badMissing["missing"] = 42;
		assert(!F4T::QuestWire::DecodeNotReachable(badMissing));

		auto emptyLabel = NotReachablePacket();
		emptyLabel["previousLabel"] = "";
		assert(!F4T::QuestWire::DecodeNotReachable(emptyLabel));

		auto longLabel = NotReachablePacket();
		longLabel["previousLabel"] = std::string(F4T::QuestWire::kMaximumLabelBytes + 1, 'x');
		assert(!F4T::QuestWire::DecodeNotReachable(longLabel));
	}

	void TestPolicyDecode()
	{
		const auto decoded = F4T::QuestWire::DecodePolicy(PolicyPacket());
		assert(decoded && decoded->mode == "faction-war" && decoded->version == 3);
		assert(decoded->soloQuests.size() == 1 && decoded->soloQuests.front() == kOther);
		assert(decoded->gates.size() == 1 && decoded->gates.front().quest == kQuest && decoded->gates.front().stage == 40);
		assert(decoded->partyQuests.size() == 1 && decoded->partyQuests.front() == kQuest);

		auto bare = PolicyPacket();
		bare.erase("soloQuests");
		bare.erase("partyQuests");
		bare.erase("gates");
		const auto minimal = F4T::QuestWire::DecodePolicy(bare);
		assert(minimal && minimal->soloQuests.empty() && minimal->partyQuests.empty() && minimal->gates.empty());
	}

	void TestPolicyDecodeRejects()
	{
		assert(!F4T::QuestWire::DecodePolicy(StagePacket()));

		auto missingMode = PolicyPacket();
		missingMode.erase("mode");
		assert(!F4T::QuestWire::DecodePolicy(missingMode));

		auto missingVersion = PolicyPacket();
		missingVersion.erase("version");
		assert(!F4T::QuestWire::DecodePolicy(missingVersion));

		auto emptyMode = PolicyPacket();
		emptyMode["mode"] = "";
		assert(!F4T::QuestWire::DecodePolicy(emptyMode));

		auto badSolo = PolicyPacket();
		badSolo["soloQuests"] = "not-an-array";
		assert(!F4T::QuestWire::DecodePolicy(badSolo));

		auto badGate = PolicyPacket();
		badGate["gates"] = Json::array({ Json{ { "quest", "nope" }, { "stage", 1 } } });
		assert(!F4T::QuestWire::DecodePolicy(badGate));

		auto tooManySolo = PolicyPacket();
		tooManySolo["soloQuests"] = Json::array();
		for (std::size_t index = 0; index <= F4T::QuestWire::kMaximumSoloQuests; ++index) {
			tooManySolo["soloQuests"].push_back(kQuest);
		}
		assert(!F4T::QuestWire::DecodePolicy(tooManySolo));

		auto badParty = PolicyPacket();
		badParty["partyQuests"] = "not-an-array";
		assert(!F4T::QuestWire::DecodePolicy(badParty));

		auto tooManyParty = PolicyPacket();
		tooManyParty["partyQuests"] = Json::array();
		for (std::size_t index = 0; index <= F4T::QuestWire::kMaximumPartyQuests; ++index) {
			tooManyParty["partyQuests"].push_back(kQuest);
		}
		assert(!F4T::QuestWire::DecodePolicy(tooManyParty));
	}

	void TestRoundTripIntoTheTracker()
	{
		F4T::QuestLogic::Tracker tracker;
		const auto decoded = F4T::QuestWire::DecodeSync(SyncPacket());
		assert(decoded);
		tracker.QueueRemoteSync(decoded->stages, decoded->completed);

		const F4T::QuestLogic::ApplyContext context{ 0, true, true, false, false };
		const auto first = tracker.TakeReadyStage(kQuest, context, 0);
		assert(first && first->stage == 10);
		const auto second = tracker.TakeReadyStage(kQuest, context, 1000);
		assert(second && second->stage == 30);

		const auto completions = tracker.TakeRemoteCompletions();
		assert(completions.size() == 1 && completions.front() == kOther);
	}
}

int main()
{
	TestReportEncoding();
	TestScopeEncoding();
	TestStageDecode();
	TestStageDecodeRejects();
	TestSyncDecode();
	TestSyncDecodeRejects();
	TestPolicyDecode();
	TestPolicyDecodeRejects();
	TestNotReachableDecode();
	TestNotReachableDecodeRejects();
	TestRoundTripIntoTheTracker();
	return 0;
}
