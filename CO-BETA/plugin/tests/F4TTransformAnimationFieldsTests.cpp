#include "F4TTransformAnimationFields.h"
#include "F4TTransformValidation.h"

#include <cassert>
#include <cmath>
#include <limits>

int main()
{
	using Json = nlohmann::json;

	Json packet{
		{ "type", "transform" },
		{ "x", 1.0 },
		{ "y", 2.0 },
		{ "z", 3.0 },
		{ "angleZ", 90.0 },
		{ "cellId", "00000ABC" },
		{ "worldspaceId", "00000000" },
		{ "movementType", "normal" }
	};
	F4T::TransformAnimationFields::WriteAnimationFields(packet, 45.0F, -30.0F, 180.0F);
	assert(packet.at("animationDirection") == 45.0);
	assert(packet.at("aimPitch") == -30.0);
	assert(packet.at("turnDelta") == 180.0);

	Json clamped{};
	F4T::TransformAnimationFields::WriteAnimationFields(clamped, 4000.0F, -400.0F, 99999.0F);
	assert(clamped.at("animationDirection") == 360.0);
	assert(clamped.at("aimPitch") == -80.0);
	assert(clamped.at("turnDelta") == 720.0);

	Json nonFinite{};
	F4T::TransformAnimationFields::WriteAnimationFields(
		nonFinite,
		std::nanf(""),
		std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity());
	assert(nonFinite.at("animationDirection") == 0.0);
	assert(nonFinite.at("aimPitch") == 0.0);
	assert(nonFinite.at("turnDelta") == 0.0);

	Json zeroAim{};
	F4T::TransformAnimationFields::WriteAnimationFields(zeroAim, 0.0F, 0.0F, 0.0F);
	assert(zeroAim.contains("aimPitch"));
	assert(zeroAim.contains("turnDelta"));

	const auto normalized = F4T::TransformValidation::NormalizeTransform(packet);
	assert(normalized.has_value());
	assert(normalized->at("animationDirection") == 45.0);
	assert(normalized->at("aimPitch") == -30.0);
	assert(normalized->at("turnDelta") == 180.0);

	Json extremes = packet;
	F4T::TransformAnimationFields::WriteAnimationFields(extremes, 360.0F, 180.0F, 10000.0F);
	const auto normalizedExtremes = F4T::TransformValidation::NormalizeTransform(extremes);
	assert(normalizedExtremes.has_value());
	assert(normalizedExtremes->at("animationDirection") == 360.0);
	assert(normalizedExtremes->at("aimPitch") == 80.0);
	assert(normalizedExtremes->at("turnDelta") == 720.0);

	return 0;
}
