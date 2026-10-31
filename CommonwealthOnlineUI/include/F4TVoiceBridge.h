#pragma once

#include "CommonwealthOnlineUI_API.h"
#include "PrismaUI_F4_API.h"

#include <cstdint>

namespace F4T::VoiceBridge
{
	enum class Surface : std::uint8_t
	{
		MainMenu,
		PauseMenu,
		Social,
		Hud
	};

	void Initialize();
	void SetCallbacks(const COMMONWEALTH_ONLINE_UI_API::VoiceCallbacks* a_callbacks);
	void PushEvent(const char* a_json);
	void AttachView(Surface a_surface, PrismaView a_view);
	void Tick();
}
