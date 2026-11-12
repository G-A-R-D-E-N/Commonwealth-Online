#include "pch.h"

#include "F4TTextEntryMode.h"

#include <mutex>

namespace F4T::TextEntryMode
{
	namespace
	{
		std::mutex g_mutex;
		bool g_entered = false;
	}

	void SetChatInputFocused(bool a_focused)
	{
		std::lock_guard lock{ g_mutex };
		if (a_focused == g_entered) {
			return;
		}
		auto* controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap) {
			return;
		}
		controlMap->SetTextEntryMode(a_focused);
		g_entered = a_focused;
	}

	bool IsChatInputFocused()
	{
		std::lock_guard lock{ g_mutex };
		return g_entered;
	}

	void Reset()
	{
		SetChatInputFocused(false);
	}
}
