#include "F4TRejectToastThrottle.h"

#include <cassert>
#include <chrono>

int main()
{
	using namespace std::chrono;
	using F4T::RejectToastThrottle::Gate;

	const auto start = steady_clock::time_point{} + seconds{ 100 };
	Gate gate;
	assert(gate.ShouldShow(start));
	assert(!gate.ShouldShow(start));
	assert(!gate.ShouldShow(start + milliseconds{ 1999 }));
	assert(gate.ShouldShow(start + seconds{ 2 }));
	assert(!gate.ShouldShow(start + seconds{ 3 }));
	assert(gate.ShouldShow(start + seconds{ 4 }));
	assert(!gate.ShouldShow(start + seconds{ 5 }));

	return 0;
}
