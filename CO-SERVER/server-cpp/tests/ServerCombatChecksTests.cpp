#include "ServerCombatChecks.h"

#include <cassert>

using namespace CommonwealthOnline::Server::CombatChecks;
using namespace CommonwealthOnline::Server::SessionTypes;

namespace
{
	void TestCombatWindowAllows()
	{
		Session session;
		assert(CombatWindowAllows(session, 10.0));
		session.combatWindowStartMonotonic = 10.0;
		session.combatHitsInWindow = kMaximumCombatHitsPerWindow - 1;
		assert(CombatWindowAllows(session, 10.5));
		session.combatHitsInWindow = kMaximumCombatHitsPerWindow;
		assert(!CombatWindowAllows(session, 10.5));
		assert(!CombatWindowAllows(session, 10.0 + kCombatHitWindowSeconds));
		assert(CombatWindowAllows(session, 10.0 + kCombatHitWindowSeconds + 0.25));
		assert(CombatWindowAllows(session, 9.5));
	}

	void TestRecordCombatHit()
	{
		Session session;
		CombatActionState action{ 7, 1, 0.0, 0 };
		RecordCombatHit(session, action, 5.0);
		assert(session.combatWindowStartMonotonic && *session.combatWindowStartMonotonic == 5.0);
		assert(session.combatHitsInWindow == 1 && action.acceptedHits == 1);
		RecordCombatHit(session, action, 5.5);
		assert(*session.combatWindowStartMonotonic == 5.0);
		assert(session.combatHitsInWindow == 2 && action.acceptedHits == 2);
		RecordCombatHit(session, action, 5.0 + kCombatHitWindowSeconds + 0.5);
		assert(*session.combatWindowStartMonotonic == 6.5);
		assert(session.combatHitsInWindow == 1 && action.acceptedHits == 3);
	}

	Json ActionEvents(std::uint32_t a_sequence, std::uint32_t a_type)
	{
		return Json{ { "actionEvents", Json::array({ Json{ { "sequence", a_sequence }, { "type", a_type } } }) } };
	}

	void TestRecordCombatActions()
	{
		Session session;
		RecordCombatActions(session, ActionEvents(5, 1), 1.0);
		assert(session.combatActions.size() == 1 && session.lastActionSequenceSeen == 5);
		RecordCombatActions(session, ActionEvents(5, 1), 1.5);
		assert(session.combatActions.size() == 1);
		RecordCombatActions(session, ActionEvents(6, 9), 2.0);
		assert(session.combatActions.empty());
	}

