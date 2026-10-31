#include "F4TReplicationRuntime.h"

#include "F4TCombatActionContract.h"
#include "F4TDebugLogging.h"
#include "F4TEquipmentState.h"
#include "F4TLocalAnimationGraphDebug.h"
#include "F4TLocalPlayerMovement.h"
#include "F4TLocalPlayerState.h"
#include "F4TLocalPlayerTransformSend.h"
#include "F4TNetworking.h"
#include "F4TNpcCombatCapture.h"
#include "F4TNpcSync.h"
#include "F4TProxyAnimationSync.h"
#include "F4TRangedCombatCapture.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TWeaponClassification.h"

#include "RE/B/BSFixedString.h"
#include "RE/I/IAnimationGraphManagerHolder.h"

#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::ReplicationRuntime
{
	using namespace F4T::LocalPlayerState;
	using namespace F4T::LocalPlayerMovement;

	namespace
	{
	constexpr auto kIdleTransformHeartbeatInterval = 2s;
	constexpr auto kTransformLogInterval = 1s;

	float TryReadLocalAnimationGraphSpeed(RE::PlayerCharacter& a_player)
	{
		auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_player);

		auto tryRead = [&](const char* a_name) -> float {
			float speed = -1.0F;
			if (graphHolder->GetGraphVariableImplFloat(RE::BSFixedString{ a_name }, speed) &&
				std::isfinite(speed) &&
				speed > kMovingSpeedThreshold) {
				return speed;
			}

			return -1.0F;
		};

		if (const auto speed = tryRead(F4T::ProxyAnimationSync::GraphVar::kSpeed); speed >= 0.0F) {
			return speed;
		}

		return tryRead(F4T::ProxyAnimationSync::GraphVar::kSpeedSmoothed);
	}

	std::vector<F4T::RemotePlayerState::RemoteActionEvent> CaptureMeleeAttackEvents(
		RE::Actor& a_player,
		std::uint32_t& a_lastMeleeAttackState,
		std::uint32_t& a_nextActionSequence)
	{
		std::vector<F4T::RemotePlayerState::RemoteActionEvent> events;

		const auto currentMeleeAttackState = a_player.meleeAttackState;
		const bool wasAttacking = a_lastMeleeAttackState != 0;
		const bool isNowAttacking = currentMeleeAttackState != 0;

		if (!wasAttacking && isNowAttacking) {
			const auto weaponArchetype = F4T::WeaponClassification::ClassifyEquippedRightHandWeapon(a_player);
			const bool isUnarmedAttack = weaponArchetype == F4T::WeaponClassification::WeaponAnimationType::kUnarmed;

			F4T::RemotePlayerState::RemoteActionEvent event{};
			event.sequence = F4T::RangedCombatCapture::AllocateNonZeroSequence(a_nextActionSequence);
			event.type = isUnarmedAttack ? 2U : 1U;
			event.eventName = "meleeattackStart";
			event.captureTime = std::chrono::steady_clock::now();
			events.push_back(event);
		}

		a_lastMeleeAttackState = currentMeleeAttackState;
		return events;
	}

	void RecordSentActions(
		bool a_sent,
		std::span<const F4T::RemotePlayerState::RemoteActionEvent> a_actions)
	{
		F4T::CombatActionContract::CommitSentActions(
			a_sent,
			a_actions,
			[](const auto& actionEvent) {
				F4T::RangedCombatCapture::NoteLocalAttackAction(
					actionEvent.sequence,
					actionEvent.type,
					actionEvent.captureTime);
			});
	}
	}

	void PollLocalPlayerTransform(SessionState& a_session, LocalPlayerCaptureState& a_capture)
	{
		auto* player = TryGetLocalPlayer();
		if (!player) {
			static bool playerUnavailableWarningLogged = false;
			if (!playerUnavailableWarningLogged) {
				LogWarningWithLocalPlayerPrefix("Player position unavailable: local player reference is not available yet.");
				playerUnavailableWarningLogged = true;
			}
			return;
		}

		auto& hasLastTransform = a_session.localPlayer.hasLastTransform;
		auto& lastSentTransform = a_session.localPlayer.lastSentTransform;
		auto& lastDebugSampleTransform = a_session.localPlayer.lastDebugSampleTransform;
		auto& lastKnownLocation = a_session.localPlayer.lastKnownLocation;
		auto& lastSentMovementState = a_session.localPlayer.lastSentMovementState;
		auto& lastSentEquipmentState = a_session.localPlayer.lastSentEquipmentState;
		auto& lastSentAppearanceState = a_session.localPlayer.lastSentAppearanceState;
		auto& lastSentPowerArmorState = a_session.localPlayer.lastSentPowerArmorState;
		auto& lastSendTime = a_session.localPlayer.lastSendTime;
		auto& lastLogTime = a_session.localPlayer.lastLogTime;
		auto& lastDebugSampleTime = a_session.localPlayer.lastDebugSampleTime;
		auto& debugMovingHoldUntil = a_session.localPlayer.debugMovingHoldUntil;
		auto& networkMovingHoldUntil = a_session.localPlayer.networkMovingHoldUntil;
		auto& soloCellTransformSuppressed = a_session.localPlayer.soloCellTransformSuppressed;
		auto& lastMeleeAttackState = a_capture.lastMeleeAttackState;
		auto& lastRangedFireState = a_capture.lastRangedFireState;
		auto& nextActionSequence = a_capture.nextActionSequence;
		auto& lastUnsentEquipmentLog = a_capture.lastUnsentEquipmentLog;
		auto& lastLoggedEquippedItems = a_capture.lastLoggedEquippedItems;

	const auto currentTransform = GetPlayerTransform(*player);
	F4T::SettlementCaptureClient::NoteLocalPosition(currentTransform.x, currentTransform.y, currentTransform.z);
	const auto currentLocation = GetPlayerLocation(*player);
	const auto currentEquipmentState = CaptureLocalEquippedItems(*player, lastLoggedEquippedItems);
	const auto currentAppearanceState = CaptureLocalAppearance(*player);
	const auto capturedPowerArmorState = F4T::PowerArmorSync::CaptureLocalPowerArmor(
		*player,
		currentEquipmentState,
		(std::chrono::steady_clock::now() - lastSendTime) >= kIdleTransformHeartbeatInterval
			? nullptr
			: std::addressof(lastSentPowerArmorState));
	const auto currentPowerArmorState = capturedPowerArmorState.value_or(lastSentPowerArmorState);
	const auto currentCharacterName = CaptureLocalCharacterName(*player);
		auto currentActionEvents =
			CaptureMeleeAttackEvents(*player, lastMeleeAttackState, nextActionSequence);
		const auto rangedFireEvents = F4T::RangedCombatCapture::CaptureRangedFireEvents(
			*player,
			lastRangedFireState,
			nextActionSequence);
	currentActionEvents.insert(currentActionEvents.end(), rangedFireEvents.begin(), rangedFireEvents.end());
	const auto currentTime = std::chrono::steady_clock::now();
		const auto debugMovementState = GetPlayerMovementState(
			*player,
			hasLastTransform ? &lastDebugSampleTransform : nullptr,
			lastDebugSampleTime,
			currentTransform,
			currentTime,
			debugMovingHoldUntil,
			true);
		F4T::LocalAnimationGraphDebug::UpdateLocalPlayerAnimationGraphDebug(
			*player,
			debugMovementState.isMoving,
			debugMovementState.isSprinting,
			debugMovementState.isSneaking,
			debugMovementState.isJumping,
			debugMovementState.weaponDrawn,
			debugMovementState.movementSpeed,
			currentTime);

		if (currentLocation.isSoloCell && soloCellTransformSuppressed) {
			lastSentTransform = currentTransform;
			lastDebugSampleTransform = currentTransform;
			lastKnownLocation = currentLocation;
			lastSentMovementState = debugMovementState;
			lastSentEquipmentState = currentEquipmentState;
			lastSentAppearanceState = currentAppearanceState;
			lastSentPowerArmorState = currentPowerArmorState;
			lastSendTime = currentTime;
			lastLogTime = currentTime;
			lastDebugSampleTime = currentTime;
			hasLastTransform = true;

			return;
		}

		if (currentLocation.isSoloCell) {
			LogInfoWithLocalPlayerPrefix("Character setup cell is solo; sending entry location update before suppressing further transforms.");
		}

		if (soloCellTransformSuppressed) {
			LogInfoWithLocalPlayerPrefix("Local player left solo cell; transform packet sending resumed.");
			soloCellTransformSuppressed = false;
		}

		if (!hasLastTransform) {
			lastSentTransform = currentTransform;
			lastDebugSampleTransform = currentTransform;
			lastKnownLocation = currentLocation;
			lastSentMovementState = debugMovementState;
			lastSendTime = currentTime;
			lastLogTime = currentTime;
			lastDebugSampleTime = currentTime;
			hasLastTransform = true;

			const auto initialActionEvents = F4T::CombatActionContract::SelectInitialActions(
				currentLocation.isSoloCell,
				std::span<const F4T::RemotePlayerState::RemoteActionEvent>{ currentActionEvents });
			const auto sentInitialTransform = F4T::Networking::SendTransformPacket(
				currentTransform,
				currentLocation,
				debugMovementState,
				"normal",
				-1.0F,
				currentEquipmentState,
				std::addressof(currentAppearanceState),
				initialActionEvents,
				currentCharacterName.c_str(),
				std::addressof(currentPowerArmorState));
			RecordSentActions(sentInitialTransform, initialActionEvents);
			if (sentInitialTransform) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Initial transform sent with cellId={:08X}{}, appearance headParts={}, morphs={}, characterName={}.",
					currentLocation.hasCellId ? currentLocation.cellId : 0U,
					currentLocation.hasWorldspaceId ?
						std::format(", worldspaceId={:08X}", currentLocation.worldspaceId) :
						std::string{},
					currentAppearanceState.headPartFormIds.size(),
					currentAppearanceState.morphSliders.size(),
					currentCharacterName.empty() ? "<empty>" : currentCharacterName));
			}
			if (currentLocation.isSoloCell) {
				soloCellTransformSuppressed = true;
			}
			return;
		}

		const auto networkMovementState = GetPlayerMovementState(
			*player,
			&lastSentTransform,
			lastSendTime,
			currentTransform,
			currentTime,
			networkMovingHoldUntil,
			false);

		const auto* movementType =
			GetSpecialMovementType(lastKnownLocation, currentLocation, lastSentTransform, currentTransform);
		const auto isSpecialMovement = movementType != std::string_view{ "normal" };
		const auto hasMeaningfulTransformDelta = HasMeaningfulTransformDelta(lastSentTransform, currentTransform);
		const auto hasMovementStateChanged = HasMovementStateChanged(lastSentMovementState, networkMovementState);
		const auto hasEquipmentChanged = !AreEquipmentStatesEqual(lastSentEquipmentState, currentEquipmentState);
		const auto hasAppearanceChanged = !AreAppearanceStatesEqual(lastSentAppearanceState, currentAppearanceState);
		const auto hasPowerArmorChanged = currentPowerArmorState != lastSentPowerArmorState;
		const auto hasPendingActionEvents = !currentActionEvents.empty();
		const auto idleHeartbeatDue = (currentTime - lastSendTime) >= kIdleTransformHeartbeatInterval;
		const auto hasSneakStateChanged =
			lastSentMovementState.isSneaking != networkMovementState.isSneaking ||
			lastSentMovementState.isCrouching != networkMovementState.isCrouching;
		const F4T::LocalPlayerTransformSend::TransformPublishState publishState{
			.bypassesThrottle = isSpecialMovement || hasSneakStateChanged || hasEquipmentChanged ||
			                    hasAppearanceChanged || hasPowerArmorChanged || hasPendingActionEvents || idleHeartbeatDue,
			.hasThrottledChange = hasMeaningfulTransformDelta || hasMovementStateChanged,
			.withinSendInterval = F4T::LocalPlayerTransformSend::IsWithinSendInterval(currentTime - lastSendTime, networkMovementState.isMoving),
			.isMoving = networkMovementState.isMoving,
			.movingEdge = networkMovementState.isMoving != lastSentMovementState.isMoving
		};
		if (!publishState.bypassesThrottle && !publishState.hasThrottledChange && !publishState.isMoving) {
			lastDebugSampleTransform = currentTransform;
			lastDebugSampleTime = currentTime;
			return;
		}
		if (!F4T::LocalPlayerTransformSend::ShouldPublishTransform(publishState)) {
			return;
		}

		auto animationGraphSpeed =
			networkMovementState.isMoving ? TryReadLocalAnimationGraphSpeed(*player) : -1.0F;
		if (networkMovementState.isMoving && animationGraphSpeed < kMovingSpeedThreshold) {
			animationGraphSpeed = networkMovementState.movementSpeed;
		}

		{
			static auto s_lastSendDiag = std::chrono::steady_clock::time_point{};
			const auto now = std::chrono::steady_clock::now();
			if (networkMovementState.isMoving &&
				(s_lastSendDiag.time_since_epoch().count() == 0 || (now - s_lastSendDiag) >= 500ms)) {
				s_lastSendDiag = now;
				LogInfoWithLocalPlayerPrefix(std::format(
					"Local send diag: isMoving={}, isSprinting={}, movementSpeed={:.1f}, localGraphSpeedRead={:.1f}.",
					networkMovementState.isMoving,
					networkMovementState.isSprinting,
					networkMovementState.movementSpeed,
					animationGraphSpeed));
			}
		}

		const auto sentTransform = F4T::Networking::SendTransformPacket(
			currentTransform,
			currentLocation,
			networkMovementState,
			movementType,
			animationGraphSpeed,
			currentEquipmentState,
			std::addressof(currentAppearanceState),
			currentActionEvents,
			currentCharacterName.c_str(),
			std::addressof(currentPowerArmorState));
		RecordSentActions(sentTransform, currentActionEvents);
		if (hasEquipmentChanged && !sentTransform && F4T::DebugLogging::IsEnabled() && currentTime - lastUnsentEquipmentLog >= std::chrono::seconds{ 1 }) {
			lastUnsentEquipmentLog = currentTime;
			F4T::DebugLogging::Log("Equipment", "[RT-VERIFY 361-J] local equipment changed but no transform sent this pass; equippedItems=[{}] count={}", F4T::EquipmentState::DescribeItems(currentEquipmentState), currentEquipmentState.size());
		}
		if (sentTransform) {
			lastSentTransform = currentTransform;
			lastKnownLocation = currentLocation;
			lastSentMovementState = networkMovementState;
			// TODO-verify(#407)
			if (F4T::DebugLogging::IsEnabled() && hasEquipmentChanged) {
				F4T::DebugLogging::Log(
					"Equipment",
					"[RT-VERIFY 361-J] send local equippedItems=[{}] count={}",
					F4T::EquipmentState::DescribeItems(currentEquipmentState),
					currentEquipmentState.size());
			}
			lastSentEquipmentState = currentEquipmentState;
			lastSentAppearanceState = currentAppearanceState;
			lastSentPowerArmorState = currentPowerArmorState;
			lastSendTime = currentTime;

			if (isSpecialMovement) {
				LogSpecialMovement(movementType);
				lastLogTime = currentTime;
			} else if (hasEquipmentChanged) {
				LogInfoWithLocalPlayerPrefix("Detected local equipment change. Sending immediate transform update.");
				lastLogTime = currentTime;
			} else if (hasAppearanceChanged) {
				LogInfoWithLocalPlayerPrefix("Detected local appearance change. Sending immediate transform update.");
				lastLogTime = currentTime;
			} else if (hasPowerArmorChanged) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Detected local power armor change (inFrame={}, pieces={}). Sending immediate transform update.",
					currentPowerArmorState.inFrame,
					currentPowerArmorState.pieces.size()));
				lastLogTime = currentTime;
			} else if (hasPendingActionEvents) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Detected local combat action (count={}). Sending immediate transform update.",
					currentActionEvents.size()));
				lastLogTime = currentTime;
			} else if (currentTime - lastLogTime >= kTransformLogInterval) {
				LogPlayerTransform(currentTransform);
				lastLogTime = currentTime;
			}

		}

		if (currentLocation.isSoloCell) {
			soloCellTransformSuppressed = true;
		}

	lastDebugSampleTransform = currentTransform;
	lastDebugSampleTime = currentTime;
}

