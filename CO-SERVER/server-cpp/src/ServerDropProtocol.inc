		std::vector<OutboundFrame> HandleDropRequest(std::uint32_t a_connectionId, Session&, const Json& a_packet)
		{
			const auto request = F4T::Drops::NormalizeDropRequest(a_packet);
			if (!request) {
				Reject();
				return {};
			}
			return { ReliableFrame(a_connectionId, Json{
				{ "type", "inventoryDropResult" },
				{ "operationId", request->operationId },
				{ "status", "rejected" },
				{ "revision", 0u },
				{ "reason", "inventoryAuthorityUnavailable" } }) };
		}

		std::vector<OutboundFrame> HandlePickupRequest(std::uint32_t a_connectionId, Session&, const Json& a_packet)
		{
			const auto operationId = ReadString(a_packet, "operationId");
			const auto entityId = ReadUInt32(a_packet, "entityId");
			if (!operationId || operationId->empty() || operationId->size() > F4T::Drops::kMaximumOperationIdChars || !entityId) {
				Reject();
				return {};
			}
			return { ReliableFrame(a_connectionId, Json{
				{ "type", "inventoryPickupResult" },
				{ "operationId", *operationId },
				{ "entityId", *entityId },
				{ "status", "rejected" },
				{ "reason", "inventoryAuthorityUnavailable" } }) };
		}
