#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace F4T::PlayerKey
{
	[[nodiscard]] std::string Derive(std::string_view a_serverSecret, std::string_view a_steamId, std::string_view a_profileId);

	[[nodiscard]] inline bool IsPlayerKey(std::string_view a_key)
	{
		return a_key.size() == 64 && std::all_of(a_key.begin(), a_key.end(), [](char a_char) {
			return (a_char >= '0' && a_char <= '9') || (a_char >= 'a' && a_char <= 'f');
		});
	}
}