void SendPendingCombatHits(const Coordinator& a_runtime)
{
	for (const auto& damage : F4T::NpcCombatCapture::GetAndClearPendingNpcDamage()) {
		if (F4T::Networking::SendNpcDamagePacket(damage.identity, damage.targetPlayerId, damage.sequence, damage.damage, damage.weaponFormId)) {
			F4T::DebugLogging::Log("Combat", "sent npc damage npcId={} targetPlayerId={} damage={}", damage.identity.npcId, damage.targetPlayerId, damage.damage);
		} else {
			F4T::DebugLogging::Log("Combat", "npc damage not sent npcId={}", damage.identity.npcId);
		}
	}
	const auto pendingHits = F4T::RangedCombatCapture::GetAndClearPendingCombatHits();
	for (const auto& hit : pendingHits) {
		if (!a_runtime.Admits(hit.sessionGeneration)) {
			F4T::DebugLogging::Log(
				"Combat",
				"dropped stale-session hit targetPlayerId={} generation={}",
				hit.targetPlayerId,
				hit.sessionGeneration);
			continue;
		}
		if (hit.kind == F4T::RangedCombatCapture::HitKind::kNpc) {
			if (F4T::Networking::SendNpcHitPacket(hit.npc, hit.sequence, hit.attackSequence, hit.damage, hit.weaponFormId)) {
				F4T::DebugLogging::Log("Combat", "sent npc hit npcId={} damage={}", hit.npc.npcId, hit.damage);
			} else {
				F4T::DebugLogging::Log("Combat", "npc hit not sent npcId={}", hit.npc.npcId);
			}
			continue;
		}
		if (F4T::Networking::SendCombatHitPacket(
				hit.sequence,
				hit.targetPlayerId,
				hit.damage,
				hit.attackSequence,
				hit.weaponFormId)) {
			F4T::DebugLogging::Log("Combat", "sent hit targetPlayerId={} damage={}", hit.targetPlayerId, hit.damage);
		} else {
			REX::WARN("Failed to send combat hit packet: targetPlayerId={}", hit.targetPlayerId);
		}
	}
}
}
