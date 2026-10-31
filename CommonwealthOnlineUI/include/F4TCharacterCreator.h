#pragma once

namespace F4T::CharacterCreator
{
	void Install();
	void Pump();
	bool IsOverlayUp();
	bool IsOverlayFocused();
	void DispatchControllerInput(const char* a_button, const char* a_phase);
}
