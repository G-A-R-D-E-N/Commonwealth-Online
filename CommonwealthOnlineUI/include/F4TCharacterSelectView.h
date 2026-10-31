#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <string_view>

namespace F4T::CharacterSelectView
{
	void Initialize();
	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::FactionCallbacks* a_callbacks);
	void NotifyStateChanged();
	void Tick();
	bool IsVisible();
	bool IsCurrentlyVisible();
	void Close(std::string_view a_reason);
}
