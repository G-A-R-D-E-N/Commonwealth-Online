#include "pch.h"

#include "F4TMainMenuInject.h"
#include "F4TPrismaUI.h"
#include "SessionCallbacks.h"

#include "Scaleform/G/GFx_Movie.h"
#include "Scaleform/G/GFx_Value.h"

#include <string_view>

namespace F4T::MainMenuVanillaSettings
{
	namespace
	{
		bool g_vanillaSettingsFromCustomMainMenu = false;

		std::string GetLogPrefix()
		{
			return std::string{COUI::Session::GetLogPrefix()} + " [MainMenuVanillaSettings]";
		}

		void LogInfo(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::INFO(std::string_view{ message });
		}

		void LogWarning(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::WARN(std::string_view{ message });
		}
	}

	void OpenGameSettings()
	{
		LogInfo("openGameSettings called — superseded by F4T::Settings native settings UI.");
	}

	void Initialize()
	{
		LogInfo("Main menu vanilla settings module initialized (stubs only).");
	}
}
