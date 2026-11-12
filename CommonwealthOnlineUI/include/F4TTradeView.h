#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <string_view>

namespace F4T::TradeView
{
	void Initialize();
	void SetActionCallback(COMMONWEALTH_ONLINE_UI_API::TradeActionFn a_callback);
	void NotifyStateChanged(const char* a_stateJson);
	void Close(std::string_view a_reason);
	void SyncControllerActions();
	void DispatchControllerInput(const char* a_button, const char* a_phase);
	bool IsCurrentlyVisible();
	void ForwardControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY);
}
