#include "pch.h"

#include "F4TVoiceUi.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TNetworking.h"

#include <atomic>

namespace F4T::VoiceUi
{
	namespace
	{
		std::atomic<ActionHandler> g_actionHandler{ nullptr };
		std::atomic<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI9*> g_ui{ nullptr };

		void Callback_SendVoiceAction(const char* a_actionJson)
		{
			if (!a_actionJson) {
				return;
			}
			try {
				if (const auto handler = g_actionHandler.load()) {
					handler(a_actionJson);
				}
			} catch (...) {
				REX::WARN(
					"{} [UIClient] Dropped a voice action after an internal error.",
					F4T::Networking::GetLocalPlayerLogPrefix());
			}
		}

		const COMMONWEALTH_ONLINE_UI_API::VoiceCallbacks kVoiceCallbacks{
			Callback_SendVoiceAction
		};
	}

	void SetActionHandler(ActionHandler a_handler)
	{
		g_actionHandler.store(a_handler);
	}

	void Register()
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<
			COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI9>();
		g_ui.store(ui);
		if (!ui) {
			REX::WARN(
				"{} [UIClient] CommonwealthOnlineUI.dll lacks voice interface V9 (IVCommonwealthOnlineUI9).",
				F4T::Networking::GetLocalPlayerLogPrefix());
			return;
		}
		if (ui->SetVoiceCallbacks) {
			ui->SetVoiceCallbacks(&kVoiceCallbacks);
		}
	}

	bool PushEvent(const char* a_eventJson)
	{
		const auto* ui = g_ui.load();
		if (!a_eventJson || !ui || !ui->PushVoiceEvent) {
			return false;
		}
		ui->PushVoiceEvent(a_eventJson);
		return true;
	}
}
