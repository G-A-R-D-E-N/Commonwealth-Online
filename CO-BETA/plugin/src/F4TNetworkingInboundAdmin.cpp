#include "pch.h"

#include "F4TAdminItems.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"

namespace F4T::Networking::Detail
{
	void HandleAdminItemOpPacket(const Json& a_packet)
	{
		const auto opId = ReadUint32(a_packet, "opId");
		if (!opId) {
			LogThrottledWarning("admin_item_op_missing_opId", "Ignoring adminItemOp packet without a valid opId.");
			return;
		}

		const auto op = ReadString(a_packet, "op").value_or(std::string{});
		const auto itemId = ReadString(a_packet, "itemId").value_or(std::string{});
		const auto count = ReadUint32(a_packet, "count").value_or(0U);
		const auto connectionGeneration = F4T::Networking::GetConnectionGeneration();
		const auto result = F4T::AdminItems::QueueItemOp(op, itemId, count, *opId);
		if (result == F4T::AdminItems::QueueResult::kBusy) {
			F4T::Networking::SendAdminItemResult(*opId, false, 0, "busy", connectionGeneration);
		} else if (result == F4T::AdminItems::QueueResult::kInvalid) {
			F4T::Networking::SendAdminItemResult(*opId, false, 0, "invalid request", connectionGeneration);
		}
	}

	void HandleAdminInventoryRequestPacket(const Json& a_packet)
	{
		const auto opId = ReadUint32(a_packet, "opId");
		if (!opId) {
			LogThrottledWarning(
				"admin_inventory_request_missing_opId",
				"Ignoring adminInventoryRequest packet without a valid opId.");
			return;
		}

		const auto connectionGeneration = F4T::Networking::GetConnectionGeneration();
		if (F4T::AdminItems::QueueInventoryRequest(*opId) == F4T::AdminItems::QueueResult::kBusy) {
			F4T::Networking::SendAdminItemResult(*opId, false, 0, "busy", connectionGeneration);
		}
	}
}
