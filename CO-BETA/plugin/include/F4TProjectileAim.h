#pragma once

#include "F4TShotReplay.h"

#include <nlohmann/json.hpp>

namespace F4T::ProjectileAim
{
	void Install();
	void FlushOutbound();
	void DiscardOutbound();
	void QueueAimForSend(const ShotReplay::Direction& a_direction);
	void HandleRelayedAimPacket(const nlohmann::json& a_packet);
	bool IsInstalled();
}
