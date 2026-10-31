#include "F4TDebugLogging.h"

#include <algorithm>
#include <cstdio>

namespace F4T::DebugLogging
{
	void Initialize() {}
	void SetEnabled(bool) {}
	bool IsEnabled() { return true; }

	std::string SanitizeLogText(std::string_view a_text, std::size_t a_maxLength)
	{
		std::string sanitized;
		sanitized.reserve((std::min)(a_text.size(), a_maxLength) + 3);
		for (std::size_t index = 0; index < a_text.size() && index < a_maxLength; ++index) {
			const auto value = static_cast<unsigned char>(a_text[index]);
			sanitized.push_back(value >= 0x20 && value != 0x7F ? static_cast<char>(value) : ' ');
		}
		if (a_text.size() > a_maxLength) {
			sanitized += "...";
		}
		return sanitized;
	}

	void Write(std::string_view a_component, std::string_view a_message)
	{
		std::fprintf(stderr, "[%.*s] %.*s\n",
			static_cast<int>(a_component.size()), a_component.data(),
			static_cast<int>(a_message.size()), a_message.data());
	}
}
