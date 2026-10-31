#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <nlohmann/json.hpp>

#include <string>

namespace F4T::ServerLive
{
	enum class LiveApply
	{
		Applied,
		Unavailable,
		Refused
	};

	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::ServerLiveCallbacks* a_callbacks);
	[[nodiscard]] LiveApply ApplySettings(const nlohmann::json& a_request, nlohmann::json& a_result, std::string& a_error);
	[[nodiscard]] LiveApply QueryRunningConfig(nlohmann::json& a_result, std::string& a_error);
}
