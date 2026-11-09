#pragma once

#include <optional>

namespace F4T::CampGround
{
	struct Surface
	{
		float z{};
		float slopeDegrees{};
	};

	[[nodiscard]] std::optional<Surface> SurfaceAt(float a_x, float a_y, float a_z);
}
