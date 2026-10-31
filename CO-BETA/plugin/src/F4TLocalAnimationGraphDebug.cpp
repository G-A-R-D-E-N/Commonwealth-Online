#include "pch.h"

#include "F4TLocalAnimationGraphDebug.h"

#include "F4TNetworking.h"
#include "F4TDebugLogging.h"

#include "RE/A/ACTOR_STANCE.h"
#include "RE/B/BGSAnimationSystemUtils.h"
#include "RE/B/BSFixedString.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/P/PlayerCharacter.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{
	constexpr auto kCandidateGraphVariables = std::to_array<std::string_view>({
		"IsSneaking",
		"bIsSneaking",
		"IsCrouching",
		"bIsCrouching",
		"Sneaking",
		"bSneaking",
		"IsMoving",
		"bIsMoving",
		"Speed",
		"speed",
		"Direction",
		"fSpeed",
		"bSprint",
		"IsSprinting",
		"IsJumping",
		"bInJumpState",
		"WeaponDrawn",
		"bWeaponDrawn",
		"iSyncGunDown",
		"iRifleDrawnStateID",
		"RifleDrawnCurrentState",
		"iAttackState",
		"isFiring",
		"iSyncFireState",
		"IsAttackReady",
		"GunGripPointer",
	});

	struct LocalAnimationGraphState
	{
		bool isMoving{ false };
		bool isSprinting{ false };
		bool isSneaking{ false };
		bool isJumping{ false };
		bool weaponDrawn{ false };
		bool playerCharacterIsSneaking{ false };
		bool actorIsSneaking{ false };
		bool apiJumping{ false };
		bool apiSprinting{ false };
		bool apiWeaponDrawn{ false };
		bool actorStateForceSneak{ false };
		std::uint32_t actorStateStance{ 0 };
		std::int32_t weaponState{ 0 };
		std::int32_t gunState{ 0 };
		float movementSpeed{ 0.0F };
		bool graphManagerAvailable{ false };
		bool graphInTransition{ false };
	};

	struct CandidateReadState
	{
		bool boolUnavailableLogged{ false };
		bool intUnavailableLogged{ false };
		bool floatUnavailableLogged{ false };

		bool hasBoolValue{ false };
		bool lastBoolValue{ false };

		bool hasIntValue{ false };
		std::int32_t lastIntValue{ 0 };

		bool hasFloatValue{ false };
		float lastFloatValue{ 0.0F };
	};

	using CandidateReadStates = std::array<CandidateReadState, kCandidateGraphVariables.size()>;

	constexpr auto kProxyGraphDumpInterval = std::chrono::milliseconds{ 500 };
	constexpr auto kProxyGraphStateRetention = std::chrono::seconds{ 60 };

	std::string GetLocalPlayerLogMessage(std::string_view a_message)
	{
		return F4T::Networking::GetLocalPlayerLogPrefix() + " " + std::string(a_message);
	}

	void LogInfoWithLocalPlayerPrefix(std::string_view a_message)
	{
		F4T::DebugLogging::Write("AnimationGraph", GetLocalPlayerLogMessage(a_message));
	}

	bool AreFloatsDifferent(float a_lhs, float a_rhs)
	{
		if (!std::isfinite(a_lhs) || !std::isfinite(a_rhs)) {
			return a_lhs != a_rhs;
		}

		return std::fabs(a_lhs - a_rhs) > 0.001F;
	}

	bool HasRelevantStateChange(
		const std::optional<LocalAnimationGraphState>& a_lastState,
		const LocalAnimationGraphState& a_currentState)
	{
		if (!a_lastState) {
			return true;
		}

		const auto& lastState = *a_lastState;
		return lastState.isMoving != a_currentState.isMoving ||
		       lastState.isSprinting != a_currentState.isSprinting ||
		       lastState.isSneaking != a_currentState.isSneaking ||
		       lastState.isJumping != a_currentState.isJumping ||
		       lastState.weaponDrawn != a_currentState.weaponDrawn ||
		       lastState.playerCharacterIsSneaking != a_currentState.playerCharacterIsSneaking ||
		       lastState.actorIsSneaking != a_currentState.actorIsSneaking ||
		       lastState.apiJumping != a_currentState.apiJumping ||
		       lastState.apiSprinting != a_currentState.apiSprinting ||
		       lastState.apiWeaponDrawn != a_currentState.apiWeaponDrawn ||
		       lastState.actorStateForceSneak != a_currentState.actorStateForceSneak ||
		       lastState.actorStateStance != a_currentState.actorStateStance ||
		       lastState.weaponState != a_currentState.weaponState ||
		       lastState.gunState != a_currentState.gunState;
	}

	bool HasGraphMetadataChange(
		const std::optional<LocalAnimationGraphState>& a_lastState,
		const LocalAnimationGraphState& a_currentState)
	{
		if (!a_lastState) {
			return true;
		}

		const auto& lastState = *a_lastState;
		return lastState.graphManagerAvailable != a_currentState.graphManagerAvailable ||
		       lastState.graphInTransition != a_currentState.graphInTransition;
	}

	void LogStateSnapshot(
		std::string_view a_reason,
		const LocalAnimationGraphState& a_state)
	{
		const auto actorStateSneaking =
			a_state.actorStateStance == static_cast<std::uint32_t>(RE::ACTOR_STANCE::kSneaking);

		LogInfoWithLocalPlayerPrefix(std::format(
			"Local animation graph debug: {} moving={}, sprinting={}, sneaking={}, jumping={}, weaponDrawn={}, movementSpeed={:.1f}, playerCharacterIsSneaking={}, actorIsSneaking={}, actorStateSneaking={}, actorStateStance={}, actorStateForceSneak={}, apiJumping={}, apiSprinting={}, apiWeaponDrawn={}, weaponState={}, gunState={}, graphManagerAvailable={}, graphInTransition={}.",
			a_reason,
			a_state.isMoving,
			a_state.isSprinting,
			a_state.isSneaking,
			a_state.isJumping,
			a_state.weaponDrawn,
			a_state.movementSpeed,
			a_state.playerCharacterIsSneaking,
			a_state.actorIsSneaking,
			actorStateSneaking,
			a_state.actorStateStance,
			a_state.actorStateForceSneak,
			a_state.apiJumping,
			a_state.apiSprinting,
			a_state.apiWeaponDrawn,
			a_state.weaponState,
			a_state.gunState,
			a_state.graphManagerAvailable,
			a_state.graphInTransition));
	}

	void ProbeBoolCandidate(
		const RE::IAnimationGraphManagerHolder& a_holder,
		std::string_view a_scope,
		std::string_view a_candidate,
		CandidateReadState& a_state,
		bool a_shouldLogSuccessfulValues)
	{
		const RE::BSFixedString variableName{ a_candidate.data() };
		bool value = false;
		if (!a_holder.GetGraphVariableImplBool(variableName, value)) {
			if (!a_state.boolUnavailableLogged) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"{} animation graph debug: candidate bool variable \"{}\" unavailable.",
					a_scope,
					a_candidate));
				a_state.boolUnavailableLogged = true;
			}
			return;
		}

		const auto changed = !a_state.hasBoolValue || a_state.lastBoolValue != value;
		if (changed || a_shouldLogSuccessfulValues) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"{} animation graph debug: candidate bool variable \"{}\"={}.",
				a_scope,
				a_candidate,
				value));
		}

		a_state.hasBoolValue = true;
		a_state.lastBoolValue = value;
	}

	void ProbeIntCandidate(
		const RE::IAnimationGraphManagerHolder& a_holder,
		std::string_view a_scope,
		std::string_view a_candidate,
		CandidateReadState& a_state,
		bool a_shouldLogSuccessfulValues)
	{
		const RE::BSFixedString variableName{ a_candidate.data() };
		std::int32_t value = 0;
		if (!a_holder.GetGraphVariableImplInt(variableName, value)) {
			if (!a_state.intUnavailableLogged) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"{} animation graph debug: candidate int variable \"{}\" unavailable.",
					a_scope,
					a_candidate));
				a_state.intUnavailableLogged = true;
			}
			return;
		}

		const auto changed = !a_state.hasIntValue || a_state.lastIntValue != value;
		if (changed || a_shouldLogSuccessfulValues) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"{} animation graph debug: candidate int variable \"{}\"={}.",
				a_scope,
				a_candidate,
				value));
		}

		a_state.hasIntValue = true;
		a_state.lastIntValue = value;
	}

	void ProbeFloatCandidate(
		const RE::IAnimationGraphManagerHolder& a_holder,
		std::string_view a_scope,
		std::string_view a_candidate,
		CandidateReadState& a_state,
		bool a_shouldLogSuccessfulValues)
	{
		const RE::BSFixedString variableName{ a_candidate.data() };
		float value = 0.0F;
		if (!a_holder.GetGraphVariableImplFloat(variableName, value)) {
			if (!a_state.floatUnavailableLogged) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"{} animation graph debug: candidate float variable \"{}\" unavailable.",
					a_scope,
					a_candidate));
				a_state.floatUnavailableLogged = true;
			}
			return;
		}

		const auto changed = !a_state.hasFloatValue || AreFloatsDifferent(a_state.lastFloatValue, value);
		if (changed || a_shouldLogSuccessfulValues) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"{} animation graph debug: candidate float variable \"{}\"={:.3f}.",
				a_scope,
				a_candidate,
				value));
		}

		a_state.hasFloatValue = true;
		a_state.lastFloatValue = value;
	}

	void ProbeCandidateGraphVariables(
		const RE::IAnimationGraphManagerHolder& a_holder,
		std::string_view a_scope,
		CandidateReadStates& a_candidateStates,
		bool a_shouldLogSuccessfulValues)
	{
		for (std::size_t index = 0; index < kCandidateGraphVariables.size(); ++index) {
			const auto candidate = kCandidateGraphVariables[index];
			auto& state = a_candidateStates[index];

			ProbeBoolCandidate(a_holder, a_scope, candidate, state, a_shouldLogSuccessfulValues);
			ProbeIntCandidate(a_holder, a_scope, candidate, state, a_shouldLogSuccessfulValues);
			ProbeFloatCandidate(a_holder, a_scope, candidate, state, a_shouldLogSuccessfulValues);
		}
	}

}

