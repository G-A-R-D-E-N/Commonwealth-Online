#pragma once

#include "ServerOutboundFrames.h"
#include "ServerSettlementAccess.h"

#include "F4TPacketRateLimit.h"
#include "F4TSettlementCapture.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct CapturePeer
	{
		std::uint32_t connectionId{};
		F4T::SettlementCapture::Presence presence;
	};

	class SettlementCaptureService
	{
	public:
		using Json = nlohmann::json;
		using Peers = std::vector<CapturePeer>;
		using Zones = SettlementAccessService::Zones;

		[[nodiscard]] const F4T::SettlementCapture::Policy& Rules() const noexcept { return tracker.Rules(); }
		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			std::uint32_t a_connectionId,
			std::string_view a_profileId,
			F4T::PacketRateLimit::TokenBucket& a_bucket,
			const Peers& a_peers,
			const Zones& a_zones,
			SettlementAccessService& a_access,
			const Json& a_packet,
			double a_monotonicSeconds);
		std::vector<OutboundFrame> Poll(
			const Peers& a_peers,
			const Zones& a_zones,
			SettlementAccessService& a_access,
			double a_monotonicSeconds);
		[[nodiscard]] std::vector<OutboundFrame> ZoneFrames(std::uint32_t a_connectionId, const Zones& a_zones) const;

	private:
		void AppendNotice(
			std::vector<OutboundFrame>& a_frames,
			const Peers& a_peers,
			const F4T::SettlementCapture::Workbench& a_workbench,
			std::string_view a_capturerProfileId,
			std::string_view a_ownerProfileId,
			F4T::SettlementCapture::Status a_status,
			double a_elapsedSeconds) const;

		F4T::SettlementCapture::Tracker tracker;
	};
}
