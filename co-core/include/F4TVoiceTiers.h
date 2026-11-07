#pragma once

#include <cstdint>

namespace F4T::Voice
{
	enum class Tier : std::uint8_t
	{
		Whisper = 0,
		Normal = 1,
		Shout = 2
	};

	inline constexpr double WhisperRange = 256.0;
	inline constexpr double NormalRange = 1024.0;
	inline constexpr double ShoutRange = 2560.0;

	[[nodiscard]] constexpr double TierRange(Tier a_tier) noexcept
	{
		switch (a_tier) {
		case Tier::Whisper:
			return WhisperRange;
		case Tier::Shout:
			return ShoutRange;
		default:
			return NormalRange;
		}
	}
}
