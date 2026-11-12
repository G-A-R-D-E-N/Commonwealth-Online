#pragma once

#include <filesystem>

namespace F4T::GamePaths
{
	// Constraint: the result is the F4SE folder F4TCrashForensics writes into.
	[[nodiscard]] bool ResolveF4SEFolder(std::filesystem::path& a_out);
}
