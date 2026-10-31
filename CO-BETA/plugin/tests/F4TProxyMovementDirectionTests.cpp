#include "F4TProxyMovementDirection.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>

namespace
{
	constexpr float kIdleSpeed = 1.0F;
	constexpr float kSpeed = 300.0F;
	constexpr float kEpsilon = 1.0e-5F;
	constexpr float kPi = std::numbers::pi_v<float>;

	float Direction(float a_moveX, float a_moveY, float a_headingZ)
	{
		return F4T::ProxyMovementDirection::RelativeMovementDirection(a_moveX, a_moveY, a_headingZ, kIdleSpeed);
	}

	bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) < kEpsilon;
	}
}

int main()
{
	assert(NearlyEqual(Direction(0.0F, kSpeed, 0.0F), 0.0F));
	assert(NearlyEqual(Direction(kSpeed, 0.0F, 0.0F), kPi / 2.0F));
	assert(NearlyEqual(Direction(-kSpeed, 0.0F, 0.0F), -kPi / 2.0F));
	assert(NearlyEqual(Direction(0.0F, -kSpeed, 0.0F), kPi));

	assert(NearlyEqual(Direction(kSpeed, 0.0F, kPi / 2.0F), 0.0F));
	assert(NearlyEqual(Direction(0.0F, -kSpeed, kPi / 2.0F), kPi / 2.0F));
	assert(NearlyEqual(Direction(0.0F, kSpeed, kPi / 2.0F), -kPi / 2.0F));

	assert(NearlyEqual(Direction(kSpeed, 0.0F, kPi / 2.0F + (2.0F * kPi)), 0.0F));
	assert(NearlyEqual(Direction(-kSpeed, 0.0F, -kPi / 2.0F), 0.0F));
	assert(NearlyEqual(Direction(0.0F, -kSpeed, -kPi / 2.0F), -kPi / 2.0F));
	assert(NearlyEqual(std::fabs(Direction(0.0F, kSpeed, 3.0F * kPi)), kPi));
	assert(NearlyEqual(Direction(-0.0F, -kSpeed, -0.0F), kPi));

	assert(Direction(0.0F, -kSpeed, 0.0F) > 0.0F);

	assert(Direction(0.0F, 0.0F, 0.0F) == 0.0F);
	assert(Direction(0.5F, 0.0F, 0.0F) == 0.0F);
	assert(Direction(0.5F, 0.0F, kPi / 2.0F) == 0.0F);

	const auto nan = std::numeric_limits<float>::quiet_NaN();
	assert(Direction(nan, kSpeed, 0.0F) == 0.0F);
	assert(Direction(kSpeed, 0.0F, nan) == 0.0F);

	assert(NearlyEqual(F4T::ProxyMovementDirection::RadiansToGraphDirection(0.0F), 0.0F));
	assert(NearlyEqual(F4T::ProxyMovementDirection::RadiansToGraphDirection(kPi / 2.0F), 0.25F));
	assert(NearlyEqual(F4T::ProxyMovementDirection::RadiansToGraphDirection(kPi), 0.5F));
	assert(NearlyEqual(F4T::ProxyMovementDirection::RadiansToGraphDirection(-kPi / 2.0F), 0.75F));
	assert(NearlyEqual(F4T::ProxyMovementDirection::RadiansToGraphDirection(-kPi), 0.5F));
	assert(F4T::ProxyMovementDirection::RadiansToGraphDirection(-kPi / 4.0F) < 1.0F);

	return 0;
}
