#include "pch.h"

#include "F4TProxyAnimationSync.h"
#include "F4TProxyAnimationSyncInternal.h"

#include "F4TNetworking.h"
#include "F4TAnimationGraphOverride.h"
#include "F4TDebugLogging.h"
#include "F4AnimationDescriptor.h"
#include "F4TProxyMovementDirection.h"
#include "F4TProxyNoTalk.h"
#include "F4TProxyPuppet.h"
#include "F4TProxyRemoteAnimationState.h"
#include "F4TWeaponClassification.h"

#include "RE/A/AIProcess.h"
#include "RE/B/BGSAnimationSystemUtils.h"
#include "RE/B/BSFixedString.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/T/TESObjectWEAP.h"
#include "RE/T/TESObjectREFR.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

using namespace F4T::ProxyAnimationSync::detail;

namespace
{
	constexpr bool kEnableProxyVisualAnimationSync = true;
	constexpr bool kEnableProxyLocomotionSync = true;
	constexpr bool kEnableProxyJumpSync = true;
	constexpr bool kEnableProxyWeaponDrawnSync = true;

	constexpr float kJumpLandRunSpeedThreshold = 150.0F;
	constexpr float kGraphSpeedAgreementRatio = 0.4F;
	constexpr float kWalkTierMax = 90.0F;
	constexpr float kRunGraphSpeedThreshold = F4T::ProxyMovementSmoothing::kRunGaitSpeed;

	constexpr float kGraphSpeedSprintFloor = 350.0F;

	namespace GraphEvent
	{
		inline constexpr const char* kMoveStop = "moveStop";
		inline constexpr const char* kSprintStart = "sprintStart";
		inline constexpr const char* kSprintStop = "sprintStop";
		inline constexpr const char* kSneakStart = "sneakStart";
		inline constexpr const char* kSneakStop = "sneakStop";
		inline constexpr const char* kWalk = "Walk";
		inline constexpr const char* kJog = "Jog";
		inline constexpr const char* kRun = "Run";
		inline constexpr const char* kJumpStart = "jumpStart";
		inline constexpr const char* kJumpStartFromWalk = "jumpStartFromWalk";
		inline constexpr const char* kJumpStartFromRun = "jumpStartFromRun";
		inline constexpr const char* kJumpUp = "JumpUp";
		inline constexpr const char* kJumpDown = "JumpDown";
		inline constexpr const char* kJumpLand = "jumpLand";
		inline constexpr const char* kJumpLandToWalk = "jumpLandToWalk";
		inline constexpr const char* kJumpLandToRun = "jumpLandToRun";
		inline constexpr const char* kJumpEnd = "jumpEnd";
		inline constexpr const char* kWeaponDraw = "weaponDraw";
		inline constexpr const char* kReadyStateEnter = "readyStateEnter";
		inline constexpr const char* kGunDownStateEnter = "gunDownStateEnter";
		inline constexpr const char* kGunDownStateExit = "gunDownStateExit";
		inline constexpr const char* kFireSingle = "fireSingle";
		inline constexpr const char* kMeleeAttackStart = "meleeattackStart";
	}

	const char* GetLocomotionTierEventName(F4T::ProxyAnimationSync::LocomotionTier a_tier)
	{
		switch (a_tier) {
		case F4T::ProxyAnimationSync::LocomotionTier::kWalk:
			return GraphEvent::kWalk;
		case F4T::ProxyAnimationSync::LocomotionTier::kJog:
			return GraphEvent::kJog;
		case F4T::ProxyAnimationSync::LocomotionTier::kRun:
			return GraphEvent::kRun;
		default:
			return GraphEvent::kJog;
		}
	}

