#include "ServerSettlementAccess.h"

#include "F4TSettlementAccessProtocol.h"
#include "F4TWorkshopRaids.h"
#include "F4TWorkshopScale.h"
#include "PersistenceGate.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <stdexcept>
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

	std::optional<F4T::SettlementAccess::Table> SettlementAccessService::StageCamp(
		PersistenceGate* a_persistence, F4T::Workshop::BuildZoneId a_buildZoneId,
		std::string_view a_ownerProfileId, std::uint64_t a_upkeepUntil) const
	{
		if (corruptStore || persistence == nullptr || persistence != a_persistence) return std::nullopt;
		auto candidate = table;
		if (candidate.SetOwner(a_buildZoneId, a_ownerProfileId, F4T::SettlementAccess::SettlementKind::Camp) != ApplyStatus::Committed ||
			candidate.SetUpkeepPaidUntil(a_buildZoneId, a_upkeepUntil) != ApplyStatus::Committed) return std::nullopt;
		return candidate;
	}

	std::vector<OutboundFrame> SettlementAccessService::CommitCamp(F4T::SettlementAccess::Table a_table,
		const Peers& a_peers, F4T::Workshop::BuildZoneId a_buildZoneId)
	{
		table = std::move(a_table);
		return StateFrames(a_peers, a_buildZoneId);
	}

	bool SettlementAccessService::CanBuild(F4T::Workshop::BuildZoneId a_buildZoneId, const std::string& a_profileId) const
	{
		if (corruptStore) {
			return false;
		}
		return table.CanBuild(a_buildZoneId, a_profileId);
	}

	bool SettlementAccessService::CanPlace(F4T::Workshop::BuildZoneId a_buildZoneId, const std::string& a_profileId, bool a_namedZone) const
	{
		if (corruptStore) {
			return false;
		}
		return table.CanPlace(a_buildZoneId, a_profileId, a_namedZone);
	}

	std::string SettlementAccessService::OwnerOf(F4T::Workshop::BuildZoneId a_buildZoneId) const
	{
		if (corruptStore) {
			return {};
		}
		const auto found = table.Find(a_buildZoneId);
		return found ? found->ownerProfileId : std::string{};
	}

	std::optional<F4T::SettlementAccess::Settlement> SettlementAccessService::Find(F4T::Workshop::BuildZoneId a_buildZoneId) const
	{
		if (corruptStore) {
			return std::nullopt;
		}
		return table.Find(a_buildZoneId);
	}

	std::optional<std::uint64_t> SettlementAccessService::UpkeepPaidUntil(F4T::Workshop::BuildZoneId a_buildZoneId) const
	{
		if (corruptStore) {
			return std::nullopt;
		}
		const auto found = table.Find(a_buildZoneId);
		if (!found || found->upkeepPaidUntil == 0) {
			return std::nullopt;
		}
		return found->upkeepPaidUntil;
	}

	bool SettlementAccessService::IsProtected(F4T::Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_unixSeconds) const
	{
		const auto found = Find(a_buildZoneId);
		return found && found->protectedUntilUnix != 0 && a_unixSeconds < found->protectedUntilUnix;
	}

	std::vector<OutboundFrame> SettlementAccessService::SettlementListFrames(std::uint32_t a_connectionId, std::string_view a_viewerProfileId, const Zones& a_zones) const
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
				StateJson(table.Find(entry.first).value_or(Settlement{ entry.first }), a_viewerProfileId)));
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
		double a_monotonicSeconds,
		const std::function<bool()>& a_allow)
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
				StateJson(table.Find(*zone).value_or(Settlement{ *zone }), a_self.profileId)) };
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
		if (!a_allow()) {
			return VetoFrames(a_self.connectionId, a_self.profileId, update->buildZoneId);
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

	void SettlementAccessService::SetFactionProfilesLookup(
		std::function<const std::vector<std::string>*(std::string_view)> a_lookup)
	{
		factionProfilesLookup = std::move(a_lookup);
	}

	void SettlementAccessService::SetOwnerProtectedUntilLookup(
		std::function<std::optional<std::uint64_t>(std::string_view)> a_lookup)
	{
		ownerProtectedUntilLookup = std::move(a_lookup);
	}

	void SettlementAccessService::SetDestroyedStructuresLookup(
		std::function<std::vector<F4T::SettlementAccessProtocol::DestroyedStructure>(F4T::Workshop::BuildZoneId)> a_lookup)
	{
		destroyedStructuresLookup = std::move(a_lookup);
	}

	SettlementAccessService::Json SettlementAccessService::StateJson(const Settlement& a_settlement, std::string_view a_viewerProfileId) const
	{
		std::optional<std::uint64_t> ownerProtectedUntil;
		if (ownerProtectedUntilLookup) {
			ownerProtectedUntil = ownerProtectedUntilLookup(a_settlement.ownerProfileId);
		}
		std::vector<F4T::SettlementAccessProtocol::DestroyedStructure> destroyed;
		if (destroyedStructuresLookup && !a_settlement.ownerProfileId.empty() && CanBuild(a_settlement.buildZoneId, std::string{ a_viewerProfileId })) {
			destroyed = destroyedStructuresLookup(a_settlement.buildZoneId);
		}
		return F4T::SettlementAccessProtocol::MakeState(a_settlement, ownerProtectedUntil, destroyed);
	}

	std::vector<OutboundFrame> SettlementAccessService::SetOwner(
		const Peers& a_peers,
		const Zones& a_zones,
		F4T::Workshop::BuildZoneId a_buildZoneId,
		std::string_view a_ownerProfileId,
		std::string& a_failure,
		std::optional<F4T::SettlementAccess::SettlementKind> a_kind,
		std::optional<std::uint64_t> a_protectedUntil)
	{
		a_failure.clear();
		if (!a_ownerProfileId.empty() && !a_zones.contains(a_buildZoneId)) {
			a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(ApplyStatus::UnknownBuildZone) };
			return {};
		}
		const auto current = table.Find(a_buildZoneId);
		const auto kind = a_kind.value_or(current ? current->kind : F4T::SettlementAccess::SettlementKind::Vanilla);
		if (!a_ownerProfileId.empty() && kind == F4T::SettlementAccess::SettlementKind::Vanilla) {
			const std::vector<std::string>* sameFaction = nullptr;
			if (factionProfilesLookup) {
				sameFaction = factionProfilesLookup(a_ownerProfileId);
			}
			std::uint32_t owned = 0;
			std::uint32_t factionOwned = 0;
			for (const auto& settlement : table.Snapshot()) {
				if (settlement.kind != F4T::SettlementAccess::SettlementKind::Vanilla) {
					continue;
				}
				if (settlement.buildZoneId == a_buildZoneId) {
					continue;
				}
				if (settlement.ownerProfileId == a_ownerProfileId) {
					++owned;
				}
				if (sameFaction != nullptr &&
					std::find(sameFaction->begin(), sameFaction->end(), settlement.ownerProfileId) != sameFaction->end()) {
					++factionOwned;
				}
			}
			if (owned >= F4T::WorkshopScale::MaxVanillaSettlementsPerProfile ||
				(sameFaction != nullptr && factionOwned >= F4T::WorkshopScale::MaxVanillaSettlementsPerFaction)) {
				a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(ApplyStatus::SettlementLimit) };
				return {};
			}
		}
		const auto snapshot = table.Snapshot();
		auto status = table.SetOwner(a_buildZoneId, a_ownerProfileId, a_kind);
		if (status == ApplyStatus::Committed && a_protectedUntil) {
			status = table.MarkProtectedUntil(a_buildZoneId, *a_protectedUntil);
			if (status != ApplyStatus::Committed) (void)table.Restore(snapshot);
		}
		status = Persist(snapshot, status);
		if (status != ApplyStatus::Committed) {
			a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(status) };
			return {};
		}
		return StateFrames(a_peers, a_buildZoneId);
	}

	std::vector<OutboundFrame> SettlementAccessService::ApplyWorkbenchDamage(
		F4T::Workshop::BuildZoneId a_buildZoneId,
		float a_damage,
		std::uint64_t a_unixSeconds,
		std::string& a_failure)
	{
		a_failure.clear();
		const auto status = table.ApplyWorkbenchDamage(a_buildZoneId, a_damage, a_unixSeconds);
		if (status != ApplyStatus::Committed) {
			a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(status) };
			return {};
		}
		damagedWorkbenchZones[a_buildZoneId] = true;
		return {};
	}

	std::vector<OutboundFrame> SettlementAccessService::FlushWorkbenchDamage(
		const std::function<Peers(F4T::Workshop::BuildZoneId)>& a_peersFor,
		std::uint64_t a_unixSeconds)
	{
		if (damagedWorkbenchZones.empty() || a_unixSeconds < lastWorkbenchDamageFlushUnix + 1) {
			return {};
		}
		if (Persist(table.Snapshot(), ApplyStatus::Committed) != ApplyStatus::Committed) {
			return {};
		}
		lastWorkbenchDamageFlushUnix = a_unixSeconds;
		const auto zones = std::move(damagedWorkbenchZones);
		damagedWorkbenchZones.clear();
		std::vector<OutboundFrame> frames;
		for (const auto& entry : zones) {
			auto part = StateFrames(a_peersFor(entry.first), entry.first);
			frames.insert(frames.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
		}
		return frames;
	}

	std::vector<OutboundFrame> SettlementAccessService::MarkProtectedUntil(
		const Peers& a_peers,
		F4T::Workshop::BuildZoneId a_buildZoneId,
		std::uint64_t a_untilUnix,
		std::string& a_failure)
	{
		a_failure.clear();
		const auto snapshot = table.Snapshot();
		const auto status = Persist(snapshot, table.MarkProtectedUntil(a_buildZoneId, a_untilUnix));
		if (status != ApplyStatus::Committed) {
			a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(status) };
			return {};
		}
		return StateFrames(a_peers, a_buildZoneId);
	}

	std::vector<OutboundFrame> SettlementAccessService::SetUpkeepPaidUntil(
		const Peers& a_peers,
		F4T::Workshop::BuildZoneId a_buildZoneId,
		std::uint64_t a_untilUnix,
		std::string& a_failure,
		std::optional<std::uint64_t> a_eraseChargeOperationId)
	{
		a_failure.clear();
		const auto snapshot = table.Snapshot();
		const auto status = Persist(snapshot, table.SetUpkeepPaidUntil(a_buildZoneId, a_untilUnix), a_eraseChargeOperationId);
		if (status != ApplyStatus::Committed) {
			a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(status) };
			return {};
		}
		return StateFrames(a_peers, a_buildZoneId);
	}

	std::vector<OutboundFrame> SettlementAccessService::RepairWorkbench(
		const Peers& a_peers,
		F4T::Workshop::BuildZoneId a_buildZoneId,
		float a_amount,
		std::string& a_failure,
		std::optional<std::uint64_t> a_eraseChargeOperationId)
	{
		a_failure.clear();
		const auto snapshot = table.Snapshot();
		const auto status = Persist(snapshot, table.RepairWorkbench(a_buildZoneId, a_amount), a_eraseChargeOperationId);
		if (status != ApplyStatus::Committed) {
			a_failure = std::string{ F4T::SettlementAccessProtocol::StatusName(status) };
			return {};
		}
		return StateFrames(a_peers, a_buildZoneId);
	}

	bool SettlementAccessService::RestoreSettlement(
		F4T::Workshop::BuildZoneId a_buildZoneId,
		const std::optional<Settlement>& a_previous)
	{
		const auto snapshot = table.Snapshot();
		auto restored = snapshot;
		std::erase_if(restored, [a_buildZoneId](const Settlement& a_settlement) {
			return a_settlement.buildZoneId == a_buildZoneId;
		});
		if (a_previous) {
			restored.push_back(*a_previous);
		}
		if (!table.Restore(restored)) {
			return false;
		}
		return Persist(restored, ApplyStatus::Committed) == ApplyStatus::Committed;
	}

	std::vector<OutboundFrame> SettlementAccessService::TickUnclaimedWorkbenchRepairs(
		const Peers& a_peers,
		std::uint64_t a_unixSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& settlement : table.Snapshot()) {
			if (!settlement.workbenchBroken || settlement.workbenchBrokenUnix == 0) {
				continue;
			}
			if (a_unixSeconds < settlement.workbenchBrokenUnix +
				static_cast<std::uint64_t>(F4T::WorkshopRaids::UnclaimedRepairTimeoutSeconds)) {
				continue;
			}
			const auto snapshot = table.Snapshot();
			if (Persist(snapshot, table.RepairUnclaimedWorkbench(settlement.buildZoneId)) != ApplyStatus::Committed) {
				continue;
			}
			auto next = StateFrames(a_peers, settlement.buildZoneId);
			frames.insert(frames.end(), std::make_move_iterator(next.begin()), std::make_move_iterator(next.end()));
		}
		return frames;
	}

	ApplyStatus SettlementAccessService::Persist(const std::vector<Settlement>& a_snapshot, ApplyStatus a_status, std::optional<std::uint64_t> a_eraseChargeOperationId)
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
			if (!persistence->SaveSettlementAccess(F4T::SettlementAccessProtocol::MakeStoredJson(table.Snapshot()), a_eraseChargeOperationId)) {
				throw std::runtime_error{ "settlement write refused" };
			}
			persistFailureLogged = false;
			return a_status;
		} catch (const std::exception& error) {
			if (!persistFailureLogged) {
				std::cerr << "Settlement access persistence unavailable: " << error.what() << '\n';
				persistFailureLogged = true;
			}
			(void)table.Restore(a_snapshot);
			return ApplyStatus::PersistenceUnavailable;
		}
	}

	std::vector<OutboundFrame> SettlementAccessService::VetoFrames(std::uint32_t a_connectionId, std::string_view a_viewerProfileId, F4T::Workshop::BuildZoneId a_buildZoneId)
	{
		auto frames = RejectFrames(a_connectionId, a_buildZoneId, ApplyStatus::NotAllowed);
		frames.push_back(ReliableFrame(a_connectionId, StateJson(table.Find(a_buildZoneId).value_or(Settlement{ a_buildZoneId }), a_viewerProfileId)));
		return frames;
	}

	std::vector<OutboundFrame> SettlementAccessService::StateFrames(
		const Peers& a_peers,
		F4T::Workshop::BuildZoneId a_buildZoneId) const
	{
		const auto after = table.Find(a_buildZoneId).value_or(Settlement{ a_buildZoneId });
		std::vector<OutboundFrame> frames;
		frames.reserve(a_peers.size());
		for (const auto& peer : a_peers) {
			frames.push_back(ReliableFrame(peer.connectionId, StateJson(after, peer.profileId)));
		}
		return frames;
	}
}
