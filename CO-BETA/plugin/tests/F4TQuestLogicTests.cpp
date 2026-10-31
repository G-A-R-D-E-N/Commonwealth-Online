#include "F4TQuestLogic.h"

#include <cassert>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// TODO-verify: 0xFE light / 0xFF runtime form-id markers per co-core/include/F4TFormIdentity.h; see E:/GardenEngine/re-requests/2026-10-18-quest-sync-runtimes.md

namespace
{
	using F4T::QuestKey::FileKind;
	using F4T::QuestKey::LoadOrderFile;
	using F4T::QuestLogic::ApplyContext;
	using F4T::QuestLogic::DropCause;
	using F4T::QuestLogic::Gate;
	using F4T::QuestLogic::Mode;
	using F4T::QuestLogic::Observation;
	using F4T::QuestLogic::Policy;
	using F4T::QuestLogic::StageEntry;
	using F4T::QuestLogic::Tracker;

	const std::string kQuest = "00ABCD:CommonwealthOnline.esp";
	const std::string kOther = "001234:Fallout4.esm";
	const std::string kLight = "000123:MyPatch.esl";

	const std::vector<LoadOrderFile> kLoadOrder{
		{ "Fallout4.esm", FileKind::Regular, 0x00 },
		{ "CommonwealthOnline.esp", FileKind::Regular, 0x01 },
		{ "DLCworkshop01.esm", FileKind::Light, 0x00 },
		{ "MyPatch.esl", FileKind::Light, 0x50 },
	};

	Observation Make(
		const std::string& a_quest,
		std::uint32_t a_stage,
		std::int32_t a_type = 1,
		bool a_started = true,
		bool a_running = true,
		bool a_completed = false)
	{
		return Observation{ a_quest, a_stage, a_type, a_running, a_completed, a_started };
	}

	ApplyContext Ready(std::uint32_t a_currentStage)
	{
		return ApplyContext{ a_currentStage, true, true, false, false };
	}

	Tracker WithPolicy(Policy a_policy)
	{
		Tracker tracker;
		tracker.SetPolicy(std::move(a_policy));
		return tracker;
	}

	void TestFormKeyRoundTrip()
	{
		const auto parsed = F4T::QuestKey::Parse("00ABCD:CommonwealthOnline.esp");
		assert(parsed && parsed->plugin == "commonwealthonline.esp" && parsed->localId == 0xABCD);
		assert(F4T::QuestKey::Format(*parsed) == "00ABCD:commonwealthonline.esp");

		const auto lower = F4T::QuestKey::Parse("00abcd:commonwealthonline.esp");
		assert(parsed && lower && *parsed == *lower);
		assert(F4T::QuestKey::Format(*lower) == "00ABCD:commonwealthonline.esp");

		const auto shortId = F4T::QuestKey::Parse("000001:Fallout4.esm");
		assert(shortId && F4T::QuestKey::Format(*shortId) == "000001:fallout4.esm");
	}

	void TestFormKeyRejects()
	{
		assert(!F4T::QuestKey::Parse(""));
		assert(!F4T::QuestKey::Parse("CommonwealthOnline.esp"));
		assert(!F4T::QuestKey::Parse(":CommonwealthOnline.esp"));
		assert(!F4T::QuestKey::Parse("ABCDE:CommonwealthOnline.esp"));
		assert(!F4T::QuestKey::Parse("ABCDEFG:CommonwealthOnline.esp"));
		assert(!F4T::QuestKey::Parse("GGGGGG:CommonwealthOnline.esp"));
		assert(!F4T::QuestKey::Parse("00ABCD:"));
		assert(!F4T::QuestKey::Parse("00ABCD:A:B.esp"));
		assert(!F4T::QuestKey::Parse("00ABCD:Bad\nName.esp"));

		const std::string maximumPlugin(128, 'P');
		const auto longest = F4T::QuestKey::Parse("00ABCD:" + maximumPlugin);
		assert(longest && longest->plugin == std::string(128, 'p'));
		assert(!F4T::QuestKey::Parse("00ABCD:" + maximumPlugin + "P"));
	}

