#include "pch.h"

#include "F4TUiPlugin.h"

#include "F4TScaleformUI.h"
#include "F4TMainMenuInject.h"
#include "F4TPrismaUI.h"
#include "F4TServerBrowserBridge.h"
#include "SessionCallbacks.h"

namespace F4T::UiPlugin
{
	void OnLoad()
	{
		REX::INFO("{} loaded", COUI::Session::GetLogPrefix());
		F4T::ScaleformUI::RegisterPlugin();
	}

	void OnMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		switch (a_message->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			if (RE::UI::GetSingleton() && F4SE::GetTaskInterface()) {
				F4T::MainMenuInject::Install();
				F4T::PrismaUI::PrepareMenuRuntime();
			} else {
				REX::WARN("{} Menu runtime deferred to GameDataReady: UI or task interface unavailable at PostPostLoad", COUI::Session::GetLogPrefix());
			}
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			F4T::ServerBrowser::ServerBrowserBridge::Shutdown();
			break;
		case F4SE::MessagingInterface::kGameLoaded:
			REX::INFO("{} Game loaded", COUI::Session::GetLogPrefix());
			break;
		case F4SE::MessagingInterface::kPostSaveGame:
			F4T::PrismaUI::NotifyPostSaveGame();
			break;
		default:
			break;
		}
	}
}
