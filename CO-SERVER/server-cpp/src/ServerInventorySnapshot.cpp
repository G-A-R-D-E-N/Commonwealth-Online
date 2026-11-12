#include "ServerProtocolImpl.h"

#include "PersistenceGate.h"

#include "F4TInventorySnapshot.h"

#include <exception>

namespace CommonwealthOnline::Server
{
	std::vector<OutboundFrame> ProtocolHost::Impl::HandleInventorySnapshotRequest(std::uint32_t a_connectionId, Session& a_session,
		const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto operation = ReadString(a_packet, "operationId");
		if (!operation || operation->empty() || operation->size() > F4T::Drops::kMaximumOperationIdChars) {
			Reject();
			return {};
		}
		const auto rejected = [&](const char* a_reason) {
			return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{
				{ "type", std::string{ F4T::InventorySnapshot::kResultType } },
				{ "operationId", *operation },
				{ "status", "rejected" },
				{ "reason", a_reason } }) };
		};
		const auto part = F4T::InventorySnapshot::NormalizePart(a_packet);
		if (!part || part->part == 0) {
			if (!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds)) {
				a_session.pendingInventorySnapshot.reset();
				return rejected("rateLimited");
			}
		}
		if (!part) {
			a_session.pendingInventorySnapshot.reset();
			return rejected("invalidRequest");
		}
		if (!InventoryNegotiated(a_session) || inventoryAuthority == nullptr) {
			a_session.pendingInventorySnapshot.reset();
			return rejected("inventoryAuthorityUnavailable");
		}
		if (!events.Allow(ServerEvent{ ServerEventKind::InventoryChange, a_connectionId, a_session.playerId, a_session.profileId, &a_packet })) {
			a_session.pendingInventorySnapshot.reset();
			Reject();
			return rejected("vetoed");
		}
		auto& pending = a_session.pendingInventorySnapshot;
		if (part->part == 0) {
			pending = SessionTypes::PendingInventorySnapshot{ part->operationId, part->baseRevision, part->parts, 0, Json::array() };
		} else if (!pending || pending->operationId != part->operationId || pending->baseRevision != part->baseRevision ||
			pending->parts != part->parts || pending->nextPart != part->part) {
			pending.reset();
			return rejected("invalidRequest");
		}
		for (const auto& item : part->wireItems) pending->items.push_back(item);
		++pending->nextPart;
		if (pending->nextPart < pending->parts) return {};
		const auto assembled = F4T::InventorySnapshot::NormalizeRequest(pending->operationId, pending->baseRevision, pending->items);
		pending.reset();
		if (!assembled) return rejected("invalidRequest");
		std::vector<OutboundFrame> reply;
		try {
			const auto outcome = inventoryAuthority->ReconcileInventory(a_connectionId, *assembled);
			if (!outcome) return rejected("inventoryAuthorityUnavailable");
			reply.push_back(ReliableFrame(a_connectionId, outcome->packet));
			if (outcome->packet.value("status", std::string{}) == "committed") {
				auto charges = ConfirmSettlementCharges(a_connectionId, a_session, a_monotonicSeconds, a_unixTimeSeconds);
				reply.insert(reply.end(), charges.begin(), charges.end());
				ExpireUnconfirmedStaleSettlementCharges(a_session.profileId);
			}
		} catch (const std::exception&) {
			return rejected("inventoryPersistenceUnavailable");
		}
		AuditParkedSuitPiecesOnSnapshot(a_connectionId, a_session, a_monotonicSeconds);
		return reply;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleStartingInventoryBegin(std::uint32_t a_connectionId, const Json& a_packet)
	{
		const auto profile = ReadString(a_packet, "profileId");
		const auto grant = ReadString(a_packet, "grantId");
		const auto character = ReadString(a_packet, "characterId");
		const auto save = ReadString(a_packet, "saveFile");
		const auto hash = ReadString(a_packet, "sha256");
		if (a_packet.size() != 6 || !profile || !grant || *grant != F4T::DurableState::kStartingGrantId ||
			!character || !save || !hash || inventoryAuthority == nullptr) {
			Reject();
			return {};
		}
		try {
			return inventoryAuthority->BeginStartingGrant(a_connectionId, *profile, *character,
				F4T::SaveProfileLogic::AttestedSave{ *save, *hash, 0 });
		} catch (const std::exception&) {
			return {};
		}
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleStartingInventoryVerify(std::uint32_t a_connectionId, const Json& a_packet)
	{
		const auto save = ReadString(a_packet, "saveFile");
		const auto hash = ReadString(a_packet, "sha256");
		if (a_packet.size() != 3 || !save || !hash || inventoryAuthority == nullptr) {
			if (inventoryAuthority != nullptr) inventoryAuthority->InvalidateStartingGrantSave(a_connectionId);
			Reject();
			return {};
		}
		bool accepted = false;
		try {
			accepted = inventoryAuthority->VerifyStartingGrantSave(a_connectionId,
				F4T::SaveProfileLogic::AttestedSave{ *save, *hash, 0 });
		} catch (const std::exception&) {
		}
		return { ReliableFrame(a_connectionId, Json{ { "type", "startingInventoryVerification" }, { "accepted", accepted } }) };
	}
}
