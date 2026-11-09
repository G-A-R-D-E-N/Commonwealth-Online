#pragma once

#include <string>
#include <string_view>

namespace F4T::Settings
{
	template <typename TStates>
	[[nodiscard]] bool IsSettingWritable(const TStates& a_states, std::string_view a_id)
	{
		const auto it = a_states.find(std::string(a_id));
		return it != a_states.end() && it->second.available;
	}
}
