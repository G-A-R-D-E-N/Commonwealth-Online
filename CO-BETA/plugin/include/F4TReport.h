#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace F4T::Report
{
	void Register();
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
}
