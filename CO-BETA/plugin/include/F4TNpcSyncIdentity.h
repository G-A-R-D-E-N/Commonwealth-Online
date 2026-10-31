#pragma once

#include "F4TNpcSync.h"
#include "F4TRemoteNpcState.h"

#include <RE/Fallout.h>

#include <cstdint>

namespace F4T::NpcSync
{
	// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W2 review R1, w2r-getnpc-templated-temp-id-og; R2 and w2r-extra-leveled-accessors-og for originalBase at +0x18, NG and AE PARTIAL)
	inline RE::TESActorBase* ResolveStableBase(const RE::Actor& a_actor)
	{
		const auto* reference = a_actor.GetObjectReference();
		if (!reference || reference->GetFormType() != RE::ENUM_FORM_ID::kNPC_) {
			return nullptr;
		}
		auto* npc = a_actor.GetNPC();
		if (!npc) {
			return nullptr;
		}
		if (!RemoteNpcState::IsRuntimeFormId(npc->GetFormID())) {
			return npc;
		}
		if (const auto& extraList = a_actor.extraList) {
			if (const auto* extra = extraList->GetByType<RE::ExtraLeveledCreature>()) {
				auto* original = extra->originalBase;
				if (original && original->GetFormType() == RE::ENUM_FORM_ID::kNPC_ &&
					!RemoteNpcState::IsRuntimeFormId(original->GetFormID())) {
					return original;
				}
			}
		}
		return nullptr;
	}

	inline std::uint32_t StableBaseFormId(const RE::Actor& a_actor)
	{
		const auto* base = ResolveStableBase(a_actor);
		return base ? base->GetFormID() : 0U;
	}
}
