#include "pch.h"

#include "F4TRangedCombatCapture.h"

#include "F4TCombatActionContract.h"
#include "F4TDebugLogging.h"
#include "F4TNpcCombatCapture.h"
#include "F4TNpcSync.h"
#include "F4TProjectileAim.h"
#include "F4TProjectileLaunchHook.h"
#include "F4TProxyActorController.h"
#include "F4TReplicationRuntime.h"
#include "F4TWeaponClassification.h"

#include "RE/A/Actor.h"
#include "RE/G/GUN_STATE.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESHitEvent.h"
#include "RE/T/TESObjectWEAP.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace F4T::RangedCombatCapture
{
	namespace
	{
		struct AttackActionContext
		{
			std::uint32_t sequence{};
			std::uint32_t type{};
			std::chrono::steady_clock::time_point capturedAt{};
		};

		struct PendingCombatHit
		{
			std::uint32_t sequence{};
			std::uint32_t targetPlayerId{};
			float damage{};
			std::uint32_t weaponFormId{};
			std::uint32_t requiredActionType{};
			std::chrono::steady_clock::time_point capturedAt{};
			bool usesHitData{};
			std::uint32_t projectileHandle{};
			std::uint64_t sessionGeneration{};
			HitKind kind{ HitKind::kPlayer };
			F4T::NpcCoop::NpcHitIdentity npc;
		};

		enum class HitRejection
		{
			kNone,
			kUnsupportedWeapon,
			kZeroDamage
		};

		struct ClassifiedHit
		{
			float damage{};
			std::uint32_t weaponFormId{};
			std::uint32_t requiredActionType{};
			const char* archetypeLabel{};
		};

		static std::mutex s_hitQueueLock;
		static std::deque<PendingCombatHit> s_pendingCombatHits;
		static std::deque<AttackActionContext> s_recentAttackActions;
		static std::deque<F4T::CombatActionContract::ProjectileLaunch> s_projectileLaunches;
		static std::uint32_t s_nextHitSequence = 1000;
		static bool s_loggedAmbiguousAttack[4]{};
		constexpr float kMaximumCombatHitDamage = 10000.0F;
		constexpr std::size_t kMaximumPendingCombatHits = 32;
		constexpr std::size_t kMaximumRecentAttackActions = 16;
		constexpr std::size_t kMaximumProjectileLaunches = 128;

		void PruneAttackActions(std::chrono::steady_clock::time_point a_now)
		{
			while (!s_recentAttackActions.empty() &&
				a_now > s_recentAttackActions.front().capturedAt + F4T::CombatActionContract::kBindingWindow + F4T::CombatActionContract::kFutureSlack) {
				s_recentAttackActions.pop_front();
			}
		}

		void PruneProjectileLaunches(std::chrono::steady_clock::time_point a_now)
		{
			while (!s_projectileLaunches.empty() &&
				a_now > s_projectileLaunches.front().launchedAt + F4T::CombatActionContract::kBindingWindow + F4T::CombatActionContract::kFutureSlack) {
				s_projectileLaunches.pop_front();
			}
		}

		HitRejection ClassifyLocalHit(
			const RE::TESHitEvent& a_event,
			RE::PlayerCharacter& a_player,
			ClassifiedHit& a_out)
		{
			const auto equippedWeaponType =
				F4T::WeaponClassification::ClassifyEquippedRightHandWeapon(a_player);
			const auto* weaponForm = a_event.hitData.weapon.object;
			const auto weaponType =
				weaponForm && weaponForm->Is(RE::ENUM_FORM_ID::kWEAP) ?
					F4T::WeaponClassification::ClassifyWeapon(
						static_cast<const RE::TESObjectWEAP*>(weaponForm)) :
					equippedWeaponType;
			const bool isUnarmedPunch =
				weaponForm == nullptr &&
				equippedWeaponType == F4T::WeaponClassification::WeaponAnimationType::kUnarmed;
			const bool isRangedHit = F4T::WeaponClassification::IsRangedArchetype(weaponType);
			const bool isMeleeWeaponHit =
				!isUnarmedPunch && F4T::WeaponClassification::UseMeleeBehavior(weaponType);
			a_out.archetypeLabel = isUnarmedPunch ? "unarmed" : F4T::WeaponClassification::GetArchetypeLabel(weaponType);
			if (!isRangedHit && !isUnarmedPunch && !isMeleeWeaponHit) {
				return HitRejection::kUnsupportedWeapon;
			}

			const auto healthDamage = a_event.hitData.healthDamage;
			const auto totalDamage = a_event.hitData.totalDamage;
			const auto rawDamage =
				std::isfinite(healthDamage) && healthDamage > 0.0F ?
					healthDamage :
					totalDamage;
			if (!std::isfinite(rawDamage) || rawDamage <= 0.0F) {
				return HitRejection::kZeroDamage;
			}

			a_out.damage = std::min(rawDamage, kMaximumCombatHitDamage);
			a_out.weaponFormId = isUnarmedPunch ? 0U :
				(a_event.hitData.weapon.object ? a_event.hitData.weapon.object->GetFormID() : a_event.sourceFormID);
			a_out.requiredActionType = isUnarmedPunch ? 2U : (isRangedHit ? 3U : 1U);
			return HitRejection::kNone;
		}

		void QueuePendingHit(PendingCombatHit a_hit)
		{
			std::lock_guard lock(s_hitQueueLock);
			a_hit.sequence = AllocateNonZeroSequence(s_nextHitSequence);
			a_hit.sessionGeneration = F4T::ReplicationRuntime::CurrentGeneration();
			s_pendingCombatHits.push_back(std::move(a_hit));
			if (s_pendingCombatHits.size() > kMaximumPendingCombatHits) {
				s_pendingCombatHits.pop_front();
			}
		}

		class RangedHitEventSink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESHitEvent& a_event,
				RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				const auto localPlayer = RE::PlayerCharacter::GetSingleton();
				if (!localPlayer) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto aggressor = a_event.hitData.aggressor.get();
				const auto eventCause = a_event.cause;
				const bool localPlayerIsAggressor =
					aggressor && aggressor.get() == localPlayer;
				const bool localPlayerIsCause =
					eventCause && eventCause.get() == static_cast<RE::TESObjectREFR*>(localPlayer);
				if (!localPlayerIsAggressor && !localPlayerIsCause) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto eventTarget = a_event.target;
				const auto hitDataTarget = a_event.hitData.target.get();
				const auto eventTargetFormId = eventTarget ? eventTarget->GetFormID() : 0;
				const auto hitDataTargetFormId = hitDataTarget ? hitDataTarget->GetFormID() : 0;
				auto hitSourceRef = a_event.hitData.sourceRef;

				F4T::DebugLogging::Log(
					"CombatCapture",
					"observed TESHitEvent eventTarget={:08X} hitDataTarget={:08X} aggressor={:08X} cause={:08X} sourceFormID={:08X} projectileFormID={:08X} healthDamage={:.2f} totalDamage={:.2f} usesHitData={} sourceRef={:08X}",
					eventTargetFormId,
					hitDataTargetFormId,
					aggressor ? aggressor->GetFormID() : 0,
					eventCause ? eventCause->GetFormID() : 0,
					a_event.sourceFormID,
					a_event.projectileFormID,
					a_event.hitData.healthDamage,
					a_event.hitData.totalDamage,
					a_event.usesHitData,
					hitSourceRef.get_handle());

				auto targetPlayerId =
					F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId(eventTargetFormId);
				auto targetFormId = eventTargetFormId;
				if (!targetPlayerId && hitDataTargetFormId != 0) {
					targetPlayerId =
						F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId(hitDataTargetFormId);
					if (targetPlayerId) {
						targetFormId = hitDataTargetFormId;
					}
				}
				if (!targetPlayerId) {
					F4T::DebugLogging::Log(
						"CombatCapture",
						"TESHitEvent did not map to remote proxy eventTarget={:08X} hitDataTarget={:08X} healthDamage={:.2f} totalDamage={:.2f}",
						eventTargetFormId,
						hitDataTargetFormId,
						a_event.hitData.healthDamage,
						a_event.hitData.totalDamage);
					return RE::BSEventNotifyControl::kContinue;
				}

				ClassifiedHit classified{};
				const auto rejection = ClassifyLocalHit(a_event, *localPlayer, classified);
				if (rejection == HitRejection::kUnsupportedWeapon) {
					F4T::DebugLogging::Log(
						"CombatCapture",
						"ignored unsupported weapon target={:08X} targetPlayerId={} archetype={}",
						targetFormId,
						*targetPlayerId,
						classified.archetypeLabel);
					return RE::BSEventNotifyControl::kContinue;
				}
				if (rejection == HitRejection::kZeroDamage) {
					F4T::DebugLogging::Log(
						"CombatCapture",
						"ignored zero-damage hit target={:08X} targetPlayerId={} healthDamage={:.2f} totalDamage={:.2f} usesHitData={}",
						targetFormId,
						*targetPlayerId,
						a_event.hitData.healthDamage,
						a_event.hitData.totalDamage,
						a_event.usesHitData);
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto damage = classified.damage;
				const auto weaponFormId = classified.weaponFormId;
				const auto requiredActionType = classified.requiredActionType;
				auto sourceRef = a_event.hitData.sourceRef;

				QueuePendingHit(PendingCombatHit{
					0,
					*targetPlayerId,
					damage,
					weaponFormId,
					requiredActionType,
					std::chrono::steady_clock::now(),
					a_event.usesHitData,
					sourceRef.get_handle()
				});

				F4T::DebugLogging::Log(
					"CombatCapture",
					"PvP hit candidate queued target={:08X} targetPlayerId={} archetype={} damage={:.2f} healthDamage={:.2f} totalDamage={:.2f} weapon={:08X}",
					targetFormId,
					*targetPlayerId,
					classified.archetypeLabel,
					damage,
					a_event.hitData.healthDamage,
					a_event.hitData.totalDamage,
					weaponFormId);
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		static RangedHitEventSink s_hitEventSink;
		static bool s_sinkRegistered = false;
	}

	void Initialize()
	{
		F4T::ProjectileLaunchHook::Install();
		F4T::ProjectileAim::Install();
		if (!s_sinkRegistered) {
			if (auto* eventSource = RE::TESHitEvent::GetEventSource()) {
				eventSource->RegisterSink(&s_hitEventSink);
				s_sinkRegistered = true;
				F4T::DebugLogging::Log("CombatCapture", "TESHitEvent sink registered for ranged, melee and unarmed PvP");
			}
		}
		F4T::NpcCombatCapture::Initialize();
	}

	void Shutdown()
	{
		F4T::NpcCombatCapture::Shutdown();
		if (s_sinkRegistered) {
			if (auto* eventSource = RE::TESHitEvent::GetEventSource()) {
				eventSource->UnregisterSink(&s_hitEventSink);
				s_sinkRegistered = false;
				F4T::DebugLogging::Log("CombatCapture", "TESHitEvent sink unregistered");
			}
		}
		std::lock_guard lock(s_hitQueueLock);
		s_pendingCombatHits.clear();
		s_recentAttackActions.clear();
		s_projectileLaunches.clear();
	}

	void QueueNpcHit(const RE::TESHitEvent& a_event, const F4T::NpcCoop::NpcHitIdentity& a_identity)
	{
		const auto localPlayer = RE::PlayerCharacter::GetSingleton();
		if (!localPlayer) {
			return;
		}
		ClassifiedHit classified{};
		if (ClassifyLocalHit(a_event, *localPlayer, classified) != HitRejection::kNone) {
			F4T::DebugLogging::Log(
				"CombatCapture",
				"ignored NPC hit npcId={} archetype={} healthDamage={:.2f} totalDamage={:.2f}",
				a_identity.npcId,
				classified.archetypeLabel,
				a_event.hitData.healthDamage,
				a_event.hitData.totalDamage);
			return;
		}
		auto sourceRef = a_event.hitData.sourceRef;
		PendingCombatHit hit{};
		hit.targetPlayerId = 0;
		hit.damage = classified.damage;
		hit.weaponFormId = classified.weaponFormId;
		hit.requiredActionType = classified.requiredActionType;
		hit.capturedAt = std::chrono::steady_clock::now();
		hit.usesHitData = a_event.usesHitData;
		hit.projectileHandle = sourceRef.get_handle();
		hit.kind = HitKind::kNpc;
		hit.npc = a_identity;
		QueuePendingHit(std::move(hit));
		F4T::DebugLogging::Log(
			"CombatCapture",
			"NPC hit candidate queued npcId={} archetype={} damage={:.2f} weapon={:08X}",
			a_identity.npcId,
			classified.archetypeLabel,
			classified.damage,
			classified.weaponFormId);
	}

	void NoteLocalAttackAction(
		std::uint32_t a_sequence,
		std::uint32_t a_type,
		std::chrono::steady_clock::time_point a_capturedAt)
	{
		if (a_sequence == 0 || (a_type != 1U && a_type != 2U && a_type != 3U) ||
			a_capturedAt.time_since_epoch().count() == 0) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		std::lock_guard lock(s_hitQueueLock);
		PruneAttackActions(now);
		s_recentAttackActions.push_back(AttackActionContext{ a_sequence, a_type, a_capturedAt });
		if (s_recentAttackActions.size() > kMaximumRecentAttackActions) {
			s_recentAttackActions.pop_front();
		}
	}

	void NoteLocalProjectileLaunch(
		std::uint32_t a_projectileHandle,
		std::chrono::steady_clock::time_point a_now)
	{
		std::lock_guard lock(s_hitQueueLock);
		PruneProjectileLaunches(a_now);
		const auto attack = F4T::CombatActionContract::FindUniqueAttackSequence(3U, a_now, s_recentAttackActions);
		s_projectileLaunches.push_back(F4T::CombatActionContract::ProjectileLaunch{
			attack.sequence,
			3U,
			a_projectileHandle,
			a_now
		});
		if (s_projectileLaunches.size() > kMaximumProjectileLaunches) {
			s_projectileLaunches.pop_front();
		}
	}

	std::vector<F4T::RemotePlayerState::RemoteActionEvent> CaptureRangedFireEvents(
		RE::Actor& a_player,
		bool& a_lastRangedFireState,
		std::uint32_t& a_nextActionSequence)
	{
		std::vector<F4T::RemotePlayerState::RemoteActionEvent> events;

		const auto weaponType = F4T::WeaponClassification::ClassifyEquippedRightHandWeapon(a_player);
		const bool isRanged = F4T::WeaponClassification::IsRangedArchetype(weaponType);
		const bool currentRangedFireState =
			isRanged &&
			(a_player.gunState == RE::GUN_STATE::kFire ||
			 a_player.gunState == RE::GUN_STATE::kFireSighted);

		if (!a_lastRangedFireState && currentRangedFireState) {
			F4T::RemotePlayerState::RemoteActionEvent event{};
			event.sequence = AllocateNonZeroSequence(a_nextActionSequence);
			event.type = 3U;
			event.eventName = "fireSingle";
			event.captureTime = std::chrono::steady_clock::now();
			events.push_back(event);

			F4T::DebugLogging::Log("CombatCapture", "ranged fire action sequence={} type=3 eventName=fireSingle", event.sequence);
		}

		a_lastRangedFireState = currentRangedFireState;
		return events;
	}

	std::vector<CombatHit> GetAndClearPendingCombatHits()
	{
		std::lock_guard<std::mutex> lock(s_hitQueueLock);
		const auto now = std::chrono::steady_clock::now();
		PruneAttackActions(now);

		std::vector<CombatHit> hits;
		hits.reserve(s_pendingCombatHits.size());
		std::deque<PendingCombatHit> waiting;
		for (const auto& candidate : s_pendingCombatHits) {
			if (!F4T::CombatActionContract::IsHitDecidable(candidate.capturedAt, now)) {
				waiting.push_back(candidate);
				continue;
			}
			const auto exactSequence = candidate.usesHitData ?
				F4T::CombatActionContract::FindExactAttackSequence(
					candidate.projectileHandle,
					candidate.requiredActionType,
					candidate.capturedAt,
					s_projectileLaunches) :
				0U;
			const auto match = exactSequence != 0 ?
				F4T::CombatActionContract::UniqueAttack{ exactSequence, 1 } :
				F4T::CombatActionContract::FindUniqueAttackSequence(
					candidate.requiredActionType,
					candidate.capturedAt,
					s_recentAttackActions);
			if (match.sequence == 0) {
				if (match.candidates > 1 && !s_loggedAmbiguousAttack[candidate.requiredActionType]) {
					s_loggedAmbiguousAttack[candidate.requiredActionType] = true;
					F4T::DebugLogging::Log(
						"CombatCapture",
						"ambiguous attack binding actionType={} candidates={}; hit left unbound",
						candidate.requiredActionType,
						match.candidates);
				}
				if (now <= candidate.capturedAt + F4T::CombatActionContract::kBindingWindow + F4T::CombatActionContract::kFutureSlack) {
					waiting.push_back(candidate);
				} else {
					F4T::DebugLogging::Log(
						"CombatCapture",
						"dropped unbound hit targetPlayerId={} sequence={} actionType={}",
						candidate.targetPlayerId,
						candidate.sequence,
						candidate.requiredActionType);
				}
				continue;
			}
			hits.push_back(CombatHit{
				candidate.sequence,
				candidate.targetPlayerId,
				candidate.damage,
				candidate.weaponFormId,
				match.sequence,
				candidate.sessionGeneration,
				candidate.kind,
				candidate.npc
			});
		}
		s_pendingCombatHits = std::move(waiting);
		return hits;
	}
}
