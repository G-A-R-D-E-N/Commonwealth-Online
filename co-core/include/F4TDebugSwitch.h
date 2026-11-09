#pragma once

#include <optional>
#include <string_view>

namespace F4T::DebugSwitch
{
	inline constexpr std::string_view EnvironmentVariable = "COMMONWEALTH_ONLINE_DEBUG";
	inline constexpr bool BetaDefaultEnabled = true;

	std::optional<bool> ParseValue(std::string_view a_value);
	bool ResolveValue(const char* a_environmentValue);
	void InitializeFromEnvironment();
	void SetEnabled(bool a_enabled);
	bool IsEnabled();
}
