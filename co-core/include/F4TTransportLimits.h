#pragma once

#include <cstddef>

namespace F4T::TransportLimits
{
	inline constexpr std::size_t DefaultMaximumMessageBytes = 256uz * 1024uz;
	inline constexpr std::size_t HardMaximumMessageBytes = 512uz * 1024uz;
	inline constexpr std::size_t MinimumMaximumMessageBytes = 64uz * 1024uz;
}
