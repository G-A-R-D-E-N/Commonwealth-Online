#include "ServerProtocolImpl.h"
#include "ServerFurnitureRelay.h"
#include "F4TSleepBedIdentity.h"

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
		Json packet = a_packet;
		if (a_family == Family::Transform) {
			packet["visibilitySequence"] = ++a_recipient.outboundVisibilitySequence;
			packet.erase("serverDowned");
			const auto playerId = packet.find("playerId");
			if (playerId != packet.end() && playerId->is_number_unsigned() &&
				playerId->get<std::uint64_t>() <= (std::numeric_limits<std::uint32_t>::max)()) {
				if (const auto state = downedService.AuthoritativeState(playerId->get<std::uint32_t>())) {
					packet["serverDowned"] = *state;
				}
			}
		}
		const auto json = packet.dump();
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

	std::vector<OutboundFrame> ProtocolHost::Impl::ReconcilePeerVisibility(std::uint32_t a_connectionId, Session& a_self, double a_unixTimeSeconds, bool a_sendTransformOnShow)
	{
		const auto selfScope = ScopeKeys::InterestScopeOf(a_self.lastMovement);
		const auto hiddenFrame = [&](std::uint32_t a_recipientConnectionId, Session& a_recipient, PeerId a_hiddenPlayerId, std::uint64_t a_incarnation) {
			return ReliableFrame(a_recipientConnectionId, Json{
				{ "type", "peerHidden" },
				{ "playerId", a_hiddenPlayerId },
				{ "playerIncarnation", a_incarnation },
				{ "visibilitySequence", ++a_recipient.outboundVisibilitySequence },
				{ "serverTime", a_unixTimeSeconds }
			});
		};
		std::set<PeerId> compatibilityAffected;
		std::vector<OutboundFrame> frames;
		for (auto& [connectionId, other] : sessions) {
			if (other.playerId == a_self.playerId || !other.active) {
				continue;
			}
			const bool desired = a_self.lastMovement && other.lastMovement &&
				F4T::InterestScope::StatesShareInterest(selfScope, ScopeKeys::InterestScopeOf(other.lastMovement));
			if (desired) {
				if (!peerVisibility.IsVisible(a_self.playerId, other.playerId) && peerVisibility.Show(a_self.playerId, other.playerId)) {
					compatibilityAffected.insert(a_self.playerId);
					if (a_sendTransformOnShow && other.lastTransformPacket) {
						Json packet = *other.lastTransformPacket;
						packet["serverTime"] = a_unixTimeSeconds;
						auto frame = SnapshotFrame(a_connectionId, a_self, Family::Transform, packet);
						if (!frame.payload.empty()) {
							frames.push_back(std::move(frame));
						}
					}
					if (auto sleep = FurnitureRelay::SleepSnapshot(a_connectionId, a_self, other)) frames.push_back(std::move(*sleep));
					if (auto seat = FurnitureRelay::SeatSnapshot(a_connectionId, a_self, other)) frames.push_back(std::move(*seat));
				}
				if (!peerVisibility.IsVisible(other.playerId, a_self.playerId) && peerVisibility.Show(other.playerId, a_self.playerId)) {
					compatibilityAffected.insert(other.playerId);
					if (auto sleep = FurnitureRelay::SleepSnapshot(connectionId, other, a_self)) frames.push_back(std::move(*sleep));
					if (auto seat = FurnitureRelay::SeatSnapshot(connectionId, other, a_self)) frames.push_back(std::move(*seat));
				}
			} else {
				if (peerVisibility.IsVisible(a_self.playerId, other.playerId) && peerVisibility.Hide(a_self.playerId, other.playerId)) {
					PruneCompatibilityRevisions(a_self.playerId, other.playerId);
					compatibilityAffected.insert(a_self.playerId);
					frames.push_back(hiddenFrame(a_connectionId, a_self, other.playerId, other.playerIncarnation));
					if (a_self.compatibilityCapabilities.Has(F4T::SleepBedIdentity::kCapabilityName)) {
						auto packet = F4T::SleepBedIdentity::MakeSleepExit();
						packet["playerId"] = other.playerId;
						frames.push_back(ReliableFrame(a_connectionId, packet));
					}
					if (auto seat = FurnitureRelay::SeatClear(a_connectionId, a_self, other.playerId)) frames.push_back(std::move(*seat));
				}
				if (peerVisibility.IsVisible(other.playerId, a_self.playerId) && peerVisibility.Hide(other.playerId, a_self.playerId)) {
					PruneCompatibilityRevisions(other.playerId, a_self.playerId);
					compatibilityAffected.insert(other.playerId);
					frames.push_back(hiddenFrame(connectionId, other, a_self.playerId, a_self.playerIncarnation));
					if (other.compatibilityCapabilities.Has(F4T::SleepBedIdentity::kCapabilityName)) {
						auto packet = F4T::SleepBedIdentity::MakeSleepExit();
						packet["playerId"] = a_self.playerId;
						frames.push_back(ReliableFrame(connectionId, packet));
					}
					if (auto seat = FurnitureRelay::SeatClear(connectionId, other, a_self.playerId)) frames.push_back(std::move(*seat));
				}
			}
		}
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
			if (connectionId == a_connectionId || !peer.active || peer.visibilityPaused || peer.resumeAwaitingTransform || !peer.lastDurablePlayerState || peer.playerId == a_target.playerId) {
				continue;
			}
			Json packet = *peer.lastDurablePlayerState;
			packet.erase("progression");
			MaskRelayedName(packet, a_target, peer);
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
		const auto targetScope = ScopeKeys::InterestScopeOf(a_target.lastMovement);
		if (!targetScope) {
			return {};
		}
		std::vector<OutboundFrame> frames;
		for (const auto& [scope, cached] : lastNpcStateByScope) {
			if (ReadUInt32(cached, "playerId").value_or(0) == a_target.playerId) {
				continue;
			}
			Json packet = cached;
			Json filtered = Json::array();
			for (const auto& npc : cached.at("npcs")) {
				if (F4T::InterestScope::StatesShareInterest(InterestScopeFromNpc(npc, scope), targetScope)) {
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
		std::map<std::uint32_t, F4T::NpcAuthority::Scope> holderWires;
		players.reserve(sessions.size());
		for (const auto& [connectionId, session] : sessions) {
			(void)connectionId;
			if (!session.active) {
				continue;
			}
			const auto scope = ScopeFromMovement(session.lastMovement);
			if (scope) {
				players.push_back(F4T::NpcAuthority::PlayerScope{ session.playerId, *scope });
				holderWires[session.playerId] = F4T::NpcAuthority::Scope{ session.lastMovement->cellId, session.lastMovement->worldspaceId };
			}
		}
		const auto changes = npcAuthority.Reconcile(players);
		std::vector<OutboundFrame> frames;
		for (const auto& change : changes) {
			lastNpcStateByScope.erase(change.scope);
			lastNpcStateMonotonicByScope.erase(change.scope);
			npcLedger.ForgetScope(change.scope);
			if (change.playerId == 0) {
				AppendFrames(frames, BroadcastReliable(AuthorityPacket(change, a_unixTimeSeconds, ScopeKeys::WireOf(npcAuthorityWire, change.scope))));
				npcAuthorityWire.erase(change.scope);
				continue;
			}
			const auto holder = holderWires.find(change.playerId);
			const auto wire = holder == holderWires.end() ? change.scope : holder->second;
			const auto announced = npcAuthorityWire.find(change.scope);
			if (announced != npcAuthorityWire.end() && announced->second != wire) {
				AppendFrames(frames, BroadcastReliable(AuthorityPacket(
					F4T::NpcAuthority::Change{ change.scope, change.previousPlayerId, 0, change.epoch }, a_unixTimeSeconds, announced->second)));
			}
			npcAuthorityWire[change.scope] = wire;
			AppendFrames(frames, BroadcastReliable(AuthorityPacket(change, a_unixTimeSeconds, wire)));
		}
		return frames;
	}

	void ProtocolHost::Impl::NoteNpcAuthorityDeliveryFailure(const OutboundFrame& a_frame)
	{
		const auto packet = Json::parse(a_frame.payload.begin(), a_frame.payload.end(), nullptr, false);
		if (!packet.is_object() || ReadString(packet, "type").value_or("") != "npcAuthority") {
			return;
		}
		const auto session = sessions.find(a_frame.connectionId);
		const auto cellId = ReadString(packet, "authorityCellId");
		const auto epoch = ReadUInt32(packet, "authorityEpoch");
		if (session == sessions.end() || !cellId || !epoch || *epoch == 0) {
			return;
		}
		const F4T::NpcAuthority::Scope scope{ *cellId, ReadString(packet, "authorityWorldspaceId").value_or("") };
		session->second.npcAuthorityRetries[scope] = *epoch;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::RetryNpcAuthority(double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (auto& [connectionId, session] : sessions) {
			if (session.npcAuthorityRetries.empty()) {
				continue;
			}
			if (session.active) {
				for (const auto& [scope, epoch] : session.npcAuthorityRetries) {
					const auto key = ScopeKeys::KeyOfWire(npcAuthorityWire, scope);
					const auto current = key ? npcAuthority.Get(*key) : std::nullopt;
					frames.push_back(ReliableFrame(connectionId, current ?
						AuthorityPacket(*current, a_unixTimeSeconds, ScopeKeys::WireOf(npcAuthorityWire, current->scope)) :
						AuthorityPacket(F4T::NpcAuthority::Change{ scope, 0, 0, epoch }, a_unixTimeSeconds, scope)));
				}
			}
			session.npcAuthorityRetries.clear();
		}
		return frames;
	}
}
