#pragma once

#include "F4TServerBrowserData.h"
#include "PrismaUI_F4_API.h"

#include <nlohmann/json_fwd.hpp>

namespace F4T::ServerBrowser
{
	class ServerBrowserBridge
	{
	public:
		static void Initialize(PRISMA_UI_API::IVPrismaUI4* a_api, PrismaView a_view);
		static void Reactivate(PRISMA_UI_API::IVPrismaUI4* a_api, PrismaView a_view);
		static void Shutdown();
		static void OnBrowserShown();
		static void OnBrowserHidden();
		static void HandleAction(const nlohmann::json& a_action);
		static void ForwardControllerInput(const char* a_button, const char* a_phase);
		static void PushConnectionStatus();
		static void PushUITheme();
		static void PushSessionState(const char* a_stateJson);
		static void PushJoinProgress(const char* a_message);

		static void SetCurrentServer(const ServerEntry& a_entry);
		static void ClearCurrentServer();
		static void PushCurrentServerInfo();
	};
}
