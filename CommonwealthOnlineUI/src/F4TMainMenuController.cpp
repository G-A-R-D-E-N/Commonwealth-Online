#include "pch.h"

#include "F4TMainMenuController.h"
#include "F4TMenuInput.h"
#include "F4TPrismaUI.h"

namespace F4T::MainMenuInject::Internal
{
	std::uintptr_t g_originalMainMenuOnThumbstickEvent = 0;
	using MainMenuOnThumbstickEventFn = void (*)(RE::BSInputEventUser*, const RE::ThumbstickEvent*);

	void MainMenuOnThumbstickEventHook(RE::BSInputEventUser* a_this, const RE::ThumbstickEvent* a_event)
	{
		if (F4T::PrismaUI::IsBrowserControllerFocused() && g_originalMainMenuOnThumbstickEvent != 0) {
			reinterpret_cast<MainMenuOnThumbstickEventFn>(g_originalMainMenuOnThumbstickEvent)(a_this, a_event);
			return;
		}
		if (F4T::PrismaUI::IsCustomMenuInputCaptured()) {
			F4T::MenuInput::ForwardThumbstickEvent(a_event);
			return;
		}

		if (g_originalMainMenuOnThumbstickEvent != 0) {
			reinterpret_cast<MainMenuOnThumbstickEventFn>(g_originalMainMenuOnThumbstickEvent)(a_this, a_event);
		}
	}
}
