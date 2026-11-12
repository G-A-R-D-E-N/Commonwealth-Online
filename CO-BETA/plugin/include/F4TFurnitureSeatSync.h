#pragma once

#include <nlohmann/json.hpp>

namespace F4T::FurnitureSeatSync
{
	void PublishLocal();
	void Tick();
	void HandleRelayedPacket(const nlohmann::json& a_packet);
}
