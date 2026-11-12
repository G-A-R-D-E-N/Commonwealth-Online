// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#pragma once

#include <string>
#include <string_view>

namespace F4T::LogRedaction
{
	[[nodiscard]] std::string RedactLog(std::string_view a_text);
	[[nodiscard]] std::string ComposeSharedLog(std::string_view a_previousRaw, std::string_view a_currentRaw);
}