	void TestFormKeyFromFormId()
	{
		const auto regular = F4T::QuestKey::FromFormId(0x0100ABCD, kLoadOrder);
		assert(regular && F4T::QuestKey::Format(*regular) == "00ABCD:commonwealthonline.esp");

		const auto master = F4T::QuestKey::FromFormId(0x00001234, kLoadOrder);
		assert(master && F4T::QuestKey::Format(*master) == "001234:fallout4.esm");

		const auto light = F4T::QuestKey::FromFormId(0xFE050123, kLoadOrder);
		assert(light && F4T::QuestKey::Format(*light) == "000123:mypatch.esl");

		const auto lightZero = F4T::QuestKey::FromFormId(0xFE000123, kLoadOrder);
		assert(lightZero && F4T::QuestKey::Format(*lightZero) == "000123:dlcworkshop01.esm");

		assert(!F4T::QuestKey::FromFormId(0xFF000123, kLoadOrder));
		assert(!F4T::QuestKey::FromFormId(0x07000123, kLoadOrder));
		assert(!F4T::QuestKey::FromFormId(0xFE990123, kLoadOrder));
		assert(!F4T::QuestKey::FromFormId(0x0100ABCD, {}));
	}

	void TestFormKeyToFormId()
	{
		const auto regular = F4T::QuestKey::ToFormId({ "CommonwealthOnline.esp", 0xABCD }, kLoadOrder);
		assert(regular && *regular == 0x0100ABCD);

		const auto master = F4T::QuestKey::ToFormId({ "Fallout4.esm", 0x1234 }, kLoadOrder);
		assert(master && *master == 0x00001234);

		const auto light = F4T::QuestKey::ToFormId({ "MyPatch.esl", 0x123 }, kLoadOrder);
		assert(light && *light == 0xFE050123);

		const auto cased = F4T::QuestKey::ToFormId({ "commonwealthonline.ESP", 0xABCD }, kLoadOrder);
		assert(cased && *cased == 0x0100ABCD);

		assert(!F4T::QuestKey::ToFormId({ "Missing.esp", 0x1234 }, kLoadOrder));
		assert(!F4T::QuestKey::ToFormId({ "MyPatch.esl", 0x1234 }, kLoadOrder));
		assert(!F4T::QuestKey::ToFormId({ "Fallout4.esm", 0x1234567 }, kLoadOrder));

		for (const std::uint32_t formId : { 0x00001234U, 0x0100ABCDU, 0xFE050123U }) {
			const auto key = F4T::QuestKey::FromFormId(formId, kLoadOrder);
			assert(key);
			const auto back = F4T::QuestKey::ToFormId(*key, kLoadOrder);
			assert(back && *back == formId);
		}
	}

	void TestModeParsing()
	{
		assert(F4T::QuestLogic::ParseMode("coop-story") == Mode::CoopStory);
		assert(F4T::QuestLogic::ParseMode("faction-war") == Mode::FactionWar);
		assert(F4T::QuestLogic::ParseMode("instanced-story") == Mode::InstancedStory);
		assert(F4T::QuestLogic::ParseMode("free-roam") == Mode::FreeRoam);
		assert(F4T::QuestLogic::ParseMode("") == Mode::CoopStory);
		assert(F4T::QuestLogic::ParseMode("something-new") == Mode::CoopStory);

		assert(F4T::QuestLogic::ModeSharesProgress(Mode::CoopStory));
		assert(F4T::QuestLogic::ModeSharesProgress(Mode::FactionWar));
		assert(!F4T::QuestLogic::ModeSharesProgress(Mode::InstancedStory));
		assert(!F4T::QuestLogic::ModeSharesProgress(Mode::FreeRoam));
	}

	void TestSharedQuestTypes()
	{
		assert(!F4T::QuestLogic::IsSharedQuestType(0));
		assert(F4T::QuestLogic::IsSharedQuestType(1));
		assert(F4T::QuestLogic::IsSharedQuestType(5));
		assert(!F4T::QuestLogic::IsSharedQuestType(6));
		assert(F4T::QuestLogic::IsSharedQuestType(7));
		assert(F4T::QuestLogic::IsSharedQuestType(0xA));
	}

