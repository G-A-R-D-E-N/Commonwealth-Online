#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace F4T::SettlementAccessClient
{
	struct Workbench
	{
		float hp{ 2000.0F };
		float maxHp{ 2000.0F };
		bool broken{};
		std::uint64_t protectedUntilUnix{};
		std::uint64_t ownerProtectedUntilUnix{};
	};

	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void Reset();
	bool HandleAction(std::string_view a_actionJson);
	std::string WithBuildAccess(std::string a_stateJson);
	std::map<std::uint64_t, std::string> OwnersByBuildZone();
	std::map<std::uint64_t, Workbench> WorkbenchByBuildZone();
	std::set<std::uint64_t> BuildZonesAllowedFor(std::string_view a_profileId);
}
