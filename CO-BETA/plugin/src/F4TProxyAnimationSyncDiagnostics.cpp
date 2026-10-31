#include "pch.h"

#include "F4TProxyAnimationSyncInternal.h"

#include "F4TAnimationGraphOverride.h"
#include "F4TNetworking.h"
#include "F4TDebugLogging.h"
#include "F4AnimationDescriptor.h"

#include "RE/B/BGSAnimationSystemUtils.h"
#include "RE/B/BSFixedString.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/T/TESObjectREFR.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <unordered_set>

namespace
{
	std::string GetLocalPlayerLogMessage(std::string_view a_message)
	{
		return F4T::Networking::GetLocalPlayerLogPrefix() + " " + std::string(a_message);
	}
}

namespace F4T::ProxyAnimationSync::detail
{
	void LogInfoWithLocalPlayerPrefix(std::string_view a_message)
	{
		F4T::DebugLogging::Write("ProxyAnimation", GetLocalPlayerLogMessage(a_message));
	}

	void LogWarningWithLocalPlayerPrefix(std::string_view a_message)
	{
		const auto message = GetLocalPlayerLogMessage(a_message);
		REX::WARN(std::string_view{ message });
	}

	bool AreFloatsDifferent(float a_lhs, float a_rhs)
	{
		if (!std::isfinite(a_lhs) || !std::isfinite(a_rhs)) {
			return a_lhs != a_rhs;
		}

		return std::fabs(a_lhs - a_rhs) > kSpeedEpsilon;
	}

	bool ProxyHasLoaded3D(const RE::Actor& a_proxy)
	{
		return a_proxy.Get3D() != nullptr ||
		       a_proxy.GetFullyLoaded3D() != nullptr ||
		       a_proxy.GetCurrent3D() != nullptr;
	}

	bool ProxyMeshReadyForPresentation(const RE::Actor& a_proxy)
	{
		if (!a_proxy.IsVisible()) {
			return false;
		}

		return a_proxy.GetFullyLoaded3D() != nullptr || a_proxy.Get3D() != nullptr;
	}

	bool CanWriteProxyGraph(RE::Actor& a_proxy, std::uint32_t a_remotePlayerId, std::uint32_t a_actorId)
	{
		if (!ProxyHasLoaded3D(a_proxy)) {
			return false;
		}

		auto* graphManager =
			a_proxy.currentProcess && a_proxy.currentProcess->middleHigh
				? a_proxy.currentProcess->middleHigh->animationGraphManager.get()
				: nullptr;
		if (!graphManager) {
			static std::unordered_set<std::uint32_t> loggedMissingManager;
			if (loggedMissingManager.insert(a_remotePlayerId).second) {
				LogWarningWithLocalPlayerPrefix(std::format(
					"Runtime proxy animation sync skipped for remote player {}: actor={:08X}, animation graph manager unavailable.",
					a_remotePlayerId,
					a_actorId));
			}
			return false;
		}

		(void)RE::BGSAnimationSystemUtils::IsActiveGraphInTransition(static_cast<RE::TESObjectREFR*>(&a_proxy));

		static std::unordered_set<std::uint32_t> loggedGraphStatus;
		if (loggedGraphStatus.insert(a_remotePlayerId).second) {
			LogInfoWithLocalPlayerPrefix(std::format(
				"Runtime proxy animation graph manager DIAGNOSTIC for remote player {}: actor={:08X}, graphManagerValid=true, graphManagerPtr={:p}, graphManagerNotNull={}.",
				a_remotePlayerId,
				a_actorId,
				static_cast<void*>(graphManager),
				graphManager != nullptr));
		}

		return true;
	}

	bool TrySendGraphEvent(RE::Actor& a_proxy, const char* a_eventName)
	{
		if (F4T::AnimationGraphOverride::Installed()) {
			return F4T::AnimationGraphOverride::SendGraphEvent(&a_proxy, a_eventName);
		}
		auto* graphHolder = static_cast<RE::IAnimationGraphManagerHolder*>(&a_proxy);
		const RE::BSFixedString eventName{ a_eventName };
		return graphHolder->NotifyAnimationGraphImpl(eventName);
	}

