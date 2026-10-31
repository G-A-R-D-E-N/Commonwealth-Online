#include "F4TServerWorldState.h"

#include <cassert>
#include <cmath>
#include <string>

int main()
{
	using namespace F4T::ServerWorldState;

	assert(NormalizeHhmm("0") == std::optional<std::string>{ "0000" });
	assert(NormalizeHhmm("600") == std::optional<std::string>{ "0600" });
	assert(NormalizeHhmm(" 1900 ") == std::optional<std::string>{ "1900" });
	assert(NormalizeHhmm("2359") == std::optional<std::string>{ "2359" });
	assert(!NormalizeHhmm("2400"));
	assert(!NormalizeHhmm("1260"));
	assert(!NormalizeHhmm("12:00"));
	assert(!NormalizeHhmm(""));

	const auto noon = HhmmToGameHour("1200");
	assert(noon && *noon == 12.0);
	const auto halfPast = HhmmToGameHour("630");
	assert(halfPast && std::abs(*halfPast - 6.5) < 0.000001);
	assert(!HhmmToGameHour("9999"));

	assert(NormalizeWeatherConsoleArg("0002b52a") == std::optional<std::string>{ "0002b52a" });
	assert(NormalizeWeatherConsoleArg("0x2B52A") == std::optional<std::string>{ "0002b52a" });
	assert(NormalizeWeatherConsoleArg(" FFFFFFFF ") == std::optional<std::string>{ "ffffffff" });
	assert(NormalizeWeatherConsoleArg("0") == std::optional<std::string>{ "00000000" });
	assert(!NormalizeWeatherConsoleArg("0x"));
	assert(!NormalizeWeatherConsoleArg("100000000"));
	assert(!NormalizeWeatherConsoleArg("weather"));

	assert(RelayWeatherFormId("0002b52a") == std::optional<std::string>{ "0002B52A" });
	assert(RelayWeatherFormId("0X1c3d5e") == std::optional<std::string>{ "001C3D5E" });

	return 0;
}
