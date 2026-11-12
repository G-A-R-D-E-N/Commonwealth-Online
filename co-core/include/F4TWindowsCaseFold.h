#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace F4T::WindowsCaseFold
{
	[[nodiscard]] std::optional<std::string> Key(std::string_view a_text);
}