	bool TrySetGraphBool(
		RE::IAnimationGraphManagerHolder& a_holder,
		const char* a_variableName,
		bool a_value)
	{
		const RE::BSFixedString variableName{ a_variableName };
		return a_holder.SetGraphVariableBool(variableName, a_value);
	}

	bool TrySetGraphInt(
		RE::IAnimationGraphManagerHolder& a_holder,
		const char* a_variableName,
		std::int32_t a_value)
	{
		const RE::BSFixedString variableName{ a_variableName };
		return a_holder.SetGraphVariableInt(variableName, a_value);
	}

	bool TrySetGraphFloat(
		RE::IAnimationGraphManagerHolder& a_holder,
		const char* a_variableName,
		float a_value)
	{
		const RE::BSFixedString variableName{ a_variableName };
		return a_holder.SetGraphVariableFloat(variableName, a_value);
	}

	float ClampGraphSpeed(float a_speed)
	{
		if (!std::isfinite(a_speed)) {
			return kGraphSpeedIdle;
		}

		return std::clamp(a_speed, kGraphSpeedMin, kGraphSpeedMax);
	}

	void PopulateLocomotionSpeedSnapshot(
		F4T::AnimationDescriptor::AnimationVariableSnapshot& a_snapshot,
		const F4T::AnimationDescriptor::F4AnimationDescriptor& a_descriptor,
		float a_graphSpeed)
	{
		const auto graphSpeed = ClampGraphSpeed(a_graphSpeed);

		const auto speedIdx = a_descriptor.GetFloatVariableIndex(F4T::ProxyAnimationSync::GraphVar::kSpeed);
		if (speedIdx != static_cast<std::size_t>(-1) && speedIdx < a_snapshot.floats.size()) {
			a_snapshot.floats[speedIdx] = graphSpeed;
		}

		const auto speedSmoothedIdx = a_descriptor.GetFloatVariableIndex(F4T::ProxyAnimationSync::GraphVar::kSpeedSmoothed);
		if (speedSmoothedIdx != static_cast<std::size_t>(-1) && speedSmoothedIdx < a_snapshot.floats.size()) {
			a_snapshot.floats[speedSmoothedIdx] = graphSpeed;
		}
	}

	float ComputeGraphSpeed(const F4T::RemotePlayerState::RemotePlayerState& a_remotePlayer)
	{
		return F4T::ProxyAnimationSync::ResolveLocomotionGraphSpeed(
			a_remotePlayer.movementSpeed,
			a_remotePlayer.animationGraphSpeed,
			a_remotePlayer.isMoving,
			a_remotePlayer.isSprinting);
	}

	void LogAppliedTransition(
		std::uint32_t a_remotePlayerId,
		std::uint32_t a_actorId,
		const DesiredProxyAnimationState& a_desired,
		bool a_initial)
	{
		const auto tier = F4T::ProxyAnimationSync::ComputeLocomotionTier(
			a_desired.graphSpeed,
			a_desired.isSprinting);
		const char* tierLabel = "jog";
		if (tier == F4T::ProxyAnimationSync::LocomotionTier::kWalk) {
			tierLabel = "walk";
		} else if (tier == F4T::ProxyAnimationSync::LocomotionTier::kRun) {
			tierLabel = "run";
		}

		LogInfoWithLocalPlayerPrefix(std::format(
			"Runtime proxy animation sync {} for remote player {}: actor={:08X}, moving={}, tier={}, sprinting={}, sneaking={}, jumping={}, crouching={}, weaponDrawn={}, graphSpeed={:.1f}, direction={:.3f}.",
			a_initial ? "initial" : "changed",
			a_remotePlayerId,
			a_actorId,
			a_desired.isMoving,
			tierLabel,
			a_desired.isSprinting,
			a_desired.isSneaking,
			a_desired.isJumping,
			a_desired.isCrouching,
			a_desired.weaponDrawn,
			a_desired.graphSpeed,
			a_desired.direction));
	}
}
