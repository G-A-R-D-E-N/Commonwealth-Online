#include "F4TF4SECapabilityPolicy.h"

namespace F4T::F4SECapabilities
{
	bool SupportsSaveFolderName(
		std::uint16_t a_major,
		std::uint16_t a_minor,
		std::uint16_t a_patch) noexcept
	{
		if (a_major != SaveFolderNameMinimumMajor) {
			return a_major > SaveFolderNameMinimumMajor;
		}
		if (a_minor != SaveFolderNameMinimumMinor) {
			return a_minor > SaveFolderNameMinimumMinor;
		}
		return a_patch >= SaveFolderNameMinimumPatch;
	}

	bool SupportsPermanentTasks(std::uint16_t a_f4seMajor, std::uint16_t a_f4seMinor) noexcept
	{
		if (a_f4seMajor != PermanentTaskMinimumMajor) {
			return a_f4seMajor > PermanentTaskMinimumMajor;
		}
		return a_f4seMinor >= PermanentTaskMinimumMinor;
	}
}
