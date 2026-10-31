		std::optional<std::vector<OutboundFrame>> DispatchInventoryPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			if (a_type == "inventoryDropRequest") {
				if (inventoryAuthority != nullptr) return HandleOwnedInventoryRequest(a_connectionId, a_session, a_packet, a_monotonicSeconds, true);
				return HandleDropRequest(a_connectionId, a_session, a_packet);
			}
			if (a_type == "inventoryPickupRequest") {
				if (inventoryAuthority != nullptr) return HandleOwnedInventoryRequest(a_connectionId, a_session, a_packet, a_monotonicSeconds, false);
				return HandlePickupRequest(a_connectionId, a_session, a_packet);
			}
			const bool container = a_type == "inventoryContainerRequest";
			if (!container && a_type != "inventoryTradeRequest") return std::nullopt;
			const auto request = container ?
				F4T::InventoryWire::NormalizeContainerRequest(a_packet) :
				F4T::InventoryWire::NormalizeTradeRequest(a_packet);
			if (!request) {
				Reject();
				return std::vector<OutboundFrame>{};
			}
			const auto& operationId = request->at("operationId").get_ref<const std::string&>();
			const auto rejected = [&](const char* a_reason) {
				return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{
					{ "type", container ? "inventoryContainerResult" : "inventoryTradeResult" },
					{ "operationId", operationId },
					{ "status", "rejected" },
					{ "reason", a_reason } }) };
			};
			if (!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds)) return rejected("rateLimited");
			return rejected("inventoryAuthorityUnavailable");
		}
