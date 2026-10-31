		std::vector<std::pair<F4T::Replication::EntityId, F4T::InterestScope::StateScope>> dropScopes;

		bool InventoryNegotiated(const Session& a_session) const
		{
			return a_session.compatibilityCapabilities.Has("inventory-stable-v1") &&
				a_session.compatibilityCapabilities.Has("component-snapshot-v1") &&
				a_session.compatibilityCapabilities.Has("component-snapshot-channels-v1");
		}

		bool ConfigureInventoryAuthority(PersistenceGate& a_persistence)
		{
			if (inventoryAuthority != nullptr || componentDefinitions || compatibilityMode || !sessions.empty() || hasEverActivatedSession) return false;
			const auto snapshot = a_persistence.GetDropSnapshot();
			if (!snapshot) return false;
			F4T::Replication::ComponentRegistry registry;
			if (!registry.RegisterDefinition(Family::StableDroppedItem, F4T::Replication::AuthorityClass::Server, F4T::Replication::Channel::Reliable) ||
				!SetComponentDefinitions(*registry.Freeze())) return false;
			inventoryAuthority = &a_persistence;
			return true;
		}

		std::vector<OutboundFrame> RefreshInventoryProjection()
		{
			try {
				const auto snapshot = inventoryAuthority->GetDropSnapshot();
				if (!snapshot) return {};
				const bool dropsChanged = *snapshot != dropState;
				if (dropsChanged) {
					std::vector<std::pair<F4T::Replication::EntityId, F4T::InterestScope::StateScope>> scopes;
					for (const auto& [key, bytes] : *snapshot) {
						const auto object = F4T::Drops::DecodeStableDroppedObject(bytes);
						if (!object) return {};
						scopes.push_back({ key.entityId, F4T::InterestScope::StateScope{ object->position.cellId, object->position.worldspaceId, object->position.x, object->position.y } });
					}
					for (const auto& [key, bytes] : dropState) {
						(void)bytes;
						if (!snapshot->contains(key)) peerVisibility.DespawnEntity(key.entityId);
					}
					dropScopes = std::move(scopes);
					dropState = *snapshot;
				}
				bool visibilityChanged = false;
				DirectComponentPublicationContract contract;
				for (const auto& [connectionId, session] : sessions) {
					if (!session.active || !InventoryNegotiated(session) || !inventoryAuthority->ProfileForConnection(connectionId)) continue;
					contract.peers.push_back({ session.playerId, true, true, { Family::StableDroppedItem } });
					const auto movement = InterestScopeFromMovement(session.lastMovement);
					for (const auto& [entityId, scope] : dropScopes) {
						peerVisibility.RegisterEntity(entityId);
						const bool visible = F4T::InterestScope::StatesShareInterest(movement, scope);
						if (visible != peerVisibility.IsVisible(session.playerId, entityId)) visibilityChanged = true;
						if (visible) peerVisibility.Show(session.playerId, entityId);
						else peerVisibility.Hide(session.playerId, entityId);
					}
				}
				if (!dropsChanged && !visibilityChanged && directPublicationContract == contract) return RetryFailedComponentFrames();
				directPublicationContract = std::move(contract);
				++stats.inventoryProjections;
				return PublishComponentSnapshot(dropState);
			} catch (const std::exception& error) {
				std::cerr << "Inventory projection unavailable: " << error.what() << '\n';
				directPublicationContract.reset();
				return {};
			}
		}

		std::vector<OutboundFrame> HandleOwnedInventoryRequest(std::uint32_t a_connectionId, Session& a_session,
			const Json& a_packet, double a_monotonicSeconds, bool a_drop)
		{
			const auto operation = ReadString(a_packet, "operationId");
			if (!operation || operation->empty() || operation->size() > F4T::Drops::kMaximumOperationIdChars) {
				Reject();
				return {};
			}
			const auto rejected = [&](const char* a_reason) {
				return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, Json{ { "type", a_drop ? "inventoryDropResult" : "inventoryPickupResult" },
					{ "operationId", *operation }, { "status", "rejected" }, { "reason", a_reason } }) };
			};
			if (!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds)) return rejected("rateLimited");
			if (!InventoryNegotiated(a_session)) return rejected("inventoryAuthorityUnavailable");
			F4T::Drops::InventoryPosition position;
			if (a_session.lastMovement && a_session.lastMovementMonotonic && std::isfinite(a_monotonicSeconds)) {
				const auto age = a_monotonicSeconds - *a_session.lastMovementMonotonic;
				if (age >= 0.0 && age <= 5.0) {
					const auto& movement = *a_session.lastMovement;
					position = { movement.cellId, movement.worldspaceId, static_cast<float>(movement.x), static_cast<float>(movement.y), static_cast<float>(movement.z) };
				}
			}
			std::optional<F4T::Drops::TransactionResult> result;
			try {
				if (!inventoryAuthority->GetDropSnapshot()) return rejected("inventoryAuthorityUnavailable");
				result = inventoryAuthority->ApplyInventoryRequest(a_connectionId, a_packet, position, a_drop);
			} catch (const std::exception&) {
				return rejected("inventoryPersistenceUnavailable");
			}
			if (!result) return rejected("inventoryAuthorityUnavailable");
			std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, result->packet) };
			if (result->outcome != F4T::Drops::Outcome::Rejected) AppendFrames(frames, RefreshInventoryProjection());
			return frames;
		}

		%:include "ServerDropProtocol.inc"
		%:include "ServerInventoryDispatch.inc"
		%:include "ServerObjectState.inc"
		%:include "ServerQuest.h"
