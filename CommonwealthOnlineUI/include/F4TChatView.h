#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <string_view>

namespace F4T::ChatView
{
	void Initialize();
	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::ChatCallbacks* a_callbacks);
	void PushEvent(const char* a_json);
	void Open(std::string_view a_reason);
	void Close(std::string_view a_reason);
	bool IsOpen();
	void ReleaseInputFocus();
	void Tick();
}
