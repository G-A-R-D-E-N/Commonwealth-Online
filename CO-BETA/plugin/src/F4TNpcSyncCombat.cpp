#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncInternal.h"

#include "F4TDebugLogging.h"
#include "F4TNpcActorGuard.h"
#include "F4TProxyActorController.h"

#include "RE/H/HitData.h"
#include "RE/S/STAGGER_MAGNITUDE.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace F4T::NpcSync::Combat
{
	namespace
	{
		constexpr auto kEngageScanInterval = 500ms;
		constexpr auto kEngageCooldown = 5s;
		constexpr float kEngageRadius = 2048.0F;
		constexpr std::size_t kMaximumEngagesPerTick = 4;
		constexpr std::size_t kMaximumEngagedPairs = 1024;

		Detail::Clock::time_point g_lastEngageScan{};
		std::unordered_map<std::uint64_t, Detail::Clock::time_point> g_lastEngageByPair;
		std::unordered_set<std::uint64_t> g_engagedPairs;

		std::uint64_t PairKey(std::uint32_t a_npcId, std::uint32_t a_remotePlayerId)
		{
			return (static_cast<std::uint64_t>(a_npcId) << 32) | a_remotePlayerId;
		}

		void RestoreProxyHealth()
		{
			const auto* actorValues = RE::ActorValue::GetSingleton();
			if (!actorValues || !actorValues->health) {
				return;
			}
			for (const auto& proxy : F4T::ProxyActorController::GetActiveProxyActors()) {
				if (proxy.actor->IsDead(false)) {
					continue;
				}
				const auto current = proxy.actor->GetActorValue(*actorValues->health);
				const auto maximum = proxy.actor->GetPermanentActorValue(*actorValues->health);
				const auto missing = maximum - current;
				if (!std::isfinite(missing) || !(missing > 0.0F)) {
					continue;
				}
				proxy.actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValues->health, missing);
			}
		}

		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (w4b-proxy-target-a-ae-v2: DoHitMe(victim NPC, aggressor proxy, healthDamage 0) reaches EnterCombat)
		void EngageProxy(RE::Actor& a_npc, RE::Actor& a_proxy)
		{
			RE::HitData hitData{
				.weapon = RE::BGSObjectInstanceT<RE::TESObjectWEAP>{ nullptr, nullptr }
			};
			hitData.aggressor = Detail::GetActorHandle(a_proxy);
			hitData.target = Detail::GetActorHandle(a_npc);
			hitData.healthDamage = 0.0F;
			hitData.totalDamage = 0.0F;
			hitData.physicalDamage = 0.0F;
			hitData.targetedLimbDamage = 0.0F;
			hitData.stagger = RE::STAGGER_MAGNITUDE::kNone;
			a_npc.DoHitMe(std::addressof(hitData));
		}

		void EngageNearbyProxies(Detail::Clock::time_point a_now)
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			const auto* playerCell = player->GetParentCell();
			if (!playerCell) {
				return;
			}
			const auto proxies = F4T::ProxyActorController::GetActiveProxyActors();
			std::erase_if(g_lastEngageByPair, [a_now](const auto& a_entry) {
				return a_now - a_entry.second >= kEngageCooldown;
			});

			std::size_t engaged = 0;
			for (const auto& [npcActorFormId, npcId] : Detail::g_hostNpcIdsByActorFormId) {
				auto* npc = RE::TESForm::GetFormByID<RE::Actor>(npcActorFormId);
				if (!npc || npc->IsDead(false)) {
					continue;
				}
				if (!npc->IsInCombat()) {
					std::erase_if(g_engagedPairs, [npcId](const std::uint64_t a_key) {
						return (a_key >> 32) == npcId;
					});
				}
				for (const auto& proxy : proxies) {
					if (engaged >= kMaximumEngagesPerTick) {
						return;
					}
					if (proxy.actor->IsDead(false) || proxy.actor->GetParentCell() != playerCell) {
						continue;
					}
					if (!npc->GetHostileToActor(proxy.actor)) {
						continue;
					}
					const auto distance = (npc->GetPosition() - proxy.actor->GetPosition()).Length();
					if (!std::isfinite(distance) || !(distance < kEngageRadius)) {
						continue;
					}
					const auto key = PairKey(npcId, proxy.remotePlayerId);
					if (npc->IsInCombat() && g_engagedPairs.contains(key)) {
						continue;
					}
					const auto last = g_lastEngageByPair.find(key);
					if (last != g_lastEngageByPair.end() && a_now - last->second < kEngageCooldown) {
						continue;
					}
					EngageProxy(*npc, *proxy.actor);
					g_lastEngageByPair[key] = a_now;
					g_engagedPairs.insert(key);
					if (g_engagedPairs.size() > kMaximumEngagedPairs) {
						g_engagedPairs.clear();
					}
					++engaged;
					F4T::DebugLogging::Log(
						"NpcCoop",
						"engaged npcId={} with proxy of playerId={} distance={:.0f}",
						npcId,
						proxy.remotePlayerId,
						distance);
				}
			}
		}
	}

	void Tick()
	{
		if (!F4T::NpcActorGuard::IsInstalled()) {
			return;
		}
		RestoreProxyHealth();
		const auto now = Detail::Clock::now();
		if (g_lastEngageScan != Detail::Clock::time_point{} && now - g_lastEngageScan < kEngageScanInterval) {
			return;
		}
		g_lastEngageScan = now;
		EngageNearbyProxies(now);
	}

	void Reset()
	{
		g_lastEngageByPair.clear();
		g_engagedPairs.clear();
	}
}
