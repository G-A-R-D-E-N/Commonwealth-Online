		std::vector<std::pair<F4T::Replication::EntityId, F4T::InterestScope::StateScope>> dropScopes;
		std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> deathRefusalLogged;

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
			downedService.SetExpiryHandler([this](const DownedService::Peers& a_peers) -> std::vector<OutboundFrame> {
				std::set<std::string> onlineCharacters;
				for (const auto& peer : a_peers) if (!peer.persistenceProfile.empty()) onlineCharacters.insert(peer.persistenceProfile);
				try { (void)inventoryAuthority->Store().ExpireDueDeathIntents(onlineCharacters); return DeathBagVoidedFrames(a_peers, inventoryAuthority->Store().ExpirePendingDeathBags()); }
				catch (const std::exception&) { std::cerr << "Offline death expiry could not be saved; inventory remains unchanged.\n"; return std::vector<OutboundFrame>{}; }
			});
			downedService.SetDownedHandler([this](std::uint32_t a_playerId, std::uint32_t a_remainingMs) {
				try {
					const auto intent = ObservedDeathIntent(a_playerId);
					if (!intent) return;
					auto& store = inventoryAuthority->Store();
					const auto character = intent->at("characterId").get<std::string>();
					if (store.GetActiveDeathIntentId(character)) return;
					auto pending = *intent;
					pending["state"] = "downed";
					pending["deadlineUnix"] = store.CurrentUnixTime() + static_cast<double>(a_remainingMs) / 1000.0;
					const auto result = store.AllocateDeathIntent(pending);
					if (result.outcome != F4T::DeathIntent::Outcome::Created && result.outcome != F4T::DeathIntent::Outcome::Replayed)
						std::cerr << "Downed intent was not recorded; inventory remains unchanged.\n";
				} catch (const std::exception&) {
					std::cerr << "Downed intent could not be saved; inventory remains unchanged.\n";
				}
			});
			const auto deathRefused = [this](std::uint32_t a_playerId, const char* a_text) {
				const auto now = std::chrono::steady_clock::now();
				const auto [found, inserted] = deathRefusalLogged.try_emplace(a_playerId, now);
				if (!inserted && now - found->second < std::chrono::seconds(60)) return false;
				found->second = now;
				std::cerr << "Player " << a_playerId << ": " << a_text << '\n';
				return false;
			};
			const auto deathCleared = [this](std::uint32_t a_playerId) {
				if (deathRefusalLogged.erase(a_playerId) != 0) std::cerr << "Player " << a_playerId << ": death finalization recovered.\n";
				return true;
			};
			downedService.SetDeathHandler([this, deathRefused, deathCleared](std::uint32_t a_playerId, std::string_view a_character) {
				try {
					auto& store = inventoryAuthority->Store();
					const std::string character{ a_character };
					if (!character.empty()) {
						const auto id = store.GetActiveDeathIntentId(character);
						if (id) {
							const auto active = store.GetActiveDeathIntent(character);
							if (active && active->at("state") == "dead") return deathCleared(a_playerId);
							if (store.MarkDeathIntentDead(character, *id)) return deathCleared(a_playerId);
							return deathRefused(a_playerId, "death intent could not be finalized; death is deferred.");
						}
					}
					const auto intent = ObservedDeathIntent(a_playerId);
					if (!intent) {
						std::cerr << "Death had no observed location; no death intent recorded and inventory remains unchanged.\n";
						return deathCleared(a_playerId);
					}
					const auto result = store.AllocateDeathIntent(*intent);
					if (result.outcome == F4T::DeathIntent::Outcome::Created || result.outcome == F4T::DeathIntent::Outcome::Replayed) return deathCleared(a_playerId);
					return deathRefused(a_playerId, "death intent allocation was refused; death is deferred and will retry.");
				} catch (const std::exception&) {
					return deathRefused(a_playerId, "death intent could not be saved; death is deferred.");
				}
			});
			downedService.SetDeathBagIdHandler([this](std::string_view a_character) -> std::optional<std::string> {
				try {
					auto& store = inventoryAuthority->Store();
					const std::string character{ a_character };
					const auto active = store.GetActiveDeathIntent(character);
					if (!active || active->at("state") != "dead") return std::nullopt;
					return store.GetActiveDeathIntentId(character);
				} catch (const std::exception&) { return std::nullopt; }
			});
			downedService.SetRetirementHandler([this](std::string_view a_profile) {
				try {
					auto& store = inventoryAuthority->Store();
					const std::string profile{ a_profile };
					const auto id = store.GetActiveDeathIntentId(profile);
					if (!id) return true;
					if (store.RetireUntransferredDeathIntent(profile, *id)) return true;
				} catch (const std::exception&) {
				}
				std::cerr << "Death intent could not be retired; revive or respawn is deferred.\n";
				return false;
			});
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

		std::optional<Json> OwnedInventoryRevision(std::uint32_t a_connectionId, const Session& a_session)
		{
			try {
				const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId).value_or(std::string{ a_session.profileId });
				const auto inventory = profile.empty() ? std::nullopt : inventoryAuthority->Store().GetInventory(profile);
				if (inventory && inventory->contains("revision") && inventory->at("revision").is_number()) return inventory->at("revision");
			} catch (const std::exception&) {
			}
			return std::nullopt;
		}

		F4T::Drops::InventoryPosition OwnedInventoryPosition(const Session& a_session, double a_monotonicSeconds) const
		{
			F4T::Drops::InventoryPosition position;
			if (a_session.lastMovement && a_session.lastMovementMonotonic && std::isfinite(a_monotonicSeconds)) {
				const auto age = a_monotonicSeconds - *a_session.lastMovementMonotonic;
				if (age >= 0.0 && age <= 5.0) {
					const auto& movement = *a_session.lastMovement;
					position = { movement.cellId, movement.worldspaceId, static_cast<float>(movement.x), static_cast<float>(movement.y), static_cast<float>(movement.z) };
				}
			}
			return position;
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
			const auto vetoed = [&]() {
				Json result{
					{ "type", a_drop ? "inventoryDropResult" : "inventoryPickupResult" },
					{ "operationId", *operation },
					{ "status", "rejected" },
					{ "reason", "vetoed" }
				};
				if (const auto revision = OwnedInventoryRevision(a_connectionId, a_session)) result["revision"] = *revision;
				return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, result) };
			};
			if (!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds)) return rejected("rateLimited");
			if (!InventoryNegotiated(a_session)) {
				return rejected("inventoryAuthorityUnavailable");
			}
			if (!events.Allow(ServerEvent{ ServerEventKind::InventoryChange, a_connectionId, a_session.playerId, a_session.profileId, &a_packet })) {
				Reject();
				return vetoed();
			}
			const auto position = OwnedInventoryPosition(a_session, a_monotonicSeconds);
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

		std::vector<OutboundFrame> HandleOwnedContainerRequest(std::uint32_t a_connectionId, Session& a_session,
			const Json& a_packet, double a_monotonicSeconds)
		{
			if (!F4T::InventoryWire::NormalizeContainerRequest(a_packet)) {
				Reject();
				return {};
			}
			const auto rejected = [&](const char* a_reason, const std::optional<Json>& a_revision = std::nullopt) {
				if (inventoryAuthority != nullptr) inventoryAuthority->NoteRefusedContainerMove(a_connectionId, a_packet, OwnedInventoryPosition(a_session, a_monotonicSeconds), a_reason);
				Json result{ { "type", "inventoryContainerResult" }, { "operationId", a_packet.at("operationId") }, { "status", "rejected" },
					{ "reason", a_reason }, { "direction", a_packet.at("direction") }, { "itemKey", a_packet.at("itemKey") }, { "count", a_packet.at("count") } };
				if (a_revision) result["revision"] = *a_revision;
				return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, result) };
			};
			if (!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds)) return rejected("rateLimited");
			if (inventoryAuthority == nullptr || !InventoryNegotiated(a_session)) return rejected("inventoryAuthorityUnavailable");
			if (!events.Allow(ServerEvent{ ServerEventKind::InventoryChange, a_connectionId, a_session.playerId, a_session.profileId, &a_packet })) {
				Reject();
				return rejected("vetoed", OwnedInventoryRevision(a_connectionId, a_session));
			}
			std::optional<F4T::Drops::TransactionResult> result;
			try {
				result = inventoryAuthority->ApplyContainerRequest(a_connectionId, a_packet, OwnedInventoryPosition(a_session, a_monotonicSeconds));
			} catch (const std::exception&) {
				return rejected("inventoryPersistenceUnavailable");
			}
			if (!result) return rejected("inventoryAuthorityUnavailable");
			if (result->outcome == F4T::Drops::Outcome::Rejected) inventoryAuthority->NoteRefusedContainerMove(a_connectionId, a_packet, OwnedInventoryPosition(a_session, a_monotonicSeconds), result->packet.value("reason", std::string{}));
			return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, result->packet) };
		}

		%:include "ServerDropProtocol.inc"
		%:include "ServerInventoryDispatch.inc"
		%:include "ServerObjectState.inc"
		%:include "ServerParkedSuits.h"
		%:include "ServerQuest.h"
		%:include "ServerTrade.h"
		%:include "ServerDeathBag.h"
