#include "pch.h"

#include "F4TPrismaUI.h"
#include "SessionCallbacks.h"

#include <string_view>

namespace F4T::PauseMenu
{
	namespace
	{
		std::string GetLogPrefix()
		{
			return std::string{COUI::Session::GetLogPrefix()} + " [PauseMenuBridge]";
		}

		void LogInfo(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::INFO(std::string_view{ message });
		}
	}

	namespace PauseMenuBridge
	{
		void ForwardControllerInput(const char* a_button, const char* a_phase)
		{
			if (!a_button || !a_phase) {
				return;
			}

			F4T::PrismaUI::DispatchPauseMenuControllerInput(a_button, a_phase);
		}

		void ForwardControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY)
		{
			F4T::PrismaUI::DispatchPauseMenuControllerAxes(a_leftX, a_leftY, a_rightX, a_rightY);
		}

		void Initialize()
		{
			LogInfo("Pause menu bridge initialized.");
		}
	}
}
