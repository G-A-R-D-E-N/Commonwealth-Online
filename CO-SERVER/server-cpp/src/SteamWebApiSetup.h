#pragma once

#include <optional>

namespace CommonwealthOnline::Server
{
	[[nodiscard]] std::optional<int> RunSteamKeySetupIfRequested(int argc, char** argv);
}
