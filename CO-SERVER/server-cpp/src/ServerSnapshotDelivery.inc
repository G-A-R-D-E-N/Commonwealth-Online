		std::vector<OutboundFrame> PublishAuthoritativeSnapshot(const SnapshotState& a_authoritativeState)
		{
			if (inventoryAuthority != nullptr && std::any_of(a_authoritativeState.begin(), a_authoritativeState.end(), [](const auto& component) {
				return component.first.family != Family::StableDroppedItem;
			})) return {};
			if (compatibilityMode || !componentDefinitions.has_value()) {
				return {};
			}
			directAuthoritativeState = a_authoritativeState;
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : sessions) {
				if (!recipient.active || !recipient.compatibilityCapabilities.Has("component-snapshot-v1")) {
					continue;
				}
				const auto contract = FindDirectPublicationPeer(recipient.playerId);
				if (contract == nullptr || !contract->componentSnapshot) {
					continue;
				}
				const auto selected = F4T::Replication::SelectPeerSnapshot(
					peerVisibility,
					recipient.playerId,
					a_authoritativeState);
				if (!FamiliesAllowed(selected, *contract)) {
					continue;
				}
				const auto partitioned = F4T::Replication::PartitionSnapshotByChannel(selected, *componentDefinitions);
				if (!partitioned) {
					continue;
				}
				const auto reliableFamiliesAllowed = FamiliesAllowed(partitioned->reliableState, *contract);
				const auto unreliableFamiliesAllowed = FamiliesAllowed(partitioned->unreliableState, *contract);
				const auto reliable = reliableFamiliesAllowed && contract->componentSnapshotChannels &&
					recipient.compatibilityCapabilities.Has("component-snapshot-channels-v1") ?
					PublishSelectedSnapshot(connectionId, recipient.playerId, partitioned->reliableState, F4T::Replication::Channel::Reliable) :
					SnapshotSendResult{};
				const auto unreliable = unreliableFamiliesAllowed ? PublishSelectedSnapshot(
					connectionId,
					recipient.playerId,
					partitioned->unreliableState,
					F4T::Replication::Channel::Unreliable) :
					SnapshotSendResult{};
				if (reliable.status == SnapshotSendStatus::Emitted && reliable.frame) {
					frames.push_back(*reliable.frame);
				}
				if (unreliable.status == SnapshotSendStatus::Emitted && unreliable.frame) {
					frames.push_back(*unreliable.frame);
				}
			}
			return frames;
		}


		SnapshotSendResult PublishSelectedSnapshot(
			std::uint32_t a_connectionId,
			PeerId a_peerId,
			const SnapshotState& a_selected,
			F4T::Replication::Channel a_channel)
		{
			const auto senderIt = peerSnapshots.find(a_peerId);
			if (senderIt == peerSnapshots.end() || !F4T::Replication::IsValidChannel(a_channel)) {
				return {};
			}
			auto& sender = senderIt->second[static_cast<std::size_t>(a_channel)];
			const auto emitBaseline = [&](std::uint32_t a_sequence) -> SnapshotSendResult {
				const auto encoded = F4T::Replication::EncodeBaseline(a_selected, a_sequence, a_channel);
				if (!encoded || encoded->empty() || encoded->size() > kMaximumMessageBytes) {
					return {};
				}
				OutboundFrame frame{
					a_connectionId,
					a_channel == F4T::Replication::Channel::Reliable ? Delivery::ReliableOrdered : Delivery::UnreliableSequenced,
					std::move(*encoded)
				};
				sender.baseline = a_selected;
				sender.lastPublished = a_selected;
				sender.baselineSequence = a_sequence;
				sender.lastSnapshotSequence = a_sequence;
				sender.publishesSinceBaseline = 0;
				sender.hasBaseline = true;
				return SnapshotSendResult{ SnapshotSendStatus::Emitted, std::move(frame) };
			};

			if (!sender.hasBaseline) {
				if (a_channel == F4T::Replication::Channel::Reliable && a_selected.empty() && !sender.pendingRetry) {
					return {};
				}
				return emitBaseline(F4T::GnsSnapshotEnvelope::NextSequence(sender.lastSnapshotSequence));
			}
			if (sender.publishesSinceBaseline < kSnapshotKeyframeInterval) {
				++sender.publishesSinceBaseline;
			}
			if (sender.publishesSinceBaseline >= kSnapshotKeyframeInterval) {
				return emitBaseline(F4T::GnsSnapshotEnvelope::NextSequence(sender.lastSnapshotSequence));
			}
			if (a_selected == sender.lastPublished) {
				return SnapshotSendResult{ SnapshotSendStatus::SuppressedUnchanged, std::nullopt };
			}

			const auto sequence = F4T::GnsSnapshotEnvelope::NextSequence(sender.lastSnapshotSequence);
			const auto delta = F4T::Replication::ComputeDelta(sender.baseline, a_selected);
			const auto encodedDelta = F4T::Replication::EncodeDelta(delta, sequence, sender.baselineSequence, a_channel);
			if (!encodedDelta || encodedDelta->empty() || encodedDelta->size() > kMaximumMessageBytes) {
				sender.publishesSinceBaseline = kSnapshotKeyframeInterval;
				return emitBaseline(sequence);
			}
			OutboundFrame frame{
				a_connectionId,
				a_channel == F4T::Replication::Channel::Reliable ? Delivery::ReliableOrdered : Delivery::UnreliableSequenced,
				std::move(*encodedDelta)
			};
			sender.lastPublished = a_selected;
			sender.lastSnapshotSequence = sequence;
			return SnapshotSendResult{ SnapshotSendStatus::Emitted, std::move(frame) };
		}

		void RecordSnapshotDelivery(const OutboundFrame& a_frame, bool a_sent)
		{
			const auto session = sessions.find(a_frame.connectionId);
			if (!componentDefinitions || session == sessions.end() || !session->second.active) return;
			F4T::Replication::DecodedFrame decoded;
			if (F4T::Replication::DecodeFrame(a_frame.payload, decoded, [this](Family a_family) {
				return componentDefinitions->Contains(a_family);
			}) != F4T::Replication::DecodeStatus::Valid) return;
			if ((decoded.channel == F4T::Replication::Channel::Reliable) != (a_frame.delivery == Delivery::ReliableOrdered)) return;
			const auto snapshots = peerSnapshots.find(session->second.playerId);
			if (snapshots == peerSnapshots.end()) return;
			auto& sender = snapshots->second[static_cast<std::size_t>(decoded.channel)];
			if (sender.lastSnapshotSequence == 0 || F4T::GnsSnapshotEnvelope::IsNewer(decoded.snapshotSequence, sender.lastSnapshotSequence)) return;
			if (!a_sent) {
				sender.hasBaseline = false;
				sender.pendingRetry = true;
				for (auto revision = session->second.compatibilityRevisions.begin(); revision != session->second.compatibilityRevisions.end();) {
					if (componentDefinitions->Find(revision->first.family) == decoded.channel) revision = session->second.compatibilityRevisions.erase(revision);
					else ++revision;
				}
			} else if (decoded.kind == F4T::Replication::FrameKind::Baseline && decoded.snapshotSequence == sender.lastSnapshotSequence) {
				sender.hasBaseline = true;
				sender.pendingRetry = false;
			}
		}

		std::vector<OutboundFrame> RetryFailedComponentFrames()
		{
			std::vector<OutboundFrame> frames;
			if (!componentDefinitions) return frames;
			for (auto& [connectionId, session] : sessions) {
				if (!session.active) continue;
				const auto snapshots = peerSnapshots.find(session.playerId);
				if (snapshots == peerSnapshots.end()) continue;
				for (const auto channel : { F4T::Replication::Channel::Reliable, F4T::Replication::Channel::Unreliable }) {
					auto& sender = snapshots->second[static_cast<std::size_t>(channel)];
					if (!sender.pendingRetry) continue;
					SnapshotState selected;
					if (compatibilityMode) {
						const auto view = BuildCompatibilityView(session.playerId, session, channel);
						if (view.status != CompatibilityViewStatus::Complete) continue;
						selected = view.state;
					} else {
						const auto contract = FindDirectPublicationPeer(session.playerId);
						if (contract == nullptr || !contract->componentSnapshot || !session.compatibilityCapabilities.Has("component-snapshot-v1") ||
							(channel == F4T::Replication::Channel::Reliable && (!contract->componentSnapshotChannels || !session.compatibilityCapabilities.Has("component-snapshot-channels-v1")))) continue;
						selected = F4T::Replication::SelectPeerSnapshot(peerVisibility, session.playerId, directAuthoritativeState);
						if (!FamiliesAllowed(selected, *contract)) continue;
						const auto partitioned = F4T::Replication::PartitionSnapshotByChannel(selected, *componentDefinitions);
						if (!partitioned) continue;
						selected = channel == F4T::Replication::Channel::Reliable ? partitioned->reliableState : partitioned->unreliableState;
					}
					sender.hasBaseline = false;
					const auto sent = PublishSelectedSnapshot(connectionId, session.playerId, selected, channel);
					if (sent.status == SnapshotSendStatus::Emitted && sent.frame) frames.push_back(*sent.frame);
				}
			}
			return frames;
		}
