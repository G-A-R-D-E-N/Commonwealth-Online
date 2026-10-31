#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <string_view>

namespace F4T::QuestView
{
	void Initialize();
	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::QuestCallbacks* a_callbacks);
	void Open(std::string_view a_reason);
	void Close(std::string_view a_reason);
	void QueueClose(std::string_view a_reason);
	void NotifyStateChanged();
	bool IsVisible();
	bool IsCurrentlyVisible();
	void DispatchControllerInput(const char* a_button, const char* a_phase);
}
