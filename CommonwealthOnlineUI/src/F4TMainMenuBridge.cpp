#include "pch.h"

#include "F4TPrismaUI.h"
#include "SessionCallbacks.h"

#include <string_view>

namespace F4T::MainMenu
{
	namespace
	{
		std::string GetLogPrefix()
		{
			return std::string{COUI::Session::GetLogPrefix()} + " [MainMenuBridge]";
		}

		void LogInfo(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::INFO(std::string_view{ message });
		}
	}

	namespace MainMenuBridge
	{
		void ForwardControllerInput(const char* a_button, const char* a_phase)
		{
			if (!a_button || !a_phase) {
				return;
			}

			F4T::PrismaUI::DispatchMainMenuControllerInput(a_button, a_phase);
		}

		void Initialize()
		{
			LogInfo("Main menu bridge initialized.");
		}
	}
}
