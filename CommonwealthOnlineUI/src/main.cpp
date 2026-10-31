#include "pch.h"

#include "F4TScaleformUI.h"
#include "F4TMainMenuInject.h"
#include "F4TPrismaUI.h"
#include "F4TServerBrowserBridge.h"
#include "SessionCallbacks.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_message)
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

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	REX::INFO("{} loaded", COUI::Session::GetLogPrefix());

	F4T::ScaleformUI::RegisterPlugin();

	const auto messaging = F4SE::GetMessagingInterface();
	if (messaging) {
		messaging->RegisterListener(MessageHandler);
	}

	return true;
}
