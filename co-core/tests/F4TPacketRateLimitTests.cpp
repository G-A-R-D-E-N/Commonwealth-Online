#include "F4TPacketRateLimit.h"

#include <cassert>

int main()
{
	using namespace F4T::PacketRateLimit;

	auto state = Initialize(10.0);
	for (std::uint32_t packet = 0; packet < DefaultMaximumPacketsPerSecond; ++packet) {
		const auto result = Observe(state, 10.25);
		assert(result.verdict == Verdict::Allowed);
		assert(!result.newViolationWindow);
	}
	assert(state.windowCount == 120);
	assert(state.violationWindows == 0);

	const auto firstLimited = Observe(state, 10.25);
	assert(firstLimited.verdict == Verdict::Limited);
	assert(firstLimited.newViolationWindow);
	assert(state.violationWindows == 1);
	assert(state.windowBlocked);

	const auto sameWindowLimited = Observe(state, 10.25);
	assert(sameWindowLimited.verdict == Verdict::Limited);
	assert(!sameWindowLimited.newViolationWindow);
	assert(state.violationWindows == 1);

	const auto nextWindow = Observe(state, 11.25);
	assert(nextWindow.verdict == Verdict::Allowed);
	assert(nextWindow.windowReset);
	assert(state.violationWindows == 1);
	assert(state.windowCount == 1);
	assert(!state.windowBlocked);

	const auto compliantReset = Observe(state, 12.25);
	assert(compliantReset.verdict == Verdict::Allowed);
	assert(compliantReset.windowReset);
	assert(state.violationWindows == 0);

	Config small{ .maximumPacketsPerWindow = 2, .maximumViolationWindows = 3, .windowSeconds = 1.0 };
	state = Initialize(20.0);
	for (int window = 0; window < 3; ++window) {
		const double now = 20.0 + window;
		assert(Observe(state, now, small).verdict == Verdict::Allowed);
		assert(Observe(state, now, small).verdict == Verdict::Allowed);
		const auto violation = Observe(state, now, small);
		assert(violation.newViolationWindow);
		if (window < 2) {
			assert(violation.verdict == Verdict::Limited);
		} else {
			assert(violation.verdict == Verdict::Disconnect);
		}
	}
	assert(state.violationWindows == 3);

	state = Initialize(30.0);
	assert(Observe(state, 30.0, small).verdict == Verdict::Allowed);
	assert(Observe(state, 30.0, small).verdict == Verdict::Allowed);
	assert(Observe(state, 30.0, small).verdict == Verdict::Limited);
	assert(state.violationWindows == 1);
	assert(Observe(state, 31.0, small).verdict == Verdict::Allowed);
	assert(state.violationWindows == 1);
	assert(Observe(state, 32.0, small).verdict == Verdict::Allowed);
	assert(state.violationWindows == 0);

	Config byteBudget{};
	byteBudget.maximumPacketsPerWindow = 1000;
	byteBudget.maximumBytesPerWindow = 4096;
	state = Initialize(40.0);
	assert(Observe(state, 40.0, byteBudget, 4096).verdict == Verdict::Allowed);
	const auto overBytes = Observe(state, 40.0, byteBudget, 1);
	assert(overBytes.verdict == Verdict::Limited);
	assert(overBytes.byteBudgetExceeded);
	assert(state.windowCount == 2);

	const auto nextByteWindow = Observe(state, 41.0, byteBudget, 16);
	assert(nextByteWindow.verdict == Verdict::Allowed);
	assert(!nextByteWindow.byteBudgetExceeded);
	assert(state.windowBytes == 16);

	TokenBucket bucket;
	for (int token = 0; token < 4; ++token) assert(TakeToken(bucket, 50.0));
	assert(!TakeToken(bucket, 50.0));
	assert(TakeToken(bucket, 50.25));
	assert(!TakeToken(bucket, 50.25));
	assert(TakeToken(bucket, 50.5));
	assert(!TakeToken(bucket, 50.5));
	for (int token = 0; token < 4; ++token) assert(TakeToken(bucket, 51.5));
	assert(!TakeToken(bucket, 51.5));
	assert(TakeToken(bucket, 1000.0));
	assert(bucket.tokens == 3.0);

	return 0;
}
