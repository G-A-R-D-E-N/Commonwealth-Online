#include "F4TInterestScope.h"

#include <cassert>
#include <optional>

int main()
{
	using F4T::InterestScope::StateScope;
	using F4T::InterestScope::StatesShareInterest;

	const std::optional<StateScope> unknown;
	const std::optional<StateScope> interiorA{ StateScope{ "0000CAFE", "", 0.0, 0.0 } };
	const std::optional<StateScope> interiorB{ StateScope{ "0000BEEF", "", 10.0, 10.0 } };
	const std::optional<StateScope> exteriorA{ StateScope{ "00000100", "0000F00D", 0.0, 0.0 } };
	const std::optional<StateScope> exteriorSameCellFar{ StateScope{ "00000100", "0000F00D", 100000.0, 100000.0 } };
	const std::optional<StateScope> exteriorNear{ StateScope{ "00000200", "0000F00D", 8192.0, 0.0 } };
	const std::optional<StateScope> exteriorFar{ StateScope{ "00000300", "0000F00D", 8192.1, 0.0 } };
	const std::optional<StateScope> otherWorld{ StateScope{ "00000400", "0000DEAD", 1.0, 1.0 } };

	assert(!StatesShareInterest(unknown, exteriorA));
	assert(!StatesShareInterest(exteriorA, unknown));
	assert(StatesShareInterest(exteriorA, exteriorSameCellFar));
	assert(!StatesShareInterest(interiorA, interiorB));
	assert(StatesShareInterest(exteriorA, exteriorNear));
	assert(!StatesShareInterest(exteriorA, exteriorFar));
	assert(!StatesShareInterest(exteriorA, otherWorld));

	return 0;
}
