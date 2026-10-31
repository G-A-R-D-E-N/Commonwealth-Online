#include "ServerNpcHitChecks.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

using namespace CommonwealthOnline::Server::NpcHitChecks;
using namespace CommonwealthOnline::Server::SessionTypes;

namespace
{
	Session PlacedAttacker(double a_x, double a_monotonicSeconds, const char* a_cellId, const char* a_worldspaceId)
	{
		Session session;
		session.lastMovement = StoredMovementState{ a_x, 0.0, 0.0, 0.0, a_cellId, a_worldspaceId };
		session.lastMovementMonotonic = a_monotonicSeconds;
		return session;
	}

	void TestNpcDamageRateAllowsBelowTotalCap()
	{
		Session session;
		for (std::size_t i = 0; i + 1 < kMaximumNpcDamagePerWindow; ++i) {
			session.recentNpcDamage.push_back(NpcDamageRecord{ 10.0, static_cast<std::uint32_t>(i), 99U });
		}
		assert(NpcDamageRateAllows(session, 1000U, 2000U, 10.5));
		assert(session.recentNpcDamage.size() == kMaximumNpcDamagePerWindow - 1);
	}

	void TestNpcDamageRateAllowsRejectsAtTotalCap()
	{
		Session session;
		for (std::size_t i = 0; i < kMaximumNpcDamagePerWindow; ++i) {
			session.recentNpcDamage.push_back(NpcDamageRecord{ 10.0, static_cast<std::uint32_t>(i), 99U });
		}
		assert(!NpcDamageRateAllows(session, 1000U, 2000U, 10.5));
		assert(session.recentNpcDamage.size() == kMaximumNpcDamagePerWindow);
	}

	void TestNpcDamageRatePrunesStaleAndFutureRecords()
	{
		Session session;
		session.recentNpcDamage.assign(kMaximumNpcDamagePerWindow, NpcDamageRecord{ 8.0, 1U, 2U });
		assert(NpcDamageRateAllows(session, 3U, 4U, 10.0));
		assert(session.recentNpcDamage.empty());

		session.recentNpcDamage = { NpcDamageRecord{ 9.5, 1U, 2U }, NpcDamageRecord{ 10.5, 1U, 2U } };
		assert(NpcDamageRateAllows(session, 3U, 4U, 10.0));
		assert(session.recentNpcDamage.size() == 1);
		assert(session.recentNpcDamage.front().monotonicSeconds == 9.5);
	}

	void TestNpcDamageRatePruneKeepsRecordAtWindowEdge()
	{
		Session session;
		session.recentNpcDamage = {
			NpcDamageRecord{ 10.0 - kNpcDamageWindowSeconds, 1U, 2U },
			NpcDamageRecord{ 10.0 - kNpcDamageWindowSeconds - 0.25, 1U, 2U },
		};
		assert(NpcDamageRateAllows(session, 3U, 4U, 10.0));
		assert(session.recentNpcDamage.size() == 1);
		assert(session.recentNpcDamage.front().monotonicSeconds == 10.0 - kNpcDamageWindowSeconds);
	}

	void TestNpcDamageRateRejectsPerTargetCapWhileOtherPairsAllowed()
	{
		Session session;
		session.recentNpcDamage.assign(kMaximumNpcDamagePerTargetWindow, NpcDamageRecord{ 10.0, 7U, 9U });
		assert(!NpcDamageRateAllows(session, 7U, 9U, 10.0));
		assert(session.recentNpcDamage.size() == kMaximumNpcDamagePerTargetWindow);
		assert(NpcDamageRateAllows(session, 7U, 8U, 10.0));
		assert(NpcDamageRateAllows(session, 8U, 9U, 10.0));
	}

	void TestNpcGeometryAllowsRejectsMissingOrStaleMovement()
	{
		Session noMovement;
		noMovement.lastMovementMonotonic = 10.0;
		assert(!NpcGeometryAllows(noMovement, 0.0, 0.0, 0.0, "cell", "worldspace", 1U, 10.0));
		Session withoutStamp = PlacedAttacker(0.0, 10.0, "cell", "worldspace");
		withoutStamp.lastMovementMonotonic.reset();
		assert(!NpcGeometryAllows(withoutStamp, 0.0, 0.0, 0.0, "cell", "worldspace", 1U, 10.0));

		const Session attacker = PlacedAttacker(0.0, 10.0, "cell", "worldspace");
		assert(NpcGeometryAllows(attacker, 0.0, 0.0, 0.0, "cell", "worldspace", 1U, 10.0));
		assert(NpcGeometryAllows(attacker, 0.0, 0.0, 0.0, "cell", "worldspace", 1U, 10.0 + kMaximumCombatPositionAgeSeconds));
		assert(!NpcGeometryAllows(attacker, 0.0, 0.0, 0.0, "cell", "worldspace", 1U, 10.0 + kMaximumCombatPositionAgeSeconds + 0.25));
		assert(!NpcGeometryAllows(attacker, 0.0, 0.0, 0.0, "cell", "worldspace", 1U, 9.0));
	}

