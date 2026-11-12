#include "ServerGnsPollRetry.h"

#include <cassert>
#include <chrono>

int main()
{
	using namespace std::chrono_literals;
	using CommonwealthOnline::Server::GnsPollFailuresExhausted;
	using CommonwealthOnline::Server::GnsPollRetryBackoff;
	using CommonwealthOnline::Server::kGnsPollRestartExitCode;
	using CommonwealthOnline::Server::kMaximumConsecutiveGnsPollFailures;

	static_assert(kGnsPollRestartExitCode != 0);
	assert(GnsPollRetryBackoff(1) == 10ms);
	assert(GnsPollRetryBackoff(kMaximumConsecutiveGnsPollFailures) == 200ms);
	assert(GnsPollRetryBackoff(1000) == 250ms);

	assert(!GnsPollFailuresExhausted(kMaximumConsecutiveGnsPollFailures - 1));
	assert(GnsPollFailuresExhausted(kMaximumConsecutiveGnsPollFailures));

	std::chrono::milliseconds total{};
	for (int failure = 1; failure < kMaximumConsecutiveGnsPollFailures; ++failure) {
		total += GnsPollRetryBackoff(failure);
	}
	assert(total <= 5s);
	return 0;
}
