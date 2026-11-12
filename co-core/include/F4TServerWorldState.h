#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ServerWorldState
{
	enum class HostWeatherRelay : std::uint8_t
	{
		Relay,
		Strip
	};

	[[nodiscard]] std::optional<std::string> NormalizeHhmm(std::string_view a_value);
	[[nodiscard]] std::optional<double> HhmmToGameHour(std::string_view a_value);
	[[nodiscard]] std::optional<std::string> NormalizeWeatherConsoleArg(std::string_view a_value);
	[[nodiscard]] std::optional<std::string> RelayWeatherFormId(std::string_view a_value);
	[[nodiscard]] HostWeatherRelay DecideHostWeatherRelay(
		const std::optional<std::string>& a_lockedWeatherFormId,
		const std::optional<std::string>& a_hostWeatherFormId);

	enum class WeatherCommand : std::uint8_t
	{
		Lock,
		Clear,
		Invalid
	};

	[[nodiscard]] WeatherCommand ClassifyWeatherCommand(std::string_view a_value);
}
