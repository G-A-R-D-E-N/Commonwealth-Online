#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <string_view>

namespace F4T::InfoViews
{
	enum class Kind
	{
		Server,
		Help,
		Report
	};

	void Initialize();
	void Open(Kind a_kind, std::string_view a_reason);
	void Close(std::string_view a_reason);
	void SyncControllerActions();
	void QueueClose(std::string_view a_reason);
	void NotifyServerInfo(std::string_view a_json);
	void NotifyWorldMode(std::string_view a_json);
	void SetReportCallbacks(const COMMONWEALTH_ONLINE_UI_API::ReportCallbacks* a_callbacks);
	void PushReportEvent(const char* a_json);
	bool IsVisible();
	bool IsCurrentlyVisible();
	void ForwardControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY);
	void DispatchControllerInput(const char* a_button, const char* a_phase);
}
