#pragma once

#include "F4TNetworkValidation.h"
#include "F4TNpcSync.h"
#include "F4TRemoteNpcState.h"

#include <RE/Fallout.h>

#include <algorithm>
#include <cmath>

namespace F4T::NpcSync
{
	inline constexpr float kMinAppliedHealth = 1.0F;
	inline constexpr float kHealthApplyEpsilon = 0.5F;

	// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W3 N4.4, w3-n44-current-health-ae VERIFIED AE only; slot-3 base as maxHealth is PARTIAL)
	inline void CaptureHealth(const RE::Actor& a_actor, RemoteNpcState::NpcState& a_state)
	{
		const auto* actorValues = RE::ActorValue::GetSingleton();
		if (!actorValues || !actorValues->health) {
			return;
		}
		const auto health = a_actor.GetActorValue(*actorValues->health);
		const auto base = a_actor.GetBaseActorValue(*actorValues->health);
		if (!std::isfinite(health) || !std::isfinite(base)) {
			return;
		}
		const auto limit = static_cast<float>(NetworkValidation::MaximumActorHealth);
		a_state.health = std::clamp(health, 0.0F, limit);
		a_state.maxHealth = std::clamp((std::max)(base, a_state.health), 0.0F, limit);
		a_state.hasHealth = true;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (W3 review item 8, w3-n44-modav-path-ae PARTIAL AE only: owner ModActorValue passes attacker 0, no aggro, kills only at 0)
	inline void ApplyAuthorityHealth(RE::Actor& a_puppet, const RemoteNpcState::NpcState& a_state)
	{
		const auto* actorValues = RE::ActorValue::GetSingleton();
		if (a_state.isDead || !a_state.hasHealth || !actorValues || !actorValues->health) {
			return;
		}
		const auto ownMaximum = a_puppet.GetBaseActorValue(*actorValues->health);
		if (!(a_state.maxHealth > 0.0F) || !(ownMaximum > 0.0F) || !std::isfinite(ownMaximum)) {
			return;
		}
		const auto fraction = std::clamp(a_state.health / a_state.maxHealth, 0.0F, 1.0F);
		const auto target = (std::max)(fraction * ownMaximum, kMinAppliedHealth);
		const auto delta = target - a_puppet.GetActorValue(*actorValues->health);
		if (std::isfinite(delta) && std::fabs(delta) >= kHealthApplyEpsilon) {
			a_puppet.ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValues->health, delta);
		}
	}
}
