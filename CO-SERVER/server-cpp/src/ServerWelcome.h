#pragma once

#include <nlohmann/json.hpp>

namespace CommonwealthOnline::Server
{
	nlohmann::json BaseWelcomeCapabilities(bool a_componentSnapshots);
}
