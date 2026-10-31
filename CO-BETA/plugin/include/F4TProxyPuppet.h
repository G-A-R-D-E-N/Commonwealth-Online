#pragma once

#include "RE/N/NiPoint3.h"

#include <cstdint>
#include <vector>

namespace F4T::ProxyPuppet
{
	bool Install();

	void SetActiveProxyFormIDs(std::vector<std::uint32_t> a_formIDs);

	void SetProxyLocomotionState(std::uint32_t a_formID, float a_speed, float a_direction);

	void SetProxyAimState(std::uint32_t a_formID, float a_aimPitchDegrees, float a_turnDelta);

	void SetProxyDesiredSpeed(std::uint32_t a_formID, float a_speed);

	void ApplyProxyLocomotionFrame(
		RE::Actor& a_actor,
		const RE::NiPoint3& a_worldDelta,
		float a_deltaSeconds,
		float a_graphSpeed,
		float a_direction);

	bool IsProxyFormID(std::uint32_t a_formID);

	RE::NiPoint3 GetProxyLocomotionVelocity(std::uint32_t a_formID);

	void GetHookStats(std::uint64_t& a_outTotalCalls, std::uint64_t& a_outProxySkips);
}
