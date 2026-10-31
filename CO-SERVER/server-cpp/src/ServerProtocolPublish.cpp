#include "ServerProtocolImpl.h"

namespace CommonwealthOnline::Server
{
	std::vector<OutboundFrame> ProtocolHost::Impl::PublishComponentSnapshot(const SnapshotState& a_state)
	{
		return PublishAuthoritativeSnapshot(a_state);
	}

	bool ProtocolHost::Impl::RegisterCompatibilityAdapter(Compatibility::FamilyAdapter a_adapter)
	{
		if (inventoryAuthority != nullptr || hasEverActivatedSession || !componentDefinitions.has_value() ||
			!componentDefinitions->Contains(a_adapter.metadata.component.family) ||
			!compatibilityRegistry.Register(std::move(a_adapter))) {
			return false;
		}
		compatibilityMode = true;
		return true;
	}

	bool ProtocolHost::Impl::SetComponentDefinitions(F4T::Replication::FrozenComponentDefinitionTable a_definitions)
	{
		if (inventoryAuthority != nullptr || hasEverActivatedSession || !sessions.empty() || !a_definitions.IsValid()) {
			return false;
		}
		componentDefinitions = std::move(a_definitions);
		return true;
	}

	bool ProtocolHost::Impl::SetDirectComponentPublicationContract(DirectComponentPublicationContract a_contract)
	{
		if (inventoryAuthority != nullptr || compatibilityMode || !componentDefinitions.has_value()) {
			return false;
		}
		std::set<PeerId> peerIds;
		for (const auto& peer : a_contract.peers) {
			if (peer.peerId == 0 || !peerIds.insert(peer.peerId).second ||
				std::any_of(peer.supportedFamilies.begin(), peer.supportedFamilies.end(), [this](Family a_family) {
					return !componentDefinitions->Contains(a_family);
				})) {
				return false;
			}
		}
		directPublicationContract = std::move(a_contract);
		return true;
	}

	CompatibilityRouteResult ProtocolHost::Impl::SetCompatibilityRecipients(
		EntityId a_entityId,
		Family a_family,
		std::span<const std::uint32_t> a_exactRecipients)
	{
		auto update = UpdateCompatibilityRecipients(a_entityId, a_family, a_exactRecipients, true);
		return CompatibilityRouteResult{ update.accepted, std::move(update.frames) };
	}

