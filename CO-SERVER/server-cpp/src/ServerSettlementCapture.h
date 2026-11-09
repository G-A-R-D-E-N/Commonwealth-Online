#pragma once

#include "ServerOutboundFrames.h"
#include "ServerSettlementAccess.h"

#include "F4TPacketRateLimit.h"
#include "F4TSettlementCapture.h"
#include "F4TWorkshopRaids.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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
		using Ratings = std::map<F4T::Workshop::BuildZoneId, float>;
		using CaptureRefusal = std::function<std::optional<std::pair<F4T::WorkshopRaids::HitRefusal, std::uint64_t>>(F4T::Workshop::BuildZoneId, const std::string&)>;

		[[nodiscard]] const F4T::SettlementCapture::Policy& Rules() const noexcept { return tracker.Rules(); }
		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			std::uint32_t a_connectionId,
			std::string_view a_profileId,
			F4T::PacketRateLimit::TokenBucket& a_bucket,
			const Peers& a_peers,
			const Zones& a_zones,
			SettlementAccessService& a_access,
			const Ratings& a_ratings,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			const std::function<bool()>& a_allow);
		std::vector<OutboundFrame> Poll(
			const Peers& a_peers,
			const Zones& a_zones,
			SettlementAccessService& a_access,
			const Ratings& a_ratings,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			const CaptureRefusal& a_refusal);
		[[nodiscard]] std::vector<OutboundFrame> ZoneFrames(std::uint32_t a_connectionId, const Zones& a_zones) const;
		[[nodiscard]] std::vector<OutboundFrame> ActiveFrames(std::uint32_t a_connectionId) const;

	private:
		void AppendActiveChange(std::vector<OutboundFrame>& a_frames, const Peers& a_peers);
		void AppendNotice(
			std::vector<OutboundFrame>& a_frames,
			const Peers& a_peers,
			const F4T::SettlementCapture::Workbench& a_workbench,
			std::string_view a_capturerProfileId,
			std::string_view a_ownerProfileId,
			F4T::SettlementCapture::Status a_status,
			double a_elapsedSeconds,
			std::optional<std::string_view> a_payloadOwnerProfileId = std::nullopt) const;

		F4T::SettlementCapture::Tracker tracker;
		std::set<F4T::Workshop::BuildZoneId> announcedActive;
	};
}
