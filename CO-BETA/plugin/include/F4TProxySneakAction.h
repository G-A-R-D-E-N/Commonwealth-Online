#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace RE
{
	class Actor;
}

namespace F4T::ProxySneakAction
{
	enum class SneakRequestOutcome
	{
		kSettled,
		kPending,
		kUseActionFallback,
		kGiveUp
	};

	inline constexpr std::uint32_t kMaxActionAttemptsPerRequest = 3;

	struct SneakRequestState
	{
		std::optional<bool> lastRequested{};
		std::optional<SneakRequestOutcome> lastOutcome{};
		std::chrono::steady_clock::time_point nextAttemptTime{};
		std::uint32_t actionAttempts{ 0 };
		std::uint64_t traceCount{ 0 };
		std::uint64_t deferredCount{ 0 };
	};

	[[nodiscard]] inline bool ShouldTraceCount(std::uint64_t a_count)
	{
		return a_count <= 5 || a_count % 300 == 0;
	}

	[[nodiscard]] inline SneakRequestOutcome ResolveSneakRequest(
		bool a_stanceMatchesRequest,
		bool a_withinRetryWindow,
		std::uint32_t a_actionAttempts)
	{
		if (a_stanceMatchesRequest) {
			return SneakRequestOutcome::kSettled;
		}

		if (a_withinRetryWindow) {
			return SneakRequestOutcome::kPending;
		}

		return a_actionAttempts < kMaxActionAttemptsPerRequest ? SneakRequestOutcome::kUseActionFallback : SneakRequestOutcome::kGiveUp;
	}

	inline void ArmSneakRetryWindow(
		std::chrono::steady_clock::time_point a_now,
		std::chrono::steady_clock::time_point& a_nextAttemptTime,
		std::chrono::milliseconds a_retryInterval)
	{
		if (a_now >= a_nextAttemptTime) {
			a_nextAttemptTime = a_now + a_retryInterval;
		}
	}

	bool RequestProxySneakState(
		SneakRequestState& a_state,
		RE::Actor& a_proxy,
		bool a_requestedSneaking,
		bool a_allowActionFallback);
}
