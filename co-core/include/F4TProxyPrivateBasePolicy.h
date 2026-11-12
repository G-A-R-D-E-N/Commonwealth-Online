#pragma once

#include <cstddef>

namespace F4T::ProxyPrivateBasePolicy
{
	inline constexpr std::size_t kMaxPrivateBasesPerSex = 8;

	[[nodiscard]] constexpr bool MayBuildPrivateBase(std::size_t a_liveForSex) noexcept
	{
		return a_liveForSex < kMaxPrivateBasesPerSex;
	}

	[[nodiscard]] constexpr bool MayParkPrivateBase(std::size_t a_parkedForSex) noexcept
	{
		return a_parkedForSex < kMaxPrivateBasesPerSex;
	}
}