	bool HasDesiredStateChanged(
		const F4T::ProxyAnimationSync::ProxyAppliedAnimationState& a_state,
		const DesiredProxyAnimationState& a_desired)
	{
		if (!a_state.hasAppliedGraphState) {
			return true;
		}

		const auto currentTier =
			F4T::ProxyAnimationSync::ComputeLocomotionTier(a_desired.graphSpeed, a_desired.isSprinting);

		const bool tierChanged =
			a_desired.isMoving &&
			(!a_state.lastLocomotionTier || *a_state.lastLocomotionTier != currentTier);

		return a_state.lastIsMoving != a_desired.isMoving ||
		       a_state.lastIsSprinting != a_desired.isSprinting ||
		       a_state.lastIsSneaking != a_desired.isSneaking ||
		       a_state.lastIsJumping != a_desired.isJumping ||
		       a_state.lastIsCrouching != a_desired.isCrouching ||
		       a_state.lastWeaponDrawn != a_desired.weaponDrawn ||
		       tierChanged ||
		       AreFloatsDifferent(a_state.lastGraphSpeed, a_desired.graphSpeed) ||
		       std::fabs(std::remainder(a_desired.direction - a_state.lastDirection, 2.0F * std::numbers::pi_v<float>)) >
		           F4T::ProxyRemoteAnimationState::kDirectionEpsilonRadians;
	}

	bool ApplyDesiredStateToGraph(
		RE::Actor& a_proxy,
		const DesiredProxyAnimationState& a_desired,
		std::uint32_t a_remotePlayerId)
	{
		auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
		const auto actorId = a_proxy.GetFormID();
		auto success = true;

		if constexpr (kEnableProxyLocomotionSync) {
			const auto graphSpeed = ClampGraphSpeed(a_desired.graphSpeed);
			const auto speedWritten = TrySetGraphFloat(*graphHolder, F4T::ProxyAnimationSync::GraphVar::kSpeed, graphSpeed);
			const auto speedSmoothedWritten =
				TrySetGraphFloat(*graphHolder, F4T::ProxyAnimationSync::GraphVar::kSpeedSmoothed, graphSpeed);
			const auto sprintWritten =
				TrySetGraphBool(*graphHolder, F4T::ProxyAnimationSync::GraphVar::kIsSprinting, a_desired.isSprinting);
			success &= speedWritten && speedSmoothedWritten && sprintWritten;
			success &= TrySetGraphFloat(*graphHolder, F4T::ProxyAnimationSync::GraphVar::kDirection, F4T::ProxyMovementDirection::RadiansToGraphDirection(a_desired.direction));

			if (a_desired.isMoving) {
				float readBackSpeed = 0.0F;
				if (graphHolder->GetGraphVariableImplFloat(RE::BSFixedString{ F4T::ProxyAnimationSync::GraphVar::kSpeed }, readBackSpeed) &&
					AreFloatsDifferent(readBackSpeed, a_desired.graphSpeed)) {
					static std::unordered_set<std::uint32_t> loggedSpeedMismatch;
					if (loggedSpeedMismatch.insert(a_remotePlayerId).second) {
						LogWarningWithLocalPlayerPrefix(std::format(
							"Runtime proxy animation sync Speed read-back mismatch for remote player {}: actor={:08X}, requested={:.1f}, readBack={:.1f}.",
							a_remotePlayerId,
							actorId,
							a_desired.graphSpeed,
							readBackSpeed));
					}
				}
			}
		}

		(void)a_desired.isSneaking;

		if constexpr (kEnableProxyWeaponDrawnSync) {
			const auto weaponArchetype = F4T::WeaponClassification::ClassifyEquippedRightHandWeapon(a_proxy);
			if (F4T::WeaponClassification::UseWeaponBehavior(weaponArchetype)) {
				RE::IAnimationGraphManagerHolder* weaponGraphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
				const std::int32_t gunDownValue = a_desired.weaponDrawn ? 0 : 1;
				success &= TrySetGraphInt(*weaponGraphHolder, F4T::ProxyAnimationSync::GraphVar::kSyncGunDown, gunDownValue);
			}
		}

		if (!success) {
			static std::unordered_set<std::uint32_t> loggedWriteFailures;
			if (loggedWriteFailures.insert(a_remotePlayerId).second) {
				LogWarningWithLocalPlayerPrefix(std::format(
					"Runtime proxy animation sync graph write returned false for remote player {}: actor={:08X}.",
					a_remotePlayerId,
					actorId));
			}
		}

		return success;
	}

	bool ApplySneakStateToGraph(RE::Actor& a_proxy, bool a_isSneaking)
	{
		auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
		return TrySetGraphInt(
			*graphHolder,
			F4T::ProxyAnimationSync::GraphVar::kIsInSneak,
			a_isSneaking ? 1 : 0);
	}

