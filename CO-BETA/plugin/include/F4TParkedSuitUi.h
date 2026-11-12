#pragma once

#include <cstdint>

namespace F4T::ParkedSuitUi
{
	void Register();
	void Open();
	void Close();
	void NotifyStateChanged();
	void NotifyRemovalFailed(std::uint32_t a_refFormId);
	void NotifyHackAttempt(std::uint32_t a_refFormId, bool a_failed);
}
