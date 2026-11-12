#include "F4TTravelCharge.h"

#include <cassert>

int main()
{
	using F4T::TravelCharge::ResolveCost;

	assert(ResolveCost(true, 5) == 0);
	assert(ResolveCost(true, 100) == 0);
	assert(ResolveCost(true, 0) == 0);
	assert(ResolveCost(false, 5) == 5);
	assert(ResolveCost(false, 100) == 100);
	assert(ResolveCost(false, 0) == 0);

	return 0;
}
