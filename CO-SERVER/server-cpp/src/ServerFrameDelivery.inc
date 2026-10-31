	void SendFrames(
		co_gns_server_handle a_server,
		CommonwealthOnline::Server::ProtocolHost& a_protocol,
		std::span<CommonwealthOnline::Server::OutboundFrame> a_frames,
		CommonwealthOnline::Server::IdentityGate* a_identityGate,
		CommonwealthOnline::Server::PersistenceGate* a_persistenceGate,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::vector<std::uint32_t>* a_retireConnections)
	{
		for (auto& frame : a_frames) {
			if (frame.payload.empty()) {
				continue;
			}
			if (F4T::Voice::HasVoicePrefix(std::as_bytes(std::span{ frame.payload }))) {
				const auto voiceResult = co_gns_server_send(
					a_server,
					frame.connectionId,
					frame.payload.data(),
					static_cast<std::uint32_t>(frame.payload.size()),
					static_cast<std::uint32_t>(frame.delivery));
				a_protocol.VoiceSendCompleted(
					voiceResult == CO_GNS_SEND_SENT,
					voiceResult == CO_GNS_SEND_BACKPRESSURE,
					voiceResult == CO_GNS_SEND_DROPPED);
				continue;
			}

			const bool identityReady = a_identityGate != nullptr && a_identityGate->EnrichSessionReady(frame);
			const bool sessionReady = identityReady ||
				RawPacketType(frame.payload).value_or(std::string{}) == "sessionReady";
			if (identityReady && a_persistenceGate != nullptr) {
				try {
					std::string prepareError;
					if (!a_persistenceGate->PrepareSessionReady(frame, prepareError)) {
						throw std::runtime_error(prepareError.empty() ? "Invalid inventory readiness frame" : prepareError);
					}
				} catch (const std::exception& exception) {
					std::cerr << "Inventory initialization failed for connection " << frame.connectionId << ": " << exception.what() << '\n';
					if (a_retireConnections != nullptr) a_retireConnections->push_back(frame.connectionId);
					continue;
				}
			}
			const auto result = co_gns_server_send(
				a_server,
				frame.connectionId,
				frame.payload.data(),
				static_cast<std::uint32_t>(frame.payload.size()),
				static_cast<std::uint32_t>(frame.delivery));
			a_protocol.FrameDeliveryCompleted(frame, result == CO_GNS_SEND_SENT);

			bool retireSession = sessionReady && result != CO_GNS_SEND_SENT;
			const char* retireReason = "sessionReady send failed";

			if (identityReady && result == CO_GNS_SEND_SENT && a_persistenceGate != nullptr) {
				try {
					for (const auto& transferFrame : a_persistenceGate->TakeInventoryTransferFrames(frame.connectionId)) {
						const auto transferResult = co_gns_server_send(
							a_server,
							transferFrame.connectionId,
							transferFrame.payload.data(),
							static_cast<std::uint32_t>(transferFrame.payload.size()),
							static_cast<std::uint32_t>(transferFrame.delivery));
						a_protocol.FrameDeliveryCompleted(transferFrame, transferResult == CO_GNS_SEND_SENT);
						if (transferResult != CO_GNS_SEND_SENT) {
							std::cerr << "Inventory transfer send failed for connection " << frame.connectionId
							          << " with result " << transferResult << '\n';
							retireSession = true;
							retireReason = "inventory transfer send failed";
							break;
						}
					}
				} catch (const std::exception& exception) {
					std::cerr << "Inventory transfer failed for connection " << frame.connectionId << ": "
					          << exception.what() << '\n';
					retireSession = true;
					retireReason = "inventory transfer failed";
				}
			}

			if (identityReady && result == CO_GNS_SEND_SENT && !retireSession) {
				{
					if (a_persistenceGate != nullptr) {
						try {
							const auto replay = a_persistenceGate->SessionReadyDelivered(frame, a_unixTimeSeconds);
							if (replay) {
								if (replay->playerState) {
									const auto replayBytes = JsonBytes(*replay->playerState);
									auto replayFrames = a_protocol.HandleMessage(
										frame.connectionId,
										std::span<const std::uint8_t>{ replayBytes.data(), replayBytes.size() },
										a_monotonicSeconds,
										a_unixTimeSeconds);
									SendFrames(
										a_server,
										a_protocol,
										replayFrames,
										a_identityGate,
										a_persistenceGate,
										a_monotonicSeconds,
										a_unixTimeSeconds,
										a_retireConnections);
								}
								for (const auto& worldPacket : replay->worldPackets) {
									if (!SendReliableJson(a_server, frame.connectionId, worldPacket)) {
										std::cerr << "Durable world-state replay send failed for connection " << frame.connectionId << '\n';
									}
								}
							}
						} catch (const std::exception& exception) {
							std::cerr << "Post-ready durable replay failed for connection " << frame.connectionId << ": " << exception.what() << '\n';
						}
					}
					if (!a_identityGate->CommitReady(frame.connectionId)) {
						std::cerr << "Resume credential storage failed for connection " << frame.connectionId
						          << "; ending the session instead of establishing it\n";
						retireSession = true;
						retireReason = "resume credential storage failed";
					}
				}
			}

			if (retireSession) {
				std::cerr << "Ending connection " << frame.connectionId << ": " << retireReason << '\n';
				if (a_retireConnections != nullptr) {
					a_retireConnections->push_back(frame.connectionId);
				}
			}

			if (result != CO_GNS_SEND_SENT && result != CO_GNS_SEND_DROPPED) {
				std::cerr << "GNS send failed for connection " << frame.connectionId << " with result " << result << '\n';
			}
		}
	}
