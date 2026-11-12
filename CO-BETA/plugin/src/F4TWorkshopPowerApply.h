#pragma once

#include "F4TWorkshopReplicationInternal.h"

#include <chrono>
#include <set>

namespace F4T::WorkshopReplicationClient
{
	void ApplyPowerWires(Workshop::BuildZoneId a_zoneId, ZoneState& a_zone);
	void RetryPowerApply(Workshop::BuildZoneId a_zoneId, ZoneState& a_zone, std::chrono::steady_clock::time_point a_now);
	void ResumePowerApply(ZoneState& a_zone);
	void RemoveAppliedPowerWires(ZoneState& a_zone);
	[[nodiscard]] bool IsApplyCreatedRef(const RE::TESObjectREFR& a_ref);
	[[nodiscard]] std::set<std::uint32_t> ApplyCreatedRefs();
	void ForgetApplyCreatedRefs(const std::set<std::uint32_t>& a_formIds);
}