	void TestForwardOnlyDetection()
	{
		Tracker tracker;

		assert(!tracker.Observe(Make(kQuest, 10), 0));
		assert(!tracker.Observe(Make(kQuest, 10), 1000));
		assert(!tracker.Observe(Make(kQuest, 5), 2000));

		const auto report = tracker.Observe(Make(kQuest, 20), 3000);
		assert(report && report->quest == kQuest && report->stage == 20 && !report->completed);

		assert(!tracker.Observe(Make(kQuest, 20), 4000));
		assert(!tracker.Observe(Make(kQuest, 15), 5000));
		assert(!tracker.Observe(Make(kQuest, 15), 6000));
	}

	void TestReportGuards()
	{
		Tracker tracker;
		tracker.Observe(Make(kQuest, 10), 0);

		assert(!tracker.Observe(Make(kQuest, 20, 1, false), 1000));
		assert(!tracker.Observe(Make(kQuest, 30, 1, true, false), 2000));
		assert(!tracker.Observe(Make(kQuest, 40, 6), 3000));
		assert(!tracker.Observe(Make(kQuest, 50, 0), 4000));

		Policy policy{};
		policy.soloQuests.push_back(kQuest);
		tracker.SetPolicy(policy);
		assert(!tracker.Observe(Make(kQuest, 60), 5000));
		assert(!tracker.Observe(Make(kOther, 10), 6000));
	}

	void TestSettleWindow()
	{
		Tracker tracker;
		tracker.BeginSettle(1000);

		assert(!tracker.Observe(Make(kQuest, 10), 1000));
		assert(!tracker.Observe(Make(kQuest, 20), 3000));
		assert(!tracker.Observe(Make(kQuest, 30), 5999));

		const auto report = tracker.Observe(Make(kQuest, 40), 6000);
		assert(report && report->stage == 40);
	}

	void TestEchoGuard()
	{
		Tracker tracker;
		tracker.Observe(Make(kQuest, 10), 0);
		tracker.NoteRemoteApplied(kQuest, 20, 1000);

		assert(!tracker.Observe(Make(kQuest, 20), 1500));
		assert(!tracker.Observe(Make(kQuest, 30), 2000));
		assert(!tracker.Observe(Make(kQuest, 35), 3999));

		const auto report = tracker.Observe(Make(kQuest, 40), 4000);
		assert(report && report->stage == 40);
	}

	void TestCompletionNotice()
	{
		Tracker tracker;
		tracker.Observe(Make(kQuest, 10), 0);

		const auto report = tracker.Observe(Make(kQuest, 10, 1, true, true, true), 1000);
		assert(report && report->completed && report->stage == 10);
		assert(!tracker.Observe(Make(kQuest, 10, 1, true, true, true), 2000));
	}

	void TestCompletionAfterRemoteStageIsSilent()
	{
		Tracker tracker;
		tracker.Observe(Make(kQuest, 10), 0);
		tracker.NoteRemoteApplied(kQuest, 20, 1000);
		assert(!tracker.Observe(Make(kQuest, 20, 1, true, true, true), 5000));

		Tracker late;
		late.Observe(Make(kQuest, 10), 0);
		late.NoteRemoteApplied(kQuest, 20, 1000);
		const auto report = late.Observe(Make(kQuest, 20, 1, true, true, true), 12000);
		assert(report && report->completed);
	}

	void TestApplyOptionOne()
	{
		Tracker tracker;
		assert(tracker.QueueRemoteStage(kQuest, 50));

		auto context = Ready(10);
		context.started = false;
		assert(!tracker.TakeReadyStage(kQuest, context, 1000).has_value());

		context = Ready(10);
		context.running = false;
		assert(!tracker.TakeReadyStage(kQuest, context, 2000).has_value());

		context = Ready(10);
		context.completed = true;
		assert(!tracker.TakeReadyStage(kQuest, context, 3000).has_value());

		context = Ready(10);
		context.busy = true;
		assert(!tracker.TakeReadyStage(kQuest, context, 4000).has_value());

		const auto applied = tracker.TakeReadyStage(kQuest, Ready(10), 5000);
		assert(applied && applied->quest == kQuest && applied->stage == 50);
		assert(!tracker.TakeReadyStage(kQuest, Ready(50), 6000).has_value());
	}