	bool ApplyJumpStateToGraph(
		RE::IAnimationGraphManagerHolder& a_holder,
		bool a_isJumping)
	{
		auto success = true;
		success &= TrySetGraphBool(a_holder, F4T::ProxyAnimationSync::GraphVar::kInJumpState, a_isJumping);

		const RE::BSFixedString syncJumpName{ F4T::ProxyAnimationSync::GraphVar::kSyncJumpState };
		success &= a_holder.SetGraphVariableInt(
			syncJumpName,
			a_isJumping ? 1 : 0);

		return success;
	}

	bool ApplyDesiredStateToGraphDescriptorBased(
		RE::Actor& a_proxy,
		const DesiredProxyAnimationState& a_desired,
		std::uint32_t a_remotePlayerId)
	{
		auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
		const auto actorId = a_proxy.GetFormID();

		if (a_desired.isJumping) {
			return ApplyJumpStateToGraph(*graphHolder, true);
		}

		if (a_desired.weaponDrawn) {
			return ApplyDesiredStateToGraph(a_proxy, a_desired, a_remotePlayerId);
		}

		auto& descriptor = F4T::AnimationDescriptor::F4AnimationDescriptor::GetHumanoidDescriptor();
		F4T::AnimationDescriptor::AnimationVariableSnapshot snapshot;

		snapshot.floats.resize(descriptor.GetFloatVariableCount(), 0.0F);
		snapshot.bools.resize(descriptor.GetBoolVariableCount(), false);
		snapshot.ints.resize(descriptor.GetIntVariableCount(), 0);

		PopulateLocomotionSpeedSnapshot(snapshot, descriptor, a_desired.graphSpeed);

		const auto isSprintingIdx = descriptor.GetBoolVariableIndex(F4T::ProxyAnimationSync::GraphVar::kIsSprinting);
		if (isSprintingIdx != static_cast<std::size_t>(-1)) {
			snapshot.bools[isSprintingIdx] = a_desired.isSprinting;
		}

		if constexpr (kEnableProxyJumpSync) {
			const auto inJumpIdx = descriptor.GetBoolVariableIndex(F4T::ProxyAnimationSync::GraphVar::kInJumpState);
			if (inJumpIdx != static_cast<std::size_t>(-1)) {
				snapshot.bools[inJumpIdx] = false;
			}

			const auto syncJumpIdx = descriptor.GetIntVariableIndex(F4T::ProxyAnimationSync::GraphVar::kSyncJumpState);
			if (syncJumpIdx != static_cast<std::size_t>(-1)) {
				snapshot.ints[syncJumpIdx] = 0U;
			}
		}

		if constexpr (kEnableProxyLocomotionSync) {
			const auto tier = static_cast<std::int32_t>(
				F4T::ProxyAnimationSync::ComputeLocomotionTier(a_desired.graphSpeed, a_desired.isSprinting));
			const auto tierValue = static_cast<std::uint32_t>(tier);

			const auto syncWalkRunIdx = descriptor.GetIntVariableIndex(F4T::ProxyAnimationSync::GraphVar::kSyncWalkRun);
			if (syncWalkRunIdx != static_cast<std::size_t>(-1)) {
				snapshot.ints[syncWalkRunIdx] = tierValue;
			}

			const auto syncLocomotionIdx =
				descriptor.GetIntVariableIndex(F4T::ProxyAnimationSync::GraphVar::kSyncLocomotionSpeed);
			if (syncLocomotionIdx != static_cast<std::size_t>(-1)) {
				snapshot.ints[syncLocomotionIdx] = tierValue;
			}
		}

		auto success = descriptor.LoadAnimationVariablesToCache(*graphHolder, snapshot);
		success &= TrySetGraphFloat(*graphHolder, F4T::ProxyAnimationSync::GraphVar::kDirection, F4T::ProxyMovementDirection::RadiansToGraphDirection(a_desired.direction));

		if (!success) {
			static std::unordered_set<std::uint32_t> loggedWriteFailures;
			if (loggedWriteFailures.insert(a_remotePlayerId).second) {
				LogWarningWithLocalPlayerPrefix(std::format(
					"Runtime proxy animation sync (descriptor-based) failed for remote player {}: actor={:08X}.",
					a_remotePlayerId,
					actorId));
			}
		}

		return success;
	}

