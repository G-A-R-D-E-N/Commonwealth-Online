#include "F4TProxySneakAction.h"

#include <cassert>
#include <chrono>
#include <cstdint>

namespace
{
	using F4T::ProxySneakAction::kMaxActionAttemptsPerRequest;
	using F4T::ProxySneakAction::ResolveSneakRequest;
	using F4T::ProxySneakAction::SneakRequestOutcome;
	using F4T::ProxySneakAction::ShouldTraceCount;
}

int main()
{
	assert(ResolveSneakRequest(true, false, 0) == SneakRequestOutcome::kSettled);
	assert(ResolveSneakRequest(true, true, 0) == SneakRequestOutcome::kSettled);
	assert(ResolveSneakRequest(true, true, kMaxActionAttemptsPerRequest) == SneakRequestOutcome::kSettled);

	assert(ResolveSneakRequest(false, true, 0) == SneakRequestOutcome::kPending);
	assert(ResolveSneakRequest(false, true, kMaxActionAttemptsPerRequest - 1) == SneakRequestOutcome::kPending);
	assert(ResolveSneakRequest(false, true, kMaxActionAttemptsPerRequest) == SneakRequestOutcome::kPending);

	assert(ResolveSneakRequest(false, false, 0) == SneakRequestOutcome::kUseActionFallback);
	assert(ResolveSneakRequest(false, false, 1) == SneakRequestOutcome::kUseActionFallback);
	assert(ResolveSneakRequest(false, false, kMaxActionAttemptsPerRequest - 1) == SneakRequestOutcome::kUseActionFallback);
	assert(ResolveSneakRequest(false, false, kMaxActionAttemptsPerRequest) == SneakRequestOutcome::kGiveUp);
	assert(ResolveSneakRequest(false, false, kMaxActionAttemptsPerRequest + 1) == SneakRequestOutcome::kGiveUp);

	std::uint32_t actionAttempts = 0;
	std::uint32_t fallbacks = 0;
	for (int tick = 0; tick < 20; ++tick) {
		if (ResolveSneakRequest(false, false, actionAttempts) == SneakRequestOutcome::kUseActionFallback) {
			++actionAttempts;
			++fallbacks;
		}
	}
	assert(fallbacks == kMaxActionAttemptsPerRequest);
	assert(actionAttempts == kMaxActionAttemptsPerRequest);

	assert(ShouldTraceCount(1));
	assert(ShouldTraceCount(5));
	assert(!ShouldTraceCount(6));
	assert(!ShouldTraceCount(299));
	assert(ShouldTraceCount(300));
	assert(!ShouldTraceCount(301));
	assert(ShouldTraceCount(600));

	using Clock = std::chrono::steady_clock;
	constexpr auto retryInterval = std::chrono::milliseconds(500);
	auto nextAttemptTime = Clock::time_point{};
	const auto firstCall = Clock::time_point{ std::chrono::seconds(10) };

	F4T::ProxySneakAction::ArmSneakRetryWindow(firstCall, nextAttemptTime, retryInterval);
	const auto armedDeadline = nextAttemptTime;
	assert(armedDeadline == firstCall + retryInterval);

	F4T::ProxySneakAction::ArmSneakRetryWindow(firstCall + std::chrono::milliseconds(5), nextAttemptTime, retryInterval);
	assert(nextAttemptTime == armedDeadline);

	F4T::ProxySneakAction::ArmSneakRetryWindow(firstCall + std::chrono::milliseconds(499), nextAttemptTime, retryInterval);
	assert(nextAttemptTime == armedDeadline);

	F4T::ProxySneakAction::ArmSneakRetryWindow(firstCall + retryInterval, nextAttemptTime, retryInterval);
	assert(nextAttemptTime == firstCall + 2 * retryInterval);

	return 0;
}
