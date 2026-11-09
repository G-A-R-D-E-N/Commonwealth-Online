#include "F4TItemHealthSnapshot.h"
#include <cassert>
#include <bit>
#include <limits>
int main()
{
	using namespace F4T::ItemHealthSnapshot;
	const auto encoded = Encode(0.75f);
	assert(encoded && Decode(*encoded) == 0.75f);
	const auto minusZero = Encode(-0.0f);
	assert(minusZero && std::bit_cast<std::uint32_t>(*Decode(*minusZero)) == std::bit_cast<std::uint32_t>(-0.0f));
	assert(!Encode(std::numeric_limits<float>::infinity()));
	assert(!Encode(std::numeric_limits<float>::quiet_NaN()));
	assert((!Decode(nlohmann::json{ { "version", 1u }, { "healthBits", 0x7f800000u } })));
	assert((!Decode(nlohmann::json{ { "version", 2u }, { "healthBits", 0u } })));
	assert((!Decode(nlohmann::json{ { "version", 1u }, { "healthBits", 0u }, { "unknown", true } })));
}
