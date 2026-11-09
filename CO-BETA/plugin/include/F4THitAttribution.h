#pragma once

namespace F4T::HitAttribution
{
	template <class T>
	[[nodiscard]] constexpr const T* PickReference(const T* a_primary, const T* a_fallback) noexcept
	{
		return a_primary ? a_primary : a_fallback;
	}
}