	void TestApplyNeverGoesBackwards()
	{
		Tracker tracker;
		assert(tracker.QueueRemoteStage(kQuest, 10));
		assert(!tracker.TakeReadyStage(kQuest, Ready(30), 0).has_value());
		assert(tracker.PendingQuests().empty());

		assert(tracker.QueueRemoteStage(kQuest, 40));
		assert(tracker.QueueRemoteStage(kQuest, 50));
		const auto applied = tracker.TakeReadyStage(kQuest, Ready(45), 1000);
		assert(applied && applied->stage == 50);
	}

	void TestOrderedReplay()
	{
		Tracker tracker;
		tracker.QueueRemoteSync({ StageEntry{ kQuest, 30 }, StageEntry{ kQuest, 10 }, StageEntry{ kQuest, 20 } }, {});

		const auto first = tracker.TakeReadyStage(kQuest, Ready(0), 0);
		assert(first && first->stage == 10);
		const auto second = tracker.TakeReadyStage(kQuest, Ready(10), 1000);
		assert(second && second->stage == 20);
		const auto third = tracker.TakeReadyStage(kQuest, Ready(20), 2000);
		assert(third && third->stage == 30);
		assert(!tracker.TakeReadyStage(kQuest, Ready(30), 3000).has_value());
		assert(tracker.PendingQuests().empty());
	}

	void TestDuplicateRemoteStageIsIdempotent()
	{
		Tracker tracker;
		assert(tracker.QueueRemoteStage(kQuest, 30));
		assert(tracker.QueueRemoteStage(kQuest, 30));
		tracker.QueueRemoteSync({ StageEntry{ kQuest, 30 } }, {});

		assert(tracker.TakeReadyStage(kQuest, Ready(0), 0).has_value());
		assert(!tracker.TakeReadyStage(kQuest, Ready(30), 1000).has_value());
	}

	void TestGatedStageDoesNotBlockLaterStages()
	{
		Policy policy{};
		policy.mode = Mode::FactionWar;
		policy.gates.push_back(Gate{ kQuest, 20 });
		auto tracker = WithPolicy(policy);

		tracker.QueueRemoteSync({ StageEntry{ kQuest, 10 }, StageEntry{ kQuest, 20 }, StageEntry{ kQuest, 30 } }, {});

		const auto first = tracker.TakeReadyStage(kQuest, Ready(0), 0);
		assert(first && first->stage == 10);
		const auto second = tracker.TakeReadyStage(kQuest, Ready(10), 1000);
		assert(second && second->stage == 30);
		assert(!tracker.TakeReadyStage(kQuest, Ready(30), 2000).has_value());
		assert(tracker.PendingQuests().empty());

		Tracker held;
		held.SetPolicy(policy);
		assert(held.QueueRemoteStage(kQuest, 20));
		assert(!held.TakeReadyStage(kQuest, Ready(10), 0).has_value());
		assert(held.PendingQuests().size() == 1);
	}

	void TestHeldCompletionSurvivesTheGate()
	{
		Policy policy{};
		policy.mode = Mode::FactionWar;
		policy.gates.push_back(Gate{ kQuest, 20 });
		auto tracker = WithPolicy(policy);

		assert(tracker.QueueRemoteStage(kQuest, 20));
		assert(tracker.QueueRemoteStage(kQuest, 20, true));
		assert(!tracker.TakeReadyStage(kQuest, Ready(10), 1000).has_value());
		assert(tracker.PendingQuests().size() == 1);

		Policy released{};
		released.mode = Mode::FactionWar;
		tracker.SetPolicy(released);
		const auto applied = tracker.TakeReadyStage(kQuest, Ready(10), 2000);
		assert(applied && applied->stage == 20 && applied->completed);
	}

