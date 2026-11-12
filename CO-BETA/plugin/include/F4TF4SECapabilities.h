#pragma once

#include "F4TF4SECapabilityPolicy.h"

#include <functional>
#include <string>

namespace F4T::F4SECapabilities
{
	[[nodiscard]] std::string GetSaveFolderName();
	void AddPermanentTask(std::function<void()> a_task);
}
