#pragma once

#include <cstdint>

namespace F4T::PauseMenuMap
{
	struct MarkerIcons
	{
		const char* typeName;
		const char* discovered;
		const char* undiscovered;
	};

	const MarkerIcons& IconsFor(std::uint8_t a_type);
}