	bool SendLocomotionTransitionEvents(
		RE::Actor& a_proxy,
		const F4T::ProxyAnimationSync::ProxyAppliedAnimationState& a_previous,
		const DesiredProxyAnimationState& a_desired,
		std::uint32_t a_remotePlayerId,
		std::uint32_t& a_walkStartSends)
	{
		if constexpr (!kEnableProxyLocomotionSync) {
			return false;
		}

		const auto actorId = a_proxy.GetFormID();
		const bool wasInitialized = a_previous.hasAppliedGraphState;
		bool walkStartRefused = false;

		auto fireEvent = [&](const char* a_eventName) {
			const bool accepted = TrySendGraphEvent(a_proxy, a_eventName);
			LogInfoWithLocalPlayerPrefix(std::format(
				"Runtime proxy locomotion event '{}' for remote player {}: actor={:08X}, graphAccepted={}.",
				a_eventName,
				a_remotePlayerId,
				actorId,
				accepted));
		};

		const auto currentTier =
			F4T::ProxyAnimationSync::ComputeLocomotionTier(a_desired.graphSpeed, a_desired.isSprinting);

		if constexpr (kEnableProxyJumpSync) {
			auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
			if (!wasInitialized || a_previous.lastIsJumping != a_desired.isJumping) {
				if (a_desired.isJumping) {
					const bool wasLocomoting =
						a_previous.lastIsMoving || a_previous.lastLocomotionTier.has_value();
					if (wasLocomoting) {
						const auto jumpTier = a_previous.lastLocomotionTier.value_or(currentTier);
						switch (jumpTier) {
						case F4T::ProxyAnimationSync::LocomotionTier::kRun:
							fireEvent(GraphEvent::kJumpStartFromRun);
							break;
						case F4T::ProxyAnimationSync::LocomotionTier::kWalk:
							fireEvent(GraphEvent::kJumpStartFromWalk);
							break;
						default:
							fireEvent(GraphEvent::kJumpStart);
							break;
						}
					} else {
						fireEvent(GraphEvent::kJumpStart);
					}
					fireEvent(GraphEvent::kJumpUp);
					ApplyJumpStateToGraph(*graphHolder, true);
				} else if (wasInitialized) {
					const auto landTier = a_previous.lastLocomotionTier.value_or(currentTier);
					fireEvent(GraphEvent::kJumpDown);
					fireEvent(GraphEvent::kJumpLand);
					const auto remoteMoving = a_desired.isMoving;
					if (remoteMoving) {
						if (landTier == F4T::ProxyAnimationSync::LocomotionTier::kRun) {
							fireEvent(GraphEvent::kJumpLandToRun);
						} else {
							fireEvent(GraphEvent::kJumpLandToWalk);
						}
						walkStartRefused = !F4T::ProxyWalkStart::SendEvents(a_proxy, a_desired.isSneaking, a_remotePlayerId, ++a_walkStartSends);
						fireEvent(GetLocomotionTierEventName(landTier));
					} else {
						fireEvent(GraphEvent::kJumpEnd);
						fireEvent(GraphEvent::kMoveStop);
					}
					ApplyJumpStateToGraph(*graphHolder, false);
				}
			}
		}

		const bool justLanded = wasInitialized && a_previous.lastIsJumping && !a_desired.isJumping;
		if (!a_desired.isJumping) {
			const bool startedMoving =
				!justLanded &&
				a_desired.isMoving && (!wasInitialized || !a_previous.lastIsMoving);
			const bool stoppedMoving =
				wasInitialized && a_previous.lastIsMoving && !a_desired.isMoving && !a_previous.lastIsJumping;
			const bool tierChanged =
				a_desired.isMoving &&
				(!a_previous.lastLocomotionTier || *a_previous.lastLocomotionTier != currentTier);

			if (startedMoving) {
				walkStartRefused = !F4T::ProxyWalkStart::SendEvents(a_proxy, a_desired.isSneaking, a_remotePlayerId, ++a_walkStartSends);
				fireEvent(GetLocomotionTierEventName(currentTier));
			} else if (stoppedMoving) {
				fireEvent(GraphEvent::kMoveStop);
			} else if (tierChanged) {
				fireEvent(GetLocomotionTierEventName(currentTier));
			}

			if (wasInitialized && a_previous.lastIsSprinting != a_desired.isSprinting) {
				fireEvent(a_desired.isSprinting ? GraphEvent::kSprintStart : GraphEvent::kSprintStop);
			}
		}

		if constexpr (kEnableProxyWeaponDrawnSync) {
			RE::BGSEquipIndex rightHandIndex;
			rightHandIndex.index = 0;
			RE::BGSObjectInstance equippedItem{ nullptr, nullptr };
			const auto* equippedWeapon = a_proxy.GetEquippedItem(&equippedItem, rightHandIndex);
			const bool hasWeaponEquipped = equippedWeapon && equippedWeapon->object && equippedWeapon->object->Is(RE::ENUM_FORM_ID::kWEAP);

			const auto weaponArchetype = hasWeaponEquipped
				? F4T::WeaponClassification::ClassifyWeapon(static_cast<RE::TESObjectWEAP*>(equippedWeapon->object))
				: F4T::WeaponClassification::WeaponAnimationType::kUnarmed;

			{
				static std::unordered_set<std::uint32_t> loggedWeaponGraphDiag;
				if (loggedWeaponGraphDiag.insert(a_remotePlayerId).second) {
					auto* graphManager =
						a_proxy.currentProcess && a_proxy.currentProcess->middleHigh
							? a_proxy.currentProcess->middleHigh->animationGraphManager.get()
							: nullptr;
					if (graphManager) {
						const auto weaponFormId =
							hasWeaponEquipped ? equippedWeapon->object->GetFormID() : 0U;
						LogInfoWithLocalPlayerPrefix(std::format(
							"Runtime proxy weapon graph DIAGNOSTIC for remote player {}: actor={:08X}, weaponFormId={:08X}, archetype={}, loadedGraphCount={}, boundChannelCount={}.",
							a_remotePlayerId,
							actorId,
							weaponFormId,
							F4T::WeaponClassification::GetArchetypeLabel(weaponArchetype),
							graphManager->graph.size(),
							graphManager->boundChannel.size()));
					}
				}
			}

			if (F4T::WeaponClassification::UseMeleeBehavior(weaponArchetype)) {
				const bool weaponDrawStateChanged =
					(!wasInitialized && a_desired.weaponDrawn) ||
					(wasInitialized && a_previous.lastWeaponDrawn != a_desired.weaponDrawn);
				if (hasWeaponEquipped && weaponDrawStateChanged) {
					RE::BGSEquipIndex equipIndex;
					equipIndex.index = 0;
					auto* weapon = static_cast<RE::TESObjectWEAP*>(equippedWeapon->object);
					if (a_desired.weaponDrawn) {
						RE::BGSObjectInstanceT<RE::TESObjectWEAP> weaponInstance{ weapon, equippedWeapon->instanceData.get() };
						a_proxy.AttachWeapon(weaponInstance, equipIndex);
					}
					a_proxy.DoReparentWeapon(weapon, equipIndex, a_desired.weaponDrawn);
					a_proxy.DrawWeaponMagicHands(a_desired.weaponDrawn);

					if (a_desired.weaponDrawn) {
						fireEvent(GraphEvent::kWeaponDraw);
						fireEvent(GraphEvent::kReadyStateEnter);
					}
				}
			}
			else if (weaponArchetype == F4T::WeaponClassification::WeaponAnimationType::kUnarmed) {
				if ((!wasInitialized && a_desired.weaponDrawn) ||
					(wasInitialized && a_previous.lastWeaponDrawn != a_desired.weaponDrawn)) {
					if (a_desired.weaponDrawn) {
						fireEvent(GraphEvent::kReadyStateEnter);
					}
				}
			}
			else if (F4T::WeaponClassification::IsRangedArchetype(weaponArchetype)) {
				const bool weaponDrawStateChanged =
					(!wasInitialized && a_desired.weaponDrawn) ||
					(wasInitialized && a_previous.lastWeaponDrawn != a_desired.weaponDrawn);
				if (hasWeaponEquipped && weaponDrawStateChanged) {
					RE::BGSEquipIndex equipIndex;
					equipIndex.index = 0;
					auto* weapon = static_cast<RE::TESObjectWEAP*>(equippedWeapon->object);
					if (a_desired.weaponDrawn) {
						RE::BGSObjectInstanceT<RE::TESObjectWEAP> weaponInstance{ weapon, equippedWeapon->instanceData.get() };
						a_proxy.AttachWeapon(weaponInstance, equipIndex);
					}
					a_proxy.DoReparentWeapon(weapon, equipIndex, a_desired.weaponDrawn);
					a_proxy.DrawWeaponMagicHands(a_desired.weaponDrawn);
					if (a_desired.weaponDrawn) {
						fireEvent(GraphEvent::kWeaponDraw);
						fireEvent(GraphEvent::kReadyStateEnter);
					} else {
						fireEvent(GraphEvent::kGunDownStateEnter);
					}
				}
			}
		}

		switch (F4T::ProxyRemoteAnimationState::DetectSneakTransition(
			wasInitialized,
			a_previous.lastIsSneaking,
			a_desired.isSneaking)) {
		case F4T::ProxyRemoteAnimationState::SneakTransition::kStart:
			fireEvent(GraphEvent::kSneakStart);
			break;
		case F4T::ProxyRemoteAnimationState::SneakTransition::kStop:
			fireEvent(GraphEvent::kSneakStop);
			break;
		case F4T::ProxyRemoteAnimationState::SneakTransition::kNone:
			break;
		}

		return walkStartRefused;
	}

}

