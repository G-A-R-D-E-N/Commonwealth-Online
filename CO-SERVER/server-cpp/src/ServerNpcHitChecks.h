#pragma once

#include "ServerSessionTypes.h"

#include <cstdint>
#include <string>

namespace CommonwealthOnline::Server::NpcHitChecks
{
	bool NpcDamageRateAllows(SessionTypes::Session& a_session, std::uint32_t a_npcId, std::uint32_t a_targetPlayerId, double a_monotonicSeconds);
	bool NpcGeometryAllows(
		const SessionTypes::Session& a_attacker,
		double a_npcX,
		double a_npcY,
		double a_npcZ,
		const std::string& a_npcCellId,
		const std::string& a_npcWorldspaceId,
		std::uint32_t a_actionType,
		double a_monotonicSeconds) noexcept;
}
