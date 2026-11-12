#include "pch.h"

#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"

#include "F4TInventorySync.h"

#include <string_view>

#include <nlohmann/json.hpp>

namespace F4T::Networking
{
	using namespace Detail;

	namespace
	{
		bool SendInventoryRequest(const nlohmann::json& a_request, std::string_view a_expectedType, std::uint64_t a_generation)
		{
			if (!IsConnectedToServer()) {
				return false;
			}

			const auto typeField = a_request.find("type");
			if (typeField == a_request.end() || !typeField->is_string()) {
				return false;
			}

			const std::string_view type{ typeField->get_ref<const std::string&>() };
			if (type != a_expectedType) {
				LogThrottledWarning("inventory_request_type", "Refusing to send an owned inventory request with an unexpected packet type.");
				return false;
			}

			return SendPacketBytesForGeneration(a_request.dump(), a_generation);
		}
	}

	bool SendDeathBagGrantReconcile(std::optional<std::string_view> a_cursor)
	{
		nlohmann::json packet{ { "type", "deathBagGrantReconcile" } };
		if (a_cursor) packet["cursor"] = *a_cursor;
		return SendInventoryRequest(packet, "deathBagGrantReconcile", GetConnectionGeneration());
	}

	bool SendDeathBagDiscover(const nlohmann::json& a_request, std::uint64_t a_generation)
	{
		return SendInventoryRequest(a_request, "deathBagDiscover", a_generation);
	}

	bool SendDeathBagWithdraw(const nlohmann::json& a_request, std::uint64_t a_generation)
	{
		return SendInventoryRequest(a_request, "deathBagWithdraw", a_generation);
	}

	bool SendInventoryDropRequest(const nlohmann::json& a_request, std::uint64_t a_generation)
	{
		return SendInventoryRequest(a_request, "inventoryDropRequest", a_generation);
	}

	bool SendInventoryPickupRequest(const nlohmann::json& a_request, std::uint64_t a_generation)
	{
		return SendInventoryRequest(a_request, "inventoryPickupRequest", a_generation);
	}

	bool SendInventorySnapshotRequest(const nlohmann::json& a_request, std::uint64_t a_generation)
	{
		return SendInventoryRequest(a_request, "inventorySnapshotRequest", a_generation);
	}

	bool SendInventoryContainerRequest(const nlohmann::json& a_request, std::uint64_t a_generation)
	{
		return SendInventoryRequest(a_request, "inventoryContainerRequest", a_generation);
	}

}

namespace F4T::Networking::Detail
{
	void HandleInventoryResultPacket(const Json& a_packet, F4T::InventoryContract::OwnedOperation a_expected)
	{
		const auto result = F4T::InventoryContract::ParseOwnedResult(a_packet, a_expected);
		if (!result) {
			LogThrottledWarning("inventory_result_malformed", "Ignoring malformed owned inventory result packet.");
			return;
		}

		F4T::InventorySync::NoteOwnedResult(*result);
	}
}
