#include "pch.h"

#include "F4TConsoleGuard.h"
#include "F4TRemotePlayerState.h"
#include "F4TVersionedDowned.h"
#include "F4TDebugLogging.h"
#include "F4TDownedGlue.h"
#include "F4TPlayerProgression.h"
#include "F4TPlayerVitals.h"
#include "F4TEquipmentState.h"
#include "F4TPvPCombatContract.h"
#include "F4TProxyActorController.h"
#include "F4TRemotePlayerCapacity.h"
#include "F4TNpcSyncRegistry.h"
#include "F4TSocialNameplate.h"
#include "F4TSleepRemote.h"
#include "F4TThrowableSync.h"

#include "RE/A/ACTOR_VALUE_MODIFIER.h"
#include "RE/A/ActorValue.h"
#include "RE/B/BSPointerHandle.h"
#include "RE/C/Console.h"
#include "RE/H/HitData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/STAGGER_MAGNITUDE.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESObjectWEAP.h"

#include <algorithm>
#include <format>
#include <memory>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{
	std::mutex g_stateMutex;
	std::optional<std::uint32_t> g_assignedPlayerId;
	bool g_assignedPlayerResumed{};
	std::unordered_map<std::uint32_t, F4T::RemotePlayerState::RemotePlayerState> g_remotePlayers;
	std::unordered_map<std::uint32_t, F4T::VersionedDowned::State> g_authoritativeDowned;

	void ApplyAuthoritativeDowned(F4T::RemotePlayerState::RemotePlayerState& a_player)
	{
		const auto found = g_authoritativeDowned.find(a_player.playerId);
		if (found == g_authoritativeDowned.end()) return;
		if (!a_player.vitals) a_player.vitals = F4T::PlayerVitals::Vitals{};
		a_player.vitals->isDowned = found->second.downed;
		a_player.vitals->isDead = found->second.dead;
	}

	std::mutex g_incomingHitsMutex;
	F4T::PvPCombatContract::InboundHitTracker g_incomingCombatHits;
	std::mutex g_incomingNpcDamageMutex;
	F4T::PvPCombatContract::InboundHitTracker g_incomingNpcDamageHits;

	constexpr float kUnarmedDamageClaimCap = 75.0F;
	constexpr float kUnknownWeaponDamageClaimCap = 300.0F;
	constexpr float kThrowableDamageClaimCap = 300.0F;
	constexpr float kWeaponBaseDamageClaimMultiplier = 10.0F;
	constexpr float kMinimumWeaponDamageClaimCap = 300.0F;
	constexpr float kMaximumWeaponDamageClaimCap = 1000.0F;

	void MergeActionEvents(
		std::vector<F4T::RemotePlayerState::RemoteActionEvent>& a_existing,
		const std::vector<F4T::RemotePlayerState::RemoteActionEvent>& a_incoming)
	{
		for (const auto& incoming : a_incoming) {
			if (incoming.eventName.empty()) {
				continue;
			}

			const auto alreadyPresent = std::any_of(
				a_existing.begin(),
				a_existing.end(),
				[&incoming](const F4T::RemotePlayerState::RemoteActionEvent& a_event) {
					return incoming.sequence != 0 && a_event.sequence == incoming.sequence;
				});
			if (alreadyPresent) {
				continue;
			}

			a_existing.push_back(incoming);
		}

		constexpr std::size_t kMaxPendingActionEvents = 32;
		if (a_existing.size() > kMaxPendingActionEvents) {
			a_existing.erase(
				a_existing.begin(),
				a_existing.begin() + static_cast<std::ptrdiff_t>(a_existing.size() - kMaxPendingActionEvents));
		}
	}

	struct RemoteWeaponClaim
	{
		bool hasState{};
		std::uint32_t rightHand{};
		std::uint32_t throwable{};
	};

	RemoteWeaponClaim GetRemoteWeaponClaim(std::uint32_t a_senderId)
	{
		const std::scoped_lock lock(g_stateMutex);
		const auto found = g_remotePlayers.find(a_senderId);
		if (found == g_remotePlayers.end()) {
			return {};
		}
		RemoteWeaponClaim claim{ true, 0, 0 };
		for (const auto& item : found->second.equippedItems) {
			if (item.slot == "rightHand" && claim.rightHand == 0) {
				claim.rightHand = item.formId;
			} else if (F4T::ThrowableSync::IsThrowableSlot(item.slot) && claim.throwable == 0) {
				claim.throwable = item.formId;
			}
		}
		return claim;
	}

	// Constraint: slot and id field names come from the relayed equipment contract (source:CO-BETA/plugin/include/F4TRemotePlayerState.h).
	std::optional<float> WeaponDamageClaimCap(std::uint32_t a_weaponFormId)
	{
		auto* weapon = RE::TESForm::GetFormByID<RE::TESObjectWEAP>(a_weaponFormId);
		if (!weapon) {
			return std::nullopt;
		}
		const auto baseDamage = static_cast<float>(weapon->weaponData.attackDamage);
		return (std::clamp)(
			baseDamage * kWeaponBaseDamageClaimMultiplier,
			kMinimumWeaponDamageClaimCap,
			kMaximumWeaponDamageClaimCap);
	}

	void ApplyDamageToLocalPlayer(RE::Actor* a_attacker, float a_damage, std::string_view a_label)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* actorValues = RE::ActorValue::GetSingleton();
		if (!player || !actorValues || !actorValues->health) {
			return;
		}
		const auto& health = *actorValues->health;
		const auto healthBefore = player->GetActorValue(health);
		const auto appliedDamage = F4T::Downed::ResolveLocalPlayerDamage(
			healthBefore,
			player->GetPermanentActorValue(health),
			a_damage);
		if (appliedDamage <= 0.0F) {
			return;
		}

		if (a_attacker) {
			// Feed the local hit/combat feedback path with zero health damage. Actual
			// HP mutation happens exactly once below through the damage modifier, so a
			// delayed native hit can never stack on top of network damage.
			RE::HitData hitData{
				.weapon = RE::BGSObjectInstanceT<RE::TESObjectWEAP>{ nullptr, nullptr }
			};
			hitData.aggressor = RE::BSPointerHandleManagerInterface<RE::Actor>::GetHandle(a_attacker);
			hitData.target = RE::BSPointerHandleManagerInterface<RE::Actor>::GetHandle(player);
			hitData.healthDamage = 0.0F;
			hitData.totalDamage = 0.0F;
			hitData.physicalDamage = 0.0F;
			hitData.targetedLimbDamage = 0.0F;
			hitData.stagger = RE::STAGGER_MAGNITUDE::kSmall;

			player->DoHitMe(std::addressof(hitData));
			player->SetBeenAttacked(true);
		}

		// Never use SetBaseActorValue/SetActorValue as a damage fallback. Those
		// mutate base Health and can permanently ratchet max HP down.
		player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, health, -appliedDamage);
		auto healthAfter = player->GetActorValue(health);
		if (healthAfter >= healthBefore - 0.01F) {
			F4T::ConsoleGuard::ExecutePluginCommand(std::format(
				"{:08X}.DamageActorValue Health {}",
				player->GetFormID(),
				appliedDamage).c_str());
			healthAfter = player->GetActorValue(health);
		}

		F4T::DebugLogging::Log(
			"Combat",
			"applied {} damage={:.1f} attacker={:08X} healthBefore={:.1f} healthAfter={:.1f} feedbackOnlyHit={}",
			a_label,
			appliedDamage,
			a_attacker ? a_attacker->GetFormID() : 0,
			healthBefore,
			healthAfter,
			a_attacker != nullptr);
	}

	void ApplyIncomingHit(const F4T::PvPCombatContract::QueuedCombatHit& a_hit)
	{
		const auto appliedDamage = F4T::RemotePlayerState::BoundClaimedDamage(a_hit.senderId, a_hit.damage);
		if (!(appliedDamage > 0.0F)) {
			return;
		}

		if (appliedDamage + 0.01F < a_hit.damage) {
			F4T::DebugLogging::Log(
				"Combat",
				"clamped remote damage claim senderId={} sequence={} claimed={:.1f} applied={:.1f}",
				a_hit.senderId,
				a_hit.sequence,
				a_hit.damage,
				appliedDamage);
		}

		auto* attacker = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(a_hit.senderId);
		ApplyDamageToLocalPlayer(attacker, appliedDamage, std::format("network hit claimed={:.1f} fromPlayerId={}", a_hit.damage, a_hit.senderId));
	}

	void ApplyIncomingNpcDamage(const F4T::PvPCombatContract::QueuedCombatHit& a_hit)
	{
		RE::Actor* attacker = nullptr;
		if (const auto puppetFormId = F4T::NpcSync::TryGetPuppetActorFormIdByNpcId(a_hit.npcId, {}, {}, a_hit.senderId)) {
			attacker = RE::TESForm::GetFormByID<RE::Actor>(*puppetFormId);
		}
		ApplyDamageToLocalPlayer(attacker, a_hit.damage, std::format("npc damage npcId={} authorityPlayerId={}", a_hit.npcId, a_hit.senderId));
	}
}

