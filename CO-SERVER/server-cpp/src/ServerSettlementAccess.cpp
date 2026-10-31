#include "ServerSettlementAccess.h"

#include "F4TSettlementAccessProtocol.h"
#include "PersistenceGate.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using F4T::SettlementAccess::ApplyStatus;
		using F4T::SettlementAccess::Settlement;

		constexpr double kRefillPerSecond = 1.0;
		constexpr double kBurst = 4.0;
		constexpr std::size_t kMaximumSettlementListZones = 512;

		std::vector<OutboundFrame> RejectFrames(std::uint32_t a_connectionId, F4T::Workshop::BuildZoneId a_buildZoneId, ApplyStatus a_status)
		{
			return { ReliableFrame(a_connectionId, F4T::SettlementAccessProtocol::MakeReject(a_buildZoneId, a_status)) };
		}
	}

	bool SettlementAccessService::Attach(PersistenceGate* a_persistence)
	{
		persistence = a_persistence;
		corruptStore = false;
		if (persistence == nullptr) {
			return true;
		}
		const auto stored = F4T::SettlementAccessProtocol::NormalizeStored(persistence->ReadStoredSettlementAccess());
		if (!stored || !table.Restore(*stored)) {
			corruptStore = true;
			std::cerr << "Settlement access: stored payload is unreadable; denying every build and every settlement write "
			             "until the durable settlementAccess entry is repaired or removed.\n";
			return false;
		}
		return true;
	}

	bool SettlementAccessService::StoreUsable() const noexcept
	{
		return !corruptStore;
	}

	bool SettlementAccessService::CanBuild(F4T::Workshop::BuildZoneId a_buildZoneId, const std::string& a_profileId) const
	{
		if (corruptStore) {
			return false;
		}
		return table.CanBuild(a_buildZoneId, a_profileId);
	}

	std::string SettlementAccessService::OwnerOf(F4T::Workshop::BuildZoneId a_buildZoneId) const
	{
		if (corruptStore) {
			return {};
		}
		const auto found = table.Find(a_buildZoneId);
		return found ? found->ownerProfileId : std::string{};
	}

	std::vector<OutboundFrame> SettlementAccessService::SettlementListFrames(std::uint32_t a_connectionId, const Zones& a_zones) const
	{
		std::vector<OutboundFrame> frames;
		if (corruptStore) {
			return frames;
		}
		frames.reserve((std::min)(a_zones.size(), kMaximumSettlementListZones));
		for (const auto& entry : a_zones) {
			if (frames.size() >= kMaximumSettlementListZones) {
				std::cerr << "Settlement access: settlement list truncated at " << kMaximumSettlementListZones << " zones.\n";
				break;
			}
			frames.push_back(ReliableFrame(
				a_connectionId,
				F4T::SettlementAccessProtocol::MakeState(table.Find(entry.first).value_or(Settlement{ entry.first }))));
		}
		return frames;
	}

	std::optional<std::vector<OutboundFrame>> SettlementAccessService::Dispatch(
		std::string_view a_type,
		const FactionPeer& a_self,
		F4T::PacketRateLimit::TokenBucket& a_bucket,
		const Peers& a_peers,
		const Zones& a_zones,
		const Json& a_packet,
		double a_monotonicSeconds)
	{
		if (a_type == "settlementAccessGet") {
			const auto zone = F4T::SettlementAccessProtocol::NormalizeGet(a_packet);
			if (!zone) {
				return std::nullopt;
			}
			if (!F4T::PacketRateLimit::TakeToken(a_bucket, a_monotonicSeconds, kRefillPerSecond, kBurst)) {
				return RejectFrames(a_self.connectionId, *zone, ApplyStatus::RateLimited);
			}
			if (corruptStore) {
				return RejectFrames(a_self.connectionId, *zone, ApplyStatus::PersistenceUnavailable);
			}
			if (!a_zones.contains(*zone)) {
				return RejectFrames(a_self.connectionId, *zone, ApplyStatus::UnknownBuildZone);
			}
			return std::vector<OutboundFrame>{ ReliableFrame(
				a_self.connectionId,
				F4T::SettlementAccessProtocol::MakeState(table.Find(*zone).value_or(Settlement{ *zone }))) };
		}
		if (a_type != "settlementAccessUpdate") {
			return std::nullopt;
		}
		const auto update = F4T::SettlementAccessProtocol::NormalizeUpdate(a_packet);
		if (!update) {
			return std::nullopt;
		}
		if (!F4T::PacketRateLimit::TakeToken(a_bucket, a_monotonicSeconds, kRefillPerSecond, kBurst)) {
			return RejectFrames(a_self.connectionId, update->buildZoneId, ApplyStatus::RateLimited);
		}
		if (!a_zones.contains(update->buildZoneId)) {
			return RejectFrames(a_self.connectionId, update->buildZoneId, ApplyStatus::UnknownBuildZone);
		}
		const auto snapshot = table.Snapshot();
		const auto status = Persist(snapshot, update->grant
			? table.Grant(update->buildZoneId, a_self.profileId, update->targetProfileId)
			: table.Revoke(update->buildZoneId, a_self.profileId, update->targetProfileId));
		if (status != ApplyStatus::Committed) {
			return RejectFrames(a_self.connectionId, update->buildZoneId, status);
		}
		return StateFrames(a_peers, update->buildZoneId);
	}

	std::vector<OutboundFrame> SettlementAccessService::SetOwner(
		const Peers& a_peers,
		const Zones& a_zones,
		F4T::Workshop::BuildZoneId a_buildZoneId,
		std::string_view a_ownerProfileId,
		std::string& a_failure)
	{
		a_failure.clear();
		if (!a_zones.contains(a_buildZoneId)) {
			a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(ApplyStatus::UnknownBuildZone) };
			return {};
		}
		const auto snapshot = table.Snapshot();
		const auto status = Persist(snapshot, table.SetOwner(a_buildZoneId, a_ownerProfileId));
		if (status != ApplyStatus::Committed) {
			a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(status) };
			return {};
		}
		return StateFrames(a_peers, a_buildZoneId);
	}

	ApplyStatus SettlementAccessService::Persist(const std::vector<Settlement>& a_snapshot, ApplyStatus a_status)
	{
		if (a_status != ApplyStatus::Committed || persistence == nullptr) {
			return a_status;
		}
		if (corruptStore) {
			std::cerr << "Settlement access: refusing to overwrite the unreadable stored payload.\n";
			(void)table.Restore(a_snapshot);
			return ApplyStatus::PersistenceUnavailable;
		}
		try {
			persistence->SaveSettlementAccess(F4T::SettlementAccessProtocol::MakeStoredJson(table.Snapshot()));
			return a_status;
		} catch (const std::exception& error) {
			std::cerr << "Settlement access persistence unavailable: " << error.what() << '\n';
			(void)table.Restore(a_snapshot);
			return ApplyStatus::PersistenceUnavailable;
		}
	}

	std::vector<OutboundFrame> SettlementAccessService::StateFrames(
		const Peers& a_peers,
		F4T::Workshop::BuildZoneId a_buildZoneId) const
	{
		const auto after = table.Find(a_buildZoneId).value_or(Settlement{ a_buildZoneId });
		const auto packet = F4T::SettlementAccessProtocol::MakeState(after);
		std::vector<OutboundFrame> frames;
		frames.reserve(a_peers.size());
		for (const auto& peer : a_peers) {
			frames.push_back(ReliableFrame(peer.connectionId, packet));
		}
		return frames;
	}
}