	void TestPassedHeldCompletionKeepsTheCompletion()
	{
		Policy policy{};
		policy.mode = Mode::FactionWar;
		auto tracker = WithPolicy(policy);

		assert(tracker.QueueRemoteStage(kQuest, 20, true));
		assert(!tracker.TakeReadyStage(kQuest, Ready(30), 0).has_value());
		assert(tracker.PendingQuests().empty());
		const auto completions = tracker.TakeRemoteCompletions();
		assert(completions.size() == 1 && completions.front() == kQuest);
	}

	void TestQuestKeyLengthMatchesTheServer()
	{
		assert(F4T::QuestKey::kMaximumQuestKeyBytes ==
			F4T::QuestKey::kLocalIdDigits + 1 + F4T::QuestKey::kMaximumPluginNameBytes);
		assert(F4T::QuestKey::kMaximumQuestKeyBytes == 135);

		const std::string longest = "00ABCD:" + std::string(F4T::QuestKey::kMaximumPluginNameBytes, 'P');
		assert(longest.size() == F4T::QuestKey::kMaximumQuestKeyBytes);
		assert(F4T::QuestKey::Parse(longest));
		assert(!F4T::QuestKey::Parse(longest + "P"));
	}

	void TestGateIsExactStage()
	{
		Policy policy{};
		policy.mode = Mode::FactionWar;
		policy.gates.push_back(Gate{ kQuest, 20 });
		auto tracker = WithPolicy(policy);

		assert(tracker.IsGated(kQuest, 20));
		assert(!tracker.IsGated(kQuest, 21));
		assert(!tracker.IsGated(kQuest, 30));
		assert(!tracker.IsGated(kQuest, 19));
		assert(!tracker.IsGated(kOther, 20));

		tracker.QueueRemoteSync({ StageEntry{ kQuest, 30 } }, {});
		const auto applied = tracker.TakeReadyStage(kQuest, Ready(0), 0);
		assert(applied && applied->stage == 30);
		assert(tracker.PendingQuests().empty());
	}

	void TestModeStopsApplying()
	{
		for (const auto mode : { Mode::InstancedStory, Mode::FreeRoam }) {
			Policy policy{};
			policy.mode = mode;
			Tracker tracker;
			tracker.QueueRemoteStage(kQuest, 50);
			tracker.SetPolicy(policy);

			assert(!tracker.QueueRemoteStage(kQuest, 60));
			assert(!tracker.TakeReadyStage(kQuest, Ready(0), 0).has_value());
			assert(tracker.PendingQuests().empty());
		}
	}

	void TestSoloStopsBothDirections()
	{
		Policy policy{};
		policy.soloQuests.push_back(kQuest);
		auto tracker = WithPolicy(policy);

		tracker.Observe(Make(kQuest, 10), 0);
		assert(!tracker.Observe(Make(kQuest, 20), 1000));

		assert(!tracker.QueueRemoteStage(kQuest, 50));
		assert(!tracker.TakeReadyStage(kQuest, Ready(0), 0).has_value());

		assert(tracker.QueueRemoteStage(kOther, 50));
		assert(tracker.TakeReadyStage(kOther, Ready(0), 0).has_value());
	}

	void TestPartyQuestsStayShared()
	{
		Policy policy{};
		policy.partyQuests.push_back(kQuest);
		auto tracker = WithPolicy(policy);
		assert(tracker.IsParty(kQuest) && !tracker.IsSolo(kQuest));
		assert(!tracker.IsParty(kOther));

		tracker.Observe(Make(kQuest, 10), 0);
		const auto report = tracker.Observe(Make(kQuest, 20), 1000);
		assert(report && report->quest == kQuest && report->stage == 20);

		assert(tracker.QueueRemoteStage(kQuest, 30));
		const auto applied = tracker.TakeReadyStage(kQuest, Ready(20), 2000);
		assert(applied && applied->stage == 30);

		Tracker idle;
		idle.SetPolicy(policy);
		idle.Observe(Make(kQuest, 0), 0);
		assert(idle.Snapshot().size() == 1);
		Tracker unlisted;
		unlisted.Observe(Make(kQuest, 0), 0);
		assert(unlisted.Snapshot().empty());
	}

