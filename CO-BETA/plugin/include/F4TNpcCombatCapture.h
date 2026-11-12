#pragma once

#include "F4TNpcSync.h"

#include "F4TNpcCoopHit.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace F4T::NpcCombatCapture
{
	void Initialize();
	void Shutdown();

	struct PendingDamage
	{
		F4T::NpcCoop::NpcDamageClaim claim;
		std::uint64_t sessionGeneration{};
		std::uint64_t connectionGeneration{};
		std::uint64_t policyRevision{};
		std::chrono::steady_clock::time_point capturedAt{};
	};

	std::vector<PendingDamage> GetAndClearPendingNpcDamage();
	void RetainPendingNpcDamage(std::span<const PendingDamage> a_unsent);
}