	void TestNpcGeometryAllowsWorldspaceAndCellRules()
	{
		const Session attacker = PlacedAttacker(0.0, 10.0, "cell", "worldspace");
		assert(!NpcGeometryAllows(attacker, 0.0, 0.0, 0.0, "cell", "elsewhere", 1U, 10.0));
		assert(!NpcGeometryAllows(attacker, 0.0, 0.0, 0.0, "cell", "", 1U, 10.0));
		assert(NpcGeometryAllows(attacker, 0.0, 0.0, 0.0, "otherCell", "worldspace", 1U, 10.0));

		const Session unnamed = PlacedAttacker(0.0, 10.0, "cell", "");
		assert(NpcGeometryAllows(unnamed, 0.0, 0.0, 0.0, "cell", "", 1U, 10.0));
		assert(!NpcGeometryAllows(unnamed, 0.0, 0.0, 0.0, "otherCell", "", 1U, 10.0));
		assert(!NpcGeometryAllows(unnamed, 0.0, 0.0, 0.0, "cell", "worldspace", 1U, 10.0));
	}

	void TestNpcGeometryAllowsDistanceByActionType()
	{
		struct Case
		{
			std::uint32_t actionType;
			double maximumDistance;
		};
		const Case cases[] = {
			{ 1U, kMaximumMeleeHitDistance },
			{ 2U, kMaximumUnarmedHitDistance },
			{ 3U, kMaximumRangedHitDistance },
			{ kNpcLootTakeGeometryType, kMaximumNpcLootTakeDistance },
		};
		for (const auto& testCase : cases) {
			assert(NpcGeometryAllows(PlacedAttacker(testCase.maximumDistance - 1.0, 10.0, "cell", "worldspace"), 0.0, 0.0, 0.0, "cell", "worldspace", testCase.actionType, 10.0));
			assert(NpcGeometryAllows(PlacedAttacker(testCase.maximumDistance, 10.0, "cell", "worldspace"), 0.0, 0.0, 0.0, "cell", "worldspace", testCase.actionType, 10.0));
			assert(!NpcGeometryAllows(PlacedAttacker(testCase.maximumDistance + 1.0, 10.0, "cell", "worldspace"), 0.0, 0.0, 0.0, "cell", "worldspace", testCase.actionType, 10.0));
		}
	}

	void TestNpcGeometryAllowsRejectsNonFiniteCoordinates()
	{
		const double nan = std::numeric_limits<double>::quiet_NaN();
		const double infinity = std::numeric_limits<double>::infinity();
		const Session attacker = PlacedAttacker(0.0, 10.0, "cell", "worldspace");
		assert(!NpcGeometryAllows(attacker, nan, 0.0, 0.0, "cell", "worldspace", 1U, 10.0));
		assert(!NpcGeometryAllows(attacker, infinity, 0.0, 0.0, "cell", "worldspace", 1U, 10.0));
		assert(!NpcGeometryAllows(attacker, 0.0, 0.0, nan, "cell", "worldspace", 3U, 10.0));
		assert(!NpcGeometryAllows(PlacedAttacker(nan, 10.0, "cell", "worldspace"), 0.0, 0.0, 0.0, "cell", "worldspace", 1U, 10.0));
	}
}

int main()
{
	TestNpcDamageRateAllowsBelowTotalCap();
	TestNpcDamageRateAllowsRejectsAtTotalCap();
	TestNpcDamageRatePrunesStaleAndFutureRecords();
	TestNpcDamageRatePruneKeepsRecordAtWindowEdge();
	TestNpcDamageRateRejectsPerTargetCapWhileOtherPairsAllowed();
	TestNpcGeometryAllowsRejectsMissingOrStaleMovement();
	TestNpcGeometryAllowsWorldspaceAndCellRules();
	TestNpcGeometryAllowsDistanceByActionType();
	TestNpcGeometryAllowsRejectsNonFiniteCoordinates();
	return 0;
}
