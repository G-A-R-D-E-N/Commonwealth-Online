#include "F4TProxyTransformFreshness.h"

#include <cassert>
#include <chrono>

int main()
{
	using Clock = std::chrono::steady_clock;
	using namespace std::chrono_literals;
	const auto received = Clock::time_point{} + 1s;
	assert(!F4T::ProxyTransformFreshness::IsFresh({}, received));
	assert(F4T::ProxyTransformFreshness::IsFresh(received, received));
	assert(F4T::ProxyTransformFreshness::IsFresh(received, received + 10s));
	assert(!F4T::ProxyTransformFreshness::IsFresh(received, received + 10s + 1ms));
	assert(!F4T::ProxyTransformFreshness::IsFresh(received, received - 1ms));
	assert(F4T::ProxyTransformFreshness::IsFresh(received + 12s, received + 12s));
	return 0;
}
