#include "pch.h"
#include "F4TUIClient.h"
#include "CommonwealthOnlineUI_API.h"

namespace F4T::UIClient
{
	std::string GetMultiplayerDisplayName()
	{
		static COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI8* api = nullptr;
		if (!api) {
			api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
				COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI8>();
		}
		char name[129]{};
		if (api && api->GetMultiplayerDisplayName && api->GetMultiplayerDisplayName(name, sizeof(name))) {
			return name;
		}
		return {};
	}
}
