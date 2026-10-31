#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TDebugLogging.h"
#include "F4TIntroQuestProbe.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncInternal.h"
#include "F4TRemoteNpcState.h"

#include <unordered_map>
#include <unordered_set>

namespace F4T::NpcSync::Detail
{

	std::unordered_map<std::uint32_t, SuppressedActor> g_suppressedActors;

	bool IsStateIdentityUsable(const NpcState& a_state)
	{
		return a_state.sourceFormId != 0 && a_state.baseFormId != 0 &&
		       !F4T::RemoteNpcState::IsRuntimeFormId(a_state.sourceFormId) &&
		       !F4T::RemoteNpcState::IsRuntimeFormId(a_state.baseFormId);
	}

	void RestoreSuppressedActors()
	{
		for (auto& [_, suppressed] : g_suppressedActors) {
			if (const auto actor = suppressed.handle.get()) {
				actor->SetAlpha(suppressed.originalAlpha);
			}
		}
		if (!g_suppressedActors.empty()) {
			g_suppressedActors.clear();
			MarkRegistryDirty();
		}
	}

	// requires Actor+0x2D0 bit 26 and middleHigh commandingActor offsets, source:E:/GardenEngine/re-answers/2026-10-27-commanding-actor-follower.md
	bool IsLocalPlayerFollower(const RE::Actor& a_actor, const RE::PlayerCharacter& a_player)
	{
		constexpr std::uint32_t kPlayerTeammateFlag = 1U << 26;
		if ((a_actor.niFlags.flags & kPlayerTeammateFlag) != 0) {
			return true;
		}
		const auto* middleHigh = a_actor.currentProcess ? a_actor.currentProcess->middleHigh : nullptr;
		return middleHigh != nullptr && middleHigh->commandingActor.get().get() == std::addressof(a_player);
	}

	RE::Actor* FindMatchingSourceActor(const NpcState& a_state, RE::PlayerCharacter& a_player)
	{
		RE::Actor* match = nullptr;
		if (!IsStateIdentityUsable(a_state)) {
			return match;
		}
		ForEachLoadedActor([&](RE::Actor& a_actor) {
			if (match || a_actor.GetFormID() != a_state.sourceFormId ||
				std::addressof(a_actor) == std::addressof(a_player) ||
				IsPuppetActor(a_actor.GetFormID()) ||
				IsRemotePlayerProxyActor(a_actor) ||
				IsLocalPlayerFollower(a_actor, a_player) ||
				!IsSameLocation(a_actor, a_player)) {
				return;
			}
			if (F4T::NpcSync::StableBaseFormId(a_actor) == a_state.baseFormId) {
				match = std::addressof(a_actor);
			}
		});
		return match;
	}

	void SuppressDuplicateLocalNpcs(
		const std::unordered_map<std::uint32_t, NpcState>& a_states,
		RE::PlayerCharacter& a_player)
	{
		std::unordered_map<std::uint32_t, std::uint32_t> activeSourceBases;
		for (const auto& [_, state] : a_states) {
			if (IsStateIdentityUsable(state)) {
				activeSourceBases[state.sourceFormId] = state.baseFormId;
			}
		}

		std::unordered_set<std::uint32_t> stillSuppressed;
		const auto suppressIfDuplicate = [&](RE::Actor& a_actor) {
			if (std::addressof(a_actor) == std::addressof(a_player) ||
				IsPuppetActor(a_actor.GetFormID()) ||
				IsRemotePlayerProxyActor(a_actor) ||
				IsLocalPlayerFollower(a_actor, a_player) ||
				!IsSameLocation(a_actor, a_player)) {
				return;
			}
			const auto source = activeSourceBases.find(a_actor.GetFormID());
			const auto baseFormId = F4T::NpcSync::StableBaseFormId(a_actor);
			if (source == activeSourceBases.end() || baseFormId != source->second) {
				return;
			}

			stillSuppressed.insert(a_actor.GetFormID());
			if (!g_suppressedActors.contains(a_actor.GetFormID())) {
				g_suppressedActors.emplace(
					a_actor.GetFormID(),
					SuppressedActor{ GetActorHandle(a_actor), a_actor.GetAlpha() });
				MarkRegistryDirty();
				F4T::DebugLogging::Log(
					"NpcSync",
					"suppressed source actor={:08X} baseFormId={:08X}",
					a_actor.GetFormID(),
					baseFormId);
				F4T::IntroQuestProbe::LogNpcDecision("suppress", a_actor.GetFormID(), baseFormId);
			}
			a_actor.SetAlpha(0.0F);
			if (a_actor.IsInCombat()) {
				a_actor.StopCombat();
			}
			a_actor.ClearAttackStates();
		};
		ForEachLoadedActor(suppressIfDuplicate);
		for (const auto& [formId, suppressed] : g_suppressedActors) {
			if (stillSuppressed.contains(formId)) {
				continue;
			}
			if (const auto actor = suppressed.handle.get()) {
				suppressIfDuplicate(*actor);
			}
		}

		for (auto iterator = g_suppressedActors.begin(); iterator != g_suppressedActors.end();) {
			auto& suppressed = iterator->second;
			if (stillSuppressed.contains(iterator->first)) {
				suppressed.missingSince = {};
				++iterator;
			} else {
				if (suppressed.missingSince == Clock::time_point{}) {
					suppressed.missingSince = Clock::now();
				}
				if (Clock::now() - suppressed.missingSince < kMissingNpcGrace) {
					if (const auto actor = suppressed.handle.get(); actor && actor->GetAlpha() != 0.0F) {
						actor->SetAlpha(0.0F);
					}
					++iterator;
				} else {
					F4T::IntroQuestProbe::LogNpcDecision("unsuppress", iterator->first, 0);
					if (const auto actor = suppressed.handle.get()) {
						actor->SetAlpha(suppressed.originalAlpha);
					}
					iterator = g_suppressedActors.erase(iterator);
					MarkRegistryDirty();
				}
			}
		}
	}

	bool HasMatchingAliveLocalNpc(const NpcState& a_state, RE::PlayerCharacter& a_player)
	{
		bool found = false;
		if (!IsStateIdentityUsable(a_state)) {
			return found;
		}
		ForEachLoadedActor([&](RE::Actor& a_actor) {
			if (found ||
				std::addressof(a_actor) == std::addressof(a_player) ||
				IsPuppetActor(a_actor.GetFormID()) ||
				IsRemotePlayerProxyActor(a_actor) ||
				IsLocalPlayerFollower(a_actor, a_player) ||
				a_actor.IsDead(true) ||
				!IsSameLocation(a_actor, a_player)) {
				return;
			}

			if (a_actor.GetFormID() == a_state.sourceFormId &&
				F4T::NpcSync::StableBaseFormId(a_actor) == a_state.baseFormId) {
				found = true;
			}
		});
		return found;
	}

	bool HasLocalFollowerMatch(const NpcState& a_state, RE::PlayerCharacter& a_player)
	{
		bool found = false;
		ForEachLoadedActor([&](RE::Actor& a_actor) {
			if (found || a_actor.GetFormID() != a_state.sourceFormId ||
				!IsLocalPlayerFollower(a_actor, a_player)) {
				return;
			}
			if (F4T::NpcSync::StableBaseFormId(a_actor) == a_state.baseFormId) {
				found = true;
			}
		});
		return found;
	}
}
