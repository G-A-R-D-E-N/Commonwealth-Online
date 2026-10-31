#pragma once

#include "F4TPacketRateLimit.h"
#include "F4TVoiceBudget.h"
#include "F4TVoiceWire.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace CommonwealthOnline::Server
{
	inline F4T::PacketRateLimit::Result ObserveInboundDatagram(
		F4T::PacketRateLimit::State& a_globalState,
		F4T::PacketRateLimit::State& a_voiceState,
		double a_nowMonotonicSeconds,
		std::span<const std::byte> a_datagram)
	{
		const auto bytes = static_cast<std::uint64_t>(a_datagram.size());
		if (F4T::Voice::HasVoicePrefix(a_datagram)) {
			return F4T::Voice::ObserveVoiceHard(a_voiceState, a_nowMonotonicSeconds, bytes);
		}
		return F4T::PacketRateLimit::Observe(a_globalState, a_nowMonotonicSeconds, {}, bytes);
	}

	inline std::string_view RateLimitReason(const F4T::PacketRateLimit::Result& a_result)
	{
		return a_result.byteBudgetExceeded ?
			"Sustained byte-rate limit exceeded." :
			"Sustained packet-rate limit exceeded.";
	}
}
