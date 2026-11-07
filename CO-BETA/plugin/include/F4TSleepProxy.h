#pragma once

#include <cstdint>

namespace F4T::SleepProxy
{
	bool OwnsMovement(std::uint32_t a_actorFormId);
	void Tick();
	void OnProxyRemoved(std::uint32_t a_playerId);
}
