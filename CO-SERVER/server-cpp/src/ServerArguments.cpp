#include "ServerArguments.h"

#include "F4TRemotePlayerCapacity.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server::Arguments
{
	bool ParsePort(std::string_view a_value, std::uint16_t& a_port)
	{
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size() || parsed < 1 || parsed > 65535) {
			return false;
		}
		a_port = static_cast<std::uint16_t>(parsed);
		return true;
	}

	bool ParseMaximumPlayers(std::string_view a_value, std::size_t& a_maximumPlayers)
	{
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size() || parsed < 1 || parsed > F4T::RemotePlayerCapacity::MaximumSessionPlayers) {
			return false;
		}
		a_maximumPlayers = parsed;
		return true;
	}

	bool ParseResumeLifetimeHours(std::string_view a_value, std::uint32_t& a_hours)
	{
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size() || parsed < 1 || parsed > 8760) {
			return false;
		}
		a_hours = parsed;
		return true;
	}

	bool ParseResumeLifetimeSeconds(std::string_view a_value, std::optional<std::uint32_t>& a_seconds)
	{
		constexpr std::uint32_t MaximumSeconds = 8760u * 60u * 60u;
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(a_value.data(), a_value.data() + a_value.size(), parsed);
		if (result.ec != std::errc{} || result.ptr != a_value.data() + a_value.size() || parsed < 1 || parsed > MaximumSeconds) {
			return false;
		}
		a_seconds = parsed;
		return true;
	}

	CommonwealthOnline::Server::Configuration::Options LoadConfiguration(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Could not open server configuration '" + a_path.string() + "'.");
		}
		nlohmann::json root;
		try {
			input >> root;
		} catch (const nlohmann::json::exception& exception) {
			throw std::runtime_error("Server configuration is not valid JSON: " + std::string{ exception.what() });
		}
		return CommonwealthOnline::Server::Configuration::Parse(root, a_path.parent_path());
	}
}
