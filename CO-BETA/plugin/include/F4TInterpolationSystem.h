#pragma once

#include "RE/N/NiPoint3.h"

namespace F4T::InterpolationSystem
{
	RE::NiPoint3 Lerp(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_alpha) noexcept;

	float LerpRotation(float a_from, float a_to, float a_alpha) noexcept;
}
