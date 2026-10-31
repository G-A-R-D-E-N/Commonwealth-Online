#pragma once

#include "F4TRemoteNpcState.h"

#include <RE/Fallout.h>

#include <chrono>
#include <cstdint>
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
		void Tick(bool a_isHost, std::uint64_t a_authorityEpoch, std::string_view a_cellId, std::string_view a_worldspaceId);
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
		using Clock = std::chrono::steady_clock;
		inline constexpr std::chrono::milliseconds kMissingNpcGrace{ 750 };

		struct PuppetSlot
		{
			std::uint32_t npcId{};
			std::uint32_t baseFormId{};
			std::uint32_t sourceFormId{};
			RE::ActorHandle handle;
			bool deathApplied{ false };
			Clock::time_point missingSince{};
			std::uint8_t deathAttempts{};
			Clock::time_point lastDeathAttempt{};
			std::string cellId;
			std::string worldspaceId;
		};

		struct SuppressedActor
		{
			RE::ActorHandle handle;
			float originalAlpha{ 1.0F };
			Clock::time_point missingSince{};
		};

		extern std::unordered_map<std::uint32_t, std::uint32_t> g_hostNpcIdsByActorFormId;
		extern std::uint32_t g_nextNpcId;
		extern Clock::time_point g_lastHostSend;
		extern Clock::time_point g_lastSummaryLog;
		extern std::unordered_map<std::uint32_t, PuppetSlot> g_puppetSlots;
		extern std::unordered_map<std::uint32_t, SuppressedActor> g_suppressedActors;

		template <class Callback>
		void ForEachLoadedActor(Callback&& a_callback)
		{
			const auto* processLists = RE::ProcessLists::GetSingleton();
			if (!processLists) {
				return;
			}

			std::unordered_set<std::uint32_t> visited;
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
		RE::Actor* FindMatchingSourceActor(const NpcState& a_state, RE::PlayerCharacter& a_player);
		bool HasMatchingAliveLocalNpc(const NpcState& a_state, RE::PlayerCharacter& a_player);
		bool HasLocalFollowerMatch(const NpcState& a_state, RE::PlayerCharacter& a_player);
		void SuppressDuplicateLocalNpcs(const std::unordered_map<std::uint32_t, NpcState>& a_states, RE::PlayerCharacter& a_player);
		void ClearPuppets();
		void PurgeStalePuppets();
		void UpdatePuppets(RE::PlayerCharacter& a_player);
		void UpdateHost(RE::PlayerCharacter& a_player, std::uint64_t a_authorityEpoch, std::string_view a_authorityCellId, std::string_view a_authorityWorldspaceId);
		void PublishRegistrySnapshot();
		void MarkRegistryDirty();
		void ClearHostNpcIds();
	}
}
