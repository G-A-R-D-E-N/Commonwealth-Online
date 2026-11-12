#pragma once

#include "F4TVoiceTiers.h"

namespace F4T::Voice
{
	inline constexpr double PttTailSeconds = 0.150;

	struct PttInput
	{
		bool keyDown{};
		bool whisperDown{};
		bool shoutDown{};
		bool inputAllowed{ true };
	};

	struct PttTick
	{
		bool transmit{};
		Tier tier{ Tier::Normal };
		bool burstStart{};
		bool burstEnd{};
	};

	class PttState
	{
	public:
		void Poll(const PttInput& a_input, double a_nowSeconds) noexcept;
		[[nodiscard]] PttTick Tick(double a_nowSeconds) noexcept;

	private:
		bool m_held{};
		bool m_tailActive{};
		bool m_pendingTransmit{};
		bool m_pendingBurstStart{};
		double m_releasedAtSeconds{};
		Tier m_tier{ Tier::Normal };
		Tier m_pendingTier{ Tier::Normal };
	};
}