namespace F4T::LocalAnimationGraphDebug
{
	void UpdateLocalPlayerAnimationGraphDebug(
		RE::PlayerCharacter& a_player,
		bool a_isMoving,
		bool a_isSprinting,
		bool a_isSneaking,
		bool a_isJumping,
		bool a_weaponDrawn,
		float a_movementSpeed,
		std::chrono::steady_clock::time_point a_currentTime)
	{
		(void)a_currentTime;

		if (!F4T::DebugLogging::IsEnabled()) {
			return;
		}

		static std::optional<LocalAnimationGraphState> lastState{};
		static CandidateReadStates localCandidateStates{};

		auto& actor = static_cast<RE::Actor&>(a_player);
		auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_player);

		const auto graphManagerAvailable =
			actor.currentProcess &&
			actor.currentProcess->middleHigh &&
			actor.currentProcess->middleHigh->animationGraphManager.get() != nullptr;
		const auto graphInTransition =
			RE::BGSAnimationSystemUtils::IsActiveGraphInTransition(static_cast<RE::TESObjectREFR*>(&a_player));

		const LocalAnimationGraphState currentState{
			a_isMoving,
			a_isSprinting,
			a_isSneaking,
			a_isJumping,
			a_weaponDrawn,
			a_player.IsSneaking(),
			actor.IsSneaking(),
			a_player.IsJumping(),
			a_player.DoGetSprinting(),
			a_player.GetWeaponMagicDrawn(),
			a_player.forceSneak != 0,
			static_cast<std::uint32_t>(a_player.stance),
			static_cast<std::int32_t>(a_player.weaponState),
			static_cast<std::int32_t>(a_player.gunState),
			a_movementSpeed,
			graphManagerAvailable,
			graphInTransition
		};

