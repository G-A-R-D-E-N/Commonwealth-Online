#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace F4T::Admin
{
	void Attach();
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();
	std::string BuildStateJson();
	void HandleAction(std::string_view a_actionJson);
}