	void TestRemoteCompletions()
	{
		Tracker tracker;
		tracker.QueueRemoteSync({}, { kOther, kOther, kQuest });
		const auto completions = tracker.TakeRemoteCompletions();
		assert(completions.size() == 2);
		assert(tracker.TakeRemoteCompletions().empty());

		Policy policy{};
		policy.soloQuests.push_back(kOther);
		tracker.SetPolicy(policy);
		tracker.QueueRemoteSync({}, { kOther, kQuest });
		const auto filtered = tracker.TakeRemoteCompletions();
		assert(filtered.size() == 1 && filtered.front() == kQuest);
	}

	void TestResetClearsEverything()
	{
		Policy policy{};
		policy.mode = Mode::FreeRoam;
		policy.soloQuests.push_back(kQuest);
		policy.gates.push_back(Gate{ kQuest, 10 });
		auto tracker = WithPolicy(policy);
		tracker.BeginSettle(1000);
		tracker.QueueRemoteSync({ StageEntry{ kOther, 10 } }, { kOther });

		tracker.Reset();

		assert(tracker.GetPolicy().mode == Mode::CoopStory);
		assert(tracker.GetPolicy().soloQuests.empty());
		assert(tracker.GetPolicy().gates.empty());
		assert(tracker.PendingQuests().empty());
		assert(tracker.TakeRemoteCompletions().empty());
		assert(tracker.SharesProgress());

		assert(tracker.QueueRemoteStage(kOther, 50));
		const auto applied = tracker.TakeReadyStage(kOther, Ready(0), 0);
		assert(applied && applied->stage == 50);
	}

	void TestViewSnapshot()
	{
		Tracker tracker;
		assert(tracker.Snapshot().empty());

		tracker.Observe(Make(kOther, 30), 0);
		tracker.Observe(Make(kQuest, 20), 0);
		tracker.Observe(Make(kQuest, 20, 1, true, true, true), 1000);

		const auto observed = tracker.Snapshot();
		assert(observed.size() == 2);
		assert(observed[0].quest == kOther && observed[0].stage == 30 && !observed[0].completed && !observed[0].held);
		assert(observed[1].quest == kQuest && observed[1].stage == 20 && observed[1].completed && !observed[1].held);

		assert(tracker.QueueRemoteStage(kOther, 40));
		assert(tracker.QueueRemoteStage(kLight, 10));

		const auto withHeld = tracker.Snapshot();
		assert(withHeld.size() == 3);
		assert(withHeld[0].quest == kLight && withHeld[0].stage == 0 && withHeld[0].held);
		assert(withHeld[1].quest == kOther && withHeld[1].stage == 30 && withHeld[1].held);
		assert(withHeld[2].quest == kQuest && !withHeld[2].held);

		const auto applied = tracker.TakeReadyStage(kOther, Ready(30), 2000);
		assert(applied && applied->stage == 40);
		const auto afterApply = tracker.Snapshot();
		assert(afterApply[0].quest == kLight && afterApply[0].held);
		assert(afterApply[1].quest == kOther && afterApply[1].stage == 40 && !afterApply[1].held);
		assert(afterApply[2].quest == kQuest && afterApply[2].completed);
	}

	void TestViewSnapshotSkipsNeverStartedQuests()
	{
		Tracker tracker;
		for (std::uint32_t index = 1; index <= 40; ++index) {
			tracker.Observe(Make("000" + std::to_string(index) + ":Idle.esp", 0), 0);
		}
		tracker.Observe(Make(kQuest, 10), 0);
		tracker.Observe(Make(kQuest, 20), 1000);

		const auto observed = tracker.Snapshot();
		assert(observed.size() == 1);
		assert(observed[0].quest == kQuest && observed[0].stage == 20 && !observed[0].held);
	}

