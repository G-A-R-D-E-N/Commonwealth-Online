#include "F4TF4SECapabilities.h"

#include "pch.h"

#include "F4TDebugLogging.h"
#include "F4TF4SEPermanentTask.h"

namespace F4T::F4SECapabilities
{
	std::string GetSaveFolderName()
	{
		const auto version = F4SE::GetF4SEVersion();
		if (SupportsSaveFolderName(version.major(), version.minor(), version.patch())) {
			return std::string{ F4SE::GetSaveFolderName() };
		}
		F4T::DebugLogging::Write(
			"F4SECapabilities",
			"F4SE runtime predates GetSaveFolderName; using the default save folder name.");
		return std::string{ "Fallout4" };
	}

	void AddPermanentTask(std::function<void()> a_task)
	{
		SchedulePermanentTask(F4SE::GetTaskInterface(), F4SE::GetF4SEVersion(), std::move(a_task));
	}
}
