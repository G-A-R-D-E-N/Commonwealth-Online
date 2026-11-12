#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TDebugLogging.h"
#include "F4TIntroQuestProbe.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncInternal.h"
#include "F4TNpcSyncPolicy.h"
#include "F4TRemoteNpcState.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace F4T::NpcSync::Detail
{

	namespace
	{
		std::unordered_map<std::uint32_t, std::uint32_t> g_stableBaseCache;

		std::uint32_t CachedStableBaseFormId(const RE::Actor& a_actor)
		{
			auto cached = g_stableBaseCache.find(a_actor.GetFormID());
			if (cached == g_stableBaseCache.end()) {
				cached = g_stableBaseCache.emplace(a_actor.GetFormID(), F4T::NpcSync::StableBaseFormId(a_actor)).first;
			}
			return cached->second;
		}

		bool IsRuntimeClaimCandidate(RE::Actor& a_actor, const NpcState& a_state, RE::PlayerCharacter& a_player)
		{
			return F4T::RemoteNpcState::IsRuntimeFormId(a_actor.GetFormID()) &&
			       std::addressof(a_actor) != std::addressof(a_player) &&
			       !IsPuppetActor(a_actor.GetFormID()) &&
			       !IsRemotePlayerProxyActor(a_actor) &&
			       !IsLocalPlayerFollower(a_actor, a_player) &&
			       IsSameLocation(a_actor, a_player) &&
			       CachedStableBaseFormId(a_actor) == a_state.baseFormId;
		}
	}

	std::unordered_map<std::uint32_t, SuppressedActor> g_suppressedActors;
	std::map<NpcKey, std::uint32_t> g_runtimeClaims;

	bool IsStateIdentityUsable(const NpcState& a_state)
	{
		return a_state.baseFormId != 0 &&
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

	LoadedActorIndex BuildLoadedActorIndex()
	{
		g_stableBaseCache.clear();
		LoadedActorIndex index;
		if (const auto* processLists = RE::ProcessLists::GetSingleton()) {
			index.reserve(processLists->highActorHandles.size() + processLists->middleHighActorHandles.size());
		}
		ForEachLoadedActor([&](RE::Actor& a_actor) {
			index.emplace(a_actor.GetFormID(), std::addressof(a_actor));
		});
		return index;
	}

	void DropDuplicateRuntimeStates(NpcStates& a_states, const std::string& a_localCellId)
	{
		std::map<std::string, std::vector<NpcKey>> keysByWorldspace;
		for (const auto& [key, state] : a_states) {
			if (state.sourceFormId == 0) {
				keysByWorldspace[state.worldspaceId].push_back(key);
			}
		}
		for (const auto& [worldspaceId, keys] : keysByWorldspace) {
			std::vector<NpcSyncPolicy::RuntimeCopy> copies;
			copies.reserve(keys.size());
			for (const auto& key : keys) {
				const auto& state = a_states.at(key);
				copies.push_back({ state.baseFormId, state.x, state.y, state.z, key.hostPlayerId, key.scope.cellId == a_localCellId });
			}
			for (const auto loser : NpcSyncPolicy::FindRuntimeDuplicateLosers(copies, NpcSyncPolicy::kRuntimeClaimDistance)) {
				a_states.erase(keys[loser]);
			}
		}
	}

	RE::Actor* FindRuntimeClaim(const NpcKey& a_key, const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors)
	{
		const RE::NiPoint3 target{ a_state.x, a_state.y, a_state.z };
		std::vector<NpcSyncPolicy::RuntimeClaimCandidate> candidates;
		for (const auto& [actorFormId, actorPointer] : a_loadedActors) {
			auto& actor = *actorPointer;
			if (IsRuntimeClaimCandidate(actor, a_state, a_player)) {
				candidates.push_back({ actorFormId, (actor.GetPosition() - target).Length(), actor.IsDead(true) });
			}
		}
		const auto claimed = NpcSyncPolicy::ResolveRuntimeClaim(g_runtimeClaims, a_key, candidates, NpcSyncPolicy::kRuntimeClaimDistance, a_state.isDead);
		return claimed ? a_loadedActors.at(*claimed) : nullptr;
	}

	RE::Actor* FindMatchingSourceActor(const NpcKey& a_key, const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors)
	{
		if (!IsStateIdentityUsable(a_state)) {
			return nullptr;
		}
		if (a_state.sourceFormId == 0) {
			return FindRuntimeClaim(a_key, a_state, a_player, a_loadedActors);
		}
		const auto entry = a_loadedActors.find(a_state.sourceFormId);
		if (entry == a_loadedActors.end()) {
			return nullptr;
		}
		auto& actor = *entry->second;
		if (std::addressof(actor) == std::addressof(a_player) ||
			IsPuppetActor(actor.GetFormID()) ||
			IsRemotePlayerProxyActor(actor) ||
			IsLocalPlayerFollower(actor, a_player) ||
			!IsSameLocation(actor, a_player)) {
			return nullptr;
		}
		return F4T::NpcSync::StableBaseFormId(actor) == a_state.baseFormId ? std::addressof(actor) : nullptr;
	}

	void LogMirrorKept(std::uint32_t a_formId, std::uint32_t a_baseFormId)
	{
		static std::unordered_set<std::uint32_t> logged;
		if (!logged.insert(a_formId).second) {
			return;
		}
		F4T::DebugLogging::Log(
			"NpcSync",
			"kept unique/aliased placed actor={:08X} baseFormId={:08X}: the placed reference stays visible and is never duplicated",
			a_formId,
			a_baseFormId);
	}

	void SuppressDuplicateLocalNpcs(
		const NpcStates& a_states,
		RE::PlayerCharacter& a_player,
		const LoadedActorIndex& a_loadedActors)
	{
		std::unordered_map<std::uint32_t, std::uint32_t> activeSourceBases;
		for (const auto& [key, state] : a_states) {
			const auto slot = g_puppetSlots.find(key);
			if (IsStateIdentityUsable(state) && slot != g_puppetSlots.end() && slot->second.handle.get()) {
				if (state.sourceFormId != 0) {
					activeSourceBases[state.sourceFormId] = state.baseFormId;
				} else if (auto* claim = FindRuntimeClaim(key, state, a_player, a_loadedActors)) {
					activeSourceBases[claim->GetFormID()] = state.baseFormId;
				}
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

			if (F4T::NpcSync::IsUniqueOrAliasedPlacedRef(a_actor.GetFormID())) {
				LogMirrorKept(a_actor.GetFormID(), baseFormId);
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
		for (const auto& [sourceFormId, _] : activeSourceBases) {
			if (const auto entry = a_loadedActors.find(sourceFormId); entry != a_loadedActors.end()) {
				suppressIfDuplicate(*entry->second);
			}
		}
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

	bool HasMatchingAliveLocalNpc(const NpcKey& a_key, const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors)
	{
		if (!IsStateIdentityUsable(a_state)) {
			return false;
		}
		if (a_state.sourceFormId == 0) {
			auto* claim = FindRuntimeClaim(a_key, a_state, a_player, a_loadedActors);
			return claim && !claim->IsDead(true);
		}
		const auto entry = a_loadedActors.find(a_state.sourceFormId);
		if (entry == a_loadedActors.end()) {
			return false;
		}
		auto& actor = *entry->second;
		if (std::addressof(actor) == std::addressof(a_player) ||
			IsPuppetActor(actor.GetFormID()) ||
			IsRemotePlayerProxyActor(actor) ||
			IsLocalPlayerFollower(actor, a_player) ||
			actor.IsDead(true) ||
			!IsSameLocation(actor, a_player)) {
			return false;
		}
		return F4T::NpcSync::StableBaseFormId(actor) == a_state.baseFormId;
	}

	bool HasLocalFollowerMatch(const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors)
	{
		const auto entry = a_loadedActors.find(a_state.sourceFormId);
		if (entry == a_loadedActors.end()) {
			return false;
		}
		auto& actor = *entry->second;
		return IsLocalPlayerFollower(actor, a_player) && F4T::NpcSync::StableBaseFormId(actor) == a_state.baseFormId;
	}
}
