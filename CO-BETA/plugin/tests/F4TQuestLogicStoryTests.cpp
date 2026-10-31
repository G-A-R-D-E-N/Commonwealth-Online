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

	F4T::QuestStory::Order StoryOrder()
	{
		const nlohmann::json document{
			{ "quests",
				{ { { "quest", "01CC2A:fallout4.esm" },
					{ "label", "Out of Time" },
					{ "requires", nlohmann::json::array() },
					{ "startable", true },
					{ "source", "test" },
					{ "verified", false } },
					{ { "quest", "0229E5:fallout4.esm" },
					{ "label", "Second" },
					{ "requires", { { { "quest", "01CC2A:fallout4.esm" }, { "completed", true } } } },
					{ "startable", true },
					{ "source", "test" },
					{ "verified", false } },
					{ { "quest", "01F25E:fallout4.esm" },
					{ "label", "Third" },
					{ "requires", { { { "quest", "0229E5:fallout4.esm" }, { "stage", 10 } } } },
					{ "startable", true },
					{ "source", "test" },
					{ "verified", false } },
					{ { "quest", "00A1B2:fallout4.esm" },
					{ "label", "Locked" },
					{ "requires", nlohmann::json::array() },
					{ "startable", false },
					{ "source", "test" },
					{ "verified", false } } } },
		};
		const auto order = F4T::QuestStory::Order::Load(document);
		assert(order);
		return *order;
	}

	void TestStoryOrderEmptyMeansReached()
	{
		Tracker tracker;
		assert(tracker.IsStoryReached("00ABCD:commonwealthonline.esp"));
		assert(tracker.QueueRemoteStage("00ABCD:commonwealthonline.esp", 10));
	}

	void TestStoryOrderUnknownQuestRefused()
	{
		Tracker tracker;
		tracker.SetStoryOrder(StoryOrder());
		const std::string unknown = "00ABCD:commonwealthonline.esp";
		assert(!tracker.IsStoryReached(unknown));
		assert(!tracker.IsStoryReached("not a key"));
		assert(!tracker.QueueRemoteStage(unknown, 10));
		tracker.QueueRemoteSync({ StageEntry{ unknown, 10 } }, { unknown });
		assert(tracker.PendingQuests().empty());
		assert(tracker.TakeRemoteCompletions().empty());
		assert(!tracker.TakeReadyStage(unknown, Ready(0), 0).has_value());
	}

	void TestStoryOrderFirstQuestReachable()
	{
		Tracker tracker;
		tracker.SetStoryOrder(StoryOrder());
		const std::string intro = "01CC2A:fallout4.esm";
		assert(tracker.IsStoryReached(intro));
		assert(tracker.QueueRemoteStage(intro, 10));
		const auto applied = tracker.TakeReadyStage(intro, Ready(0), 0);
		assert(applied && applied->stage == 10);
	}

	void TestStoryOrderPrerequisiteChain()
	{
		Tracker tracker;
		tracker.SetStoryOrder(StoryOrder());
		const std::string intro = "01CC2A:fallout4.esm";
		const std::string next = "0229E5:fallout4.esm";
		assert(!tracker.IsStoryReached(next));
		assert(!tracker.QueueRemoteStage(next, 5));
		tracker.QueueRemoteSync({}, { next });
		assert(tracker.TakeRemoteCompletions().empty());

		tracker.Observe(Make(intro, 10), 0);
		assert(!tracker.IsStoryReached(next));
		const auto completed = tracker.Observe(Make(intro, 10, 1, true, true, true), 1000);
		assert(completed && completed->completed);
		assert(tracker.IsStoryReached(next));
		assert(tracker.QueueRemoteStage(next, 5));
		const auto applied = tracker.TakeReadyStage(next, Ready(0), 2000);
		assert(applied && applied->stage == 5);
	}

	void TestStoryOrderStageRequirement()
	{
		Tracker tracker;
		tracker.SetStoryOrder(StoryOrder());
		const std::string middle = "0229E5:fallout4.esm";
		const std::string later = "01F25E:fallout4.esm";
		assert(!tracker.IsStoryReached(middle));
		assert(!tracker.IsStoryReached(later));
		tracker.Observe(Make(middle, 5), 0);
		assert(tracker.IsStoryReached(middle));
		assert(!tracker.IsStoryReached(later));
		tracker.Observe(Make(middle, 10), 1000);
		assert(tracker.IsStoryReached(later));
		assert(tracker.QueueRemoteStage(later, 3));
	}

	void TestStoryOrderStartableFalseRefused()
	{
		Tracker tracker;
		tracker.SetStoryOrder(StoryOrder());
		const std::string locked = "00A1B2:fallout4.esm";
		assert(!tracker.IsStoryReached(locked));
		assert(!tracker.QueueRemoteStage(locked, 1));
		tracker.Observe(Make(locked, 1), 0);
		assert(tracker.IsStoryReached(locked));
		assert(tracker.QueueRemoteStage(locked, 2));
	}

	void TestNextStoryStep()
	{
		Tracker empty;
		assert(!empty.NextStoryStep().has_value());

		Tracker tracker;
		tracker.SetStoryOrder(StoryOrder());
		const auto first = tracker.NextStoryStep();
		assert(first && *first == "01CC2A:fallout4.esm");

		tracker.Observe(Make("01CC2A:fallout4.esm", 5), 0);
		const auto held = tracker.NextStoryStep();
		assert(held && *held == "01CC2A:fallout4.esm");

		tracker.Observe(Make("01CC2A:fallout4.esm", 5, 1, true, true, true), 1000);
		const auto second = tracker.NextStoryStep();
		assert(second && *second == "0229E5:fallout4.esm");
	}

	void TestStoryOrderCanBeReplaced()
	{
		Tracker tracker;
		tracker.SetStoryOrder(StoryOrder());
		assert(!tracker.IsStoryReached("00ABCD:commonwealthonline.esp"));
		tracker.SetStoryOrder(F4T::QuestStory::Order{});
		assert(tracker.IsStoryReached("00ABCD:commonwealthonline.esp"));
	}
}

int main()
{
	TestStoryOrderEmptyMeansReached();
	TestStoryOrderUnknownQuestRefused();
	TestStoryOrderFirstQuestReachable();
	TestStoryOrderPrerequisiteChain();
	TestStoryOrderStageRequirement();
	TestStoryOrderStartableFalseRefused();
	TestNextStoryStep();
	TestStoryOrderCanBeReplaced();
	return 0;
}