namespace F4T::RemotePlayerState
{
	void SetAssignedPlayerId(std::uint32_t a_playerId)
	{
		const std::scoped_lock lock(g_stateMutex);
		g_assignedPlayerId = a_playerId;
		F4T::DebugLogging::Log("RemoteState", "assigned local playerId={}", a_playerId);
	}

	std::optional<std::uint32_t> GetAssignedPlayerId()
	{
		const std::scoped_lock lock(g_stateMutex);
		return g_assignedPlayerId;
	}

	void ApplyServerDamageToLocalPlayer(float a_damage)
	{
		ApplyDamageToLocalPlayer(nullptr, a_damage, "settlement turret");
	}

	bool IsAssignedPlayerId(std::uint32_t a_playerId)
	{
		const std::scoped_lock lock(g_stateMutex);
		return g_assignedPlayerId.has_value() && *g_assignedPlayerId == a_playerId;
	}

	void ClearAssignedPlayerId()
	{
		const std::scoped_lock lock(g_stateMutex);
		g_assignedPlayerId.reset();
		g_assignedPlayerResumed = false;
	}

	void SetAssignedPlayerResumed(bool a_resumed)
	{
		const std::scoped_lock lock(g_stateMutex);
		g_assignedPlayerResumed = a_resumed;
	}

