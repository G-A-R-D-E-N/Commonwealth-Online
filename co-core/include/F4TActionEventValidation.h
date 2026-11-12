#pragma once

#include <nlohmann/json.hpp>

namespace F4T::ActionEventValidation
{
	[[nodiscard]] nlohmann::json NormalizeActionEvents(const nlohmann::json& a_value, bool a_strict);
}
