#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::LocalServer
{
	[[nodiscard]] std::string EnsureRunning(std::string_view a_name, std::uint32_t a_maxPlayers, std::uint16_t a_port);
}
