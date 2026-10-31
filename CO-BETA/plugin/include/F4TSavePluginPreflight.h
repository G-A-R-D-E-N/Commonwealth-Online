#pragma once

#include <string>
#include <string_view>

namespace F4T::SavePluginPreflight
{
	[[nodiscard]] bool AllowSaveLoad(std::string_view a_saveFile, std::string& a_error);
}