	void TestCombatActionAllows()
	{
		Session session;
		session.lastDurablePlayerState = Json{ { "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "form" "Id", "0001F66F" } } }) } };
		session.combatActions.push_back(CombatActionState{ 7, 1, 1.0, 0 });
		const Json hit{ { "attackSequence", 7 }, { "weaponForm" "Id", "0001F66F" } };
		std::uint32_t type = 0;
		CombatActionState* action = nullptr;
		assert(CombatActionAllows(session, hit, 1.0 + kCombatActionLifetimeSeconds, type, action));
		assert(action && type == 1);
		action->acceptedHits = kMaximumMeleeHitsPerAction;
		assert(!CombatActionAllows(session, hit, 1.5, type, action));
		action->acceptedHits = kMaximumMeleeHitsPerAction - 1;
		assert(CombatActionAllows(session, hit, 1.5, type, action));
		assert(!CombatActionAllows(session, Json{ { "attackSequence", 8 } }, 1.5, type, action));
		assert(!CombatActionAllows(session, hit, 1.0 + kCombatActionLifetimeSeconds + 0.25, type, action));
	}

	Session PlacedSession(double a_x, double a_monotonicSeconds, const char* a_worldspaceId)
	{
		Session session;
		session.lastMovement = StoredMovementState{ a_x, 0.0, 0.0, 0.0, "cell", a_worldspaceId };
		session.lastMovementMonotonic = a_monotonicSeconds;
		return session;
	}

	void TestCombatGeometryAllows()
	{
		const Session attacker = PlacedSession(0.0, 10.0, "worldspace");
		const Session meleeInRange = PlacedSession(kMaximumMeleeHitDistance - 1.0, 10.0, "worldspace");
		const Session meleeOutOfRange = PlacedSession(kMaximumMeleeHitDistance + 1.0, 10.0, "worldspace");
		assert(CombatGeometryAllows(attacker, meleeInRange, 1, 10.0));
		assert(!CombatGeometryAllows(attacker, meleeOutOfRange, 1, 10.0));
		const Session rangedInRange = PlacedSession(kMaximumRangedHitDistance - 1.0, 10.0, "worldspace");
		const Session rangedOutOfRange = PlacedSession(kMaximumRangedHitDistance + 1.0, 10.0, "worldspace");
		assert(CombatGeometryAllows(attacker, rangedInRange, 3, 10.0));
		assert(!CombatGeometryAllows(attacker, rangedOutOfRange, 3, 10.0));
		const Session otherWorldspace = PlacedSession(1.0, 10.0, "elsewhere");
		assert(!CombatGeometryAllows(attacker, otherWorldspace, 1, 10.0));
		assert(!CombatGeometryAllows(attacker, meleeInRange, 1, 10.0 + kMaximumCombatPositionAgeSeconds + 0.25));
	}

	Session ArmedSession(const char* a_rightHandFormId, std::uint32_t a_type)
	{
		Session session;
		session.lastDurablePlayerState = Json{ { "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "form" "Id", a_rightHandFormId } } }) } };
		session.combatActions.push_back(CombatActionState{ 7, a_type, 1.0, 0 });
		return session;
	}

	void TestCombatActionAllowsUnarmedAndRanged()
	{
		std::uint32_t type = 0;
		CombatActionState* action = nullptr;
		const Json noWeapon{ { "attackSequence", 7 } };
		const Json nullWeapon{ { "attackSequence", 7 }, { "weaponForm" "Id", "00000000" } };
		const Json weapon{ { "attackSequence", 7 }, { "weaponForm" "Id", "0001F66F" } };
		const Json otherWeapon{ { "attackSequence", 7 }, { "weaponForm" "Id", "0002A111" } };

		Session unarmed = ArmedSession("", 2);
		assert(CombatActionAllows(unarmed, noWeapon, 1.5, type, action) && type == 2);
		assert(CombatActionAllows(unarmed, nullWeapon, 1.5, type, action));
		assert(!CombatActionAllows(unarmed, weapon, 1.5, type, action));
		action->acceptedHits = kMaximumUnarmedHitsPerAction;
		assert(!CombatActionAllows(unarmed, noWeapon, 1.5, type, action));
		Session unarmedButHolding = ArmedSession("0001F66F", 2);
		assert(!CombatActionAllows(unarmedButHolding, noWeapon, 1.5, type, action));

		Session ranged = ArmedSession("0001F66F", 3);
		assert(CombatActionAllows(ranged, weapon, 1.5, type, action) && type == 3);
		assert(!CombatActionAllows(ranged, otherWeapon, 1.5, type, action));
		assert(!CombatActionAllows(ranged, noWeapon, 1.5, type, action));
		assert(!CombatActionAllows(ranged, nullWeapon, 1.5, type, action));
		action->acceptedHits = kMaximumRangedHitsPerAction;
		assert(!CombatActionAllows(ranged, weapon, 1.5, type, action));
		Session rangedEmptyHand = ArmedSession("", 3);
		assert(!CombatActionAllows(rangedEmptyHand, weapon, 1.5, type, action));

		Session noRightHand;
		noRightHand.combatActions.push_back(CombatActionState{ 7, 1, 1.0, 0 });
		assert(!CombatActionAllows(noRightHand, weapon, 1.5, type, action));
		noRightHand.lastDurablePlayerState = Json{ { "equippedItems", Json::array() } };
		assert(!CombatActionAllows(noRightHand, weapon, 1.5, type, action));
	}

	Session CellSession(const char* a_cellId, const char* a_worldspaceId)
	{
		Session session;
		session.lastMovement = StoredMovementState{ 0.0, 0.0, 0.0, 0.0, a_cellId, a_worldspaceId };
		session.lastMovementMonotonic = 10.0;
		return session;
	}

	void TestCombatGeometryAllowsCellRules()
	{
		const Session interiorA = CellSession("cellA", "");
		assert(CombatGeometryAllows(interiorA, CellSession("cellA", ""), 1, 10.0));
		assert(!CombatGeometryAllows(interiorA, CellSession("cellB", ""), 1, 10.0));
		assert(!CombatGeometryAllows(interiorA, CellSession("cellA", "worldspace"), 1, 10.0));
		const Session exteriorA = CellSession("cellA", "worldspace");
		assert(CombatGeometryAllows(exteriorA, CellSession("cellB", "worldspace"), 1, 10.0));
		assert(!CombatGeometryAllows(exteriorA, CellSession("cellA", ""), 1, 10.0));
	}
}

int main()
{
	TestCombatWindowAllows();
	TestRecordCombatHit();
	TestRecordCombatActions();
	TestCombatActionAllows();
	TestCombatActionAllowsUnarmedAndRanged();
	TestCombatGeometryAllows();
	TestCombatGeometryAllowsCellRules();
	return 0;
}
