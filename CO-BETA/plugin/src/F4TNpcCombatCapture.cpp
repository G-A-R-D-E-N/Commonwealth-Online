#include "pch.h"

#include "F4TNpcSync.h"

#include "F4TNpcCombatCapture.h"
#include "F4THitAttribution.h"
#include "F4TDebugLogging.h"
#include "F4TNetworking.h"
#include "F4TNpcActorGuard.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TRangedCombatCapture.h"
#include "F4TNpcSyncInternal.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerState.h"

#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESHitEvent.h"
#include "RE/T/TESObjectWEAP.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace F4T::NpcCombatCapture
{
	namespace
	{
		std::atomic<bool> g_loggedMirrorOnly{ false };

		constexpr std::size_t kMaximumPendingNpcDamage = 64;
		std::mutex g_pendingNpcDamageMutex;
		std::vector<NpcCoop::NpcDamageClaim> g_pendingNpcDamage;
		std::uint32_t g_nextNpcDamageSequence = 1;

		void QueueNpcDamage(NpcCoop::NpcDamageClaim a_claim)
		{
			const std::scoped_lock lock(g_pendingNpcDamageMutex);
			if (g_pendingNpcDamage.size() >= kMaximumPendingNpcDamage) {
				return;
			}
			a_claim.sequence = F4T::RangedCombatCapture::AllocateNonZeroSequence(g_nextNpcDamageSequence);
			g_pendingNpcDamage.push_back(std::move(a_claim));
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
				if (!puppet) {
					return RE::BSEventNotifyControl::kContinue;
				}

				if (!NpcActorGuard::IsInstalled()) {
					if (!g_loggedMirrorOnly.exchange(true)) {
						F4T::DebugLogging::Log(
							"NpcCoop",
							"NPC actor guard is not installed; shared NPC health stays mirror-only and local hits on puppets are not sent");
					}
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
				const auto authority = F4T::Networking::GetNpcAuthorityForScope(puppet->cellId, puppet->worldspaceId);
				if (!localPlayerId || !authority || authority->epoch == 0 ||
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
		};

		class NpcDamageCapture : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESHitEvent& a_event,
				RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				const auto localPlayer = RE::PlayerCharacter::GetSingleton();
				if (!localPlayer || !a_event.usesHitData || !NpcActorGuard::IsInstalled()) {
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

				const auto* cell = localPlayer->GetParentCell();
				if (!cell) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto cellId = NpcSync::Detail::FormatFormId(cell->GetFormID());
				const auto worldspaceId = cell->IsExterior() && cell->worldSpace ?
				                              NpcSync::Detail::FormatFormId(cell->worldSpace->GetFormID()) :
				                              std::string{};
				const auto localPlayerId = F4T::RemotePlayerState::GetAssignedPlayerId();
				const auto authority = F4T::Networking::GetNpcAuthorityForScope(cellId, worldspaceId);
				if (!localPlayerId || !authority || authority->playerId != *localPlayerId || authority->epoch == 0 ||
					authority->epoch > (std::numeric_limits<std::uint32_t>::max)()) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto* weapon = a_event.hitData.weapon.object;
				QueueNpcDamage(NpcCoop::NpcDamageClaim{
					NpcCoop::NpcHitIdentity{ attacker->npcId, attacker->sourceFormId, cellId, worldspaceId, authority->epoch },
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

	std::vector<NpcCoop::NpcDamageClaim> GetAndClearPendingNpcDamage()
	{
		const std::scoped_lock lock(g_pendingNpcDamageMutex);
		return std::exchange(g_pendingNpcDamage, {});
	}
}
