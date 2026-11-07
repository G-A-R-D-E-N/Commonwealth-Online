			std::vector<OutboundFrame> DeathBagVoidedFrames(const DownedService::Peers& a_peers, const std::vector<F4T::DeathBag::Expired>& a_expired)
			{
				std::vector<OutboundFrame> frames;
				for (const auto& voided : a_expired) {
					for (const auto& peer : a_peers) {
						if (peer.persistenceProfile == voided.characterId) frames.push_back(ReliableFrame(peer.connectionId, Json{ { "type", "deathBagVoided" }, { "deathId", voided.deathId }, { "stacks", voided.stacks } }));
					}
				}
				return frames;
			}

			std::vector<OutboundFrame> HandleDeathBagDiscover(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds)
			{
				if (!a_session.active || inventoryAuthority == nullptr || !InventoryNegotiated(a_session) || a_session.questProfile.empty() ||
					!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds) || !a_packet.is_object() || a_packet.size() != 1)
					return std::vector<OutboundFrame>{};
				try {
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					if (!profile) return std::vector<OutboundFrame>{};
					const auto bags = inventoryAuthority->Store().DiscoverDeathBags(*profile, a_session.questProfile, allowDeathBagLootByOthers);
					if (!bags) return std::vector<OutboundFrame>{};
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{ { "type", "deathBagDiscovery" }, { "bags", *bags } }) };
				} catch (const std::exception&) { return std::vector<OutboundFrame>{}; }
			}

			std::vector<OutboundFrame> HandleDeathBagWithdraw(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds)
			{
				if (!a_session.active || inventoryAuthority == nullptr || !InventoryNegotiated(a_session) || a_session.questProfile.empty() ||
					!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds) || !a_packet.is_object() ||
					(a_packet.size() != 6 && (a_packet.size() != 7 || !a_packet.contains("receiptProof") || !a_packet.at("receiptProof").is_boolean())) ||
					!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("itemKey") || !a_packet.at("itemKey").is_string() ||
					!a_packet.contains("count") || !a_packet.at("count").is_number_unsigned() || !a_packet.contains("expectedRevision") ||
					!a_packet.at("expectedRevision").is_number_unsigned() || !a_packet.contains("operationId") || !a_packet.at("operationId").is_string())
					return std::vector<OutboundFrame>{};
				const auto& id = a_packet.at("deathId").get_ref<const std::string&>();
				const auto& itemKey = a_packet.at("itemKey").get_ref<const std::string&>();
				const auto& operation = a_packet.at("operationId").get_ref<const std::string&>();
				const auto count = a_packet.at("count").get<std::uint64_t>();
				const auto revision = a_packet.at("expectedRevision").get<std::uint64_t>();
				const bool receiptProof = a_packet.contains("receiptProof") && a_packet.at("receiptProof").get<bool>();
				if (!F4T::DeathIntent::ValidId(id) || !F4T::InventoryContract::ParseItemKey(itemKey) || operation.empty() || count == 0 ||
					count > (std::numeric_limits<std::uint32_t>::max)() || revision > (std::numeric_limits<std::uint32_t>::max)()) return std::vector<OutboundFrame>{};
				try {
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					Json metadata;
					bool fresh = false;
					if (!profile || !inventoryAuthority->Store().WithdrawDeathBag(id, *profile, a_session.questProfile, allowDeathBagLootByOthers, itemKey,
						static_cast<std::uint32_t>(count), static_cast<std::uint32_t>(revision), operation, &metadata, receiptProof ? &fresh : nullptr, receiptProof)) return std::vector<OutboundFrame>{};
					Json response{ { "type", "deathBagHealthGrant" }, { "deathId", id }, { "operationId", operation }, { "itemKey", itemKey },
						{ "count", static_cast<std::uint32_t>(count) }, { "metadata", metadata } };
					if (receiptProof) response["fresh"] = fresh;
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, response) };
				} catch (const std::exception&) { return std::vector<OutboundFrame>{}; }
			}

			std::vector<OutboundFrame> HandleDeathBagSnapshot(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds)
			{
				if (inventoryAuthority == nullptr || !InventoryNegotiated(a_session) || !a_session.active || a_session.questProfile.empty() ||
					!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds) || !a_packet.is_object() || a_packet.size() != 5 ||
					!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("status") ||
					a_packet.at("status") != "countsVerified" || !a_packet.contains("stacks") || !a_packet.at("stacks").is_array() ||
					!a_packet.contains("retained") || !a_packet.at("retained").is_array()) return std::vector<OutboundFrame>{};
				const auto& id = a_packet.at("deathId").get_ref<const std::string&>();
				if (!F4T::DeathIntent::ValidId(id)) return std::vector<OutboundFrame>{};
				try {
					auto& store = inventoryAuthority->Store();
					const auto activeId = store.GetActiveDeathIntentId(a_session.questProfile);
					const auto intent = store.GetActiveDeathIntent(a_session.questProfile);
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					if (!activeId || *activeId != id || !intent || intent->at("state") != "dead" || !profile ||
						intent->at("profileId") != *profile || intent->at("characterId") != a_session.questProfile) return std::vector<OutboundFrame>{};
					const bool committed = store.CreateAndDepositDeathBag(id, *profile, a_session.questProfile, a_packet.at("stacks"), a_packet.at("retained"));
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{
						{ "type", "deathBagDepositResult" }, { "deathId", id }, { "status", committed ? "committed" : "rejected" } }) };
				} catch (const std::exception&) { return std::vector<OutboundFrame>{}; }
			}

			std::vector<OutboundFrame> HandleDeathBagPlaceFailed(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds)
			{
				if (inventoryAuthority == nullptr || !InventoryNegotiated(a_session) || !a_session.active || a_session.questProfile.empty() ||
					!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds)) return std::vector<OutboundFrame>{};
				const auto failed = F4T::DeathBagWire::DecodePlaceFailed(a_packet);
				if (!failed) return std::vector<OutboundFrame>{};
				const auto& id = failed->deathId;
				try {
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					if (!profile) return std::vector<OutboundFrame>{};
					Json stacks;
					const auto outcome = inventoryAuthority->Store().ApplyDeathBagPlaceFailed(id, *profile, a_session.questProfile, failed->restored, failed->remaining, &stacks);
					if (outcome == F4T::DeathBag::PlaceFailedOutcome::Rejected) return std::vector<OutboundFrame>{};
					std::vector<OutboundFrame> frames;
					if (outcome == F4T::DeathBag::PlaceFailedOutcome::Discarded) frames.push_back(ReliableFrame(a_connectionId, Json{ { "type", "deathBagVoided" }, { "deathId", id }, { "stacks", stacks } }));
					if (const auto ack = F4T::DeathBagWire::EncodePlaceFailedAck(id)) frames.push_back(ReliableFrame(a_connectionId, *ack));
					return frames;
				} catch (const std::exception&) { return std::vector<OutboundFrame>{}; }
			}

			std::vector<OutboundFrame> HandleDeathBagPlaced(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds)
			{
				if (inventoryAuthority == nullptr || !InventoryNegotiated(a_session) || !a_session.active || a_session.questProfile.empty() ||
					!F4T::PacketRateLimit::TakeToken(a_session.deathBagPlacements, a_monotonicSeconds) || !a_packet.is_object() || a_packet.size() != 2 ||
					!a_packet.contains("deathId") || !a_packet.at("deathId").is_string()) return std::vector<OutboundFrame>{};
				const auto& id = a_packet.at("deathId").get_ref<const std::string&>();
				if (!F4T::DeathIntent::ValidId(id)) return std::vector<OutboundFrame>{};
				bool placed = false;
				try {
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					if (profile) {
						auto& store = inventoryAuthority->Store();
						placed = store.PlaceDeathBag(id, *profile, a_session.questProfile);
						if (!placed) {
							const auto bag = store.GetDeathBag(id);
							placed = bag && bag->is_object() && bag->value("placed", false) &&
								bag->value("ownerProfileId", std::string{}) == *profile && bag->value("characterId", std::string{}) == a_session.questProfile;
						}
					}
				} catch (const std::exception&) {}
				return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{ { "type", "deathBagPlacedResult" }, { "deathId", id },
					{ "status", placed ? "placed" : "rejected" } }) };
			}

			std::vector<OutboundFrame> HandleDeathBagUnplaced(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds)
			{
				if (inventoryAuthority == nullptr || !InventoryNegotiated(a_session) || !a_session.active || a_session.questProfile.empty() ||
					!F4T::PacketRateLimit::TakeToken(a_session.deathBagPlacements, a_monotonicSeconds) || !a_packet.is_object() || a_packet.size() != 3 ||
					!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("operationId") || !a_packet.at("operationId").is_string()) return std::vector<OutboundFrame>{};
				const auto& id = a_packet.at("deathId").get_ref<const std::string&>();
				const auto& operation = a_packet.at("operationId").get_ref<const std::string&>();
				if (!F4T::DeathIntent::ValidId(id)) return std::vector<OutboundFrame>{};
				try {
					const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
					if (!profile) return std::vector<OutboundFrame>{};
					const auto outcome = inventoryAuthority->Store().ReportDeathBagUnplaced(id, *profile, a_session.questProfile, operation);
					const char* status = outcome == F4T::DeathBag::UnplacedOutcome::Refunded ? "refunded" : outcome == F4T::DeathBag::UnplacedOutcome::Absent ? "absent" : "refused";
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{ { "type", "deathBagUnplacedResult" }, { "deathId", id }, { "status", status } }) };
				} catch (const std::exception&) { return std::vector<OutboundFrame>{}; }
			}
