#pragma once

#include "F4TFlyOverPath.h"

#include <cstddef>

namespace F4T::FlyOver
{
	struct PathLoad
	{
		bool        loaded{ false };
		bool        fromFile{ false };
		std::size_t pointCount{ 0 };
	};

	[[nodiscard]] PathLoad BuildPathForPlayer(Path& a_path);
}
