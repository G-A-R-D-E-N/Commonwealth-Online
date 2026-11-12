#pragma once

#include <string>
#include <string_view>

namespace F4T::LocalServer
{
	inline std::string SelectLastLogLine(std::string_view a_text)
	{
		std::string last;
		std::string line;
		const auto take = [&] {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (!line.empty()) {
				last = line;
			}
			line.clear();
		};
		for (const char character : a_text) {
			if (character == '\n') {
				take();
				continue;
			}
			line.push_back(character);
		}
		take();
		return last.size() > 300 ? last.substr(0, 300) : last;
	}
}
