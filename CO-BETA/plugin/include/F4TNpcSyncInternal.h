#pragma once

#include "F4TRemoteNpcState.h"

#include <RE/Fallout.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace F4T::NpcSync
{
	namespace Health
	{
		void Tick();
	}

	namespace Loot
	{
		void Tick(bool a_isHost, std::uint64_t a_authorityEpoch, std::string_view a_cellId, std::string_view a_worldspaceId, const std::function<void(const std::function<void()>&)>& a_runGameplay);
		void Reset();
	}

	namespace Combat
	{
		void Tick();
		void Reset();
	}

	namespace Detail
	{
		using NpcState = RemoteNpcState::NpcState;
		using NpcKey = RemoteNpcState::NpcKey;
		using NpcStates = std::map<NpcKey, NpcState>;
		using Clock = std::chrono::steady_clock;
		using LoadedActorIndex = std::unordered_map<std::uint32_t, RE::Actor*>;
		inline constexpr std::chrono::milliseconds kMissingNpcGrace{ 750 };

		struct PuppetSlot
		{
			NpcKey key;
			std::uint32_t npcId{};
			std::uint32_t baseFormId{};
			std::uint32_t sourceFormId{};
			RE::ActorHandle handle;
			bool deathApplied{ false };
			Clock::time_point missingSince{};
			std::uint8_t deathAttempts{};
			Clock::time_point lastDeathAttempt{};
			Clock::time_point lastRedraw{};
			Clock::time_point lastNeeded{};
			std::uint8_t redrawCount{};
			std::string cellId;
			std::string worldspaceId;
		};

		struct SuppressedActor
		{
			RE::ActorHandle handle;
			float originalAlpha{ 1.0F };
			Clock::time_point missingSince{};
		};

		struct HostNpcIdRecord
		{
			std::uint32_t npcId{};
			std::uint32_t baseFormId{};
			Clock::time_point missingSince{};
		};

		extern std::unordered_map<std::uint32_t, HostNpcIdRecord> g_hostNpcIdsByActorFormId;
		extern std::uint32_t g_nextNpcId;
		extern Clock::time_point g_lastHostSend;
		extern Clock::time_point g_lastSummaryLog;
		extern std::map<NpcKey, PuppetSlot> g_puppetSlots;
		extern std::unordered_map<std::uint32_t, SuppressedActor> g_suppressedActors;
		extern std::map<NpcKey, std::uint32_t> g_runtimeClaims;
		extern std::unordered_set<std::uint32_t> g_lastHostSentSources;
		extern std::unordered_set<std::uint32_t> g_lastHostSentRuntimeNpcIds;

		template <class Callback>
		void ForEachLoadedActor(Callback&& a_callback)
		{
			const auto* processLists = RE::ProcessLists::GetSingleton();
			if (!processLists) {
				return;
			}

			thread_local std::unordered_set<std::uint32_t> visited;
			visited.clear();
			const auto visit = [&](const RE::BSTArray<RE::ActorHandle>& a_handles) {
				for (const auto& handle : a_handles) {
					const auto actor = handle.get();
					if (!actor || !visited.insert(actor->GetFormID()).second) {
						continue;
					}
					a_callback(*actor);
				}
			};

			visit(processLists->highActorHandles);
			visit(processLists->middleHighActorHandles);
			visit(processLists->middleLowActorHandles);
			visit(processLists->lowActorHandles);
		}

		RE::ActorHandle GetActorHandle(RE::Actor& a_actor);
		std::string FormatFormId(std::uint32_t a_formId);
		bool IsSameLocation(const RE::Actor& a_actor, const RE::PlayerCharacter& a_player);
		bool IsRemotePlayerProxyActor(const RE::Actor& a_actor);
		bool IsLocalPlayerFollower(const RE::Actor& a_actor, const RE::PlayerCharacter& a_player);
		bool IsPuppetActor(std::uint32_t a_formId);
		bool IsRecordedCreatedPuppet(std::uint32_t a_formId);
		bool IsStateIdentityUsable(const NpcState& a_state);
		void RestoreSuppressedActors();
		LoadedActorIndex BuildLoadedActorIndex();
		RE::Actor* FindRuntimeClaim(const NpcKey& a_key, const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors);
		RE::Actor* FindMatchingSourceActor(const NpcKey& a_key, const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors);
		bool HasMatchingAliveLocalNpc(const NpcKey& a_key, const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors);
		bool HasLocalFollowerMatch(const NpcState& a_state, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors);
		void DropDuplicateRuntimeStates(NpcStates& a_states, const std::string& a_localCellId);
		void SuppressDuplicateLocalNpcs(const NpcStates& a_states, RE::PlayerCharacter& a_player, const LoadedActorIndex& a_loadedActors);
		void ClearPuppets();
		void PurgeStalePuppets();
		void UpdatePuppets(RE::PlayerCharacter& a_player);
		void UpdateHost(RE::PlayerCharacter& a_player, std::uint64_t a_authorityEpoch, std::string_view a_authorityCellId, std::string_view a_authorityWorldspaceId);
		void PublishRegistrySnapshot();
		void MarkRegistryDirty();
		void ClearHostNpcIds();
	}
}
