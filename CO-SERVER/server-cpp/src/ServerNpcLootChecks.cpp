#include "ServerNpcLootChecks.h"

#include <deque>

namespace CommonwealthOnline::Server::NpcLootChecks
{
	namespace
	{
		using SessionTypes::Session;
		using SessionTypes::kMaximumCombatPositionAgeSeconds;
		using SessionTypes::kMaximumNpcLootTakesPerWindow;
		using SessionTypes::kNpcLootTakeWindowSeconds;
		using F4T::NpcAuthority::Scope;
	}

	bool NpcLootTakeRateAllows(Session& a_session, double a_monotonicSeconds)
	{
		auto& recent = a_session.recentNpcLootTakes;
		std::erase_if(recent, [a_monotonicSeconds](double a_takenSeconds) {
			const auto age = a_monotonicSeconds - a_takenSeconds;
			return !(age >= 0.0) || age > kNpcLootTakeWindowSeconds;
		});
		return recent.size() < kMaximumNpcLootTakesPerWindow;
	}

	bool NpcLootTakerAllows(const Session& a_taker, const Scope& a_scope, double a_monotonicSeconds)
	{
		if (!a_taker.lastMovement || !a_taker.lastMovementMonotonic) {
			return false;
		}
		const auto age = a_monotonicSeconds - *a_taker.lastMovementMonotonic;
		return age >= 0.0 && age <= kMaximumCombatPositionAgeSeconds &&
			a_taker.lastMovement->cellId == a_scope.cellId &&
			a_taker.lastMovement->worldspaceId == a_scope.worldspaceId;
	}
}
