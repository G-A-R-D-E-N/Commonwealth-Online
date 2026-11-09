#pragma once

#include <filesystem>
#include <string>

namespace F4T::UserPaths
{
	[[nodiscard]] std::string EnvironmentValue(const char* a_name);
	[[nodiscard]] std::filesystem::path CommonwealthOnlineDocumentsDirectory();
	[[nodiscard]] std::filesystem::path GameExecutableDirectory();
	[[nodiscard]] std::filesystem::path GameDataDirectory();
	[[nodiscard]] std::filesystem::path ModuleDataDirectory();
	[[nodiscard]] std::filesystem::path PackagedServerDirectory();
}
