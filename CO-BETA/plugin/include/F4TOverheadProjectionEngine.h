#pragma once

#include "F4TOverheadProjection.h"

namespace F4T::OverheadProjection
{
	[[nodiscard]] std::optional<Pixel> ProjectWorldPoint(float a_x, float a_y, float a_z, float a_width, float a_height);
}
