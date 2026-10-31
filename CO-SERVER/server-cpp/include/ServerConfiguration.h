#pragma once

#include "F4TServerConfiguration.h"

namespace CommonwealthOnline::Server::Configuration
{
	using F4T::ServerConfiguration::IsAllowedDirectoryUrl;
	using F4T::ServerConfiguration::Options;
	using F4T::ServerConfiguration::Validate;

	[[nodiscard]] Options Parse(const nlohmann::json& a_root, const std::filesystem::path& a_baseDirectory = {});
	[[nodiscard]] F4T::Identity::Requirements IdentityRequirements(const Options& a_options);
}
