// Copyright (C) 2026 FatherLamb. Portions derived from FathersFallout, GPL-3.0 with additional terms. Used with the author's permission.
#pragma once

#include <chrono>

namespace F4T::ProxyEquipmentDebounce
{
	using Clock = std::chrono::steady_clock;

	constexpr auto kSettle = std::chrono::milliseconds(350);
	constexpr auto kMinGap = std::chrono::milliseconds(900);

	struct State
	{
		Clock::time_point wantChangedAt{};
		Clock::time_point appliedAt{};
	};

	[[nodiscard]] inline bool ShouldApply(const State& a_state, bool a_firstApply, Clock::time_point a_now) noexcept
	{
		if (a_firstApply) {
			return true;
		}
		return a_now - a_state.wantChangedAt >= kSettle && a_now - a_state.appliedAt >= kMinGap;
	}
}
