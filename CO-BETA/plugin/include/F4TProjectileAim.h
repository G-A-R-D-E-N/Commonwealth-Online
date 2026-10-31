#pragma once

#include "F4TShotReplay.h"

#include <nlohmann/json.hpp>

namespace F4T::ProjectileAim
{
	void Install();
	void Tick();
	void QueueAimForSend(const ShotReplay::Direction& a_direction);
	void HandleRelayedAimPacket(const nlohmann::json& a_packet);
	bool IsInstalled();
}
