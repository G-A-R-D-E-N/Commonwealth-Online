#pragma once

#include "F4TWorldPolicyClient.h"

#include <cstdint>

namespace F4T::WorldPolicyEnforcer
{
	inline constexpr std::uint64_t kNoSessionGeneration = F4T::WorldPolicyClient::kNoSessionGeneration;

	void AcceptPolicy(const F4T::WorldPolicy::Policy& a_policy);
	bool HasAdoptedPolicy() noexcept;
	void ReleaseForSave();
	void ReapplyAfterSave();
	void Tick(std::uint64_t a_nowMs, std::uint64_t a_generation);
}
