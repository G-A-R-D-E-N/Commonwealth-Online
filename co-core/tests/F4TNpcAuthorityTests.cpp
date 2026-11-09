#include "F4TNpcAuthority.h"

#include "F4TAuthorityElection.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

int main()
{
	using namespace F4T::NpcAuthority;

	Manager manager;
	const Scope concord{ "0000CAFE", "0000F00D" };
	const Scope sanctuary{ "0000BEEF", "0000F00D" };

	const std::vector<PlayerScope> firstPlayers{
		{ 5, concord },
		{ 3, concord },
		{ 0, concord }
	};
	const auto first = manager.Reconcile(firstPlayers);
	assert(first.size() == 1);
	assert(first[0].scope == concord);
	assert(first[0].previousPlayerId == 0);
	assert(first[0].playerId == 3);
	assert(first[0].epoch == 1);
	assert(manager.Authorize(3, concord, 1));
	assert(!manager.Authorize(5, concord, 1));

	assert(manager.Reconcile(firstPlayers).empty());

	const std::vector<PlayerScope> handoffPlayers{
		{ 5, concord }
	};
	const auto handoff = manager.Reconcile(handoffPlayers);
	assert(handoff.size() == 1);
	assert(handoff[0].previousPlayerId == 3);
	assert(handoff[0].playerId == 5);
	assert(handoff[0].epoch == 2);
	assert(manager.Authorize(5, concord, 2));
	assert(!manager.Authorize(3, concord, 1));

	const std::vector<PlayerScope> none;
	const auto cleared = manager.Reconcile(none);
	assert(cleared.size() == 1);
	assert(cleared[0].previousPlayerId == 5);
	assert(cleared[0].playerId == 0);
	assert(cleared[0].epoch == 3);
	assert(!manager.Get(concord).has_value());

	const std::vector<PlayerScope> returned{
		{ 7, concord }
	};
	const auto returnedChange = manager.Reconcile(returned);
	assert(returnedChange.size() == 1);
	assert(returnedChange[0].playerId == 7);
	assert(returnedChange[0].epoch == 4);
	assert(manager.Authorize(7, concord, 4));

	const std::vector<PlayerScope> twoScopes{
		{ 7, concord },
		{ 9, sanctuary },
		{ 8, sanctuary }
	};
	const auto secondScope = manager.Reconcile(twoScopes);
	assert(secondScope.size() == 1);
	assert(secondScope[0].scope == sanctuary);
	assert(secondScope[0].playerId == 8);
	assert(secondScope[0].epoch == 1);

	const auto assignments = manager.Assignments();
	assert(assignments.size() == 2);
	assert(assignments[0].scope < assignments[1].scope);
	assert(manager.Authorize(8, sanctuary, 1));

	manager.Clear();
	assert(manager.Assignments().empty());
	assert(!manager.Get(concord).has_value());
	const auto afterClear = manager.Reconcile(returned);
	assert(afterClear.size() == 1);
	assert(afterClear[0].epoch == 1);

	Manager churn;
	const Scope held{ "0000AAAA", "0000BBBB" };
	const std::vector<PlayerScope> holder{ PlayerScope{ 1, held } };
	assert(churn.Reconcile(holder).size() == 1);
	for (std::uint32_t index = 0; index < MaximumTrackedEpochScopes + 512; ++index) {
		char cellId[9]{};
		std::snprintf(cellId, sizeof(cellId), "%08X", index + 0x00100000U);
		const std::vector<PlayerScope> visitor{
			PlayerScope{ 1, held },
			PlayerScope{ 2, Scope{ cellId, "0000BBBB" } }
		};
		(void)churn.Reconcile(visitor);
		(void)churn.Reconcile(holder);
	}
	assert(churn.TrackedEpochScopes() <= MaximumTrackedEpochScopes);
	assert(churn.Get(held).has_value());

	{
		using F4T::Authority::ElectSticky;
		const std::uint32_t poolA[]{ 5, 3, 7 };
		assert(ElectSticky(0, std::span<const std::uint32_t>(poolA)) == 3);
		assert(ElectSticky(7, std::span<const std::uint32_t>(poolA)) == 7);
		const std::uint32_t poolB[]{ 5, 7 };
		assert(ElectSticky(3, std::span<const std::uint32_t>(poolB)) == 5);
		assert(ElectSticky(4, std::span<const std::uint32_t>()) == 0);

		Manager sticky;
		const Scope market{ "0000D00D", "0000F00D" };

		const std::vector<PlayerScope> firstOwner{ { 6, market } };
		const auto owned = sticky.Reconcile(firstOwner);
		assert(owned.size() == 1 && owned[0].playerId == 6 && owned[0].epoch == 1);

		const std::vector<PlayerScope> lowerJoins{ { 6, market }, { 2, market } };
		assert(sticky.Reconcile(lowerJoins).empty());
		assert(sticky.Authorize(6, market, 1));
		assert(!sticky.Authorize(2, market, 1));

		const std::vector<PlayerScope> ownerLeft{ { 2, market }, { 9, market } };
		const auto handoff2 = sticky.Reconcile(ownerLeft);
		assert(handoff2.size() == 1);
		assert(handoff2[0].previousPlayerId == 6 && handoff2[0].playerId == 2 && handoff2[0].epoch == 2);
		assert(sticky.Authorize(2, market, 2));
		assert(!sticky.Authorize(6, market, 1));
	}

	return 0;
}
