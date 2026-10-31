#pragma once

namespace RE
{
	class ButtonEvent;
	class ThumbstickEvent;
}

namespace F4T::MenuInput
{
	void Register();
	void SetBrowserActive(bool a_active);
	void SetMainMenuActive(bool a_active);
	void SetPauseMenuActive(bool a_active);
	bool IsGamepadConnected();
	void LogInputState();
	void PollGamepad();
	bool ForwardButtonEvent(const RE::ButtonEvent* a_event);
	bool ForwardThumbstickEvent(const RE::ThumbstickEvent* a_event);
}
