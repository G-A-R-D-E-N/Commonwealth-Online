#pragma once

#include <filesystem>

#include <nlohmann/json_fwd.hpp>

namespace F4T::ServerSettings
{
	[[nodiscard]] std::filesystem::path HostServerDirectory();
	void HandleAction(const nlohmann::json& a_action);
}
