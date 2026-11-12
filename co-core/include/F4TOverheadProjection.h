#pragma once

#include <optional>

namespace F4T::OverheadProjection
{
	struct Pixel
	{
		float x{};
		float y{};
	};

	[[nodiscard]] inline std::optional<Pixel> ToPixels(float a_w, float a_x, float a_y, float a_width, float a_height)
	{
		if (!(a_w > 1e-5F) || !(a_x >= 0.0F && a_x <= 1.0F) || !(a_y >= 0.0F && a_y <= 1.0F)) {
			return std::nullopt;
		}
		return Pixel{ a_x * a_width, (1.0F - a_y) * a_height };
	}
}
