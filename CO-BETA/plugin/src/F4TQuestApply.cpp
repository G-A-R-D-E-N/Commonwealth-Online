#include "F4TQuestApply.h"

#include <string>
#include <vector>

namespace F4T::QuestApply
{
	namespace
	{
		void CompleteQuest(
			QuestLogic::Tracker& a_tracker,
			std::string_view     a_key,
			std::uint32_t        a_formId,
			const QuestView&     a_quest,
			std::uint64_t        a_nowMilliseconds,
			const Host&          a_host)
		{
			if (a_quest.completed) {
				return;
			}

			a_tracker.NoteRemoteApplied(a_key, a_quest.stage, a_nowMilliseconds);
			if (a_host.enqueue) {
				a_host.enqueue(Command{ std::string(a_key), a_formId, -1 });
			}
			if (a_host.log.completed) {
				a_host.log.completed(a_key, a_formId);
			}
		}
	}

	void ApplyRemoteCompletions(QuestLogic::Tracker& a_tracker, const Host& a_host, std::uint64_t a_nowMilliseconds)
	{
		const auto queued = a_tracker.TakeRemoteCompletions();
		std::vector<std::string> waiting;
		for (const auto& key : queued) {
			const auto form = a_host.formOf ? a_host.formOf(key) : std::nullopt;
			const auto quest = form && a_host.quest ? a_host.quest(*form) : std::nullopt;
			if (!form || !quest) {
				if (a_host.log.unresolved) {
					a_host.log.unresolved("complete", key);
				}
				continue;
			}
			if (quest->busy) {
				waiting.push_back(key);
				continue;
			}

			CompleteQuest(a_tracker, key, *form, *quest, a_nowMilliseconds, a_host);
		}
		a_tracker.RestoreRemoteCompletions(std::move(waiting));
	}

	void ApplyHeldStages(QuestLogic::Tracker& a_tracker, const Host& a_host, std::uint64_t a_nowMilliseconds)
	{
		if (!a_host.formOf || !a_host.quest) {
			return;
		}

		for (const auto& key : a_tracker.PendingQuests()) {
			const auto form = a_host.formOf(key);
			const auto quest = form ? a_host.quest(*form) : std::nullopt;
			if (!form || !quest) {
				if (a_host.log.unresolved) {
					a_host.log.unresolved("apply a stage to", key);
				}
				continue;
			}

			const QuestLogic::ApplyContext context{ quest->stage, quest->started, quest->running, quest->completed, quest->busy };
			const auto entry = a_tracker.TakeReadyStage(key, context, a_nowMilliseconds);
			if (!entry) {
				continue;
			}

			if (a_host.enqueue) {
				a_host.enqueue(Command{ entry->quest, *form, static_cast<std::int32_t>(entry->stage) });
			}
			if (a_host.log.applied) {
				a_host.log.applied(*entry, *form);
			}
			if (entry->completed) {
				CompleteQuest(a_tracker, entry->quest, *form, *quest, a_nowMilliseconds, a_host);
			}
		}
	}
}
