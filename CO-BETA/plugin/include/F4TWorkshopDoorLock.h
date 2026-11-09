#pragma once

#include "RE/T/TESObjectREFR.h"

namespace F4T::WorkshopDoorLock
{
	bool SetDoorLocked(RE::TESObjectREFR& a_ref, bool a_locked);
	void RefreshActivationBlock(RE::TESObjectREFR& a_ref, bool a_ownerSide, bool a_ownedZone, bool a_doorLocked);
	void EnsureActivateSink();
}
