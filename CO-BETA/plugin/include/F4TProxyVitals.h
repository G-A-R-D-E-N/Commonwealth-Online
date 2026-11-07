#pragma once

#include "F4TPlayerVitals.h"

#include "RE/A/Actor.h"

#include <cstdint>
#include <optional>

namespace F4T::ProxyVitals
{
	struct State
	{
		std::optional<std::int32_t> appliedHealthPercent;
		double nextHealthReassertSeconds{ 0.0 };
		std::optional<double> lastResurrectSeconds;
		double nextPoseRetrySeconds{ 0.0 };
		bool poseDowned{ false };
		bool killed{ false };
		bool restoreEssential{ false };
		bool restoreProtected{ false };
	};

	void Apply(RE::Actor& a_proxy, State& a_state, const F4T::PlayerVitals::Vitals& a_vitals);
}
