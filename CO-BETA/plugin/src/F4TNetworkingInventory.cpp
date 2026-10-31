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
		bool SendInventoryRequest(const nlohmann::json& a_request, std::string_view a_expectedType)
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

			return SendPacketBytes(a_request.dump());
		}
	}

	bool SendInventoryDropRequest(const nlohmann::json& a_request)
	{
		return SendInventoryRequest(a_request, "inventoryDropRequest");
	}

	bool SendInventoryPickupRequest(const nlohmann::json& a_request)
	{
		return SendInventoryRequest(a_request, "inventoryPickupRequest");
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
