#pragma once

#include "CommonwealthOnlineUI_API.h"

namespace F4T::ResourceViewHost
{
	void SetResourceMessageCallback(COMMONWEALTH_ONLINE_UI_API::ResourceMessageFn a_callback);
	void PushStateBag(const char* a_stateJson);
	bool OpenResourceView(const char* a_resource, const char* a_view);
	void CloseResourceView(const char* a_resource);
	void Tick();
}
