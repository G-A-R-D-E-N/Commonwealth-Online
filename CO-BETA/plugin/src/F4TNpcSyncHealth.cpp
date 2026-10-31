#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncInternal.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"
#include "F4TNpcCoopHit.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"

#include "RE/H/HitData.h"
#include "RE/S/STAGGER_MAGNITUDE.h"

#include <chrono>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace F4T::NpcSync::Health
{
	namespace
	{
		constexpr std::size_t kMaximumPendingNpcHits = 64;
		constexpr std::size_t kMaximumNpcHitsPerTick = 8;
		constexpr auto kMaximumNpcHitAge = 5s;

		struct PendingNpcHit
		{
			NpcCoop::NpcHitClaim claim;
			Detail::Clock::time_point receivedAt{};
		};

		std::mutex g_pendingMutex;
		std::deque<PendingNpcHit> g_pendingHits;

		// source:RE-Work/subsystems/commonwealth-online-re/npc-sync-co250/README.md (w4b-proxy-target-a-ae-v2 VERIFIED: DoHitMe(victim=NPC, aggressor=proxy) reaches EnterCombat; w3-n44-modav-path-ae PARTIAL AE only: ModActorValue(kDamage) with a negative delta, no aggro; w3-n45-killer-myKiller-ae VERIFIED AE, PARTIAL OG/NG: KillImpl credits the named attacker; w3-n45-essential-kill-ae PARTIAL)
		void ApplyNpcHit(const NpcCoop::NpcHitClaim& a_claim, RE::PlayerCharacter& a_player)
		{
			const auto& identity = a_claim.identity;
			const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
			const auto authority = F4T::Networking::GetNpcAuthorityForScope(identity.cellId, identity.worldspaceId);
			if (!localPlayerId || !authority || authority->playerId != *localPlayerId ||
				authority->epoch != identity.authorityEpoch) {
				F4T::DebugLogging::Log("NpcCoop", "dropped npc hit npcId={}: not the authority for that scope and epoch", identity.npcId);
				return;
			}

			auto* attacker = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(a_claim.senderPlayerId);
			if (!attacker) {
				F4T::DebugLogging::Log("NpcCoop", "dropped npc hit npcId={}: sender {} has no proxy", identity.npcId, a_claim.senderPlayerId);
				return;
			}

			const auto actorFormId = TryGetHostActorFormIdByNpcId(identity.npcId);
			if (!actorFormId || *actorFormId != identity.sourceFormId) {
				F4T::DebugLogging::Log("NpcCoop", "dropped npc hit npcId={}: no matching host NPC", identity.npcId);
				return;
			}
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(*actorFormId);
			if (!actor || actor == attacker || actor == std::addressof(a_player) ||
				Detail::IsPuppetActor(actor->GetFormID()) ||
				Detail::IsRemotePlayerProxyActor(*actor) ||
				!Detail::IsSameLocation(*actor, a_player) ||
				actor->IsDead(false)) {
				F4T::DebugLogging::Log("NpcCoop", "dropped npc hit npcId={}: target is gone, dead, essential-down or not a host NPC", identity.npcId);
				return;
			}

			const auto* actorValues = RE::ActorValue::GetSingleton();
			if (!actorValues || !actorValues->health) {
				return;
			}
			const auto damage = F4T::RemotePlayerState::BoundClaimedDamage(a_claim.senderPlayerId, a_claim.damage);
			const auto current = actor->GetActorValue(*actorValues->health);
			if (!(damage > 0.0F) || !std::isfinite(current) || !(current > 0.0F)) {
				return;
			}

			RE::HitData hitData{
				.weapon = RE::BGSObjectInstanceT<RE::TESObjectWEAP>{ nullptr, nullptr }
			};
			hitData.aggressor = Detail::GetActorHandle(*attacker);
			hitData.target = Detail::GetActorHandle(*actor);
			hitData.healthDamage = 0.0F;
			hitData.totalDamage = 0.0F;
			hitData.physicalDamage = 0.0F;
			hitData.targetedLimbDamage = 0.0F;
			hitData.stagger = RE::STAGGER_MAGNITUDE::kSmall;

			actor->DoHitMe(std::addressof(hitData));
			if (actor->IsDead(false)) {
				return;
			}
			const auto* base = F4T::NpcSync::ResolveStableBase(*actor);
			const auto protectedActor = actor->boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kProtected) ||
					!base || base->IsEssential() || base->IsProtected();
			if (current - damage > 0.0F || protectedActor) {
				actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValues->health, -damage);
			} else {
				actor->KillImpl(attacker, damage, true, false);
			}
			F4T::DebugLogging::Log(
				"NpcCoop",
				"applied npc hit npcId={} actor={:08X} fromPlayerId={} claimed={:.1f} applied={:.1f} healthBefore={:.1f} healthAfter={:.1f}",
				identity.npcId,
				actor->GetFormID(),
				a_claim.senderPlayerId,
				a_claim.damage,
				damage,
				current,
				actor->GetActorValue(*actorValues->health));
		}

		void ApplyPendingNpcHits()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}

			std::vector<NpcCoop::NpcHitClaim> claims;
			{
				const std::scoped_lock lock(g_pendingMutex);
				const auto now = Detail::Clock::now();
				while (!g_pendingHits.empty() && claims.size() < kMaximumNpcHitsPerTick) {
					auto pending = std::move(g_pendingHits.front());
					g_pendingHits.pop_front();
					if (now - pending.receivedAt <= kMaximumNpcHitAge) {
						claims.push_back(std::move(pending.claim));
					}
				}
			}
			for (const auto& claim : claims) {
				ApplyNpcHit(claim, *player);
			}
		}
	}

	bool EnqueueNpcHit(NpcCoop::NpcHitClaim a_claim)
	{
		const std::scoped_lock lock(g_pendingMutex);
		if (g_pendingHits.size() >= kMaximumPendingNpcHits) {
			return false;
		}
		g_pendingHits.push_back(PendingNpcHit{ std::move(a_claim), Detail::Clock::now() });
		return true;
	}

	void Tick()
	{
		ApplyPendingNpcHits();
	}
}
