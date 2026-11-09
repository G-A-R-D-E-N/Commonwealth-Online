#include "ServerSettlementCapture.h"

#include "F4TSettlementAccessProtocol.h"
#include "F4TWorkshopProtocol.h"
#include "F4TWorkshopRaids.h"

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

		float RatingOf(const SettlementCaptureService::Ratings& a_ratings, F4T::Workshop::BuildZoneId a_zoneId) noexcept
		{
			const auto found = a_ratings.find(a_zoneId);
			return found == a_ratings.end() ? 0.0F : found->second;
		}

		void AppendRaidRefusal(
			std::vector<OutboundFrame>& a_frames,
			const SettlementCaptureService::Peers& a_peers,
			F4T::Workshop::BuildZoneId a_zoneId,
			std::string_view a_capturerProfileId,
			const std::pair<F4T::WorkshopRaids::HitRefusal, std::uint64_t>& a_refusal)
		{
			for (const auto& peer : a_peers) {
				if (peer.presence.profileId == a_capturerProfileId) {
					a_frames.push_back(ReliableFrame(peer.connectionId, F4T::WorkshopProtocol::MakeRaidHitRefused(a_zoneId, a_refusal.first, a_refusal.second)));
				}
			}
		}
	}

	void SettlementCaptureService::AppendNotice(
		std::vector<OutboundFrame>& a_frames,
		const Peers& a_peers,
		const Capture::Workbench& a_workbench,
		std::string_view a_capturerProfileId,
		std::string_view a_ownerProfileId,
		Capture::Status a_status,
		double a_elapsedSeconds,
		std::optional<std::string_view> a_payloadOwnerProfileId) const
	{
		const auto payloadOwner = a_payloadOwnerProfileId.value_or(a_ownerProfileId);
		const auto packet = Capture::MakeState(a_workbench, a_capturerProfileId, payloadOwner, a_status, a_elapsedSeconds, tracker.Rules().holdSeconds);
		for (const auto& peer : a_peers) {
			if (peer.presence.profileId == a_capturerProfileId || (!a_ownerProfileId.empty() && peer.presence.profileId == a_ownerProfileId)) {
				a_frames.push_back(ReliableFrame(peer.connectionId, packet));
			}
		}
	}

	void SettlementCaptureService::AppendActiveChange(std::vector<OutboundFrame>& a_frames, const Peers& a_peers)
	{
		auto active = tracker.ActiveZones();
		if (active == announcedActive) {
			return;
		}
		announcedActive = std::move(active);
		const auto packet = Capture::MakeActiveZones(announcedActive);
		for (const auto& peer : a_peers) {
			a_frames.push_back(ReliableFrame(peer.connectionId, packet));
		}
	}

	std::vector<OutboundFrame> SettlementCaptureService::ActiveFrames(std::uint32_t a_connectionId) const
	{
		if (announcedActive.empty()) {
			return {};
		}
		return { ReliableFrame(a_connectionId, Capture::MakeActiveZones(announcedActive)) };
	}

	std::optional<std::vector<OutboundFrame>> SettlementCaptureService::Dispatch(
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
		const std::function<bool()>& a_allow)
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
		const auto settlement = a_access.Find(request->buildZoneId);
		if (settlement && settlement->kind == F4T::SettlementAccess::SettlementKind::Camp &&
			(request->op == Capture::Op::Start || request->op == Capture::Op::Continue)) {
			return reject(Capture::Status::NotCapturable);
		}
		if ((request->op == Capture::Op::Start || request->op == Capture::Op::Continue) &&
			settlement && !settlement->ownerProfileId.empty() && settlement->ownerProfileId != a_profileId && !settlement->workbenchBroken) {
			return reject(Capture::Status::WorkbenchIntact);
		}
		if (request->op == Capture::Op::Start && !a_allow()) {
			const auto owner = a_access.OwnerOf(request->buildZoneId);
			const auto workbench = Capture::WorkbenchOf(zone->second.Policy(), tracker.Rules());
			const auto live = tracker.Current(request->buildZoneId, a_profileId, a_monotonicSeconds);
			const auto state = live.value_or(Capture::Reply{ Capture::Status::Cancelled, 0.0, false });
			return std::vector<OutboundFrame>{
				ReliableFrame(a_connectionId, Capture::MakeReject(request->buildZoneId, Capture::Status::Vetoed)),
				ReliableFrame(a_connectionId, Capture::MakeState(workbench, a_profileId, owner, state.status, state.elapsedSeconds, tracker.Rules().holdSeconds))
			};
		}
		const auto owner = a_access.OwnerOf(request->buildZoneId);
		const bool unpaid = [&] {
			const auto paidUntil = a_access.UpkeepPaidUntil(request->buildZoneId);
			const auto unixSeconds = static_cast<std::uint64_t>(a_unixTimeSeconds);
			return paidUntil && unixSeconds > *paidUntil;
		}();
		tracker.SetHoldSeconds(F4T::WorkshopRaids::ClaimHoldSeconds(RatingOf(a_ratings, request->buildZoneId), unpaid));
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
		AppendActiveChange(frames, a_peers);
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
		const Ratings& a_ratings,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		const CaptureRefusal& a_refusal)
	{
		std::vector<OutboundFrame> frames;
		const auto presences = Presences(a_peers);
		const auto unixSeconds = static_cast<std::uint64_t>(a_unixTimeSeconds);
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
			const auto settlement = a_access.Find(zoneId);
			if (settlement && !settlement->ownerProfileId.empty() && !settlement->workbenchBroken) {
				const auto intactOwner = a_access.OwnerOf(zoneId);
				const auto intactBench = Capture::WorkbenchOf(zone->second.Policy(), tracker.Rules());
				for (const auto& capturer : tracker.CapturersForZone(zoneId)) {
					AppendNotice(frames, a_peers, intactBench, capturer, intactOwner, Capture::Status::WorkbenchIntact, 0.0);
				}
				tracker.DropZone(zoneId);
				continue;
			}
			const bool unpaid = [&] {
				const auto paidUntil = a_access.UpkeepPaidUntil(zoneId);
				return paidUntil && unixSeconds > *paidUntil;
			}();
			tracker.SetHoldSeconds(F4T::WorkshopRaids::ClaimHoldSeconds(RatingOf(a_ratings, zoneId), unpaid));
			const auto owner = a_access.OwnerOf(zoneId);
			const auto workbench = Capture::WorkbenchOf(zone->second.Policy(), tracker.Rules());
			for (const auto& event : tracker.Update(workbench, owner, presences, a_monotonicSeconds)) {
				auto status = event.status;
				if (status == Capture::Status::Captured) {
					if (const auto refusal = a_refusal(zoneId, std::string{ event.capturerProfileId })) {
						AppendRaidRefusal(frames, a_peers, zoneId, event.capturerProfileId, *refusal);
						status = Capture::Status::Cancelled;
						AppendNotice(frames, a_peers, workbench, event.capturerProfileId, owner, status, event.elapsedSeconds);
						continue;
					}
					std::string failure;
					const auto kind = settlement ? settlement->kind : F4T::SettlementAccess::SettlementKind::Vanilla;
					if (kind == F4T::SettlementAccess::SettlementKind::Camp) {
						status = Capture::Status::NotCapturable;
						AppendNotice(frames, a_peers, workbench, event.capturerProfileId, owner, status, event.elapsedSeconds);
						continue;
					}
					const auto protectUntil = unixSeconds + static_cast<std::uint64_t>(
						F4T::WorkshopRaids::PostClaimProtectionHours * 3600.0F);
					auto owned = a_access.SetOwner(AccessPeers(a_peers), a_zones, zoneId, event.capturerProfileId, failure, kind, protectUntil);
					if (failure.empty()) {
						frames.insert(frames.end(), std::make_move_iterator(owned.begin()), std::make_move_iterator(owned.end()));
					} else {
						status = FailureStatus(failure);
					}
				}
				// Constraint: recipients stay the pre-capture owner so a defeated owner is told; the payload carries the new owner on success.
				const std::optional<std::string_view> payloadOwner = status == Capture::Status::Captured ? std::optional<std::string_view>{ event.capturerProfileId } : std::nullopt;
				AppendNotice(frames, a_peers, workbench, event.capturerProfileId, owner, status, event.elapsedSeconds, payloadOwner);
			}
		}
		AppendActiveChange(frames, a_peers);
		return frames;
	}
}
