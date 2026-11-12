#include "F4TProxySneakGraphSync.h"

#include <cassert>
#include <chrono>
#include <cstdint>

int main()
{
	using namespace std::chrono_literals;
	using F4T::ProxySneakGraphSync::kFastRetryFailures;
	using F4T::ProxySneakGraphSync::NoteWrite;
	using F4T::ProxySneakGraphSync::RetryDue;
	using F4T::ProxySneakGraphSync::RetryIntervalAfter;
	using F4T::ProxySneakGraphSync::State;

	const auto t0 = std::chrono::steady_clock::time_point{ std::chrono::seconds{ 100 } };

	assert(kFastRetryFailures == 5);
	assert(RetryIntervalAfter(1) == 100ms && RetryIntervalAfter(5) == 100ms);
	assert(RetryIntervalAfter(6) == 1000ms && RetryIntervalAfter(1000) == 1000ms);

	State clean{};
	assert(!clean.pending && !RetryDue(clean, t0));

	State failed{};
	NoteWrite(failed, false, t0);
	assert(failed.pending && failed.failures == 1);
	assert(!RetryDue(failed, t0));
	assert(!RetryDue(failed, t0 + 99ms));
	assert(RetryDue(failed, t0 + 100ms));
	assert(RetryDue(failed, t0 + 5s));

	NoteWrite(failed, true, t0 + 100ms);
	assert(!failed.pending && failed.failures == 0 && !RetryDue(failed, t0 + 10s));

	State fast{};
	NoteWrite(fast, false, t0);
	auto now = t0;
	for (std::uint32_t i = 2; i <= kFastRetryFailures; ++i) {
		now = now + 100ms;
		assert(RetryDue(fast, now));
		NoteWrite(fast, false, now);
	}
	assert(fast.failures == kFastRetryFailures);
	assert(!RetryDue(fast, now + 99ms) && RetryDue(fast, now + 100ms));

	now = now + 100ms;
	assert(RetryDue(fast, now));
	NoteWrite(fast, false, now);
	assert(fast.failures == kFastRetryFailures + 1);
	assert(!RetryDue(fast, now + 999ms) && RetryDue(fast, now + 1000ms));

	State storm{};
	NoteWrite(storm, false, t0);
	std::uint32_t retries = 0;
	for (std::int64_t ms = 1; ms <= 10000; ++ms) {
		const auto tick = t0 + std::chrono::milliseconds{ ms };
		if (RetryDue(storm, tick)) {
			++retries;
			NoteWrite(storm, false, tick);
		}
	}
	assert(retries == 14);
	assert(storm.pending && storm.failures == 15);

	NoteWrite(storm, true, t0 + 11s);
	assert(!storm.pending && storm.failures == 0);

	return 0;
}