		const auto relevantStateChanged = HasRelevantStateChange(lastState, currentState);
		const auto graphMetadataChanged = HasGraphMetadataChange(lastState, currentState);
		if (relevantStateChanged || graphMetadataChanged) {
			LogStateSnapshot(
				relevantStateChanged ? "state transition" : "graph metadata changed",
				currentState);
		}

		ProbeCandidateGraphVariables(
			*graphHolder,
			"Local",
			localCandidateStates,
			relevantStateChanged || graphMetadataChanged);

		lastState = currentState;
	}

	void UpdateProxyAnimationGraphReadOnlyDebug(
		RE::Actor& a_proxy,
		std::uint32_t a_remotePlayerId)
	{
		if (!F4T::DebugLogging::IsEnabled()) {
			return;
		}

		static std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> lastDumpTimes{};
		static std::unordered_map<std::uint32_t, CandidateReadStates> proxyCandidateStates{};

		const auto now = std::chrono::steady_clock::now();
		for (auto entry = lastDumpTimes.begin(); entry != lastDumpTimes.end();) {
			if (now - entry->second > kProxyGraphStateRetention) {
				proxyCandidateStates.erase(entry->first);
				entry = lastDumpTimes.erase(entry);
			} else {
				++entry;
			}
		}

		auto& lastDumpTime = lastDumpTimes[a_remotePlayerId];
		if (lastDumpTime.time_since_epoch().count() != 0 && now - lastDumpTime < kProxyGraphDumpInterval) {
			return;
		}

		const auto firstAttempt = lastDumpTime.time_since_epoch().count() == 0;
		lastDumpTime = now;

		const auto graphManagerAvailable =
			a_proxy.currentProcess &&
			a_proxy.currentProcess->middleHigh &&
			a_proxy.currentProcess->middleHigh->animationGraphManager.get() != nullptr;
		if (!graphManagerAvailable || a_proxy.Get3D() == nullptr) {
			if (firstAttempt) {
				LogInfoWithLocalPlayerPrefix(std::format(
					"Proxy animation graph dump skipped for remote player {}: the proxy has no loaded 3D or animation graph manager.",
					a_remotePlayerId));
			}
			return;
		}

		ProbeCandidateGraphVariables(
			*static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy),
			std::format("Proxy {}", a_remotePlayerId),
			proxyCandidateStates[a_remotePlayerId],
			false);
	}
}