	bool GetAssignedPlayerResumed()
	{
		const std::scoped_lock lock(g_stateMutex);
		return g_assignedPlayerResumed;
	}

	float BoundClaimedDamage(std::uint32_t a_senderId, float a_claimedDamage)
	{
		if (!std::isfinite(a_claimedDamage) || !(a_claimedDamage > 0.0F)) {
			return 0.0F;
		}
		const auto claim = GetRemoteWeaponClaim(a_senderId);
		if (!claim.hasState) {
			return (std::min)(a_claimedDamage, kUnknownWeaponDamageClaimCap);
		}
		if (claim.throwable != 0) {
			return (std::min)(a_claimedDamage, WeaponDamageClaimCap(claim.throwable).value_or(kThrowableDamageClaimCap));
		}
		if (claim.rightHand == 0) {
			return (std::min)(a_claimedDamage, kUnarmedDamageClaimCap);
		}
		return (std::min)(a_claimedDamage, WeaponDamageClaimCap(claim.rightHand).value_or(kUnknownWeaponDamageClaimCap));
	}

	void UpdateRemotePlayer(RemotePlayerState a_state)
	{
		std::unique_lock lock(g_stateMutex);
		const auto existing = g_remotePlayers.find(a_state.playerId);
		if (existing == g_remotePlayers.end() &&
			F4T::RemotePlayerCapacity::Decide(g_remotePlayers.size(), false) ==
				F4T::RemotePlayerCapacity::Admission::RejectAtCapacity) {
			F4T::DebugLogging::Log(
				"RemoteState",
				"rejected remote playerId={} at capacity {}",
				a_state.playerId,
				F4T::RemotePlayerCapacity::MaximumTrackedRemotePlayers);
			return;
		}

		// TODO-verify(#407)
		const bool logReceive = F4T::DebugLogging::IsEnabled() && a_state.hasEquipmentUpdate && (existing == g_remotePlayers.end() || !existing->second.hasEquipmentUpdate ||
		                                   existing->second.equippedItems != a_state.equippedItems);
		const auto receivedPlayerId = a_state.playerId;
		const auto receivedItems = logReceive ? F4T::EquipmentState::DescribeItems(a_state.equippedItems) : std::string{};

		a_state.nameResolved = F4T::SocialNameplate::ResolveNameFlag(
			a_state.characterName,
			a_state.displayName,
			existing != g_remotePlayers.end() && existing->second.nameResolved);

		if (existing != g_remotePlayers.end()) {
			MergeActionEvents(existing->second.actionEvents, a_state.actionEvents);
			a_state.actionEvents = existing->second.actionEvents;
			if (a_state.characterName.empty() && !existing->second.characterName.empty()) {
				a_state.characterName = existing->second.characterName;
			}
			if (a_state.displayName.empty() && !existing->second.displayName.empty()) {
				a_state.displayName = existing->second.displayName;
			}
			if (!a_state.hasFactionTagUpdate && existing->second.hasFactionTagUpdate) {
				a_state.hasFactionTagUpdate = true;
				a_state.factionTag = existing->second.factionTag;
			}
			if (a_state.playerKey.empty() && !existing->second.playerKey.empty()) {
				a_state.playerKey = existing->second.playerKey;
			}
			if (!a_state.hasAppearanceUpdate && existing->second.hasAppearanceUpdate) {
				a_state.hasAppearanceUpdate = true;
				a_state.appearance = existing->second.appearance;
			}
			if (!a_state.hasEquipmentUpdate && existing->second.hasEquipmentUpdate) {
				a_state.hasEquipmentUpdate = true;
				a_state.equippedItems = existing->second.equippedItems;
			}
			if (!a_state.hasPowerArmorUpdate && existing->second.hasPowerArmorUpdate) {
				a_state.hasPowerArmorUpdate = true;
				a_state.powerArmorInFrame = existing->second.powerArmorInFrame;
				a_state.powerArmorFrameBaseFormId = existing->second.powerArmorFrameBaseFormId;
				a_state.powerArmorPieces = existing->second.powerArmorPieces;
			}
			if (!a_state.vitals && existing->second.vitals) {
				a_state.vitals = existing->second.vitals;
			}
		}

		if (a_state.powerArmorInFrame) {
			a_state.isSneaking = false;
			a_state.isCrouching = false;
		}
		ApplyAuthoritativeDowned(a_state);
		g_remotePlayers[a_state.playerId] = std::move(a_state);
		lock.unlock();
		if (logReceive) {
			F4T::DebugLogging::Log(
				"Equipment",
				"[RT-VERIFY 361-J] receive remote playerId={} equippedItems=[{}]",
				receivedPlayerId,
				receivedItems);
		}
	}