namespace F4T::ProxyAnimationSync
{
	bool TrySendProxyGraphEvent(RE::Actor& a_proxy, std::string_view a_eventName)
	{
		return detail::TrySendGraphEvent(a_proxy, a_eventName.data());
	}

	float MapMovementSpeedToGraphSpeed(float a_movementSpeed, bool a_isSprinting)
	{
		if (a_movementSpeed < kIdleSpeedThreshold) {
			return kGraphSpeedIdle;
		}

		float graphSpeed = a_movementSpeed;
		if (a_isSprinting) {
			graphSpeed = std::max(graphSpeed, kGraphSpeedSprintFloor);
		}

		return ClampGraphSpeed(graphSpeed);
	}

	float ResolveLocomotionGraphSpeed(
		float a_movementSpeed,
		float a_animationGraphSpeed,
		bool a_isMoving,
		bool a_isSprinting)
	{
		if (!a_isMoving || a_movementSpeed < kIdleSpeedThreshold) {
			return kGraphSpeedIdle;
		}

		auto graphSpeed = MapMovementSpeedToGraphSpeed(a_movementSpeed, a_isSprinting);

		if (a_animationGraphSpeed > kIdleSpeedThreshold && std::isfinite(a_animationGraphSpeed)) {
			const float baseline = std::max(a_movementSpeed, 1.0F);
			const float divergence = std::fabs(a_animationGraphSpeed - a_movementSpeed) / baseline;
			if (divergence <= kGraphSpeedAgreementRatio) {
				graphSpeed = ClampGraphSpeed(a_animationGraphSpeed);
			}
		}

		return graphSpeed;
	}

