#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <string>
#include <string_view>

namespace F4T::FactionView
{
	void Initialize();
	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::FactionCallbacks* a_callbacks);
	void Open(std::string_view a_reason);
	void Close(std::string_view a_reason);
	void QueueClose(std::string_view a_reason);
	void NotifyStateChanged();
	bool IsVisible();
	bool IsCurrentlyVisible();
	void DispatchControllerInput(const char* a_button, const char* a_phase);
	void InvokeScript(const std::string& a_script);
}
