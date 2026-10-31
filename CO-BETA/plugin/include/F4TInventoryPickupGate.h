#pragma once

#include "F4TDropLedger.h"
#include <RE/Fallout.h>
#include <cstdint>
#include <string>

namespace F4T::InventoryPickupGate
{
	using Original = void (*)(RE::Actor*, RE::TESObjectREFR*, std::int32_t, bool);
	bool BindReference(F4T::Drops::EntityId a_entityId, RE::TESObjectREFR& a_reference);
	bool Hold(RE::TESObjectREFR* a_reference, std::int32_t a_count, bool a_sounds, Original a_original);
	void Complete(const std::string& a_operationId, bool a_committed);
}
