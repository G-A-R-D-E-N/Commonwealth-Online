#pragma once

#include "AdminControlServer.h"
#include "BanStore.h"
#include "DurableStateFileStore.h"
#include "F4TWorldPolicy.h"
#include "OpenSslTokenService.h"
#include "ServerConfiguration.h"

#include "co_gns_server_bridge.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace CommonwealthOnline::Server
{
	struct ServerBootstrap
	{
		Configuration::Options options;
		OpenSslTokenService identityTokens;
		std::unique_ptr<DurableStateFileStore> durableStore;
		std::unique_ptr<BanStore> banStore;
		std::unique_ptr<AdminControlServer> admin;
		co_gns_server_handle server{};
		F4T::WorldPolicy::Data worldPolicyData;
		F4T::WorldPolicy::Mode worldMode{ F4T::WorldPolicy::Mode::CoopStory };
		std::filesystem::path baseDirectory;
		std::filesystem::path worldPolicyPath;

		[[nodiscard]] static std::optional<int> Prepare(int argc, char** argv, ServerBootstrap& a_out);
	};
}
