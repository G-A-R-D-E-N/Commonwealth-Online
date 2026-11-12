#pragma once

namespace F4T::PauseMenu
{
	namespace PauseMenuBridge
	{
		void ForwardControllerInput(const char* a_button, const char* a_phase);
		void ForwardControllerAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY);
		void Initialize();
	}
}
