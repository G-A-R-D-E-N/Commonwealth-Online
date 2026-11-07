#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace CommonwealthOnline::Server::RuntimeSupport
{
	// constraint: these fields come from the wire, so no control byte may reach a log line.
	[[nodiscard]] inline std::string SanitizeLogField(std::string_view a_value)
	{
		if (a_value.empty()) {
			return "<none>";
		}
		std::string text;
		text.reserve(a_value.size());
		for (const char character : a_value) {
			const auto byte = static_cast<unsigned char>(character);
			text += byte < 0x20 || byte == 0x7F ? ' ' : character;
		}
		return text;
	}
}
