#pragma once

#include "ServerSessionTypes.h"

#include <cstdint>

namespace CommonwealthOnline::Server::CombatChecks
{
	inline constexpr float kMaximumCombatDamageClaim = 1000.0F;
	inline constexpr float kMaximumUnarmedCombatDamageClaim = 75.0F;

	float ClampCombatDamageClaim(float a_claimedDamage, float a_maximumDamage = kMaximumCombatDamageClaim) noexcept;
	float CapForAction(std::uint32_t a_actionType) noexcept;
	void RecordCombatActions(SessionTypes::Session& a_session, const SessionTypes::Json& a_normalized, double a_monotonicSeconds);
	bool CombatWindowAllows(const SessionTypes::Session& a_session, double a_monotonicSeconds) noexcept;
	bool CombatActionAllows(
		SessionTypes::Session& a_session,
		const SessionTypes::Json& a_hit,
		double a_monotonicSeconds,
		std::uint32_t& a_actionType,
		SessionTypes::CombatActionState*& a_action);
	void RecordCombatHit(SessionTypes::Session& a_session, SessionTypes::CombatActionState& a_action, double a_monotonicSeconds) noexcept;
	void RecordAcceptedHit(SessionTypes::Session& a_session, SessionTypes::AcceptedHitRecord a_record, double a_monotonicSeconds);
	[[nodiscard]] SessionTypes::AcceptedHitRecord* FindAcceptedHit(
		SessionTypes::Session& a_session,
		std::uint32_t a_attackSequence,
		std::uint32_t a_sequence,
		double a_monotonicSeconds) noexcept;
	bool CombatGeometryAllows(
		const SessionTypes::Session& a_attacker,
		const SessionTypes::Session& a_target,
		std::uint32_t a_actionType,
		double a_monotonicSeconds) noexcept;
}
