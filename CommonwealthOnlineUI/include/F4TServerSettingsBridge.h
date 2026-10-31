#pragma once

#include <nlohmann/json_fwd.hpp>

namespace F4T::ServerSettings
{
	void HandleAction(const nlohmann::json& a_action);
}
