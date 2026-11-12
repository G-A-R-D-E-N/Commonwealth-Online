#pragma once

#include <cstddef>

namespace F4T::NpcSync::AliasScan
{
	struct Cursor
	{
		std::size_t questIndex{};
		std::size_t aliasIndex{};
	};

	// Constraint: only aliases of running quests count against a_budget; quests that are not running and empty aliases are skipped for free.
	template <class TQuests, class TIsRunning, class TAliasCount, class TVisit>
	bool Advance(const TQuests& a_quests, Cursor& a_cursor, std::size_t a_budget, TIsRunning a_isRunning, TAliasCount a_aliasCount, TVisit a_visit)
	{
		std::size_t spent = 0;
		while (a_cursor.questIndex < a_quests.size()) {
			const auto& quest = a_quests[a_cursor.questIndex];
			if (!a_isRunning(quest) || a_cursor.aliasIndex >= a_aliasCount(quest)) {
				++a_cursor.questIndex;
				a_cursor.aliasIndex = 0;
				continue;
			}
			if (spent >= a_budget) {
				return false;
			}
			if (a_visit(quest, a_cursor.aliasIndex)) {
				++spent;
			}
			++a_cursor.aliasIndex;
		}
		a_cursor = {};
		return true;
	}
}
