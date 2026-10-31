#pragma once

#include "F4TSteamTunnel.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace F4T::SteamHostLink
{
	struct LinkEndpoint
	{
		std::uint16_t port{};
		std::string token;
	};

	[[nodiscard]] std::optional<LinkEndpoint> ReadEndpoint(const std::filesystem::path& a_directory, std::uint16_t a_gamePort);
	[[nodiscard]] bool Send(const LinkEndpoint& a_endpoint, nlohmann::json a_request, nlohmann::json* a_reply = nullptr);
	[[nodiscard]] std::optional<nlohmann::json> Request(const LinkEndpoint& a_endpoint, nlohmann::json a_request);
	[[nodiscard]] std::optional<SteamAdmission::HostPolicy> ReadHostPolicy(const LinkEndpoint& a_endpoint);
	[[nodiscard]] Transport::SteamIdentityBinder MakeIdentityBinder(
		const std::filesystem::path& a_directory,
		std::uint16_t a_gamePort,
		std::uint64_t a_hostSteamId);
	[[nodiscard]] Transport::SteamIdentityBinder MakeDefaultIdentityBinder(std::uint16_t a_gamePort, std::uint64_t a_hostSteamId);
}
