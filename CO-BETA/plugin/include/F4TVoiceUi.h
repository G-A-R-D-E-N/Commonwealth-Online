#pragma once

namespace F4T::VoiceUi
{
	using ActionHandler = void (*)(const char* a_actionJson);

	void SetActionHandler(ActionHandler a_handler);
	void Register();
	bool PushEvent(const char* a_eventJson);
}