	void TestMixedCaseLocalKeyMatchesServerKey()
	{
		const auto local = F4T::QuestKey::FromFormId(0x0100ABCD, kLoadOrder);
		assert(local);
		const std::string localKey = F4T::QuestKey::Format(*local);
		const std::string serverKey = "00ABCD:commonwealthonline.esp";
		assert(localKey == serverKey);

		Tracker shared;
		shared.Observe(Make(localKey, 10), 0);
		assert(shared.QueueRemoteStage(serverKey, 20));
		const auto applied = shared.TakeReadyStage(serverKey, Ready(10), 1000);
		assert(applied && applied->quest == serverKey && applied->stage == 20);
		assert(!shared.Observe(Make(localKey, 20), 2000));
		const auto rows = shared.Snapshot();
		assert(rows.size() == 1 && rows[0].quest == serverKey && rows[0].stage == 20 && !rows[0].held);

		Policy policy{};
		policy.soloQuests.push_back(serverKey);
		auto solo = WithPolicy(policy);
		assert(!solo.Observe(Make(localKey, 10), 0));
		assert(!solo.Observe(Make(localKey, 20), 1000));
		assert(solo.IsSolo(serverKey));
		assert(!solo.QueueRemoteStage(serverKey, 30));
		assert(solo.Snapshot().size() == 1);
	}

	void TestFriendStagesFollowTheRow()
	{
		Tracker tracker;
		tracker.Observe(Make(kQuest, 10), 0);
		tracker.NoteFriendStage(kQuest, 9, 30, false);
		tracker.NoteFriendStage(kQuest, 7, 20, true);
		tracker.NoteFriendStage(kQuest, 7, 25, false);

		const auto rows = tracker.Snapshot();
		assert(rows.size() == 1 && rows[0].friends.size() == 2);
		assert(rows[0].friends[0].playerId == 7 && rows[0].friends[0].stage == 25 && !rows[0].friends[0].completed);
		assert(rows[0].friends[1].playerId == 9 && rows[0].friends[1].stage == 30);

		Tracker held;
		assert(held.QueueRemoteStage(kQuest, 20));
		held.NoteFriendStage(kQuest, 7, 20, false);
		const auto heldRows = held.Snapshot();
		assert(heldRows.size() == 1 && heldRows[0].held && heldRows[0].friends.size() == 1);

		Policy solo{};
		solo.soloQuests.push_back(kQuest);
		auto soloTracker = WithPolicy(solo);
		soloTracker.Observe(Make(kQuest, 10), 0);
		soloTracker.NoteFriendStage(kQuest, 7, 20, false);
		assert(soloTracker.Snapshot().size() == 1 && soloTracker.Snapshot()[0].friends.empty());
	}

	void TestFriendRowsExcludeTheLocalPlayer()
	{
		Tracker tracker;
		tracker.SetLocalPlayerId(3);
		tracker.Observe(Make(kQuest, 10), 0);
		tracker.NoteFriendStage(kQuest, 3, 10, false);
		tracker.NoteFriendStage(kQuest, 7, 20, false);

		const auto rows = tracker.Snapshot();
		assert(rows.size() == 1 && rows[0].friends.size() == 1 && rows[0].friends[0].playerId == 7);
	}

	void TestFriendRowsAreBounded()
	{
		Tracker tracker;
		tracker.Observe(Make(kQuest, 10), 0);
		for (std::uint32_t player = 1; player <= 70; ++player) {
			tracker.NoteFriendStage(kQuest, player, 10, false);
		}
		const auto rows = tracker.Snapshot();
		assert(rows[0].friends.size() == F4T::QuestLogic::kMaximumFriendsPerQuest);
		assert(rows[0].friends.front().playerId == 1 && rows[0].friends.back().playerId == 64);
		tracker.NoteFriendStage(kQuest, 2, 40, false);
		assert(tracker.Snapshot()[0].friends[1].stage == 40);

		for (std::uint32_t index = 1; index <= 33; ++index) {
			const std::string quest = "000" + std::to_string(index) + ":Idle.esp";
			tracker.Observe(Make(quest, 10), 0);
			tracker.NoteFriendStage(quest, 7, 10, false);
		}
		std::size_t withFriends = 0;
		for (const auto& row : tracker.Snapshot()) {
			if (!row.friends.empty()) {
				++withFriends;
			}
		}
		assert(withFriends == F4T::QuestLogic::kMaximumFriendQuests);
	}

