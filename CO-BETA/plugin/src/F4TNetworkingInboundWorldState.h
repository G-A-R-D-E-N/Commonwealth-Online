#pragma once

#include <nlohmann/json.hpp>

namespace F4T::Networking::Detail
{
	void HandleWorldStatePacket(const nlohmann::json& a_packet);
	void HandleWorldStateHostPacket(const nlohmann::json& a_packet);
}
