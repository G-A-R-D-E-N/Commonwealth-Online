#include "ServerNpcLootChecks.h"

#include <cassert>
#include <cstddef>

using namespace CommonwealthOnline::Server::NpcLootChecks;
using namespace CommonwealthOnline::Server::SessionTypes;
using F4T::NpcAuthority::Scope;

namespace
{
	void TestNpcLootTakeRateAllows()
	{
		Session session;
		assert(NpcLootTakeRateAllows(session, 10.0));
		for (std::size_t i = 0; i + 1 < kMaximumNpcLootTakesPerWindow; ++i) {
			session.recentNpcLootTakes.push_back(10.0);
		}
		assert(NpcLootTakeRateAllows(session, 10.5));
		session.recentNpcLootTakes.push_back(10.0);
		assert(!NpcLootTakeRateAllows(session, 10.5));
		assert(session.recentNpcLootTakes.size() == kMaximumNpcLootTakesPerWindow);
	}

	void TestNpcLootTakeRatePrunesStaleAndFutureEntries()
	{
		Session session;
		session.recentNpcLootTakes.assign(kMaximumNpcLootTakesPerWindow, 8.0);
		assert(NpcLootTakeRateAllows(session, 10.0));
		assert(session.recentNpcLootTakes.empty());

		session.recentNpcLootTakes = { 9.5, 10.5 };
		assert(NpcLootTakeRateAllows(session, 10.0 + kNpcLootTakeWindowSeconds));
		assert(session.recentNpcLootTakes.size() == 1);
		assert(session.recentNpcLootTakes.front() == 10.5);

		session.recentNpcLootTakes.assign(kMaximumNpcLootTakesPerWindow, 10.0 - kNpcLootTakeWindowSeconds);
		assert(!NpcLootTakeRateAllows(session, 10.0));
		assert(session.recentNpcLootTakes.size() == kMaximumNpcLootTakesPerWindow);

		session.recentNpcLootTakes = { 20.0 };
		assert(NpcLootTakeRateAllows(session, 10.0));
		assert(session.recentNpcLootTakes.empty());
	}

	Session PlacedTaker(double a_monotonicSeconds, const char* a_cellId, const char* a_worldspaceId)
	{
		Session session;
		session.lastMovement = StoredMovementState{ 0.0, 0.0, 0.0, 0.0, a_cellId, a_worldspaceId };
		session.lastMovementMonotonic = a_monotonicSeconds;
		return session;
	}

	void TestNpcLootTakerAllows()
	{
		const Session taker = PlacedTaker(10.0, "cell", "worldspace");
		const Scope scope{ "cell", "worldspace" };
		assert(NpcLootTakerAllows(taker, scope, 10.0));
		assert(NpcLootTakerAllows(taker, scope, 10.0 + kMaximumCombatPositionAgeSeconds));
		assert(!NpcLootTakerAllows(taker, scope, 10.0 + kMaximumCombatPositionAgeSeconds + 0.25));
		assert(!NpcLootTakerAllows(taker, scope, 9.0));
		const Session unplaced;
		assert(!NpcLootTakerAllows(unplaced, scope, 10.0));
		Session withoutStamp = PlacedTaker(10.0, "cell", "worldspace");
		withoutStamp.lastMovementMonotonic.reset();
		assert(!NpcLootTakerAllows(withoutStamp, scope, 10.0));
		assert(!NpcLootTakerAllows(taker, Scope{ "otherCell", "worldspace" }, 10.0));
		assert(!NpcLootTakerAllows(taker, Scope{ "cell", "elsewhere" }, 10.0));
	}
}

int main()
{
	TestNpcLootTakeRateAllows();
	TestNpcLootTakeRatePrunesStaleAndFutureEntries();
	TestNpcLootTakerAllows();
	return 0;
}
