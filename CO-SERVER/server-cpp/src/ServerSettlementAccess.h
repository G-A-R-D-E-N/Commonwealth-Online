#pragma once

#include "ServerFactions.h"
#include "ServerOutboundFrames.h"

#include "F4TPacketRateLimit.h"
#include "F4TSettlementAccess.h"
#include "F4TSettlementAccessProtocol.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	class PersistenceGate;

	class SettlementAccessService
	{
	public:
		using Json = nlohmann::json;
		using Peers = std::vector<FactionPeer>;
		using Zones = std::map<F4T::Workshop::BuildZoneId, F4T::Workshop::SettlementState>;

		bool Attach(PersistenceGate* a_persistence);
		void SetFactionProfilesLookup(std::function<const std::vector<std::string>*(std::string_view)> a_lookup);
		void SetDestroyedStructuresLookup(std::function<std::vector<F4T::SettlementAccessProtocol::DestroyedStructure>(F4T::Workshop::BuildZoneId)> a_lookup);
		[[nodiscard]] bool CanBuild(F4T::Workshop::BuildZoneId a_buildZoneId, const std::string& a_profileId) const;
		[[nodiscard]] bool CanPlace(F4T::Workshop::BuildZoneId a_buildZoneId, const std::string& a_profileId, bool a_namedZone) const;
		[[nodiscard]] bool StoreUsable() const noexcept;
		[[nodiscard]] std::optional<F4T::SettlementAccess::Table> StageCamp(
			PersistenceGate* a_persistence, F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_ownerProfileId, std::uint64_t a_upkeepUntil) const;
		std::vector<OutboundFrame> CommitCamp(F4T::SettlementAccess::Table a_table,
			const Peers& a_peers, F4T::Workshop::BuildZoneId a_buildZoneId);
		[[nodiscard]] std::string OwnerOf(F4T::Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] std::optional<F4T::SettlementAccess::Settlement> Find(F4T::Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] std::optional<std::uint64_t> UpkeepPaidUntil(F4T::Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] bool IsProtected(F4T::Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_unixSeconds) const;
		[[nodiscard]] std::vector<OutboundFrame> SettlementListFrames(std::uint32_t a_connectionId, std::string_view a_viewerProfileId, const Zones& a_zones) const;
		std::vector<OutboundFrame> FlushWorkbenchDamage(
			const std::function<Peers(F4T::Workshop::BuildZoneId)>& a_peersFor,
			std::uint64_t a_unixSeconds);
		std::vector<OutboundFrame> ApplyWorkbenchDamage(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			float a_damage,
			std::uint64_t a_unixSeconds,
			std::string& a_failure);
		std::vector<OutboundFrame> MarkProtectedUntil(
			const Peers& a_peers,
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::uint64_t a_untilUnix,
			std::string& a_failure);
		std::vector<OutboundFrame> SetUpkeepPaidUntil(
				const Peers& a_peers,
				F4T::Workshop::BuildZoneId a_buildZoneId,
				std::uint64_t a_untilUnix,
				std::string& a_failure,
				std::optional<std::uint64_t> a_eraseChargeOperationId = std::nullopt);
		std::vector<OutboundFrame> RepairWorkbench(
			const Peers& a_peers,
			F4T::Workshop::BuildZoneId a_buildZoneId,
			float a_amount,
			std::string& a_failure,
			std::optional<std::uint64_t> a_eraseChargeOperationId = std::nullopt);
			std::vector<OutboundFrame> TickUnclaimedWorkbenchRepairs(const Peers& a_peers, std::uint64_t a_unixSeconds);
		bool RestoreSettlement(F4T::Workshop::BuildZoneId a_buildZoneId, const std::optional<F4T::SettlementAccess::Settlement>& a_previous);
		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			const FactionPeer& a_self,
			F4T::PacketRateLimit::TokenBucket& a_bucket,
			const Peers& a_peers,
			const Zones& a_zones,
			const Json& a_packet,
			double a_monotonicSeconds,
			const std::function<bool()>& a_allow);
		std::vector<OutboundFrame> SetOwner(
			const Peers& a_peers,
			const Zones& a_zones,
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_ownerProfileId,
			std::string& a_failure,
			std::optional<F4T::SettlementAccess::SettlementKind> a_kind = std::nullopt,
			std::optional<std::uint64_t> a_protectedUntil = std::nullopt);
		std::vector<OutboundFrame> VetoFrames(std::uint32_t a_connectionId, std::string_view a_viewerProfileId, F4T::Workshop::BuildZoneId a_buildZoneId);
		[[nodiscard]] std::vector<OutboundFrame> StateFrames(
			const Peers& a_peers,
			F4T::Workshop::BuildZoneId a_buildZoneId) const;

	private:
		[[nodiscard]] F4T::SettlementAccess::ApplyStatus Persist(
			const std::vector<F4T::SettlementAccess::Settlement>& a_snapshot,
			F4T::SettlementAccess::ApplyStatus a_status,
			std::optional<std::uint64_t> a_eraseChargeOperationId = std::nullopt);
		[[nodiscard]] Json StateJson(const F4T::SettlementAccess::Settlement& a_settlement, std::string_view a_viewerProfileId) const;

		F4T::SettlementAccess::Table table;
		std::map<F4T::Workshop::BuildZoneId, bool> damagedWorkbenchZones;
		std::uint64_t lastWorkbenchDamageFlushUnix{};
		PersistenceGate* persistence{};
		bool corruptStore{};
		bool persistFailureLogged{};
		std::function<const std::vector<std::string>*(std::string_view)> factionProfilesLookup;
		std::function<std::vector<F4T::SettlementAccessProtocol::DestroyedStructure>(F4T::Workshop::BuildZoneId)> destroyedStructuresLookup;
	};
}
