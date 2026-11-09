#include "F4TSurvivalLogPolicy.h"

#include <cassert>
#include <optional>
#include <string_view>

int main()
{
	using F4T::SurvivalLogPolicy::FlagText;
	using F4T::SurvivalLogPolicy::Latch;
	using F4T::SurvivalLogPolicy::Throttle;

	Throttle throttle{ 30.0 };
	assert(throttle.Due(100.0));
	assert(!throttle.Due(100.0));
	assert(!throttle.Due(129.9));
	assert(throttle.Due(130.0));
	assert(!throttle.Due(131.0));
	assert(!throttle.Due(159.9));
	assert(throttle.Due(160.5));
	throttle.Reset();
	assert(throttle.Due(161.0));

	Latch latch;
	assert(latch.Claim(1, 1));
	assert(!latch.Claim(1, 1));
	assert(latch.Claim(1, 2));
	assert(!latch.Claim(1, 2));
	assert(latch.Claim(2, 2));
	assert(latch.Claim(1, 2) == true);
	assert(latch.Claim(3, 3, "object-null"));
	assert(latch.Claim(3, 3, "transition-failed"));
	assert(!latch.Claim(3, 3, "transition-failed"));

	assert(FlagText(std::nullopt) == std::string_view{ "missing" });
	assert(FlagText(true) == std::string_view{ "true" });
	assert(FlagText(false) == std::string_view{ "false" });

	return 0;
}
