#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace F4T::ServerWorldState
{
	[[nodiscard]] std::optional<std::string> NormalizeHhmm(std::string_view a_value);
	[[nodiscard]] std::optional<double> HhmmToGameHour(std::string_view a_value);
	[[nodiscard]] std::optional<std::string> NormalizeWeatherConsoleArg(std::string_view a_value);
	[[nodiscard]] std::optional<std::string> RelayWeatherFormId(std::string_view a_value);
}