	bool HasRemotePlayer(std::uint32_t a_playerId)
	{
		const std::scoped_lock lock(g_stateMutex);
		return g_remotePlayers.contains(a_playerId);
	}

	bool RemoveRemotePlayer(std::uint32_t a_playerId)
	{
		F4T::SleepRemote::Forget(a_playerId);
		bool removed = false;
		{
			const std::scoped_lock lock(g_stateMutex);
			removed = g_remotePlayers.erase(a_playerId) > 0;
			if (removed) {
				F4T::DebugLogging::Log("RemoteState", "removed remote playerId={}", a_playerId);
			}
		}
		{
			const std::scoped_lock lock(g_incomingHitsMutex);
			g_incomingCombatHits.PurgeSender(a_playerId);
		}
		{
			const std::scoped_lock lock(g_incomingNpcDamageMutex);
			g_incomingNpcDamageHits.PurgeSender(a_playerId);
		}
		return removed;
	}

	void ClearRemotePlayers()
	{
		F4T::SleepRemote::Clear();
		{
			const std::scoped_lock lock(g_stateMutex);
			if (!g_remotePlayers.empty()) {
				F4T::DebugLogging::Log("RemoteState", "clearing remote players count={}", g_remotePlayers.size());
			}
			g_remotePlayers.clear();
			g_authoritativeDowned.clear();
		}
		{
			const std::scoped_lock lock(g_incomingHitsMutex);
			g_incomingCombatHits = {};
		}
		{
			const std::scoped_lock lock(g_incomingNpcDamageMutex);
			g_incomingNpcDamageHits = {};
		}
	}

	std::vector<RemotePlayerState> GetRemotePlayerSnapshot()
	{
		const std::scoped_lock lock(g_stateMutex);

		std::vector<RemotePlayerState> snapshot;
		snapshot.reserve(g_remotePlayers.size());
		for (const auto& [_, player] : g_remotePlayers) {
			snapshot.push_back(player);
		}

		return snapshot;
	}

	std::optional<RemotePlayerState> GetRemotePlayer(std::uint32_t a_playerId)
	{
		const std::scoped_lock lock(g_stateMutex);
		const auto found = g_remotePlayers.find(a_playerId);
		if (found == g_remotePlayers.end()) return std::nullopt;
		return found->second;
	}

	void GetRemotePlayerVoiceRoster(std::vector<RemotePlayerVoiceInfo>& a_out)
	{
		const std::scoped_lock lock(g_stateMutex);

		a_out.clear();
		for (const auto& [_, player] : g_remotePlayers) {
			a_out.push_back(RemotePlayerVoiceInfo{
				.playerId = player.playerId,
				.x = player.x,
				.y = player.y,
				.z = player.z,
				.name = F4T::SocialNameplate::ResolveDisplayName(player.displayName, player.characterName),
				.nameResolved = player.nameResolved,
				.playerKey = player.playerKey });
		}
	}

