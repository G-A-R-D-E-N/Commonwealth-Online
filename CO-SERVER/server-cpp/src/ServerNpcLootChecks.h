#pragma once

#include "F4TNpcAuthority.h"
#include "ServerSessionTypes.h"

namespace CommonwealthOnline::Server::NpcLootChecks
{
	bool NpcLootTakeRateAllows(SessionTypes::Session& a_session, double a_monotonicSeconds);
	bool NpcLootTakerAllows(const SessionTypes::Session& a_taker, const F4T::NpcAuthority::Scope& a_scope, double a_monotonicSeconds);
}
