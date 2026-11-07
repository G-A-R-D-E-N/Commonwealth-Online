#include "F4TOverheadProjection.h"

#include <cassert>
#include <cmath>
#include <limits>

int main()
{
	using F4T::OverheadProjection::ToPixels;

	const auto centre = ToPixels(1.0F, 0.5F, 0.5F, 1920.0F, 1080.0F);
	assert(centre && std::fabs(centre->x - 960.0F) < 1e-3F && std::fabs(centre->y - 540.0F) < 1e-3F);

	const auto bottomLeft = ToPixels(1.0F, 0.0F, 0.0F, 1920.0F, 1080.0F);
	assert(bottomLeft && bottomLeft->x == 0.0F && bottomLeft->y == 1080.0F);

	const auto topRight = ToPixels(1.0F, 1.0F, 1.0F, 1920.0F, 1080.0F);
	assert(topRight && topRight->x == 1920.0F && topRight->y == 0.0F);

	assert(!ToPixels(1.0F, 1.01F, 0.5F, 1920.0F, 1080.0F));
	assert(!ToPixels(1.0F, 0.5F, -0.01F, 1920.0F, 1080.0F));
	assert(!ToPixels(0.0F, 0.5F, 0.5F, 1920.0F, 1080.0F));
	assert(!ToPixels(-2.0F, 0.5F, 0.5F, 1920.0F, 1080.0F));
	assert(!ToPixels(std::nanf(""), 0.5F, 0.5F, 1920.0F, 1080.0F));
	assert(!ToPixels(1.0F, std::numeric_limits<float>::quiet_NaN(), 0.5F, 1920.0F, 1080.0F));

	return 0;
}
