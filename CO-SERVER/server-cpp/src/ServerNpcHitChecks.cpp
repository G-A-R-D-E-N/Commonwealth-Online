#include "ServerNpcHitChecks.h"
#include "ServerScopeKeys.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace CommonwealthOnline::Server::NpcHitChecks
{
	namespace
	{
		using SessionTypes::NpcDamageRecord;
		using SessionTypes::Session;
		using SessionTypes::kMaximumCombatPositionAgeSeconds;
		using SessionTypes::kMaximumMeleeHitDistance;
		using SessionTypes::kMaximumNpcDamagePerTargetWindow;
		using SessionTypes::kMaximumNpcDamagePerWindow;
		using SessionTypes::kMaximumNpcDamageSumPerWindow;
		using SessionTypes::kMaximumNpcLootTakeDistance;
		using SessionTypes::kMaximumRangedHitDistance;
		using SessionTypes::kMaximumUnarmedHitDistance;
		using SessionTypes::kNpcDamageWindowSeconds;
		using SessionTypes::kNpcLootTakeGeometryType;
	}

	bool NpcDamageRateAllows(
		Session& a_session,
		std::uint32_t a_npcId,
		std::uint32_t a_targetPlayerId,
		double a_monotonicSeconds,
		double a_damage)
	{
		auto& recent = a_session.recentNpcDamage;
		std::erase_if(recent, [a_monotonicSeconds](const NpcDamageRecord& a_record) {
			const auto age = a_monotonicSeconds - a_record.monotonicSeconds;
			return !(age >= 0.0) || age > kNpcDamageWindowSeconds;
		});
		if (recent.size() >= kMaximumNpcDamagePerWindow) {
			return false;
		}
		const auto sameTarget = std::count_if(recent.begin(), recent.end(), [a_npcId, a_targetPlayerId](const NpcDamageRecord& a_record) {
			return a_record.npcId == a_npcId && a_record.targetPlayerId == a_targetPlayerId;
		});
		if (static_cast<std::size_t>(sameTarget) >= kMaximumNpcDamagePerTargetWindow) {
			return false;
		}
		double summed = a_damage;
		for (const auto& record : recent) {
			if (record.targetPlayerId == a_targetPlayerId) {
				summed += record.damage;
			}
		}
		return summed <= kMaximumNpcDamageSumPerWindow;
	}

	bool NpcGeometryAllows(
		const Session& a_attacker,
		double a_npcX,
		double a_npcY,
		double a_npcZ,
		const std::string& a_npcCellId,
		const std::string& a_npcWorldspaceId,
		std::uint32_t a_actionType,
		double a_monotonicSeconds) noexcept
	{
		if (!a_attacker.lastMovement || !a_attacker.lastMovementMonotonic) {
			return false;
		}
		const auto age = a_monotonicSeconds - *a_attacker.lastMovementMonotonic;
		if (!(age >= 0.0) || age > kMaximumCombatPositionAgeSeconds) {
			return false;
		}
		const auto& attackerPosition = *a_attacker.lastMovement;
		const auto attackerScope = ScopeKeys::MovementScope(attackerPosition);
		if (attackerScope.worldspaceId.empty() || a_npcWorldspaceId.empty()) {
			if (attackerScope.worldspaceId != a_npcWorldspaceId ||
				attackerScope.cellId != a_npcCellId) {
				return false;
			}
		} else if (attackerScope.worldspaceId != a_npcWorldspaceId) {
			return false;
		}
		const auto dx = attackerPosition.x - a_npcX;
		const auto dy = attackerPosition.y - a_npcY;
		const auto dz = attackerPosition.z - a_npcZ;
		const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (!std::isfinite(distance)) {
			return false;
		}
		const auto maximumDistance =
			a_actionType == kNpcLootTakeGeometryType ? kMaximumNpcLootTakeDistance :
			a_actionType == 2U ? kMaximumUnarmedHitDistance :
			a_actionType == 1U ? kMaximumMeleeHitDistance :
			                     kMaximumRangedHitDistance;
		return distance <= maximumDistance;
	}
}
