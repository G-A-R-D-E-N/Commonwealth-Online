#include "F4TServerWorldState.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ServerWorldState
{
	namespace
	{
		std::string Trim(std::string_view a_value)
		{
			std::size_t begin = 0;
			std::size_t end = a_value.size();
			while (begin < end && std::isspace(static_cast<unsigned char>(a_value[begin])) != 0) {
				++begin;
			}
			while (end > begin && std::isspace(static_cast<unsigned char>(a_value[end - 1])) != 0) {
				--end;
			}
			return std::string{ a_value.substr(begin, end - begin) };
		}

		std::optional<std::uint32_t> ParseWeather(std::string_view a_value)
		{
			auto text = Trim(a_value);
			if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
				text.erase(0, 2);
			}
			if (text.empty() || text.size() > 8) {
				return std::nullopt;
			}
			std::uint32_t parsed = 0;
			const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 16);
			if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
				return std::nullopt;
			}
			return parsed;
		}

		std::string FormatWeather(std::uint32_t a_value, bool a_uppercase)
		{
			char buffer[9]{};
			std::snprintf(buffer, sizeof(buffer), a_uppercase ? "%08X" : "%08x", a_value);
			return buffer;
		}
	}

	std::optional<std::string> NormalizeHhmm(std::string_view a_value)
	{
		auto text = Trim(a_value);
		if (text.empty() || text.size() > 4 ||
			!std::all_of(text.begin(), text.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) {
			return std::nullopt;
		}
		text.insert(text.begin(), 4 - text.size(), '0');
		const auto hours = static_cast<unsigned int>((text[0] - '0') * 10 + (text[1] - '0'));
		const auto minutes = static_cast<unsigned int>((text[2] - '0') * 10 + (text[3] - '0'));
		if (hours > 23 || minutes > 59) {
			return std::nullopt;
		}
		return text;
	}

	std::optional<double> HhmmToGameHour(std::string_view a_value)
	{
		const auto normalized = NormalizeHhmm(a_value);
		if (!normalized) {
			return std::nullopt;
		}
		const auto hours = static_cast<unsigned int>(((*normalized)[0] - '0') * 10 + ((*normalized)[1] - '0'));
		const auto minutes = static_cast<unsigned int>(((*normalized)[2] - '0') * 10 + ((*normalized)[3] - '0'));
		return static_cast<double>(hours) + static_cast<double>(minutes) / 60.0;
	}

	std::optional<std::string> NormalizeWeatherConsoleArg(std::string_view a_value)
	{
		const auto parsed = ParseWeather(a_value);
		return parsed ? std::optional{ FormatWeather(*parsed, false) } : std::nullopt;
	}

	std::optional<std::string> RelayWeatherFormId(std::string_view a_value)
	{
		const auto parsed = ParseWeather(a_value);
		return parsed ? std::optional{ FormatWeather(*parsed, true) } : std::nullopt;
	}
}
