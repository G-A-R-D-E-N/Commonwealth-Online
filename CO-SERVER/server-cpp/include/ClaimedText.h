#pragma once

#include <string_view>

namespace CommonwealthOnline::Server
{
	[[nodiscard]] bool IsSafeClaimedText(std::string_view a_text) noexcept;
}
