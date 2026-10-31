#include "ServerProtocolImpl.h"

namespace CommonwealthOnline::Server
{
	OutboundFrame ProtocolHost::Impl::PositionCorrectionFrame(
		std::uint32_t a_connectionId,
		const StoredMovementState& a_previous,
		std::string_view a_reason,
		double a_unixTimeSeconds) const
	{
		const auto reason = a_reason.substr(0, 160);
		return ReliableFrame(a_connectionId, Json{
			{ "type", "positionCorrection" },
			{ "reason", std::string{ reason } },
			{ "x", a_previous.x },
			{ "y", a_previous.y },
			{ "z", a_previous.z },
			{ "angleZ", a_previous.angleZ },
			{ "cellId", a_previous.cellId },
			{ "worldspaceId", a_previous.worldspaceId },
			{ "serverTime", a_unixTimeSeconds }
		});
	}

	OutboundFrame ProtocolHost::Impl::SnapshotFrame(std::uint32_t a_connectionId, Session& a_recipient, Family a_family, const Json& a_packet)
	{
		const auto json = a_packet.dump();
		const auto index = FamilyIndex(a_family);
		const auto sequence = F4T::GnsSnapshotEnvelope::NextSequence(a_recipient.outboundSnapshotSequences[index]);
		a_recipient.outboundSnapshotSequences[index] = sequence;
		OutboundFrame frame;
		frame.connectionId = a_connectionId;
		frame.delivery = Delivery::UnreliableSequenced;
		frame.payload.resize(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		if (!F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ frame.payload }, a_family, sequence)) {
			frame.payload.clear();
			return frame;
		}
		std::copy(json.begin(), json.end(), frame.payload.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return frame;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::BroadcastReliable(const Json& a_packet)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& [connectionId, recipient] : sessions) {
			if (recipient.active) {
				frames.push_back(ReliableFrame(connectionId, a_packet));
			}
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ReconcilePeerVisibility(const Session& a_self)
	{
		const auto selfScope = InterestScopeFromMovement(a_self.lastMovement);
		std::set<PeerId> compatibilityAffected;
		for (const auto& [connectionId, other] : sessions) {
			(void)connectionId;
			if (other.playerId == a_self.playerId || !other.active) {
				continue;
			}
			const bool desired = a_self.lastMovement && other.lastMovement &&
				F4T::InterestScope::StatesShareInterest(selfScope, InterestScopeFromMovement(other.lastMovement));
			if (desired) {
				if (!peerVisibility.IsVisible(a_self.playerId, other.playerId) && peerVisibility.Show(a_self.playerId, other.playerId)) {
					compatibilityAffected.insert(a_self.playerId);
				}
				if (!peerVisibility.IsVisible(other.playerId, a_self.playerId) && peerVisibility.Show(other.playerId, a_self.playerId)) {
					compatibilityAffected.insert(other.playerId);
				}
			} else {
				if (peerVisibility.IsVisible(a_self.playerId, other.playerId) && peerVisibility.Hide(a_self.playerId, other.playerId)) {
					PruneCompatibilityRevisions(a_self.playerId, other.playerId);
					compatibilityAffected.insert(a_self.playerId);
				}
				if (peerVisibility.IsVisible(other.playerId, a_self.playerId) && peerVisibility.Hide(other.playerId, a_self.playerId)) {
					PruneCompatibilityRevisions(other.playerId, a_self.playerId);
					compatibilityAffected.insert(other.playerId);
				}
			}
		}
		std::vector<OutboundFrame> frames;
		if (compatibilityMode) {
			for (const auto peerId : compatibilityAffected) {
				AppendFrames(frames, ReconcileCompatibilityPeer(peerId));
			}
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ExistingTransformFrames(
		std::uint32_t a_connectionId,
		Session& a_target,
		double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& [connectionId, peer] : sessions) {
			if (connectionId == a_connectionId || !peer.active || !peer.lastTransformPacket) {
				continue;
			}
			if (!peerVisibility.IsVisible(a_target.playerId, peer.playerId)) {
				continue;
			}
			Json packet = *peer.lastTransformPacket;
			packet["serverTime"] = a_unixTimeSeconds;
			auto frame = SnapshotFrame(a_connectionId, a_target, Family::Transform, packet);
			if (!frame.payload.empty()) {
				frames.push_back(std::move(frame));
			}
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ExistingPlayerStateFrames(
		std::uint32_t a_connectionId,
		const Session& a_target,
		double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames = ExistingFactionFrames(a_connectionId, a_target, a_unixTimeSeconds);
		AppendFrames(frames, ExistingFriendFrames(a_connectionId, a_target, a_unixTimeSeconds));
		for (const auto& [connectionId, peer] : sessions) {
			if (connectionId == a_connectionId || !peer.active || !peer.lastDurablePlayerState || peer.playerId == a_target.playerId) {
				continue;
			}
			Json packet = *peer.lastDurablePlayerState;
			if (const auto cachedName = packet.find("characterName"); cachedName != packet.end() && cachedName->is_string()) {
				const auto view = socialGraph.NameViewFor(a_target.profileId, peer.profileId, cachedName->get<std::string>(), factionService.SameFaction(a_target.profileId, peer.profileId));
				packet["displayName"] = view.displayName;
				if (!view.disclosedName) {
					// Constraint: legacy clients get display text in characterName instead of no characterName.
					if (!SessionHasSocialNames(a_target)) packet["characterName"] = view.displayName;
					else packet.erase("characterName");
				}
			}
			packet["serverTime"] = a_unixTimeSeconds;
			frames.push_back(ReliableFrame(a_connectionId, packet));
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ExistingNpcStateFrames(
		std::uint32_t a_connectionId,
		Session& a_target,
		double a_unixTimeSeconds)
	{
		const auto targetScope = InterestScopeFromMovement(a_target.lastMovement);
		if (!targetScope) {
			return {};
		}
		std::vector<OutboundFrame> frames;
		for (const auto& [scope, cached] : lastNpcStateByScope) {
			(void)scope;
			if (ReadUInt32(cached, "playerId").value_or(0) == a_target.playerId) {
				continue;
			}
			Json packet = cached;
			Json filtered = Json::array();
			for (const auto& npc : cached.at("npcs")) {
				if (F4T::InterestScope::StatesShareInterest(InterestScopeFromNpc(npc), targetScope)) {
					filtered.push_back(npc);
				}
			}
			if (filtered.empty()) {
				continue;
			}
			packet["npcs"] = std::move(filtered);
			packet["serverTime"] = a_unixTimeSeconds;
			auto frame = SnapshotFrame(a_connectionId, a_target, Family::NpcState, packet);
			if (!frame.payload.empty()) {
				frames.push_back(std::move(frame));
			}
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ReconcileNpcAuthority(double a_unixTimeSeconds)
	{
		std::vector<F4T::NpcAuthority::PlayerScope> players;
		players.reserve(sessions.size());
		for (const auto& [connectionId, session] : sessions) {
			(void)connectionId;
			if (!session.active) {
				continue;
			}
			const auto scope = ScopeFromMovement(session.lastMovement);
			if (scope) {
				players.push_back(F4T::NpcAuthority::PlayerScope{ session.playerId, *scope });
			}
		}
		const auto changes = npcAuthority.Reconcile(players);
		std::vector<OutboundFrame> frames;
		for (const auto& change : changes) {
			lastNpcStateByScope.erase(change.scope);
			lastNpcStateMonotonicByScope.erase(change.scope);
			AppendFrames(frames, BroadcastReliable(AuthorityPacket(change, a_unixTimeSeconds)));
		}
		return frames;
	}
}
