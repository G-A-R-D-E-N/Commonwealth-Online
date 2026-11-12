#include "F4TAuthorityElection.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>

int main()
{
	using F4T::Authority::ElectSticky;

	const std::array<std::uint32_t, 3> remaining{ 2, 3, 5 };
	assert(ElectSticky(1, std::span<const std::uint32_t>(remaining)) == 2);
	assert(ElectSticky(3, std::span<const std::uint32_t>(remaining)) == 3);
	assert(ElectSticky(0, std::span<const std::uint32_t>(remaining)) == 2);
	assert(ElectSticky(4, std::span<const std::uint32_t>()) == 0);

	const std::array<std::uint32_t, 2> withUnassigned{ 0, 7 };
	assert(ElectSticky(0, std::span<const std::uint32_t>(withUnassigned)) == 7);
	return 0;
}
