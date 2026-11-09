#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcCombatCapture.h"
#include "F4TExplosionSync.h"
#include "F4THitAttribution.h"
#include "F4TDebugLogging.h"
#include "F4TCombatActionContract.h"
#include "F4TNetworking.h"
#include "F4TNpcActorGuard.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TRangedCombatCapture.h"
#include "F4TNpcSyncInternal.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"
#include "F4TReplicationRuntime.h"
#include "F4TSurvivalPolicyClient.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESHitEvent.h"
#include "RE/T/TESObjectWEAP.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace F4T::NpcCombatCapture
{
	namespace
	{
		std::atomic<bool> g_loggedGuardMissing{ false };

		constexpr std::size_t kMaximumPendingNpcDamage = 64;
		std::mutex g_pendingNpcDamageMutex;
		std::vector<PendingDamage> g_pendingNpcDamage;
		std::vector<PendingDamage> g_retainedNpcDamage;
		std::uint32_t g_nextNpcDamageSequence = 1;

		void QueueNpcDamage(NpcCoop::NpcDamageClaim a_claim)
		{
			const auto connection = F4T::Networking::GetConnectionGeneration();
			const auto generation = F4T::ReplicationRuntime::CurrentGeneration();
			const auto revision = F4T::SurvivalPolicy::g_clientState.ReadyRevision(connection, generation);
			if (connection == 0 || generation == F4T::ReplicationRuntime::kNoSession || !revision || !F4T::Networking::IsConnectedToServer()) return;
			const std::scoped_lock lock(g_pendingNpcDamageMutex);
			if (g_pendingNpcDamage.size() >= kMaximumPendingNpcDamage) {
				return;
			}
			a_claim.sequence = F4T::RangedCombatCapture::AllocateNonZeroSequence(g_nextNpcDamageSequence);
			g_pendingNpcDamage.push_back(PendingDamage{ std::move(a_claim), generation, connection, *revision, std::chrono::steady_clock::now() });
		}

		struct LocalScope
		{
			std::string cellId;
			std::string worldspaceId;
		};

		std::optional<LocalScope> CurrentLocalScope(RE::PlayerCharacter& a_player)
		{
			const auto* cell = a_player.GetParentCell();
			if (!cell) {
				return std::nullopt;
			}
			return LocalScope{
				NpcSync::Detail::FormatFormId(cell->GetFormID()),
				cell->IsExterior() && cell->worldSpace ?
					NpcSync::Detail::FormatFormId(cell->worldSpace->GetFormID()) :
					std::string{}
			};
		}

		class NpcHitCapture : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESHitEvent& a_event,
				RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				const auto localPlayer = RE::PlayerCharacter::GetSingleton();
				if (!localPlayer || !a_event.usesHitData) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (F4T::ExplosionSync::IsReplayedExplosion(a_event.hitData.sourceRef)) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto aggressor = a_event.hitData.aggressor.get();
				if (!aggressor || aggressor.get() != localPlayer) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto hitDataTarget = a_event.hitData.target.get();
				const auto* hitTarget = HitAttribution::PickReference(
					static_cast<const RE::TESObjectREFR*>(hitDataTarget.get()),
					static_cast<const RE::TESObjectREFR*>(a_event.target.get()));
				const auto targetFormId = hitTarget ? hitTarget->GetFormID() : 0U;
				const auto puppet = NpcSync::TryGetPuppetByActorFormId(targetFormId);
				const auto host = puppet ? std::nullopt : NpcSync::TryGetHostNpcByActorFormId(targetFormId);
				if (!puppet && !host) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (!NpcActorGuard::IsInstalled() && !g_loggedGuardMissing.exchange(true)) {
					REX::WARN("NPC actor guard is not installed; local hits on puppets are still sent, but engine damage and kill on puppets are not held");
				}

				const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
				if (!localPlayerId) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (puppet) {
					const auto authority = F4T::Networking::GetNpcAuthorityForScope(puppet->cellId, puppet->worldspaceId);
					if (!authority || authority->playerId != puppet->authorityPlayerId || authority->epoch != puppet->authorityEpoch || authority->epoch == 0 ||
						authority->epoch > (std::numeric_limits<std::uint32_t>::max)() ||
						authority->playerId == *localPlayerId) {
						return RE::BSEventNotifyControl::kContinue;
					}
					F4T::RangedCombatCapture::QueueNpcHit(
						a_event,
						F4T::NpcCoop::NpcHitIdentity{
							puppet->npcId,
							puppet->sourceFormId,
							puppet->cellId,
							puppet->worldspaceId,
							authority->epoch });
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto scope = CurrentLocalScope(*localPlayer);
				if (!scope) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto authority = F4T::Networking::GetNpcAuthorityForScope(scope->cellId, scope->worldspaceId);
				if (!authority || authority->playerId != *localPlayerId || authority->epoch == 0 ||
					authority->epoch > (std::numeric_limits<std::uint32_t>::max)()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				F4T::RangedCombatCapture::QueueNpcHit(
					a_event,
					F4T::NpcCoop::NpcHitIdentity{
						host->npcId,
						host->sourceFormId,
						scope->cellId,
						scope->worldspaceId,
						authority->epoch });
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class NpcDamageCapture : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESHitEvent& a_event,
				RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				const auto localPlayer = RE::PlayerCharacter::GetSingleton();
				if (!localPlayer || !a_event.usesHitData) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (F4T::ExplosionSync::IsReplayedExplosion(a_event.hitData.sourceRef)) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto cause = a_event.cause;
				const auto hitDataTarget = a_event.hitData.target.get();
				const auto hitDataAggressor = a_event.hitData.aggressor.get();
				const auto* hitTarget = HitAttribution::PickReference(
					static_cast<const RE::TESObjectREFR*>(hitDataTarget.get()),
					static_cast<const RE::TESObjectREFR*>(a_event.target.get()));
				const auto* aggressor = HitAttribution::PickReference(
					static_cast<const RE::TESObjectREFR*>(hitDataAggressor.get()),
					static_cast<const RE::TESObjectREFR*>(cause.get()));
				if (!hitTarget || !aggressor) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto targetPlayerId = F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId(hitTarget->GetFormID());
				const auto attacker = NpcSync::TryGetHostNpcByActorFormId(aggressor->GetFormID());
				const auto damage = a_event.hitData.healthDamage;
				if (!targetPlayerId || !attacker || !std::isfinite(damage) || !(damage > 0.0F)) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto scope = CurrentLocalScope(*localPlayer);
				if (!scope) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
				const auto authority = F4T::Networking::GetNpcAuthorityForScope(scope->cellId, scope->worldspaceId);
				if (!localPlayerId || !authority || authority->playerId != *localPlayerId || authority->epoch == 0 ||
					authority->epoch > (std::numeric_limits<std::uint32_t>::max)()) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto* weapon = a_event.hitData.weapon.object;
				QueueNpcDamage(NpcCoop::NpcDamageClaim{
					NpcCoop::NpcHitIdentity{ attacker->npcId, attacker->sourceFormId, scope->cellId, scope->worldspaceId, authority->epoch },
					*targetPlayerId,
					0,
					(std::min)(damage, NpcCoop::kMaximumNpcDamage),
					weapon ? weapon->GetFormID() : 0U });
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		NpcDamageCapture g_damageSink;

		NpcHitCapture g_sink;
		bool g_registered = false;
	}

	void Initialize()
	{
		if (g_registered) {
			return;
		}
		if (auto* eventSource = RE::TESHitEvent::GetEventSource()) {
			eventSource->RegisterSink(&g_sink);
			eventSource->RegisterSink(&g_damageSink);
			g_registered = true;
			F4T::DebugLogging::Log("NpcCoop", "TESHitEvent sink registered for shared NPC health");
		}
	}

	void Shutdown()
	{
		if (!g_registered) {
			return;
		}
		if (auto* eventSource = RE::TESHitEvent::GetEventSource()) {
			eventSource->UnregisterSink(&g_sink);
			eventSource->UnregisterSink(&g_damageSink);
			g_registered = false;
		}
	}

	void RetainPendingNpcDamage(std::span<const PendingDamage> a_unsent)
	{
		const std::scoped_lock lock(g_pendingNpcDamageMutex);
		g_retainedNpcDamage.insert(g_retainedNpcDamage.begin(), a_unsent.begin(), a_unsent.end());
		if (g_retainedNpcDamage.size() > kMaximumPendingNpcDamage) {
			const auto dropped = g_retainedNpcDamage.size() - kMaximumPendingNpcDamage;
			g_retainedNpcDamage.erase(g_retainedNpcDamage.begin(), g_retainedNpcDamage.begin() + static_cast<std::ptrdiff_t>(dropped));
			F4T::DebugLogging::Log("Combat", "retained npc damage over capacity; oldest dropped count={}", dropped);
		}
	}

	std::vector<PendingDamage> GetAndClearPendingNpcDamage()
	{
		const std::scoped_lock lock(g_pendingNpcDamageMutex);
		const auto now = std::chrono::steady_clock::now();
		std::vector<PendingDamage> pending;
		for (auto& retained : std::exchange(g_retainedNpcDamage, {})) {
			if (now <= retained.capturedAt + F4T::CombatActionContract::kBindingWindow) {
				pending.push_back(std::move(retained));
			} else {
				F4T::DebugLogging::Log("Combat", "dropped expired retained npc damage npcId={}", retained.claim.identity.npcId);
			}
		}
		auto fresh = std::exchange(g_pendingNpcDamage, {});
		pending.insert(pending.end(), fresh.begin(), fresh.end());
		return pending;
	}
}
