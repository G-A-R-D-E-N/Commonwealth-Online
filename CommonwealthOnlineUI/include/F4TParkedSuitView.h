#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <string_view>

namespace F4T::ParkedSuitView
{
	void Initialize();
	void EnsureView();
	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::ParkedSuitCallbacks* a_callbacks);
	void Open(std::string_view a_reason);
	void QueueClose(std::string_view a_reason);
	void NotifyStateChanged();
	void SyncControllerActions();
	[[nodiscard]] bool IsVisible();
	[[nodiscard]] bool IsCurrentlyVisible();
}
