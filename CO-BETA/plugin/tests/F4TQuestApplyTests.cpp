#include "F4TQuestApply.h"
#include "F4TQuestLogic.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using F4T::QuestApply::Host;
	using F4T::QuestApply::QuestView;
	using F4T::QuestLogic::Gate;
	using F4T::QuestLogic::Mode;
	using F4T::QuestLogic::Policy;
	using F4T::QuestLogic::Tracker;

	const std::string kQuest = "1A2B3C:Fallout4.esm";
	constexpr std::uint32_t kFormId = 0x00080000;

	struct Recorded
	{
		std::string key;
		std::uint32_t resolvedForm{};
		std::int32_t stage{};
	};

	struct Game
	{
		QuestView view{ 10, true, true, false, false };
		std::vector<Recorded> commands;

		Host HostFor()
		{
			Host host;
			host.formOf = [](std::string_view a_key) -> std::optional<std::uint32_t> {
				if (a_key != kQuest) {
					return std::nullopt;
				}
				return kFormId;
			};
			host.quest = [this](std::uint32_t a_formId) -> std::optional<QuestView> {
				if (a_formId != kFormId) {
					return std::nullopt;
				}
				return view;
			};
			host.enqueue = [this](const F4T::QuestApply::Command& a_command) {
				commands.push_back(Recorded{ a_command.key, a_command.resolvedForm, a_command.stage });
			};
			return host;
		}
	};

	Policy Shared()
	{
		Policy policy{};
		policy.mode = Mode::FactionWar;
		return policy;
	}

	Policy Gated(std::uint32_t a_stage)
	{
		Policy policy = Shared();
		policy.gates.push_back(Gate{ kQuest, a_stage });
		return policy;
	}

	void TestHeldCompletionAppliesStageThenCompletion()
	{
		Tracker tracker;
		tracker.SetPolicy(Gated(20));
		assert(tracker.QueueRemoteStage(kQuest, 20, true));

		Game game;
		const auto host = game.HostFor();

		F4T::QuestApply::ApplyHeldStages(tracker, host, 1000);
		assert(game.commands.empty());
		assert(tracker.PendingQuests().size() == 1);

		tracker.SetPolicy(Shared());
		F4T::QuestApply::ApplyHeldStages(tracker, host, 2000);
		assert(game.commands.size() == 2);
		assert(game.commands[0].stage == 20);
		assert(game.commands[0].resolvedForm == kFormId);
		assert(game.commands[1].stage == -1);
		assert(game.commands[1].resolvedForm == kFormId);
		assert(tracker.PendingQuests().empty());
	}

	void TestRunningQuestAtStageZeroIsHeld()
	{
		Tracker tracker;
		tracker.SetPolicy(Shared());
		assert(tracker.QueueRemoteStage(kQuest, 20));

		Game game;
		game.view = QuestView{ 0, false, true, false, false };
		const auto host = game.HostFor();

		F4T::QuestApply::ApplyHeldStages(tracker, host, 1000);
		assert(game.commands.empty());
		assert(tracker.PendingQuests().size() == 1);

		game.view = QuestView{ 5, true, true, false, false };
		F4T::QuestApply::ApplyHeldStages(tracker, host, 2000);
		assert(game.commands.size() == 1);
		assert(game.commands[0].stage == 20);
		assert(tracker.PendingQuests().empty());
	}

	void TestCompletionForAPassedStageIsApplied()
	{
		Tracker tracker;
		tracker.SetPolicy(Shared());
		assert(tracker.QueueRemoteStage(kQuest, 20, true));

		Game game;
		game.view = QuestView{ 30, true, true, false, false };
		const auto host = game.HostFor();

		F4T::QuestApply::ApplyHeldStages(tracker, host, 1000);
		assert(game.commands.empty());

		F4T::QuestApply::ApplyRemoteCompletions(tracker, host, 1500);
		assert(game.commands.size() == 1);
		assert(game.commands[0].stage == -1);
		assert(game.commands[0].resolvedForm == kFormId);

		F4T::QuestApply::ApplyRemoteCompletions(tracker, host, 2000);
		assert(game.commands.size() == 1);
	}

	void TestGatedStageDoesNotConsumeTheCompletion()
	{
		Tracker tracker;
		tracker.SetPolicy(Gated(20));
		assert(tracker.QueueRemoteStage(kQuest, 20, true));
		assert(tracker.QueueRemoteStage(kQuest, 30));

		Game game;
		game.view.stage = 10;
		const auto host = game.HostFor();

		F4T::QuestApply::ApplyHeldStages(tracker, host, 1000);
		assert(game.commands.size() == 1);
		assert(game.commands[0].stage == 30);
		assert(game.commands[0].resolvedForm == kFormId);
		assert(tracker.PendingQuests().size() == 1);

		F4T::QuestApply::ApplyRemoteCompletions(tracker, host, 1100);
		assert(game.commands.size() == 1);

		game.view.stage = 30;
		F4T::QuestApply::ApplyHeldStages(tracker, host, 2000);
		assert(game.commands.size() == 1);
		assert(tracker.PendingQuests().empty());

		F4T::QuestApply::ApplyRemoteCompletions(tracker, host, 2100);
		assert(game.commands.size() == 2);
		assert(game.commands[1].stage == -1);
		assert(game.commands[1].resolvedForm == kFormId);
	}

	void TestBusyCompletionStaysQueuedUntilThePlayerIsFree()
	{
		const std::string questB = "0000AA:Fallout4.esm";
		constexpr std::uint32_t formB = 0x000000AA;

		Tracker tracker;
		tracker.SetPolicy(Shared());
		tracker.QueueRemoteSync({}, { kQuest, questB });

		bool busy = true;
		std::vector<Recorded> commands;
		Host host;
		host.formOf = [&](std::string_view a_key) -> std::optional<std::uint32_t> {
			if (a_key == kQuest) {
				return kFormId;
			}
			if (a_key == questB) {
				return formB;
			}
			return std::nullopt;
		};
		host.quest = [&](std::uint32_t a_formId) -> std::optional<QuestView> {
			if (a_formId != kFormId && a_formId != formB) {
				return std::nullopt;
			}
			return QuestView{ 10, true, true, false, busy };
		};
		host.enqueue = [&](const F4T::QuestApply::Command& a_command) {
			commands.push_back(Recorded{ a_command.key, a_command.resolvedForm, a_command.stage });
		};

		F4T::QuestApply::ApplyRemoteCompletions(tracker, host, 1000);
		assert(commands.empty());
		const auto kept = tracker.TakeRemoteCompletions();
		assert(kept.size() == 2);
		assert(kept[0] == kQuest);
		assert(kept[1] == questB);

		tracker.QueueRemoteSync({}, { kQuest, questB });
		busy = false;
		F4T::QuestApply::ApplyRemoteCompletions(tracker, host, 2000);
		assert(commands.size() == 2);
		assert(commands[0].key == kQuest);
		assert(commands[0].resolvedForm == kFormId);
		assert(commands[0].stage == -1);
		assert(commands[1].key == questB);
		assert(commands[1].resolvedForm == formB);
		assert(commands[1].stage == -1);

		F4T::QuestApply::ApplyRemoteCompletions(tracker, host, 3000);
		assert(commands.size() == 2);
	}
}

int main()
{
	TestHeldCompletionAppliesStageThenCompletion();
	TestRunningQuestAtStageZeroIsHeld();
	TestCompletionForAPassedStageIsApplied();
	TestGatedStageDoesNotConsumeTheCompletion();
	TestBusyCompletionStaysQueuedUntilThePlayerIsFree();
	return 0;
}
