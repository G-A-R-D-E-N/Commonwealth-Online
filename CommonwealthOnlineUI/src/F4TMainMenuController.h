#pragma once

#include "RE/B/BSInputEventUser.h"
#include "RE/T/ThumbstickEvent.h"

#include <cstdint>

namespace F4T::MainMenuInject::Internal
{
	extern std::uintptr_t g_originalMainMenuOnThumbstickEvent;
	void MainMenuOnThumbstickEventHook(RE::BSInputEventUser* a_this, const RE::ThumbstickEvent* a_event);
}
