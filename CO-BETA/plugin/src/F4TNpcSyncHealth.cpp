#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcSyncInternal.h"

#include "F4TDebugLogging.h"
#include "F4TNetworking.h"
#include "F4TNpcCoopHit.h"
#include "F4TNpcSyncHealth.h"
#include "F4TNpcSyncIdentity.h"
#include "F4TNpcSyncPolicy.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TSurvivalPolicyClient.h"

#include "RE/H/HitData.h"
#include "RE/S/STAGGER_MAGNITUDE.h"

#include <atomic>
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
			std::uint64_t connectionGeneration{};
			std::uint64_t sessionGeneration{};
			std::uint64_t policyRevision{};
		};

		std::mutex g_pendingMutex;
		std::deque<PendingNpcHit> g_pendingHits;
		std::atomic<std::uint64_t> g_capacityDroppedNpcHits{};
		std::atomic<std::uint64_t> g_staleDroppedNpcHits{};

		NpcSyncPolicy::HitRoute ApplyNpcHit(const NpcCoop::NpcHitClaim& a_claim, RE::PlayerCharacter& a_player)
		{
			const auto& identity = a_claim.identity;
			const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
			const auto authority = F4T::Networking::GetNpcAuthorityForScope(identity.cellId, identity.worldspaceId);
			const auto route = NpcSyncPolicy::RouteAuthority(
				localPlayerId && authority,
				localPlayerId && authority && authority->playerId == *localPlayerId,
				identity.authorityEpoch,
				authority ? authority->epoch : 0);
			if (route != NpcSyncPolicy::HitRoute::kApply) {
				if (route == NpcSyncPolicy::HitRoute::kDrop) {
					REX::WARN("dropped npc hit npcId={}: this client is not the authority for scope {} at epoch {}", identity.npcId, identity.cellId, identity.authorityEpoch);
				}
				return route;
			}

			auto* attacker = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(a_claim.senderPlayerId);
			if (!attacker) {
				return NpcSyncPolicy::HitRoute::kRetry;
			}

			const auto actorFormId = TryGetHostActorFormIdByNpcId(identity.npcId);
			if (!actorFormId) {
				return NpcSyncPolicy::HitRoute::kRetry;
			}
			if (!F4T::NpcSync::SourceMatchesActor(identity.sourceFormId, *actorFormId)) {
				F4T::DebugLogging::Log("NpcCoop", "dropped npc hit npcId={}: host NPC has another source form", identity.npcId);
				return NpcSyncPolicy::HitRoute::kDrop;
			}
			auto* actor = RE::TESForm::GetFormByID<RE::Actor>(*actorFormId);
			if (!actor) {
				return NpcSyncPolicy::HitRoute::kRetry;
			}
			if (actor == attacker || actor == std::addressof(a_player) ||
				Detail::IsPuppetActor(actor->GetFormID()) ||
				Detail::IsRemotePlayerProxyActor(*actor)) {
				F4T::DebugLogging::Log("NpcCoop", "dropped npc hit npcId={}: target is the attacker, the local player, a puppet or a proxy", identity.npcId);
				return NpcSyncPolicy::HitRoute::kDrop;
			}
			if (!Detail::IsSameLocation(*actor, a_player)) {
				return NpcSyncPolicy::HitRoute::kRetry;
			}
			if (actor->IsDead(false)) {
				F4T::DebugLogging::Log("NpcCoop", "dropped npc hit npcId={}: target is dead", identity.npcId);
				return NpcSyncPolicy::HitRoute::kDrop;
			}

			const auto* actorValues = RE::ActorValue::GetSingleton();
			if (!actorValues || !actorValues->health) {
				return NpcSyncPolicy::HitRoute::kRetry;
			}
			const auto damage = F4T::RemotePlayerState::BoundClaimedDamage(a_claim.senderPlayerId, a_claim.damage);
			const auto current = actor->GetActorValue(*actorValues->health);
			if (!(damage > 0.0F) || !std::isfinite(current) || !(current > 0.0F)) {
				F4T::DebugLogging::Log("NpcCoop", "dropped npc hit npcId={}: claimed damage {:.1f} or host health {:.1f} is not applicable", identity.npcId, a_claim.damage, current);
				return NpcSyncPolicy::HitRoute::kDrop;
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
				return NpcSyncPolicy::HitRoute::kApply;
			}
			const auto* base = F4T::NpcSync::ResolveStableBase(*actor);
			const auto protectedActor = actor->boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kProtected) || !base || base->IsEssential() || base->IsProtected();
			const auto applied = protectedActor ? NpcSyncPolicy::DamageAboveFloor(current, damage, kMinAppliedHealth) : damage;
			if (current - damage <= 0.0F && !protectedActor) {
				actor->KillImpl(attacker, damage, true, false);
			} else if (applied > 0.0F) {
				actor->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *actorValues->health, -applied);
			}
			F4T::DebugLogging::Log(
				"NpcCoop",
				"applied npc hit npcId={} actor={:08X} fromPlayerId={} claimed={:.1f} applied={:.1f} healthBefore={:.1f} healthAfter={:.1f}",
				identity.npcId,
				actor->GetFormID(),
				a_claim.senderPlayerId,
				a_claim.damage,
				applied,
				current,
				actor->GetActorValue(*actorValues->health));
			return NpcSyncPolicy::HitRoute::kApply;
		}

		void ApplyPendingNpcHits()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}

			std::deque<PendingNpcHit> claims;
			{
				const std::scoped_lock lock(g_pendingMutex);
				claims.swap(g_pendingHits);
			}

			std::deque<PendingNpcHit> deferred;
			std::size_t applied = 0;
			for (auto& pending : claims) {
				if (pending.connectionGeneration != F4T::Networking::GetConnectionGeneration() ||
					pending.sessionGeneration != F4T::ReplicationRuntime::CurrentGeneration() ||
					F4T::SurvivalPolicy::g_clientState.Revision(pending.connectionGeneration) != pending.policyRevision ||
					!F4T::Networking::IsConnectedToServer()) {
					if (const auto total = ++g_staleDroppedNpcHits; total == 1 || total % 50 == 0) {
						REX::WARN("dropped npc hit npcId={} from player {}: connection, session or policy changed; total dropped={}", pending.claim.identity.npcId, pending.claim.senderPlayerId, total);
					}
					continue;
				}
				const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(Detail::Clock::now() - pending.receivedAt);
				if (!NpcSyncPolicy::KeepPendingHit(NpcSyncPolicy::HitRoute::kRetry, age, kMaximumNpcHitAge)) {
					REX::WARN("dropped npc hit npcId={} from player {}: expired after {} ms", pending.claim.identity.npcId, pending.claim.senderPlayerId, age.count());
					continue;
				}
				if (applied >= kMaximumNpcHitsPerTick) {
					deferred.push_back(std::move(pending));
					continue;
				}
				const auto route = ApplyNpcHit(pending.claim, *player);
				if (route == NpcSyncPolicy::HitRoute::kApply) {
					++applied;
				} else if (route == NpcSyncPolicy::HitRoute::kRetry) {
					deferred.push_back(std::move(pending));
				}
			}

			const std::scoped_lock lock(g_pendingMutex);
			while (!deferred.empty() && g_pendingHits.size() + deferred.size() > kMaximumPendingNpcHits) {
				const auto npcId = deferred.front().claim.identity.npcId;
				deferred.pop_front();
				if (const auto total = ++g_capacityDroppedNpcHits; total == 1 || total % 50 == 0) {
					REX::WARN("dropped npc hit npcId={}: pending queue full, oldest hit evicted; total dropped={}", npcId, total);
				}
			}
			for (auto hit = deferred.rbegin(); hit != deferred.rend(); ++hit) {
				g_pendingHits.push_front(std::move(*hit));
			}
		}
	}

	bool EnqueueNpcHit(NpcCoop::NpcHitClaim a_claim)
	{
		const auto connection = F4T::Networking::GetConnectionGeneration();
		const auto session = F4T::ReplicationRuntime::CurrentGeneration();
		const auto revision = F4T::SurvivalPolicy::g_clientState.ReadyRevision(connection, session);
		if (connection == 0 || session == F4T::ReplicationRuntime::kNoSession || !revision || !F4T::Networking::IsConnectedToServer()) return false;
		const std::scoped_lock lock(g_pendingMutex);
		if (g_pendingHits.size() >= kMaximumPendingNpcHits) {
			++g_capacityDroppedNpcHits;
			return false;
		}
		g_pendingHits.push_back(PendingNpcHit{ std::move(a_claim), Detail::Clock::now(), connection, session, *revision });
		return true;
	}

	std::uint64_t CapacityDroppedNpcHitCount()
	{
		return g_capacityDroppedNpcHits.load();
	}

	void Tick()
	{
		ApplyPendingNpcHits();
	}
}