	bool SetRemotePlayerVitals(std::uint32_t a_playerId, const F4T::PlayerVitals::Vitals& a_vitals)
	{
		const std::scoped_lock lock(g_stateMutex);
		const auto found = g_remotePlayers.find(a_playerId);
		if (found == g_remotePlayers.end()) {
			return false;
		}
		if (found->second.vitals && *found->second.vitals == a_vitals) {
			return false;
		}
		found->second.vitals = a_vitals;
		ApplyAuthoritativeDowned(found->second);
		F4T::DebugLogging::Log(
			"RemoteState",
			"remote playerId={} vitals healthPercent={} downed={} dead={} level={}",
			a_playerId,
			a_vitals.healthPercent,
			a_vitals.isDowned,
			a_vitals.isDead,
			a_vitals.level.value_or(0));
		return true;
	}

	bool SetRemotePlayerAuthoritativeDowned(std::uint32_t a_playerId, std::uint64_t a_incarnation, std::uint64_t a_revision, bool a_downed, bool a_isDead)
	{
		if (a_playerId == 0 || a_playerId > (std::numeric_limits<std::uint16_t>::max)()) return false;
		const std::scoped_lock lock(g_stateMutex);
		auto& state = g_authoritativeDowned[a_playerId];
		if (!F4T::VersionedDowned::Accept(state, { a_incarnation, a_revision, a_downed, a_isDead })) return false;
		if (const auto found = g_remotePlayers.find(a_playerId); found != g_remotePlayers.end()) ApplyAuthoritativeDowned(found->second);
		return true;
	}

	bool SetRemotePlayerDownedState(std::uint32_t a_playerId, bool a_downed, bool a_isDead)
	{
		const std::scoped_lock lock(g_stateMutex);
		if (g_authoritativeDowned.contains(a_playerId)) return false;
		const auto found = g_remotePlayers.find(a_playerId);
		if (found == g_remotePlayers.end()) {
			return false;
		}
		auto& vitals = found->second.vitals;
		if (vitals && vitals->isDowned == a_downed && vitals->isDead == a_isDead) {
			return false;
		}
		if (!vitals) {
			vitals = F4T::PlayerVitals::Vitals{};
		}
		vitals->isDowned = a_downed;
		vitals->isDead = a_isDead;
		F4T::DebugLogging::Log(
			"RemoteState",
			"remote playerId={} downed={} dead={}",
			a_playerId,
			a_downed,
			a_isDead);
		return true;
	}

	bool QueueIncomingCombatHit(std::uint32_t a_senderId, std::uint32_t a_sequence, float a_damage)
	{
		std::lock_guard<std::mutex> lock(g_incomingHitsMutex);
		return g_incomingCombatHits.QueueHit(a_senderId, a_sequence, a_damage);
	}

	bool QueueIncomingNpcDamage(std::uint32_t a_authorityPlayerId, std::uint32_t a_sequence, float a_damage, std::uint32_t a_npcId)
	{
		if (a_npcId == 0) {
			return false;
		}
		std::lock_guard<std::mutex> lock(g_incomingNpcDamageMutex);
		return g_incomingNpcDamageHits.QueueHit(a_authorityPlayerId, a_sequence, a_damage, a_npcId);
	}

	void ApplyPendingCombatHits()
	{
		// Called only from the permanent game-thread task. The queued damage is
		// applied here and nowhere else.
		auto* actorValues = RE::ActorValue::GetSingleton();
		if (!RE::PlayerCharacter::GetSingleton() || !actorValues || !actorValues->health) {
			return;
		}

		std::vector<F4T::PvPCombatContract::QueuedCombatHit> hits;
		{
			std::lock_guard<std::mutex> lock(g_incomingHitsMutex);
			hits.assign(
				g_incomingCombatHits.pendingHits.begin(),
				g_incomingCombatHits.pendingHits.end());
			g_incomingCombatHits.pendingHits.clear();
		}
		for (const auto& hit : hits) {
			ApplyIncomingHit(hit);
		}

		std::vector<F4T::PvPCombatContract::QueuedCombatHit> npcHits;
		{
			std::lock_guard<std::mutex> lock(g_incomingNpcDamageMutex);
			npcHits.assign(
				g_incomingNpcDamageHits.pendingHits.begin(),
				g_incomingNpcDamageHits.pendingHits.end());
			g_incomingNpcDamageHits.pendingHits.clear();
		}
		for (const auto& hit : npcHits) {
			ApplyIncomingNpcDamage(hit);
		}

		F4T::PlayerVitals::PublishTick();
		F4T::PlayerProgression::PublishTick();
	}
}
