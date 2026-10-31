#pragma once

#include "ServerFactions.h"
#include "ServerOutboundFrames.h"

#include "F4TPacketRateLimit.h"
#include "F4TSettlementAccess.h"
#include "F4TWorkshopTransactions.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
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
		[[nodiscard]] bool CanBuild(F4T::Workshop::BuildZoneId a_buildZoneId, const std::string& a_profileId) const;
		[[nodiscard]] bool StoreUsable() const noexcept;
		[[nodiscard]] std::string OwnerOf(F4T::Workshop::BuildZoneId a_buildZoneId) const;
		[[nodiscard]] std::vector<OutboundFrame> SettlementListFrames(std::uint32_t a_connectionId, const Zones& a_zones) const;
		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::string_view a_type,
			const FactionPeer& a_self,
			F4T::PacketRateLimit::TokenBucket& a_bucket,
			const Peers& a_peers,
			const Zones& a_zones,
			const Json& a_packet,
			double a_monotonicSeconds);
		std::vector<OutboundFrame> SetOwner(
			const Peers& a_peers,
			const Zones& a_zones,
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_ownerProfileId,
			std::string& a_failure);

	private:
		[[nodiscard]] F4T::SettlementAccess::ApplyStatus Persist(
			const std::vector<F4T::SettlementAccess::Settlement>& a_snapshot,
			F4T::SettlementAccess::ApplyStatus a_status);
		[[nodiscard]] std::vector<OutboundFrame> StateFrames(
			const Peers& a_peers,
			F4T::Workshop::BuildZoneId a_buildZoneId) const;

		F4T::SettlementAccess::Table table;
		PersistenceGate* persistence{};
		bool corruptStore{};
	};
}
