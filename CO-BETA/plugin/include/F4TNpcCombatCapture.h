#pragma once

#include "F4TNpcSync.h"

#include "F4TNpcCoopHit.h"

#include <vector>

namespace F4T::NpcCombatCapture
{
	void Initialize();
	void Shutdown();

	std::vector<F4T::NpcCoop::NpcDamageClaim> GetAndClearPendingNpcDamage();
}
