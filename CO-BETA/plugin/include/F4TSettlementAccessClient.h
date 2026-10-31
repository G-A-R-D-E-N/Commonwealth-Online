#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace F4T::SettlementAccessClient
{
	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();
	bool HandleAction(std::string_view a_actionJson);
	std::string WithBuildAccess(std::string a_stateJson);
	std::map<std::uint64_t, std::string> OwnersByBuildZone();
}
