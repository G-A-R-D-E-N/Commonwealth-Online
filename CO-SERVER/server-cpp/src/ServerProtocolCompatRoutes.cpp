#include "ServerProtocolImpl.h"

namespace CommonwealthOnline::Server
{
	const DirectComponentPublicationPeer* ProtocolHost::Impl::FindDirectPublicationPeer(PeerId a_peerId) const
	{
		if (!directPublicationContract.has_value()) {
			return nullptr;
		}
		for (const auto& peer : directPublicationContract->peers) {
			if (peer.peerId == a_peerId) {
				return std::addressof(peer);
			}
		}
		return nullptr;
	}

	bool ProtocolHost::Impl::FamiliesAllowed(const SnapshotState& a_state, const DirectComponentPublicationPeer& a_peer) const
	{
		return std::all_of(a_state.begin(), a_state.end(), [&a_peer](const auto& entry) {
			return a_peer.supportedFamilies.contains(entry.first.family);
		});
	}

	bool ProtocolHost::Impl::PeerUsesComponentFamily(const Session& a_session, Family a_family) const
	{
		const auto* adapter = compatibilityRegistry.FindByComponentFamily(a_family);
		if (adapter == nullptr || !adapter->Supports(Compatibility::ConversionDirection::LegacyToComponent)) {
			return false;
		}
		const auto required = compatibilityRegistry.RequiredComponentCapability(a_family);
		const auto channel = componentDefinitions->Find(a_family);
		return channel.has_value() &&
			(*channel != F4T::Replication::Channel::Reliable || a_session.compatibilityCapabilities.Has("component-snapshot-channels-v1")) &&
			!required.empty() &&
			a_session.compatibilityCapabilities.Has("component-snapshot-v1") &&
			a_session.compatibilityCapabilities.Has(required);
	}

	ProtocolHost::Impl::CompatibilityView ProtocolHost::Impl::BuildCompatibilityView(PeerId a_peerId, const Session& a_session, F4T::Replication::Channel a_channel)
	{
		CompatibilityView view;
		const auto senderIt = peerSnapshots.find(a_peerId);
		const bool senderHasState = senderIt != peerSnapshots.end() &&
			(senderIt->second[static_cast<std::size_t>(a_channel)].hasBaseline || senderIt->second[static_cast<std::size_t>(a_channel)].pendingRetry);
		bool hasComponentSurface = false;
		for (const auto& route : compatibilityRoutes) {
			if (route.peerId != a_peerId || !peerVisibility.IsVisible(a_peerId, route.component.entityId) ||
				!PeerUsesComponentFamily(a_session, route.component.family)) {
				continue;
			}
			const auto familyChannel = componentDefinitions->Find(route.component.family);
			if (!familyChannel.has_value() || *familyChannel != a_channel) {
				continue;
			}
			hasComponentSurface = true;
			const auto projection = compatibilityProjections.find(route.component);
			if (projection == compatibilityProjections.end() || !projection->second.hasBytes || projection->second.dirty) {
				view.status = CompatibilityViewStatus::Incomplete;
				view.state.clear();
				return view;
			}
			view.state[route.component] = projection->second.bytes;
		}
		if (!hasComponentSurface && !senderHasState) {
			view.status = CompatibilityViewStatus::NoSurface;
			return view;
		}
		view.status = CompatibilityViewStatus::Complete;
		return view;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ReconcileCompatibilityPeer(PeerId a_peerId)
	{
		if (!compatibilityMode) {
			return {};
		}
		auto peer = FindActiveSession(a_peerId);
		if (!peer) {
			return {};
		}
		std::vector<OutboundFrame> frames;
		for (const auto channel : { F4T::Replication::Channel::Reliable, F4T::Replication::Channel::Unreliable }) {
			const auto view = BuildCompatibilityView(a_peerId, *peer->second, channel);
			if (view.status != CompatibilityViewStatus::Complete) {
				continue;
			}
			const auto sent = PublishSelectedSnapshot(peer->first, a_peerId, view.state, channel);
			if (sent.status == SnapshotSendStatus::Emitted && sent.frame) {
				frames.push_back(*sent.frame);
			}
		}
		return frames;
	}

	ProtocolHost::Impl::RouteUpdateResult ProtocolHost::Impl::UpdateCompatibilityRecipients(
		EntityId a_entityId,
		Family a_family,
		std::span<const std::uint32_t> a_exactRecipients,
		bool a_reconcileAdded)
	{
		RouteUpdateResult result;
		if (!compatibilityMode || a_entityId == 0 || compatibilityRegistry.FindByComponentFamily(a_family) == nullptr) {
			return result;
		}
		std::set<PeerId> requested;
		for (const auto peerId : a_exactRecipients) {
			if (!requested.insert(peerId).second) {
				return result;
			}
		}
		std::set<PeerId> next;
		for (const auto peerId : requested) {
			if (FindActiveSession(peerId)) {
				next.insert(peerId);
			}
		}
		const ComponentKey component{ a_entityId, a_family };
		std::set<PeerId> current;
		for (const auto& route : compatibilityRoutes) {
			if (route.component == component) {
				current.insert(route.peerId);
			}
		}
		for (const auto peerId : current) {
			if (!next.contains(peerId)) {
				result.removed.insert(peerId);
			}
		}
		for (const auto peerId : next) {
			if (!current.contains(peerId)) {
				result.added.insert(peerId);
			}
		}
		for (const auto peerId : result.removed) {
			compatibilityRoutes.erase(CompatibilityRouteKey{ peerId, component });
			if (auto peer = FindActiveSession(peerId)) {
				peer->second->compatibilityRevisions.erase(component);
			}
		}
		for (const auto peerId : result.added) {
			compatibilityRoutes.insert(CompatibilityRouteKey{ peerId, component });
		}
		result.accepted = true;
		for (const auto peerId : result.removed) {
			AppendFrames(result.frames, ReconcileCompatibilityPeer(peerId));
		}
		if (a_reconcileAdded) {
			for (const auto peerId : result.added) {
				AppendFrames(result.frames, ReconcileCompatibilityPeer(peerId));
			}
		}
		return result;
	}

	void ProtocolHost::Impl::RemoveCompatibilityPeerRoutes(PeerId a_peerId)
	{
		for (auto iterator = compatibilityRoutes.begin(); iterator != compatibilityRoutes.end();) {
			if (iterator->peerId == a_peerId) {
				iterator = compatibilityRoutes.erase(iterator);
			} else {
				++iterator;
			}
		}
	}

	void ProtocolHost::Impl::PruneCompatibilityRevisions(PeerId a_peerId, EntityId a_entityId)
	{
		auto peer = FindActiveSession(a_peerId);
		if (!peer) {
			return;
		}
		for (auto iterator = peer->second->compatibilityRevisions.begin(); iterator != peer->second->compatibilityRevisions.end();) {
			if (iterator->first.entityId == a_entityId) {
				iterator = peer->second->compatibilityRevisions.erase(iterator);
			} else {
				++iterator;
			}
		}
	}
}
