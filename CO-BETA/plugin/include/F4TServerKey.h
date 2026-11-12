#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::ServerProfileKey
{
	std::string NormalizeHost(std::string_view a_host);
	bool TryParseServerKey(std::string_view a_serverKey, std::string& a_host, std::uint16_t& a_port);
	bool ServerKeyMatches(const std::string& a_existingKey, const std::string& a_host, std::uint16_t a_port);
	bool IsSteamGuestJoinId(std::string_view a_joinId);
	std::string SteamGuestProfileHost(std::string_view a_joinId);
}