	void TestFriendRowsClearWithSession()
	{
		Tracker shared;
		shared.Observe(Make(kQuest, 10), 0);
		shared.NoteFriendStage(kQuest, 7, 20, false);
		Policy freeRoam{};
		freeRoam.mode = Mode::FreeRoam;
		shared.SetPolicy(freeRoam);
		assert(shared.Snapshot()[0].friends.empty());

		Tracker reset;
		reset.Observe(Make(kQuest, 10), 0);
		reset.NoteFriendStage(kQuest, 7, 20, false);
		reset.Reset();
		assert(reset.Snapshot().empty());
		reset.Observe(Make(kQuest, 10), 0);
		assert(reset.Snapshot()[0].friends.empty());
	}

	void TestDroppedStagesCarryTheCause()
	{
		Policy freeRoam{};
		freeRoam.mode = Mode::FreeRoam;
		auto notShared = WithPolicy(freeRoam);
		assert(!notShared.QueueRemoteStage(kQuest, 20));
		const auto dropped = notShared.TakeDroppedStages();
		assert(dropped.size() == 1);
		assert(dropped.front().quest == kQuest);
		assert(dropped.front().cause == DropCause::NotShared);
		assert(notShared.TakeDroppedStages().empty());

		Policy soloPolicy{};
		soloPolicy.mode = Mode::FactionWar;
		soloPolicy.soloQuests.push_back(kQuest);
		auto solo = WithPolicy(soloPolicy);
		solo.QueueRemoteSync({ StageEntry{ kQuest, 20 } }, {});
		const auto soloDropped = solo.TakeDroppedStages();
		assert(soloDropped.size() == 1);
		assert(soloDropped.front().quest == kQuest);
		assert(soloDropped.front().cause == DropCause::Solo);

		Policy story{};
		story.mode = Mode::FactionWar;
		auto late = WithPolicy(story);
		late.QueueRemoteSync({ StageEntry{ kQuest, 20 } }, {});
		assert(late.PendingQuests().size() == 1);
		late.SetStoryOrder(F4T::QuestStory::Order::Default());
		assert(!late.TakeReadyStage(kQuest, Ready(0), 0).has_value());
		const auto never = late.TakeDroppedStages();
		assert(never.size() == 1);
		assert(never.front().cause == DropCause::NotReached);
		assert(late.TakeDroppedStages().empty());
	}
}

int main()
{
	TestFormKeyRoundTrip();
	TestFormKeyRejects();
	TestFormKeyFromFormId();
	TestFormKeyToFormId();
	TestModeParsing();
	TestSharedQuestTypes();
	TestForwardOnlyDetection();
	TestReportGuards();
	TestSettleWindow();
	TestEchoGuard();
	TestCompletionNotice();
	TestCompletionAfterRemoteStageIsSilent();
	TestApplyOptionOne();
	TestApplyNeverGoesBackwards();
	TestOrderedReplay();
	TestDuplicateRemoteStageIsIdempotent();
	TestGatedStageDoesNotBlockLaterStages();
	TestHeldCompletionSurvivesTheGate();
	TestPassedHeldCompletionKeepsTheCompletion();
	TestGateIsExactStage();
	TestQuestKeyLengthMatchesTheServer();
	TestModeStopsApplying();
	TestSoloStopsBothDirections();
	TestPartyQuestsStayShared();
	TestRemoteCompletions();
	TestResetClearsEverything();
	TestViewSnapshot();
	TestViewSnapshotSkipsNeverStartedQuests();
	TestMixedCaseLocalKeyMatchesServerKey();
	TestFriendStagesFollowTheRow();
	TestFriendRowsExcludeTheLocalPlayer();
	TestFriendRowsAreBounded();
	TestFriendRowsClearWithSession();
	TestDroppedStagesCarryTheCause();
	return 0;
}
