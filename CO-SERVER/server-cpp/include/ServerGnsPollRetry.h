#pragma once

#include <algorithm>
#include <chrono>

namespace CommonwealthOnline::Server
{
	// constraint: the host GUI and the in-process host must treat this exit code as a restart request.
	inline constexpr int kGnsPollRestartExitCode = 75;
	inline constexpr int kMaximumConsecutiveGnsPollFailures = 20;

	[[nodiscard]] inline std::chrono::milliseconds GnsPollRetryBackoff(int a_consecutiveFailures) noexcept
	{
		return std::chrono::milliseconds{ std::min(10 * a_consecutiveFailures, 250) };
	}

	[[nodiscard]] inline bool GnsPollFailuresExhausted(int a_consecutiveFailures) noexcept
	{
		return a_consecutiveFailures >= kMaximumConsecutiveGnsPollFailures;
	}
}
