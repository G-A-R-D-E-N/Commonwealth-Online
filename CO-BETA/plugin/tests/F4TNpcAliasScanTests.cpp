#include "F4TNpcAliasScan.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Scan = F4T::NpcSync::AliasScan;

namespace
{
	struct FakeQuest
	{
		bool running{};
		std::vector<std::uint32_t> aliases;
	};

	std::vector<std::uint32_t> Range(std::uint32_t a_first, std::size_t a_count)
	{
		std::vector<std::uint32_t> values;
		for (std::size_t i = 0; i < a_count; ++i) {
			values.push_back(a_first + static_cast<std::uint32_t>(i));
		}
		return values;
	}

	bool Run(const std::vector<FakeQuest>& a_quests, Scan::Cursor& a_cursor, std::size_t a_budget, std::vector<std::uint32_t>& a_seen)
	{
		return Scan::Advance(
			a_quests,
			a_cursor,
			a_budget,
			[](const FakeQuest& a_quest) { return a_quest.running; },
			[](const FakeQuest& a_quest) { return a_quest.aliases.size(); },
			[&](const FakeQuest& a_quest, std::size_t a_aliasIndex) {
				const auto refId = a_quest.aliases[a_aliasIndex];
				if (refId == 0) {
					return false;
				}
				a_seen.push_back(refId);
				return true;
			});
	}
}

int main()
{
	{
		const std::vector<FakeQuest> quests{
			{ false, Range(100, 5) },
			{ true, { 10, 11 } },
		};
		Scan::Cursor cursor;
		std::vector<std::uint32_t> seen;
		assert(!Run(quests, cursor, 1, seen));
		assert((seen == std::vector<std::uint32_t>{ 10 }));
		assert(Run(quests, cursor, 1, seen));
		assert((seen == std::vector<std::uint32_t>{ 10, 11 }));
		assert(cursor.questIndex == 0 && cursor.aliasIndex == 0);
	}

	{
		const std::vector<FakeQuest> quests{
			{ true, { 0, 0, 0, 20 } },
		};
		Scan::Cursor cursor;
		std::vector<std::uint32_t> seen;
		assert(Run(quests, cursor, 1, seen));
		assert((seen == std::vector<std::uint32_t>{ 20 }));
	}

	{
		const std::vector<FakeQuest> quests{
			{ true, Range(1, 7) },
			{ false, Range(900, 100) },
			{ true, {} },
			{ true, Range(8, 9) },
			{ true, Range(17, 5) },
		};
		Scan::Cursor cursor;
		std::vector<std::uint32_t> seen;
		std::size_t calls = 0;
		while (!Run(quests, cursor, 4, seen)) {
			++calls;
			assert(calls < 16);
		}
		assert(calls == 5);
		std::sort(seen.begin(), seen.end());
		assert(seen.size() == 21);
		assert(seen == Range(1, 21));
		assert(std::none_of(seen.begin(), seen.end(), [](std::uint32_t a_refId) { return a_refId >= 900; }));
	}

	{
		const std::vector<FakeQuest> quests;
		Scan::Cursor cursor;
		std::vector<std::uint32_t> seen;
		assert(Run(quests, cursor, 512, seen));
		assert(seen.empty());
	}

	return 0;
}