	void ResetAppliedAnimationState(ProxyAppliedAnimationState& a_state)
	{
		if (a_state.walkStart.pending) {
			F4T::DebugLogging::Log(
				"ProxyAnim",
				"[RT-VERIFY 364-B] pending walk start retry dropped by applied state reset: refusedTicks={}.",
				a_state.walkStart.refusedTicks);
		}
		a_state = {};
	}

	void ApplyProxyAnimationFromRemoteState(
		ProxyAppliedAnimationState& a_state,
		RE::Actor& a_proxy,
		const RemotePlayerState::RemotePlayerState& a_remotePlayer,
		F4T::ProxyMovementSmoothing::Gait a_smoothedGait)
	{
		if constexpr (!kEnableProxyVisualAnimationSync) {
			return;
		}

		const auto actorId = a_proxy.GetFormID();
		if (!CanWriteProxyGraph(a_proxy, a_remotePlayer.playerId, actorId)) {
			return;
		}

		auto desired = BuildDebouncedDesiredState(a_state, a_remotePlayer);
		desired.isSneaking = desired.isSneaking || desired.isCrouching;
		if (desired.isMoving) {
			desired.graphSpeed = F4T::ProxyMovementSmoothing::GaitGraphSpeed(desired.graphSpeed, a_smoothedGait);
		}
		const auto velocity = F4T::ProxyPuppet::GetProxyLocomotionVelocity(actorId);
		desired.direction = F4T::ProxyMovementDirection::RelativeMovementDirection(
			velocity.x, velocity.y, a_proxy.GetHeading(), kIdleSpeedThreshold);
		if (a_remotePlayer.hasAnimationDirection) {
			desired.direction = F4T::ProxyRemoteAnimationState::AnimationDirectionDegreesToRadians(
				a_remotePlayer.animationDirection);
		}

		const float puppetSpeed =
			(desired.isJumping || !desired.isMoving) ? -1.0F : desired.graphSpeed;
		F4T::ProxyPuppet::SetProxyLocomotionState(actorId, puppetSpeed, a_remotePlayer.angleZ);
		if (a_remotePlayer.hasAimState) {
			F4T::ProxyPuppet::SetProxyAimState(actorId, a_remotePlayer.aimPitch, a_remotePlayer.turnDelta);
		}
		if (!F4T::AnimationGraphOverride::Installed()) {
			F4T::AnimationGraphOverride::Install(&a_proxy);
		}
		F4T::ProxyNoTalk::Install(&a_proxy);
		if (F4T::AnimationGraphOverride::Installed()) {
			F4T::AnimationGraphOverride::DetachMovementChannels(&a_proxy);
			F4T::AnimationGraphOverride::SetProxySpeed(
				&a_proxy, desired.isMoving ? desired.graphSpeed : 0.0F, desired.direction);
			const float aimDegrees = a_remotePlayer.hasAimState ? a_remotePlayer.aimPitch : 0.0F;
			F4T::AnimationGraphOverride::SetProxyPitch(&a_proxy, aimDegrees);
			// TODO-verify: confirm the pitch sign on AE (positive aimPitch = look up or down).
			if (F4T::AnimationGraphOverride::TakeProxyResync(&a_proxy) && desired.isMoving) {
				a_state.walkStart.pending = true;
			}
		}

		{
			static std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> s_lastDiag;
			const auto now = std::chrono::steady_clock::now();
			auto& last = s_lastDiag[a_remotePlayer.playerId];
			if (last.time_since_epoch().count() == 0 || (now - last) >= std::chrono::milliseconds{ 500 }) {
				last = now;
				auto* diagHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
				float readSpeed = -1.0F;
				float readSpeedSmoothed = -1.0F;
				const auto haveSpeed =
					diagHolder->GetGraphVariableImplFloat(RE::BSFixedString{ F4T::ProxyAnimationSync::GraphVar::kSpeed }, readSpeed);
				const auto haveSpeedSmoothed =
					diagHolder->GetGraphVariableImplFloat(RE::BSFixedString{ F4T::ProxyAnimationSync::GraphVar::kSpeedSmoothed }, readSpeedSmoothed);
				LogInfoWithLocalPlayerPrefix(std::format(
					"Proxy anim diag p{} actor={:08X}: recvMoving={}, recvSprint={}, recvMovementSpeed={:.1f}, recvAnimGraphSpeed={:.1f}, computedGraphSpeed={:.1f}, graphSpeedReadBack={}({:.1f}), speedSmoothedReadBack={}({:.1f}).",
					a_remotePlayer.playerId,
					actorId,
					a_remotePlayer.isMoving,
					a_remotePlayer.isSprinting,
					a_remotePlayer.movementSpeed,
					a_remotePlayer.animationGraphSpeed,
					desired.graphSpeed,
					haveSpeed,
					readSpeed,
					haveSpeedSmoothed,
					readSpeedSmoothed));
			}
		}

		const auto now = std::chrono::steady_clock::now();
		const auto changed = HasDesiredStateChanged(a_state, desired);
		const auto sneakReassertDue = desired.isSneaking && now >= a_state.sneakReassertDue;
		const auto shouldApply = F4T::ProxyRemoteAnimationState::ShouldApplyProxyAnimation(
			a_state.hasAppliedGraphState,
			changed,
			desired.isJumping,
			sneakReassertDue);

		F4T::ProxyWalkStart::RetryPending(
			a_state.walkStart,
			a_proxy,
			desired.isMoving,
			desired.isJumping,
			desired.isSneaking,
			a_remotePlayer.playerId);
		if (!shouldApply) {
			return;
		}

		const auto initial = !a_state.hasAppliedGraphState;
		(void)ApplyDesiredStateToGraphDescriptorBased(a_proxy, desired, a_remotePlayer.playerId);

		const auto sneakGraphWritten = ApplySneakStateToGraph(a_proxy, desired.isSneaking);
		const auto traceSneakWrite = F4T::ProxySneakAction::ShouldTraceCount(++a_state.sneakGraphWrites);
		const auto sneakChanged = a_state.lastIsSneaking != desired.isSneaking;
		if (initial || sneakChanged || traceSneakWrite) {
			F4T::DebugLogging::Log(
				"ProxyAnim",
				"[RT-VERIFY 364-B] iIsInSneak graph write for remote player {} actor={:08X}: sneaking={}, written={}, writeCount={}.",
				a_remotePlayer.playerId,
				actorId,
				desired.isSneaking,
				sneakGraphWritten,
				a_state.sneakGraphWrites);
		}
		if (desired.isSneaking) {
			a_state.sneakReassertDue = now + F4T::ProxyRemoteAnimationState::kSneakReassertInterval;
		}

		const auto walkStartRefused = SendLocomotionTransitionEvents(a_proxy, a_state, desired, a_remotePlayer.playerId, a_state.walkStartSends);
		if (walkStartRefused) {
			a_state.walkStart.pending = true;
			a_state.walkStart.refusedTicks = 0;
			a_state.walkStart.stanceResentFor.reset();
		}

		if (initial || changed) {
			LogAppliedTransition(a_remotePlayer.playerId, actorId, desired, initial);
		}

		a_state.hasAppliedGraphState = true;
		a_state.lastIsMoving = desired.isMoving;
		a_state.lastIsSprinting = desired.isSprinting;
		a_state.lastIsSneaking = desired.isSneaking;
		a_state.lastIsJumping = desired.isJumping;
		a_state.lastIsCrouching = desired.isCrouching;
		a_state.lastWeaponDrawn = desired.weaponDrawn;
		a_state.lastGraphSpeed = desired.graphSpeed;
		a_state.lastDirection = desired.direction;
		if (desired.isJumping) {
		} else if (desired.isMoving) {
			a_state.lastLocomotionTier =
				ComputeLocomotionTier(desired.graphSpeed, desired.isSprinting);
		} else {
			a_state.lastLocomotionTier.reset();
		}
	}

