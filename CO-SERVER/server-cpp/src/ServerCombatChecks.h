#pragma once

#include "ServerSessionTypes.h"

#include <cstdint>

namespace CommonwealthOnline::Server::CombatChecks
{
	void RecordCombatActions(SessionTypes::Session& a_session, const SessionTypes::Json& a_normalized, double a_monotonicSeconds);
	bool CombatWindowAllows(const SessionTypes::Session& a_session, double a_monotonicSeconds) noexcept;
	bool CombatActionAllows(
		SessionTypes::Session& a_session,
		const SessionTypes::Json& a_hit,
		double a_monotonicSeconds,
		std::uint32_t& a_actionType,
		SessionTypes::CombatActionState*& a_action);
	void RecordCombatHit(SessionTypes::Session& a_session, SessionTypes::CombatActionState& a_action, double a_monotonicSeconds) noexcept;
	bool CombatGeometryAllows(
		const SessionTypes::Session& a_attacker,
		const SessionTypes::Session& a_target,
		std::uint32_t a_actionType,
		double a_monotonicSeconds) noexcept;
}
