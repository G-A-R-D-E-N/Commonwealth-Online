#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace F4T::LanDiscovery
{
	struct DiscoveredServer
	{
		std::string host;
		std::uint16_t port{ 7777 };
		std::string name;
		std::string description;
		int         players{ 0 };
		int         maxPlayers{ 16 };
		int         ping{ 0 };
	};

	std::vector<DiscoveredServer> Scan(std::string& a_error);
}
