#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace F4T::MultiplayerProfile
{
	std::string GetDisplayName();
	bool CopyDisplayName(char* a_buffer, std::size_t a_capacity);
	bool SaveDisplayName(std::string_view a_name, std::string& a_error);
#ifdef COUI_PROFILE_TESTING
	void SetStorePathForTesting(const std::string& a_path);
#endif
}
