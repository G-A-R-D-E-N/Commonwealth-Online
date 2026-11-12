#pragma once

#include <chrono>
#include <cstdint>

namespace F4T::ProxySneakGraphSync
{
	inline constexpr auto kRetryInterval = std::chrono::milliseconds{ 100 };
	inline constexpr auto kBackoffRetryInterval = std::chrono::milliseconds{ 1000 };
	inline constexpr std::uint32_t kFastRetryFailures = 5;

	struct State
	{
		bool pending{ false };
		std::uint32_t failures{ 0 };
		std::chrono::steady_clock::time_point retryDue{};
	};

	[[nodiscard]] inline std::chrono::milliseconds RetryIntervalAfter(std::uint32_t a_failures) noexcept
	{
		return a_failures <= kFastRetryFailures ? kRetryInterval : kBackoffRetryInterval;
	}

	[[nodiscard]] inline bool RetryDue(
		const State& a_state,
		std::chrono::steady_clock::time_point a_now) noexcept
	{
		return a_state.pending && a_now >= a_state.retryDue;
	}

	inline void NoteWrite(
		State& a_state,
		bool a_written,
		std::chrono::steady_clock::time_point a_now) noexcept
	{
		if (a_written) {
			a_state = State{};
			return;
		}

		++a_state.failures;
		a_state.pending = true;
		a_state.retryDue = a_now + RetryIntervalAfter(a_state.failures);
	}
}