	LocomotionTier ComputeLocomotionTier(float a_graphSpeed, bool a_isSprinting)
	{
		if (a_isSprinting || a_graphSpeed >= kRunGraphSpeedThreshold) {
			return LocomotionTier::kRun;
		}
		if (a_graphSpeed > kWalkTierMax) {
			return LocomotionTier::kJog;
		}
		return LocomotionTier::kWalk;
	}

	void ClearProxyAnimationState(
		ProxyAppliedAnimationState& a_state,
		RE::Actor& a_proxy,
		std::uint32_t a_remotePlayerId,
		std::string_view a_reason)
	{
		if constexpr (!kEnableProxyVisualAnimationSync) {
			ResetAppliedAnimationState(a_state);
			return;
		}

		F4T::AnimationGraphOverride::ClearProxy(&a_proxy);
		if (!a_state.hasAppliedGraphState) {
			ResetAppliedAnimationState(a_state);
			return;
		}

		const auto actorId = a_proxy.GetFormID();
		if (CanWriteProxyGraph(a_proxy, a_remotePlayerId, actorId)) {
			const DesiredProxyAnimationState idle{};
			if (ApplyDesiredStateToGraph(a_proxy, idle, a_remotePlayerId)) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Runtime proxy animation sync cleared on {} for remote player {}: actor={:08X}.",
					a_reason,
					a_remotePlayerId,
					actorId));
			}
		}

		ResetAppliedAnimationState(a_state);
	}
}
