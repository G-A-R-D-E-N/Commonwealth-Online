#include "ServerSettlementCapture.h"

#include "F4TSettlementAccessProtocol.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		namespace Capture = F4T::SettlementCapture;

		constexpr double kRefillPerSecond = 2.0;
		constexpr double kBurst = 6.0;
		constexpr std::size_t kMaximumCaptureZones = 1024;
		constexpr std::size_t kZonesPerFrame = 256;

		std::vector<F4T::SettlementCapture::Presence> Presences(const SettlementCaptureService::Peers& a_peers)
		{
			std::vector<Capture::Presence> presences;
			presences.reserve(a_peers.size());
			for (const auto& peer : a_peers) {
				presences.push_back(peer.presence);
			}
			return presences;
		}

		std::vector<FactionPeer> AccessPeers(const SettlementCaptureService::Peers& a_peers)
		{
			std::vector<FactionPeer> peers;
			peers.reserve(a_peers.size());
			for (const auto& peer : a_peers) {
				peers.push_back(FactionPeer{ peer.connectionId, 0, peer.presence.profileId, {} });
			}
			return peers;
		}

		Capture::Status FailureStatus(const std::string& a_failure)
		{
			if (a_failure == F4T::SettlementAccessProtocol::StatusName(F4T::SettlementAccess::ApplyStatus::SettlementLimit)) {
				return Capture::Status::SettlementLimit;
			}
			if (a_failure == F4T::SettlementAccessProtocol::StatusName(F4T::SettlementAccess::ApplyStatus::PersistenceUnavailable)) {
				return Capture::Status::PersistenceUnavailable;
			}
			std::cerr << "Settlement capture: unexpected SetOwner failure '" << a_failure << "'\n";
			return Capture::Status::PersistenceUnavailable;
		}
	}

	void SettlementCaptureService::AppendNotice(
		std::vector<OutboundFrame>& a_frames,
		const Peers& a_peers,
		const Capture::Workbench& a_workbench,
		std::string_view a_capturerProfileId,
		std::string_view a_ownerProfileId,
		Capture::Status a_status,
		double a_elapsedSeconds) const
	{
		const auto packet = Capture::MakeState(a_workbench, a_capturerProfileId, a_ownerProfileId, a_status, a_elapsedSeconds, tracker.Rules().holdSeconds);
		for (const auto& peer : a_peers) {
			if (peer.presence.profileId == a_capturerProfileId || (!a_ownerProfileId.empty() && peer.presence.profileId == a_ownerProfileId)) {
				a_frames.push_back(ReliableFrame(peer.connectionId, packet));
			}
		}
	}

	std::optional<std::vector<OutboundFrame>> SettlementCaptureService::Dispatch(
		std::string_view a_type,
		std::uint32_t a_connectionId,
		std::string_view a_profileId,
		F4T::PacketRateLimit::TokenBucket& a_bucket,
		const Peers& a_peers,
		const Zones& a_zones,
		SettlementAccessService& a_access,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		if (a_type != "settlementCapture") {
			return std::nullopt;
		}
		const auto request = Capture::NormalizeRequest(a_packet);
		if (!request) {
			return std::nullopt;
		}
		const auto reject = [&](Capture::Status a_status) {
			return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Capture::MakeReject(request->buildZoneId, a_status)) };
		};
		if (!F4T::PacketRateLimit::TakeToken(a_bucket, a_monotonicSeconds, kRefillPerSecond, kBurst)) {
			return reject(Capture::Status::RateLimited);
		}
		const auto zone = a_zones.find(request->buildZoneId);
		if (zone == a_zones.end()) {
			return reject(Capture::Status::UnknownBuildZone);
		}
		if (request->op == Capture::Op::Start && !a_access.StoreUsable()) {
			return reject(Capture::Status::PersistenceUnavailable);
		}
		const auto owner = a_access.OwnerOf(request->buildZoneId);
		const auto workbench = Capture::WorkbenchOf(zone->second.Policy(), tracker.Rules());
		const auto reply = tracker.Handle(*request, workbench, owner, a_profileId, Presences(a_peers), a_monotonicSeconds);
		switch (reply.status) {
		case Capture::Status::Started:
		case Capture::Status::Holding:
		case Capture::Status::Contested:
		case Capture::Status::Cancelled:
			break;
		default:
			if (!reply.attemptEnded) {
				return reject(reply.status);
			}
			break;
		}
		std::vector<OutboundFrame> frames;
		AppendNotice(frames, a_peers, workbench, a_profileId, owner, reply.status, reply.elapsedSeconds);
		return frames;
	}

	std::vector<OutboundFrame> SettlementCaptureService::ZoneFrames(std::uint32_t a_connectionId, const Zones& a_zones) const
	{
		std::vector<OutboundFrame> frames;
		std::vector<Capture::Workbench> chunk;
		std::size_t listed{};
		for (const auto& entry : a_zones) {
			if (listed == kMaximumCaptureZones) {
				std::cerr << "Settlement capture: zone list truncated at " << kMaximumCaptureZones << " zones.\n";
				break;
			}
			chunk.push_back(Capture::WorkbenchOf(entry.second.Policy(), tracker.Rules()));
			++listed;
			if (chunk.size() == kZonesPerFrame) {
				frames.push_back(ReliableFrame(a_connectionId, Capture::MakeZones(chunk)));
				chunk.clear();
			}
		}
		if (!chunk.empty()) {
			frames.push_back(ReliableFrame(a_connectionId, Capture::MakeZones(chunk)));
		}
		return frames;
	}

	std::vector<OutboundFrame> SettlementCaptureService::Poll(
		const Peers& a_peers,
		const Zones& a_zones,
		SettlementAccessService& a_access,
		double a_monotonicSeconds)
	{
		std::vector<OutboundFrame> frames;
		const auto presences = Presences(a_peers);
		for (const auto zoneId : tracker.ActiveZones()) {
			const auto zone = a_zones.find(zoneId);
			if (zone == a_zones.end()) {
				const Capture::Workbench removed{ zoneId, 0.0, 0.0, 0.0, 0.0 };
				const auto removedOwner = a_access.OwnerOf(zoneId);
				for (const auto& capturer : tracker.CapturersForZone(zoneId)) {
					AppendNotice(frames, a_peers, removed, capturer, removedOwner, Capture::Status::Cancelled, 0.0);
				}
				tracker.DropZone(zoneId);
				continue;
			}
			const auto owner = a_access.OwnerOf(zoneId);
			const auto workbench = Capture::WorkbenchOf(zone->second.Policy(), tracker.Rules());
			for (const auto& event : tracker.Update(workbench, owner, presences, a_monotonicSeconds)) {
				auto status = event.status;
				if (status == Capture::Status::Captured) {
					std::string failure;
					auto owned = a_access.SetOwner(AccessPeers(a_peers), a_zones, zoneId, event.capturerProfileId, failure);
					if (failure.empty()) {
						frames.insert(frames.end(), std::make_move_iterator(owned.begin()), std::make_move_iterator(owned.end()));
					} else {
						status = FailureStatus(failure);
					}
				}
				AppendNotice(frames, a_peers, workbench, event.capturerProfileId, owner, status, event.elapsedSeconds);
			}
		}
		return frames;
	}
}
