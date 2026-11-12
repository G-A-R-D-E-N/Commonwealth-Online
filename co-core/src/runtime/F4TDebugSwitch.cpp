#include "F4TDebugSwitch.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <string>

namespace
{
	std::atomic_bool g_debugEnabled{ F4T::DebugSwitch::BetaDefaultEnabled };

	std::string Normalize(std::string_view a_value)
	{
		std::string value{ a_value };
		value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));
		value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
		}).base(), value.end());
		std::ranges::transform(value, value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}
}

namespace F4T::DebugSwitch
{
	std::optional<bool> ParseValue(std::string_view a_value)
	{
		const auto value = Normalize(a_value);
		if (value == "1" || value == "true" || value == "yes" || value == "on" || value == "debug" || value == "enabled") {
			return true;
		}
		if (value == "0" || value == "false" || value == "no" || value == "off" || value == "release" || value == "disabled") {
			return false;
		}
		return std::nullopt;
	}

	bool ResolveValue(const char* a_environmentValue)
	{
		if (!a_environmentValue || a_environmentValue[0] == '\0') {
			return BetaDefaultEnabled;
		}
		return ParseValue(a_environmentValue).value_or(BetaDefaultEnabled);
	}

	void InitializeFromEnvironment()
	{
		SetEnabled(ResolveValue(std::getenv(EnvironmentVariable.data())));
	}

	void SetEnabled(bool a_enabled)
	{
		g_debugEnabled.store(a_enabled, std::memory_order_relaxed);
	}

	bool IsEnabled()
	{
		return g_debugEnabled.load(std::memory_order_relaxed);
	}
}
