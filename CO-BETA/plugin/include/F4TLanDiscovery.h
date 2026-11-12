#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "F4TLanDiscoveryPorts.h"
#include "F4TRemotePlayerCapacity.h"

namespace F4T::LanDiscovery
{
	struct DiscoveredServer
	{
		std::string host;
		std::uint16_t port{ 7777 };
		std::string name;
		std::string description;
		int         players{ 0 };
		int         maxPlayers{ F4T::RemotePlayerCapacity::MaximumSessionPlayers };
		int         ping{ 0 };
		bool        passworded{ true };
	};

	std::vector<DiscoveredServer> Scan(
		std::string& a_error,
		std::uint16_t a_firstDiscoverPort = DefaultDiscoverPort,
		std::uint16_t a_lastDiscoverPort = LastScannedDiscoverPort);
}
