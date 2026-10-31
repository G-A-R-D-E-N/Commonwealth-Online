#include "ServerProtocolImpl.h"
#include "ServerAdminMenu.h"

namespace CommonwealthOnline::Server
{
	std::vector<OutboundFrame> ProtocolHost::Impl::HandleHello(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto protocolVersion = ReadUInt32(a_packet, "protocolVersion");
		const auto compatibilityCapabilities = NormalizeCompatibilityCapabilities(a_packet);
		if (a_session.active || !protocolVersion || *protocolVersion != kProtocolVersion || !compatibilityCapabilities) {
			Reject();
			return {};
		}
		a_session.compatibilityCapabilities = *compatibilityCapabilities;
		a_session.compatibilityRevisions.clear();
		hasEverActivatedSession = true;
		Json slotReadyBlock = Json::object();
		std::string persistenceProfile;
		std::vector<OutboundFrame> frames;
		if (characterSlots.Enabled()) {
			std::vector<std::uint32_t> replaced;
			bool replacedActive = false;
			for (const auto& [connectionId, other] : sessions) {
				if (connectionId != a_connectionId && !a_session.profileId.empty() && other.profileId == a_session.profileId) {
					replaced.push_back(connectionId);
					replacedActive = replacedActive || other.active;
				}
			}
			for (const auto connectionId : replaced) {
				frames.push_back(ReliableFrame(connectionId, Json{ { "type", "sessionEnded" }, { "code", "session_superseded" }, { "reason", "This session was replaced by a newer connection for the same character account." }, { "serverTime", a_unixTimeSeconds } }));
				characterSlots.ReleaseConnection(connectionId);
				AppendFrames(frames, HandleDisconnected(connectionId, a_unixTimeSeconds));
			}
			if (replacedActive) {
				AppendFrames(frames, FactionDisconnectFrames(a_session.profileId, a_unixTimeSeconds));
			}
			auto slotHello = characterSlots.OnHello(
				a_connectionId, a_session.profileId, a_packet, a_monotonicSeconds, a_unixTimeSeconds,
				[&](std::uint32_t a_other, std::string_view a_account) {
					const auto other = sessions.find(a_other);
					return other != sessions.end() && other->second.profileId == a_account;
				});
			AppendFrames(frames, ForgetErasedProfiles(a_unixTimeSeconds));
			slotReadyBlock = std::move(slotHello.readyBlock);
			persistenceProfile = std::move(slotHello.persistenceProfile);
		}
		a_session.active = true;
		peerVisibility.AddPeer(a_session.playerId);
		peerVisibility.RegisterEntity(a_session.playerId);
		peerSnapshots.insert_or_assign(a_session.playerId, std::array<PeerSnapshotSender, 2>{});
		const bool becameHost = worldStateHostPlayerId == 0;
		if (becameHost) {
			worldStateHostPlayerId = a_session.playerId;
		}
		Json ready{
			{ "type", "sessionReady" },
			{ "profileId", a_session.profileId },
			{ "playerId", a_session.playerId },
			{ "protocolVersion", kProtocolVersion },
			{ "serverProtocolVersion", kProtocolVersion },
			{ "worldStateHostPlayerId", worldStateHostPlayerId },
			{ "consoleAllowed", IsConsoleAdmin(a_session.profileId) },
			{ "serverTime", a_unixTimeSeconds }
		};
		if (!slotReadyBlock.empty()) {
			ready["characterSlots"] = std::move(slotReadyBlock);
		}
		if (!persistenceProfile.empty()) {
			ready["persistenceProfile"] = std::move(persistenceProfile);
		}
		AppendFrames(frames, SendLifecycleState(a_connectionId, a_session, F4T::Session::LifecycleState::Connected, a_unixTimeSeconds));
		frames.push_back(ReliableFrame(a_connectionId, ready));
		if (IsLiveAdminSession(a_session)) frames.push_back(ReliableFrame(a_connectionId, AdminMenuService::MakeState(a_unixTimeSeconds)));
		AppendFrames(frames, SendLifecycleState(a_connectionId, a_session, F4T::Session::LifecycleState::Ready, a_unixTimeSeconds));
		AppendFrames(frames, ExistingWelcomeStateFrames(a_connectionId, a_session, a_unixTimeSeconds));
		AppendFrames(frames, downedService.Join(DownedPeers(), a_connectionId, a_session.playerId, a_session.profileId, a_monotonicSeconds));
		AppendFrames(frames, ExistingPluginListFrames(a_connectionId));
		if (becameHost) {
			AppendFrames(frames, BroadcastReliable(WorldStateHostPacket(a_session.playerId, a_unixTimeSeconds)));
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleTransform(
		std::uint32_t a_connectionId,
		Session& a_session,
		Json a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		auto current = NormalizeTransform(a_packet);
		if (!current) {
			Reject();
			stats.movementRejected++;
			return {};
		}
		std::optional<F4T::NetworkValidation::MovementState> previous;
		if (a_session.lastMovement) {
			previous = F4T::NetworkValidation::MovementState{
				a_session.lastMovement->x,
				a_session.lastMovement->y,
				a_session.lastMovement->z,
				a_session.lastMovement->cellId,
				a_session.lastMovement->worldspaceId,
				a_session.lastMovement->movementType
			};
		}
		const F4T::NetworkValidation::MovementState currentView{
			current->x,
			current->y,
			current->z,
			current->cellId,
			current->worldspaceId,
			current->movementType
		};
		const auto movement = F4T::NetworkValidation::ValidateMovement(
			previous,
			a_session.lastMovementMonotonic,
			currentView,
			a_monotonicSeconds,
			a_session.movementTransitions);
		if (!movement.Accepted()) {
			Reject();
			stats.movementRejected++;
			if (a_session.lastMovement) {
				stats.movementCorrectionsSent++;
				return { PositionCorrectionFrame(
					a_connectionId,
					*a_session.lastMovement,
					MovementRejectReason(movement),
					a_unixTimeSeconds) };
			}
			return {};
		}
		if (movement.verdict == F4T::NetworkValidation::MovementVerdict::AcceptedExplicitTransition) {
			a_session.movementTransitions.Record(a_monotonicSeconds);
		}
		if (movement.verdict == F4T::NetworkValidation::MovementVerdict::AcceptedBoundaryCrossing) {
			a_session.movementTransitions.RecordBoundaryCrossing(a_monotonicSeconds, a_session.lastMovement->cellId, current->cellId);
		}
		const auto previousScope = ScopeFromMovement(a_session.lastMovement);
		a_packet["playerId"] = a_session.playerId;
		a_packet["serverTime"] = a_unixTimeSeconds;
		a_session.lastMovement = std::move(current);
		a_session.lastMovementMonotonic = a_monotonicSeconds;
		a_session.lastTransformPacket = a_packet;
		std::vector<OutboundFrame> frames = ReconcilePeerVisibility(a_session);
		AppendFrames(frames, LifecycleFramesForMovement(a_connectionId, a_session, a_unixTimeSeconds));
		if (inventoryAuthority != nullptr) AppendFrames(frames, RefreshInventoryProjection());

		for (auto& [connectionId, recipient] : sessions) {
			if (connectionId == a_connectionId || !recipient.active ||
				!peerVisibility.IsVisible(recipient.playerId, a_session.playerId)) {
				continue;
			}
			auto frame = SnapshotFrame(connectionId, recipient, Family::Transform, a_packet);
			if (!frame.payload.empty()) {
				frames.push_back(std::move(frame));
			}
		}
		AppendFrames(frames, ReconcileNpcAuthority(a_unixTimeSeconds));
		const auto currentScope = ScopeFromMovement(a_session.lastMovement);
		if (currentScope && currentScope != previousScope) {
			const auto assignment = npcAuthority.Get(*currentScope);
			if (assignment) {
				frames.push_back(ReliableFrame(a_connectionId, AuthorityPacket(*assignment, a_unixTimeSeconds)));
			}
			if (!previousScope) {
				AppendFrames(frames, ExistingTransformFrames(a_connectionId, a_session, a_unixTimeSeconds));
			}
			AppendFrames(frames, ExistingNpcCoopFrames(a_connectionId, a_session, a_unixTimeSeconds));
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandlePlayerState(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		auto normalized = NormalizePlayerState(a_packet);
		if (!normalized) {
			Reject();
			return {};
		}
		RecordCombatActions(a_session, *normalized, a_monotonicSeconds);
		(*normalized)["playerId"] = a_session.playerId;
		(*normalized)["serverTime"] = a_unixTimeSeconds;
		if (normalized->contains("vitals") &&
			!F4T::PacketRateLimit::TakeToken(a_session.vitalsUpdates, a_monotonicSeconds, kVitalsUpdatesRefillPerSecond, kVitalsUpdatesBurst)) {
			normalized->erase("vitals");
		}
		const bool hasVitals = normalized->contains("vitals");
		const bool hasDurableState = normalized->contains("equippedItems") || normalized->contains("appearance") || normalized->contains("characterName") || normalized->contains("powerArmor");
		if (hasDurableState || hasVitals) {
			Json cached = a_session.lastDurablePlayerState.value_or(Json::object());
			cached["type"] = "playerState";
			cached["playerId"] = a_session.playerId;
			cached["serverTime"] = a_unixTimeSeconds;
			for (const char* field : { "equippedItems", "appearance", "characterName", "powerArmor" }) {
				if (normalized->contains(field)) {
					cached[field] = (*normalized)[field];
				}
			}
			if (const auto vitals = normalized->find("vitals"); vitals != normalized->end()) {
				cached["vitals"] = *vitals;
			}
			cached.erase("actionEvents");
			a_session.lastDurablePlayerState = std::move(cached);
		}
		std::vector<OutboundFrame> frames;
		for (const auto& [connectionId, recipient] : sessions) {
			if (connectionId == a_connectionId || !recipient.active) {
				continue;
			}
			Json relay = *normalized;
			if (relay.contains("actionEvents") && !peerVisibility.IsVisible(recipient.playerId, a_session.playerId)) {
				relay.erase("actionEvents");
			}
			if (const auto relayedName = relay.find("characterName"); relayedName != relay.end() && relayedName->is_string()) {
				const auto view = socialGraph.NameViewFor(recipient.profileId, a_session.profileId, relayedName->get<std::string>(), factionService.SameFaction(recipient.profileId, a_session.profileId));
				relay["displayName"] = view.displayName;
				if (!view.disclosedName) {
					// Constraint: legacy clients get display text in characterName instead of no characterName.
					if (!SessionHasSocialNames(recipient)) relay["characterName"] = view.displayName;
					else relay.erase("characterName");
				}
			}
			const bool relayHasDurableState = relay.contains("equippedItems") || relay.contains("appearance") || relay.contains("characterName") || relay.contains("displayName") || relay.contains("powerArmor");
			if (!relay.contains("actionEvents") && !relayHasDurableState && !relay.contains("vitals")) {
				continue;
			}
			frames.push_back(ReliableFrame(connectionId, relay));
		}
		if (F4T::DownedValidation::CarriesVitals(*normalized)) {
			const auto vitals = F4T::DownedValidation::ReadVitals(*normalized);
			AppendFrames(frames, downedService.ObserveVitals([this]() { return DownedPeers(); }, a_session.playerId, vitals.isDowned, vitals.isDead, a_monotonicSeconds, vitals.hasStateFlags));
		}
		if (hasVitals && factionService.Enabled()) {
			AppendFrames(frames, factionService.VitalsFrames(FactionPeers(), a_session.profileId, a_monotonicSeconds, a_unixTimeSeconds));
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::HandleWorldState(
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_unixTimeSeconds)
	{
		if (worldStateHostPlayerId != a_session.playerId) {
			Reject();
			stats.worldStateRejected++;
			return {};
		}
		auto normalized = NormalizeWorldState(a_packet);
		if (!normalized) {
			Reject();
			stats.worldStateRejected++;
			return {};
		}
		(*normalized)["playerId"] = a_session.playerId;
		(*normalized)["serverTime"] = a_unixTimeSeconds;
		stats.worldStatePacketsReceived++;
		std::vector<OutboundFrame> frames;
		for (const auto& [connectionId, recipient] : sessions) {
			if (connectionId != a_connectionId && recipient.active) {
				frames.push_back(ReliableFrame(connectionId, *normalized));
			}
		}
		return frames;
	}
}
