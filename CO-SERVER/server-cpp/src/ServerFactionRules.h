#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server::FactionRules
{
	constexpr std::size_t kMaximumFactionListEntries = 64;
	constexpr std::uint32_t kFactionVitalsHealthStep = 5;
	constexpr double kFactionVitalsPushSeconds = 1.0;
	constexpr std::size_t kMaximumProfileIdLength = 128;
	constexpr std::uint32_t kMaximumFactionNumber = 999999999;

	inline std::string LowerAscii(std::string_view a_value)
	{
		std::string lower{ a_value };
		for (auto& ch : lower) {
			if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
		}
		return lower;
	}

	inline std::string FactionNameKey(std::string_view a_name)
	{
		auto key = LowerAscii(a_name);
		std::erase_if(key, [](char a_ch) { return a_ch == ' ' || a_ch == '-' || a_ch == '\''; });
		return key;
	}

	inline std::uint32_t FactionNumber(const std::string& a_factionId)
	{
		const auto digits = a_factionId.starts_with("f-") ? a_factionId.substr(2) : std::string{};
		if (digits.empty() || digits.size() > 9 || digits.front() == '0' || !std::all_of(digits.begin(), digits.end(), [](char a_ch) { return a_ch >= '0' && a_ch <= '9'; })) return 0;
		return static_cast<std::uint32_t>(std::stoul(digits));
	}

	inline bool ProfileIdValid(const std::string& a_profileId)
	{
		return !a_profileId.empty() && a_profileId.size() <= kMaximumProfileIdLength;
	}
}
