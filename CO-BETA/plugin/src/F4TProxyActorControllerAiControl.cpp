#include "pch.h"

#include "F4TProxyPathPredicates.h"
#include "F4TProxyActorControllerInternal.h"
#include "F4TProxyProtection.h"

using namespace F4T::ProxyActorController::detail;

namespace
{
	bool g_preserveProxyAIForAnimationTesting = true;

}

namespace F4T::ProxyActorController::detail
{
	void DiagnosticProxyAIState(
		RE::Actor& a_proxy,
		std::string_view a_label)
	{
		if (!a_proxy.currentProcess) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"[{}] Proxy {:08X}: NO AIProcess",
				a_label,
				a_proxy.GetFormID()));
			return;
		}

		std::string packageInfo = "NO_PACKAGE";
		if (a_proxy.currentProcess->currentPackage.package) {
			packageInfo = std::format("PACKAGE_EXISTS");
		}

		const auto [isPathing, isPathValid, isPathComplete] = F4T::ProxyPathPredicates::ReadPathState(a_proxy);

		LogInfoWithLocalPlayerPrefix(std::format(
			"[{}] Proxy {:08X}: AIProcess={}, {}, isPathing={}, pathValid={}, pathComplete={}",
			a_label,
			a_proxy.GetFormID(),
			(void*)a_proxy.currentProcess,
			packageInfo,
			isPathing,
			isPathValid,
			isPathComplete));
	}

	void NeutralizeProxyActor(
		ProxyActorSlot& a_slot,
		RE::Actor& a_proxy,
		const RE::PlayerCharacter& a_player,
		ProxyNeutralizationReason a_reason)
	{
		if (std::addressof(a_proxy) == std::addressof(a_player)) {
			return;
		}

		F4T::ProxyProtection::ApplyToProxyActor(a_proxy);

		const auto reason = GetProxyNeutralizationReasonLabel(a_reason);
		const auto actorId = a_proxy.GetFormID();
		const auto playerId = a_slot.remotePlayerId;
		if (g_preserveProxyAIForAnimationTesting) {
			LogThrottledInfo(
				"runtime_proxy_ai_preserved_" + std::to_string(playerId) + "_" + std::to_string(actorId),
				std::format(
					"Runtime proxy actor {:08X} for remote player {} preserved AI/combat/process state for animation testing. skippedNeutralizationReason={}.",
					actorId,
					playerId,
					reason),
				kProxyNeutralizationInterval);
			return;
		}

		if (a_proxy.IsInCombat()) {
			a_proxy.StopCombat();
			a_proxy.ClearAttackStates();
			LogThrottledInfo(
				"runtime_proxy_combat_stopped_" + std::to_string(playerId) + "_" + std::to_string(actorId),
				std::format(
					"Runtime proxy actor {:08X} for remote player {} entered combat and was neutralized. reason={}.",
					actorId,
					playerId,
					reason));
		}

		if (a_proxy.currentCombatTarget) {
			const auto targetHandle = a_proxy.currentCombatTarget.get_handle();
			a_proxy.currentCombatTarget.reset();
			a_proxy.ClearAttackStates();
			LogThrottledInfo(
				"runtime_proxy_combat_target_cleared_" + std::to_string(playerId) + "_" + std::to_string(actorId),
				std::format(
					"Runtime proxy actor {:08X} for remote player {} had combat target handle {:08X} cleared. reason={}.",
					actorId,
					playerId,
					targetHandle,
					reason));
		}

		auto hostileStateReset = false;
		if (a_proxy.HasBeenAttacked()) {
			a_proxy.SetBeenAttacked(false);
			hostileStateReset = true;
		}
		if (a_proxy.boolFlags.any(RE::Actor::BOOL_FLAGS::kAngryWithPlayer, RE::Actor::BOOL_FLAGS::kAttackOnSight)) {
			a_proxy.boolFlags.reset(RE::Actor::BOOL_FLAGS::kAngryWithPlayer, RE::Actor::BOOL_FLAGS::kAttackOnSight);
			hostileStateReset = true;
		}
		if (a_proxy.boolFlags.none(RE::Actor::BOOL_FLAGS::kAttackingDisabled)) {
			a_proxy.boolFlags.set(RE::Actor::BOOL_FLAGS::kAttackingDisabled);
			hostileStateReset = true;
		}

		if (auto* actorValues = RE::ActorValue::GetSingleton()) {
			auto resetActorValue = [&](RE::ActorValueInfo* a_valueInfo, float a_value) {
				if (!a_valueInfo) {
					return false;
				}

				if (a_proxy.GetActorValue(*a_valueInfo) == a_value) {
					return false;
				}

				a_proxy.SetActorValue(*a_valueInfo, a_value);
				return true;
			};

			hostileStateReset |= resetActorValue(actorValues->aggression, 0.0F);
			hostileStateReset |= resetActorValue(actorValues->confidence, 0.0F);
			hostileStateReset |= resetActorValue(actorValues->assistance, 0.0F);
		}

		if (hostileStateReset) {
			LogThrottledInfo(
				"runtime_proxy_hostility_reset_" + std::to_string(playerId) + "_" + std::to_string(actorId),
				std::format(
					"Runtime proxy actor {:08X} for remote player {} had hostility/aggression state reset. reason={}.",
					actorId,
					playerId,
					reason));
		}

		if (auto* process = a_proxy.currentProcess) {
			auto processSuppressed = false;
			if (!process->ignoringCombat) {
				process->ignoringCombat = true;
				processSuppressed = true;
			}
			if (!process->endAlarmOnActor) {
				process->endAlarmOnActor = true;
				processSuppressed = true;
			}

			if (processSuppressed) {
				LogThrottledInfo(
					"runtime_proxy_ai_process_suppressed_" + std::to_string(playerId) + "_" + std::to_string(actorId),
					std::format(
						"Runtime proxy actor {:08X} for remote player {} had safe AI/process combat intervention suppressed. reason={}.",
						actorId,
						playerId,
						reason));
			}
		}
	}
}