	CompatibilityPublishResult ProtocolHost::Impl::PublishCompatible(const CompatiblePublication& a_publication)
	{
		CompatibilityPublishResult result;
		if (!compatibilityMode || a_publication.entityId == 0 || a_publication.revision == 0 || a_publication.legacyFamily.empty()) {
			return result;
		}
		const auto* adapter = compatibilityRegistry.FindByLegacyFamily(a_publication.legacyFamily);
		if (adapter == nullptr || adapter->metadata.component.family != a_publication.componentFamily) {
			return result;
		}

		const ComponentKey key{ a_publication.entityId, a_publication.componentFamily };
		auto stateIt = compatibilityProjections.find(key);
		const auto latestRevision = stateIt == compatibilityProjections.end() ? 0ULL : stateIt->second.latestRevision;
		if (a_publication.revision < latestRevision) {
			result.status = CompatibilityPublishStatus::Stale;
			return result;
		}

		const bool isNewRevision = a_publication.revision > latestRevision;
		if (isNewRevision) {
			auto routeUpdate = UpdateCompatibilityRecipients(
				a_publication.entityId,
				a_publication.componentFamily,
				a_publication.eligibleRecipients,
				false);
			if (!routeUpdate.accepted) {
				return result;
			}
			AppendFrames(result.frames, std::move(routeUpdate.frames));
			auto& state = compatibilityProjections[key];
			state.latestRevision = a_publication.revision;
			stateIt = compatibilityProjections.find(key);
		}

		auto& projectionState = compatibilityProjections[key];
		if (adapter->Supports(Compatibility::ConversionDirection::LegacyToComponent) &&
			(isNewRevision || projectionState.dirty || !projectionState.hasBytes)) {
			const auto projected = Compatibility::EncodeComponent(*adapter, a_publication.value);
			if (projected) {
				projectionState.bytes = *projected;
				projectionState.hasBytes = true;
				projectionState.dirty = false;
			} else {
				projectionState.dirty = true;
			}
		}

		Compatibility::RepresentationPolicy policy{ compatibilityRegistry, *componentDefinitions };
		Compatibility::RepresentationLedger ledger;
		std::set<PeerId> recipients;
		for (const auto& route : compatibilityRoutes) {
			if (route.component == key) {
				recipients.insert(route.peerId);
			}
		}

		for (const auto peerId : recipients) {
			auto peer = FindActiveSession(peerId);
			if (!peer) {
				continue;
			}
			auto& session = *peer->second;
			const auto committed = session.compatibilityRevisions.find(key);
			if (committed != session.compatibilityRevisions.end() && committed->second >= a_publication.revision) {
				continue;
			}

			const Compatibility::RepresentationSelectionKey selectionKey{
				peerId,
				a_publication.entityId,
				a_publication.componentFamily,
				a_publication.revision
			};
			const auto selection = policy.Select(selectionKey, a_publication.legacyFamily, session.compatibilityCapabilities);
			if (!selection || !ledger.Commit(*selection)) {
				continue;
			}

			if (selection->representation == Compatibility::Representation::Legacy) {
				const auto projected = Compatibility::Project(*selection, a_publication.value);
				if (!projected || !std::holds_alternative<std::string>(projected->payload) || !a_publication.legacyEgress) {
					continue;
				}
				try {
					const auto frame = a_publication.legacyEgress(
						peer->first,
						std::get<std::string>(projected->payload));
					if (!frame || frame->connectionId != peer->first || frame->payload.empty() || frame->payload.size() > kMaximumMessageBytes) {
						continue;
					}
					result.frames.push_back(*frame);
					session.compatibilityRevisions[key] = a_publication.revision;
				} catch (...) {
				}
				continue;
			}

			const auto channel = componentDefinitions->Find(a_publication.componentFamily);
			if (!channel.has_value()) {
				continue;
			}
			const auto view = BuildCompatibilityView(peerId, session, *channel);
			if (view.status != CompatibilityViewStatus::Complete) {
				continue;
			}
			const auto sent = PublishSelectedSnapshot(peer->first, peerId, view.state, *channel);
			if (sent.status == SnapshotSendStatus::Emitted && sent.frame) {
				result.frames.push_back(*sent.frame);
			}
			if (sent.status == SnapshotSendStatus::Emitted || sent.status == SnapshotSendStatus::SuppressedUnchanged) {
				session.compatibilityRevisions[key] = a_publication.revision;
			}
		}

		result.status = CompatibilityPublishStatus::Accepted;
		return result;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::RemoveCompatibilityEntity(EntityId a_entityId)
	{
		if (!compatibilityMode || a_entityId == 0) {
			return {};
		}
		std::set<PeerId> affected;
		for (auto iterator = compatibilityRoutes.begin(); iterator != compatibilityRoutes.end();) {
			if (iterator->component.entityId == a_entityId) {
				affected.insert(iterator->peerId);
				iterator = compatibilityRoutes.erase(iterator);
			} else {
				++iterator;
			}
		}
		for (auto iterator = compatibilityProjections.begin(); iterator != compatibilityProjections.end();) {
			if (iterator->first.entityId == a_entityId) {
				iterator = compatibilityProjections.erase(iterator);
			} else {
				++iterator;
			}
		}
		for (auto& [connectionId, session] : sessions) {
			(void)connectionId;
			for (auto iterator = session.compatibilityRevisions.begin(); iterator != session.compatibilityRevisions.end();) {
				if (iterator->first.entityId == a_entityId) {
					iterator = session.compatibilityRevisions.erase(iterator);
				} else {
					++iterator;
				}
			}
		}
		std::vector<OutboundFrame> frames;
		for (const auto peerId : affected) {
			AppendFrames(frames, ReconcileCompatibilityPeer(peerId));
		}
		return frames;
	}

	void ProtocolHost::Impl::FrameDeliveryCompleted(const OutboundFrame& a_frame, bool a_sent)
	{
		RecordSnapshotDelivery(a_frame, a_sent);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::RetryComponentSnapshots()
	{
		if (inventoryAuthority != nullptr) return RefreshInventoryProjection();
		return RetryFailedComponentFrames();
	}

	bool ProtocolHost::Impl::EnableInventoryTransactions(PersistenceGate& a_persistence)
	{
		return ConfigureInventoryAuthority(a_persistence);
	}
}
