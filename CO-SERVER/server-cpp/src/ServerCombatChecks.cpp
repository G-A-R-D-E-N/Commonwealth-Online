#include "ServerCombatChecks.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "ServerPacketRead.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace CommonwealthOnline::Server::CombatChecks
{
	namespace
	{
		using SessionTypes::CombatActionState;
		using SessionTypes::Json;
		using SessionTypes::Session;
		using SessionTypes::kCombatActionLifetimeSeconds;
		using SessionTypes::kCombatHitWindowSeconds;
		using SessionTypes::kMaximumCombatHitsPerWindow;
		using SessionTypes::kMaximumCombatPositionAgeSeconds;
		using SessionTypes::kMaximumMeleeHitDistance;
		using SessionTypes::kMaximumMeleeHitsPerAction;
		using SessionTypes::kMaximumRangedHitDistance;
		using SessionTypes::kMaximumRangedHitsPerAction;
		using SessionTypes::kMaximumRecentCombatActions;
		using SessionTypes::kMaximumUnarmedHitDistance;
		using SessionTypes::kMaximumUnarmedHitsPerAction;

		void PruneCombatActions(Session& a_session, double a_monotonicSeconds)
		{
			while (!a_session.combatActions.empty()) {
				const auto age = a_monotonicSeconds - a_session.combatActions.front().monotonicSeconds;
				if (age >= 0.0 && age <= kCombatActionLifetimeSeconds) {
					break;
				}
				a_session.combatActions.pop_front();
			}
			while (a_session.combatActions.size() > kMaximumRecentCombatActions) {
				a_session.combatActions.pop_front();
			}
		}

		std::optional<std::string> RightHandWeaponFormId(const Session& a_session)
		{
			if (!a_session.lastDurablePlayerState) {
				return std::nullopt;
			}
			const auto items = a_session.lastDurablePlayerState->find("equippedItems");
			if (items == a_session.lastDurablePlayerState->end() || !items->is_array()) {
				return std::nullopt;
			}
			for (const auto& item : *items) {
				if (!item.is_object() || ReadString(item, "slot").value_or("") != "rightHand") {
					continue;
				}
				return ReadString(item, "form" "Id").value_or("");
			}
			return std::nullopt;
		}
	}

	void RecordCombatActions(Session& a_session, const Json& a_normalized, double a_monotonicSeconds)
	{
		PruneCombatActions(a_session, a_monotonicSeconds);
		const auto actions = a_normalized.find("actionEvents");
		if (actions == a_normalized.end() || !actions->is_array()) {
			return;
		}
		for (const auto& action : *actions) {
			const auto sequence = ReadUInt32(action, "sequence").value_or(0);
			const auto type = ReadUInt32(action, "type").value_or(0);
			if (sequence == 0 || type == 0 ||
				!F4T::GnsSnapshotEnvelope::IsNewer(sequence, a_session.lastActionSequenceSeen)) {
				continue;
			}
			a_session.lastActionSequenceSeen = sequence;
			if (type == 1U || type == 2U || type == 3U) {
				a_session.combatActions.push_back(CombatActionState{ sequence, type, a_monotonicSeconds, 0 });
				while (a_session.combatActions.size() > kMaximumRecentCombatActions) {
					a_session.combatActions.pop_front();
				}
			} else {
				a_session.combatActions.clear();
			}
		}
	}

	bool CombatWindowAllows(const Session& a_session, double a_monotonicSeconds) noexcept
	{
		if (!a_session.combatWindowStartMonotonic) {
			return true;
		}
		const auto sinceWindow = a_monotonicSeconds - *a_session.combatWindowStartMonotonic;
		if (!(sinceWindow >= 0.0) || sinceWindow > kCombatHitWindowSeconds) {
			return true;
		}
		return a_session.combatHitsInWindow < kMaximumCombatHitsPerWindow;
	}

	bool CombatActionAllows(
		Session& a_session,
		const Json& a_hit,
		double a_monotonicSeconds,
		std::uint32_t& a_actionType,
		CombatActionState*& a_action)
	{
		PruneCombatActions(a_session, a_monotonicSeconds);
		const auto claimedAttack = ReadUInt32(a_hit, "attackSequence").value_or(0);
		if (claimedAttack == 0) {
			return false;
		}
		const auto found = std::find_if(
			a_session.combatActions.begin(),
			a_session.combatActions.end(),
			[claimedAttack](const CombatActionState& action) { return action.sequence == claimedAttack; });
		if (found == a_session.combatActions.end()) {
			return false;
		}
		const auto age = a_monotonicSeconds - found->monotonicSeconds;
		if (!(age >= 0.0) || age > kCombatActionLifetimeSeconds) {
			return false;
		}
		const auto rightHand = RightHandWeaponFormId(a_session);
		if (!rightHand) {
			return false;
		}
		const auto claimedWeapon = ReadString(a_hit, "weaponForm" "Id");
		a_actionType = found->type;
		a_action = std::addressof(*found);
		if (a_actionType == 2U) {
			if (found->acceptedHits >= kMaximumUnarmedHitsPerAction || !rightHand->empty()) {
				return false;
			}
			return !claimedWeapon || *claimedWeapon == "00000000";
		}
		if (a_actionType == 3U) {
			if (found->acceptedHits >= kMaximumRangedHitsPerAction || rightHand->empty()) {
				return false;
			}
			return claimedWeapon && *claimedWeapon != "00000000" && *claimedWeapon == *rightHand;
		}
		if (a_actionType == 1U) {
			if (found->acceptedHits >= kMaximumMeleeHitsPerAction || rightHand->empty()) {
				return false;
			}
			return claimedWeapon && *claimedWeapon != "00000000" && *claimedWeapon == *rightHand;
		}
		return false;
	}

	void RecordCombatHit(Session& a_session, CombatActionState& a_action, double a_monotonicSeconds) noexcept
	{
		if (!a_session.combatWindowStartMonotonic ||
			a_monotonicSeconds < *a_session.combatWindowStartMonotonic ||
			a_monotonicSeconds - *a_session.combatWindowStartMonotonic > kCombatHitWindowSeconds) {
			a_session.combatWindowStartMonotonic = a_monotonicSeconds;
			a_session.combatHitsInWindow = 0;
		}
		++a_session.combatHitsInWindow;
		++a_action.acceptedHits;
	}

	bool CombatGeometryAllows(
		const Session& a_attacker,
		const Session& a_target,
		std::uint32_t a_actionType,
		double a_monotonicSeconds) noexcept
	{
		if (!a_attacker.lastMovement || !a_target.lastMovement ||
			!a_attacker.lastMovementMonotonic || !a_target.lastMovementMonotonic) {
			return false;
		}
		for (const auto stamp : { *a_attacker.lastMovementMonotonic, *a_target.lastMovementMonotonic }) {
			const auto age = a_monotonicSeconds - stamp;
			if (!(age >= 0.0) || age > kMaximumCombatPositionAgeSeconds) {
				return false;
			}
		}
		const auto& attackerPosition = *a_attacker.lastMovement;
		const auto& targetPosition = *a_target.lastMovement;
		if (attackerPosition.worldspaceId.empty() || targetPosition.worldspaceId.empty()) {
			if (attackerPosition.worldspaceId != targetPosition.worldspaceId ||
				attackerPosition.cellId != targetPosition.cellId) {
				return false;
			}
		} else if (attackerPosition.worldspaceId != targetPosition.worldspaceId) {
			return false;
		}
		const auto dx = attackerPosition.x - targetPosition.x;
		const auto dy = attackerPosition.y - targetPosition.y;
		const auto dz = attackerPosition.z - targetPosition.z;
		const auto distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (!std::isfinite(distance)) {
			return false;
		}
		const auto maximumDistance =
			a_actionType == 2U ? kMaximumUnarmedHitDistance :
			a_actionType == 1U ? kMaximumMeleeHitDistance :
			                     kMaximumRangedHitDistance;
		return distance <= maximumDistance;
	}
}
