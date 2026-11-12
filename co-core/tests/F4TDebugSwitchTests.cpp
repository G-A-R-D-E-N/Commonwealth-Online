#include "F4TDebugSwitch.h"

#include <cassert>

int main()
{
	using namespace F4T::DebugSwitch;

	assert(ParseValue("1") == true);
	assert(ParseValue("true") == true);
	assert(ParseValue("YES") == true);
	assert(ParseValue(" on ") == true);
	assert(ParseValue("debug") == true);
	assert(ParseValue("enabled") == true);
	assert(ParseValue("0") == false);
	assert(ParseValue("false") == false);
	assert(ParseValue("NO") == false);
	assert(ParseValue(" off ") == false);
	assert(ParseValue("release") == false);
	assert(ParseValue("disabled") == false);
	assert(!ParseValue("maybe").has_value());
	assert(ResolveValue(nullptr) == BetaDefaultEnabled);
	assert(ResolveValue("") == BetaDefaultEnabled);
	assert(ResolveValue("off") == false);
	assert(ResolveValue("on") == true);

	SetEnabled(false);
	assert(!IsEnabled());
	SetEnabled(true);
	assert(IsEnabled());

	return 0;
}
