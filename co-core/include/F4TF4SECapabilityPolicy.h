#pragma once

#include <cstdint>

namespace F4T::F4SECapabilities
{
	inline constexpr std::uint16_t SaveFolderNameMinimumMajor = 0;
	inline constexpr std::uint16_t SaveFolderNameMinimumMinor = 7;
	inline constexpr std::uint16_t SaveFolderNameMinimumPatch = 1;
	inline constexpr std::uint16_t PermanentTaskMinimumMajor = 0;
	inline constexpr std::uint16_t PermanentTaskMinimumMinor = 7;

	[[nodiscard]] bool SupportsSaveFolderName(
		std::uint16_t a_major,
		std::uint16_t a_minor,
		std::uint16_t a_patch) noexcept;
	[[nodiscard]] bool SupportsPermanentTasks(std::uint16_t a_f4seMajor, std::uint16_t a_f4seMinor) noexcept;
}
