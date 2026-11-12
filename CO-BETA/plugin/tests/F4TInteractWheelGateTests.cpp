#include "F4TInteractWheelGate.h"

#include <cassert>

int main()
{
	using namespace F4T::InteractWheelGate;

	assert(ShouldCloseWheelForTrade(true, true));
	assert(!ShouldCloseWheelForTrade(false, true));
	assert(!ShouldCloseWheelForTrade(true, false));
	assert(!ShouldCloseWheelForTrade(false, false));

	assert(CanOpenWheel(false, false));
	assert(!CanOpenWheel(true, false));
	assert(!CanOpenWheel(false, true));
	assert(!CanOpenWheel(true, true));

	return 0;
}
