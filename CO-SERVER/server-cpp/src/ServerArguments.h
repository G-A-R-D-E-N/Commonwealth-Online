#pragma once

#include "ServerConfiguration.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace CommonwealthOnline::Server::Arguments
{
	[[nodiscard]] bool ParsePort(std::string_view a_value, std::uint16_t& a_port);
	[[nodiscard]] bool ParseMaximumPlayers(std::string_view a_value, std::size_t& a_maximumPlayers);
	[[nodiscard]] bool ParseResumeLifetimeHours(std::string_view a_value, std::uint32_t& a_hours);
	[[nodiscard]] bool ParseResumeLifetimeSeconds(std::string_view a_value, std::optional<std::uint32_t>& a_seconds);
	[[nodiscard]] CommonwealthOnline::Server::Configuration::Options LoadConfiguration(const std::filesystem::path& a_path);
}
