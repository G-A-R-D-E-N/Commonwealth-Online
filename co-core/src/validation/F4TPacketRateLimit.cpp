#include "F4TPacketRateLimit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace F4T::PacketRateLimit
{
	State Initialize(double a_nowMonotonicSeconds) noexcept
	{
		State state{};
		state.windowStartSeconds = std::isfinite(a_nowMonotonicSeconds) ? a_nowMonotonicSeconds : 0.0;
		return state;
	}

	Result Observe(
		State& a_state,
		double a_nowMonotonicSeconds,
		Config a_config,
		std::uint64_t a_payloadBytes) noexcept
	{
		if (a_config.maximumPacketsPerWindow == 0) {
			a_config.maximumPacketsPerWindow = 1;
		}
		if (a_config.maximumBytesPerWindow == 0) {
			a_config.maximumBytesPerWindow = 1;
		}
		if (a_config.maximumViolationWindows == 0) {
			a_config.maximumViolationWindows = 1;
		}
		if (!std::isfinite(a_config.windowSeconds) || a_config.windowSeconds <= 0.0) {
			a_config.windowSeconds = DefaultWindowSeconds;
		}

		Result result{};
		const auto now = std::isfinite(a_nowMonotonicSeconds) ? a_nowMonotonicSeconds : a_state.windowStartSeconds;
		if (now - a_state.windowStartSeconds >= a_config.windowSeconds) {
			if (a_state.windowCount <= a_config.maximumPacketsPerWindow &&
				a_state.windowBytes <= a_config.maximumBytesPerWindow &&
				a_state.violationWindows > 0) {
				--a_state.violationWindows;
			}
			a_state.windowStartSeconds = now;
			a_state.windowCount = 0;
			a_state.windowBytes = 0;
			a_state.windowBlocked = false;
			result.windowReset = true;
		}

		if (a_state.windowCount != (std::numeric_limits<std::uint32_t>::max)()) {
			++a_state.windowCount;
		}
		const auto byteHeadroom = (std::numeric_limits<std::uint64_t>::max)() - a_state.windowBytes;
		a_state.windowBytes += (std::min)(byteHeadroom, a_payloadBytes);

		const bool overPackets = a_state.windowCount > a_config.maximumPacketsPerWindow;
		const bool overBytes = a_state.windowBytes > a_config.maximumBytesPerWindow;
		if (!overPackets && !overBytes) {
			result.verdict = Verdict::Allowed;
			return result;
		}
		result.byteBudgetExceeded = overBytes;

		if (!a_state.windowBlocked) {
			a_state.windowBlocked = true;
			if (a_state.violationWindows != (std::numeric_limits<std::uint32_t>::max)()) {
				++a_state.violationWindows;
			}
			result.newViolationWindow = true;
		}
		result.verdict = a_state.violationWindows >= a_config.maximumViolationWindows ? Verdict::Disconnect : Verdict::Limited;
		return result;
	}

	bool TakeToken(TokenBucket& a_bucket, double a_nowMonotonicSeconds, double a_refillPerSecond, double a_capacity) noexcept
	{
		if (std::isfinite(a_nowMonotonicSeconds) && a_nowMonotonicSeconds > a_bucket.lastSeconds) {
			a_bucket.tokens = (std::min)(a_capacity, a_bucket.tokens + (a_nowMonotonicSeconds - a_bucket.lastSeconds) * a_refillPerSecond);
			a_bucket.lastSeconds = a_nowMonotonicSeconds;
		}
		if (a_bucket.tokens < 1.0) {
			return false;
		}
		a_bucket.tokens -= 1.0;
		return true;
	}
}
