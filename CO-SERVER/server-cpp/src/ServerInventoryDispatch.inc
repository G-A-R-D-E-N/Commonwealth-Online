		std::optional<std::vector<OutboundFrame>> DispatchInventoryPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			if (a_type == "deathBagDiscover") return HandleDeathBagDiscover(a_connectionId, a_session, a_packet, a_monotonicSeconds);
			if (a_type == "deathBagWithdraw") return HandleDeathBagWithdraw(a_connectionId, a_session, a_packet, a_monotonicSeconds);
			if (a_type == "deathBagGrantReconcile") {
				if (!F4T::DeathBagLocal::Enabled || !a_session.active || !inventoryAuthority || !InventoryNegotiated(a_session) ||
					!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds) || !a_packet.is_object() ||
					(a_packet.size() != 1 && (a_packet.size() != 2 || !a_packet.contains("cursor") || !a_packet.at("cursor").is_string())))
					return std::vector<OutboundFrame>{};
				try {
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					if (!profile || a_session.questProfile.empty()) return std::vector<OutboundFrame>{};
					const auto cursor = a_packet.contains("cursor") && a_packet.at("cursor").is_string() ?
						std::optional<std::string>{ a_packet.at("cursor").get<std::string>() } : std::nullopt;
					if (a_packet.contains("cursor") && !cursor) return std::vector<OutboundFrame>{};
					std::optional<std::string> nextCursor;
					const auto grants = inventoryAuthority->Store().GetDeathBagRecipientGrants(*profile, a_session.questProfile, cursor, &nextCursor);
					if (!grants) return std::vector<OutboundFrame>{};
					Json offered = *grants;
					for (auto& grant : offered) {
						if (cursor && grant.value("freshProof", false)) grant["fresh"] = true;
						grant.erase("freshProof");
						grant.erase("claimManaged");
					}
					Json response{ { "type", "deathBagGrantReconciliation" }, { "grants", std::move(offered) } };
					if (cursor) response["nextCursor"] = nextCursor ? Json(*nextCursor) : Json{};
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, response) };
				} catch (const std::exception&) { return std::vector<OutboundFrame>{}; }
			}
			if (a_type == "deathBagGrantAttempted") {
				if (!F4T::DeathBagLocal::Enabled || !a_session.active || !inventoryAuthority || !InventoryNegotiated(a_session) ||
					!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds) || !a_packet.is_object() ||
					a_packet.size() != 3 || !a_packet.contains("deathId") || !a_packet.at("deathId").is_string() ||
					!a_packet.contains("operationId") || !a_packet.at("operationId").is_string()) return std::vector<OutboundFrame>{};
				const auto& id = a_packet.at("deathId").get_ref<const std::string&>();
				const auto& operation = a_packet.at("operationId").get_ref<const std::string&>();
				if (!F4T::DeathIntent::ValidId(id) || operation.empty() || operation.size() + 5 + id.size() > F4T::Drops::kMaximumOperationIdChars) return std::vector<OutboundFrame>{};
				try {
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					if (!profile || a_session.questProfile.empty() || !inventoryAuthority->Store().MarkDeathBagGrantAttempted(*profile, a_session.questProfile, id, operation)) return std::vector<OutboundFrame>{};
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{ { "type", "deathBagGrantReceipt" }, { "deathId", id }, { "operationId", operation }, { "status", "attempted" } }) };
				} catch (const std::exception&) { return std::vector<OutboundFrame>{}; }
			}
			if (a_type == "deathBagGrantClaim") {
				if (!F4T::DeathBagLocal::Enabled || !a_session.active || !inventoryAuthority || !InventoryNegotiated(a_session) ||
					!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds) || !a_packet.is_object() || a_packet.size() != 3 ||
					!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("operationId") || !a_packet.at("operationId").is_string())
					return std::vector<OutboundFrame>{};
				const auto& id = a_packet.at("deathId").get_ref<const std::string&>();
				const auto& operation = a_packet.at("operationId").get_ref<const std::string&>();
				if (!F4T::DeathIntent::ValidId(id) || operation.empty() || operation.size() + 5 + id.size() > F4T::Drops::kMaximumOperationIdChars) return std::vector<OutboundFrame>{};
				try {
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					if (!profile || a_session.questProfile.empty() || !inventoryAuthority->Store().ClaimDeathBagGrant(*profile, a_session.questProfile, id, operation)) return std::vector<OutboundFrame>{};
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{ { "type", "deathBagGrantReceipt" }, { "deathId", id }, { "operationId", operation }, { "status", "claimed" } }) };
				} catch (const std::exception&) { return std::vector<OutboundFrame>{}; }
			}
			if (a_type == "deathBagSnapshot") return HandleDeathBagSnapshot(a_connectionId, a_session, a_packet, a_monotonicSeconds);
			if (a_type == "deathBagPlaced") return HandleDeathBagPlaced(a_connectionId, a_session, a_packet, a_monotonicSeconds);
			if (a_type == "deathBagPlaceFailed") return HandleDeathBagPlaceFailed(a_connectionId, a_session, a_packet, a_monotonicSeconds);
			if (a_type == "deathBagUnplaced") return HandleDeathBagUnplaced(a_connectionId, a_session, a_packet, a_monotonicSeconds);
			if (a_type == "startingInventoryBegin") return HandleStartingInventoryBegin(a_connectionId, a_packet);
			if (a_type == "startingInventoryVerify") return HandleStartingInventoryVerify(a_connectionId, a_packet);
			if (auto refused = VetoInventory(a_type, a_connectionId, a_session, a_packet)) return std::move(*refused);
			if (a_type == "inventorySnapshotRequest") return HandleInventorySnapshotRequest(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (a_type == "inventoryDropRequest") {
				if (inventoryAuthority != nullptr) return HandleOwnedInventoryRequest(a_connectionId, a_session, a_packet, a_monotonicSeconds, true);
				return HandleDropRequest(a_connectionId, a_session, a_packet);
			}
			if (a_type == "inventoryPickupRequest") {
				if (inventoryAuthority != nullptr) return HandleOwnedInventoryRequest(a_connectionId, a_session, a_packet, a_monotonicSeconds, false);
				return HandlePickupRequest(a_connectionId, a_session, a_packet);
			}
			if (a_type == "inventoryContainerRequest") return HandleOwnedContainerRequest(a_connectionId, a_session, a_packet, a_monotonicSeconds);
			if (a_type == "tradeRequest" || a_type == "tradeRespond" || a_type == "tradeOffer" || a_type == "tradeConfirm" || a_type == "tradeCancel") return HandleTradeIntent(a_connectionId, a_session, a_packet, a_monotonicSeconds);
			if (a_type != "inventoryTradeRequest") return std::nullopt;
			const auto request = F4T::InventoryWire::NormalizeTradeRequest(a_packet);
			if (!request) {
				Reject();
				return std::vector<OutboundFrame>{};
			}
			const auto& operationId = request->at("operationId").get_ref<const std::string&>();
			const auto rejected = [&](const char* a_reason) {
				return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{
					{ "type", "inventoryTradeResult" },
					{ "operationId", operationId },
					{ "status", "rejected" },
					{ "reason", a_reason } }) };
			};
			if (!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds)) return rejected("rateLimited");
			if (!events.Allow(ServerEvent{ ServerEventKind::InventoryChange, a_connectionId, a_session.playerId, a_session.profileId, &a_packet })) Reject();
			return rejected("inventoryAuthorityUnavailable");
		}
