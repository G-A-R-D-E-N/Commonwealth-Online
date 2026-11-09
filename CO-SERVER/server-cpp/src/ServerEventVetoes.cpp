#include "ServerProtocolImpl.h"

#include "F4TWorkshopProtocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CommonwealthOnline::Server
{
	std::optional<std::vector<OutboundFrame>> ProtocolHost::Impl::VetoQuest(
		std::uint32_t a_connectionId,
		const Session& a_session,
		const std::string& a_type,
		const Json& a_packet,
		double a_unixTimeSeconds)
	{
		if (events.Allow(ServerEvent{ ServerEventKind::QuestReport, a_connectionId, a_session.playerId, a_session.profileId, &a_packet })) return std::nullopt;
		Reject();
		if (a_type == "questReport") {
			stats.questReportsRejected++;
		} else {
			stats.questScopesRejected++;
		}
		return ExistingQuestFrames(a_connectionId, a_session, a_unixTimeSeconds);
	}

	std::optional<std::vector<OutboundFrame>> ProtocolHost::Impl::VetoInventory(
		const std::string& a_type,
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet)
	{
		const bool drop = a_type == "inventoryDropRequest";
		if ((!drop && a_type != "inventoryPickupRequest") || inventoryAuthority != nullptr) return std::nullopt;
		if (drop ? !F4T::Drops::NormalizeDropRequest(a_packet) : !PickupRequestValid(a_packet)) return std::nullopt;
		if (events.Allow(ServerEvent{ ServerEventKind::InventoryChange, a_connectionId, a_session.playerId, a_session.profileId, &a_packet })) return std::nullopt;
		Reject();
		if (drop) return HandleDropRequest(a_connectionId, a_session, a_packet);
		return HandlePickupRequest(a_connectionId, a_session, a_packet);
	}

	std::optional<std::vector<OutboundFrame>> ProtocolHost::Impl::VetoWorkshop(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		const F4T::Workshop::Request& a_request,
		double a_monotonicSeconds)
	{
		if (events.Allow(ServerEvent{ ServerEventKind::SettlementAction, a_connectionId, a_session.playerId, a_session.profileId, &a_packet })) return std::nullopt;
		Reject();
		stats.workshopOpsRejected++;
		const auto zone = workshopZones.find(a_request.buildZoneId);
		const auto currentRevision = zone == workshopZones.end() ? 0 : zone->second.CurrentRevision();
		const F4T::Workshop::Result result{ F4T::Workshop::ApplyStatus::NotAuthorized, currentRevision, a_request.objectId, false, std::nullopt };
		std::vector<OutboundFrame> rejected{ ReliableFrame(a_connectionId, F4T::WorkshopProtocol::MakeReject(a_request, result)) };
		if (zone != workshopZones.end() && F4T::PacketRateLimit::TakeToken(
			a_session.workshopSnapshotRequests,
			a_monotonicSeconds,
			kWorkshopSnapshotRefillPerSecond,
			kWorkshopSnapshotBurst)) {
			AppendFrames(rejected, WorkshopSnapshotFrames(a_connectionId, zone->second));
		}
		return rejected;
	}
}
