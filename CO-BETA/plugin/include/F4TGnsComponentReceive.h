#pragma once

#include "F4TCompatibilityShim.h"
#include "F4TGnsWinsockShimState.h"
#include "F4TInventorySync.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace F4T::ComponentReceive
{
	inline constexpr F4T::Replication::Family kDropFamily = F4T::Replication::Family::StableDroppedItem;
	inline constexpr F4T::Replication::Channel kDropChannel = F4T::Replication::Channel::Reliable;
	inline constexpr std::string_view kDropLegacyFamily = "stableDroppedItem";
	inline constexpr std::string_view kComponentPathUnavailable = "component receive path is unavailable";
	inline constexpr std::string_view kComponentDecodeFailure = "component receive payload is not decodable";

	void InstallComponentReceive(F4T::GnsWinsockShimState::SocketState& a_state);
	[[nodiscard]] F4T::Compatibility::ReceiveDemuxResult ClassifyReceive(
		F4T::GnsWinsockShimState::SocketState& a_state,
		std::span<const std::uint8_t> a_payload);
	[[nodiscard]] std::string HandleComponentFrame(const F4T::Compatibility::ReceiveDemuxResult& a_classified);
}
