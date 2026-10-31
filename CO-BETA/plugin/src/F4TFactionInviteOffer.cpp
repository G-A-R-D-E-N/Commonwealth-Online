#include "pch.h"

#include "F4TFactionInviteOffer.h"

#include "F4TDownedEngine.h"
#include "F4TInteractWheel.h"

namespace F4T::FactionInviteOffer
{
	namespace
	{
		bool g_activateWasHeld = false;
	}

	void Tick()
	{
		const auto activateHeld = F4T::Downed::Engine::IsActivateHeld();
		const bool pressed = activateHeld && !g_activateWasHeld;
		g_activateWasHeld = activateHeld;
		if (!pressed || !F4T::Downed::Engine::CanTouchWorld()) {
			return;
		}
		F4T::InteractWheel::OpenForActivation();
	}
}
