	void SendFrames(
		co_gns_server_handle a_server,
		CommonwealthOnline::Server::ProtocolHost& a_protocol,
		std::span<CommonwealthOnline::Server::OutboundFrame> a_frames,
		CommonwealthOnline::Server::IdentityGate* a_identityGate,
		CommonwealthOnline::Server::PersistenceGate* a_persistenceGate,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::vector<std::uint32_t>* a_retireConnections,
		const CommonwealthOnline::Server::DeferredFramePolicy& a_deferred);

	namespace
	{
		struct FrameDelivery
		{
			co_gns_server_handle server{};
			CommonwealthOnline::Server::ProtocolHost* protocol{};
			CommonwealthOnline::Server::IdentityGate* identityGate{};
			CommonwealthOnline::Server::PersistenceGate* persistenceGate{};
			double monotonicSeconds{};
			double unixTimeSeconds{};
			std::vector<std::uint32_t>* retireConnections{};
			CommonwealthOnline::Server::DeferredFramePolicy deferred{};
		};

		std::uint32_t FramePlayerId(const CommonwealthOnline::Server::OutboundFrame& a_frame)
		{
			const auto packet = nlohmann::json::parse(a_frame.payload.begin(), a_frame.payload.end(), nullptr, false);
			if (!packet.is_object()) {
				return 0;
			}
			const auto found = packet.find("playerId");
			return found != packet.end() && found->is_number_unsigned() ? found->get<std::uint32_t>() : 0;
		}

		bool IsVoiceFrame(const CommonwealthOnline::Server::OutboundFrame& a_frame)
		{
			return F4T::Voice::HasVoicePrefix(std::as_bytes(std::span{ a_frame.payload }));
		}

		void DeliverFrame(FrameDelivery& a_context, CommonwealthOnline::Server::OutboundFrame& a_frame, bool a_identityReady)
		{
			const bool sessionReady = a_identityReady ||
				RawPacketType(a_frame.payload).value_or(std::string{}) == "sessionReady";
			if (sessionReady && !CommonwealthOnline::Server::RefreshReadySurvivalPolicy(a_frame, *a_context.protocol)) {
				a_context.protocol->FrameDeliveryCompleted(a_frame, false);
				if (a_context.retireConnections) a_context.retireConnections->push_back(a_frame.connectionId);
				return;
			}
			if (a_context.protocol->HeldCombatRelayBlocks(a_frame)) {
				a_context.protocol->FrameDeliveryCompleted(a_frame, false);
				return;
			}
			auto wirePayload = F4T::PayloadCodec::Encode(a_frame.payload);
			const auto maxBytes = co_gns_maximum_message_bytes();
			int result = CO_GNS_SEND_ERROR;
			if (wirePayload.size() <= maxBytes) {
				result = co_gns_server_send(
					a_context.server,
					a_frame.connectionId,
					wirePayload.data(),
					static_cast<std::uint32_t>(wirePayload.size()),
					static_cast<std::uint32_t>(a_frame.delivery));
			} else if (a_frame.delivery == CommonwealthOnline::Server::Delivery::ReliableOrdered) {
				static std::atomic<std::uint32_t> fragmentMessageId{ 1 };
				const auto parts = F4T::MessageFragments::Split(
					fragmentMessageId.fetch_add(1, std::memory_order_relaxed),
					wirePayload,
					maxBytes);
				if (parts.empty()) {
					std::cerr << "GNS send skipped for connection " << a_frame.connectionId << ": fragment split failed\n";
				}
				result = parts.empty() ? CO_GNS_SEND_ERROR : CO_GNS_SEND_SENT;
				for (const auto& part : parts) {
					const auto partResult = co_gns_server_send(
						a_context.server,
						a_frame.connectionId,
						part.data(),
						static_cast<std::uint32_t>(part.size()),
						static_cast<std::uint32_t>(a_frame.delivery));
					if (partResult != CO_GNS_SEND_SENT) {
						result = partResult;
						break;
					}
				}
			} else {
				std::cerr << "GNS send skipped for connection " << a_frame.connectionId << ": encoded payload too large for unreliable delivery\n";
				result = CO_GNS_SEND_DROPPED;
			}
			a_context.protocol->FrameDeliveryCompleted(a_frame, result == CO_GNS_SEND_SENT);
			if (sessionReady && result == CO_GNS_SEND_SENT && a_context.deferred.sessionReadyDelivered != nullptr) {
				a_context.deferred.sessionReadyDelivered->push_back(std::make_pair(a_frame.connectionId, FramePlayerId(a_frame)));
			}

			bool retireSession = sessionReady && result != CO_GNS_SEND_SENT;
			const char* retireReason = "sessionReady send failed";

			if (a_identityReady && result == CO_GNS_SEND_SENT && a_context.persistenceGate != nullptr) {
				try {
					auto transferFrames = a_context.persistenceGate->TakeInventoryTransferFrames(a_frame.connectionId);
					for (auto& grantFrame : a_context.persistenceGate->TakeStartingGrantFrames(a_frame.connectionId)) {
						transferFrames.push_back(std::move(grantFrame));
					}
					for (const auto& transferFrame : transferFrames) {
						auto transferWire = F4T::PayloadCodec::Encode(transferFrame.payload);
						if (transferWire.size() > co_gns_maximum_message_bytes()) {
							std::cerr << "Inventory transfer send failed for connection " << a_frame.connectionId
									  << ": encoded payload too large\n";
							retireSession = true;
							retireReason = "inventory transfer send failed";
							break;
						}
						const auto transferResult = co_gns_server_send(
							a_context.server,
							transferFrame.connectionId,
							transferWire.data(),
							static_cast<std::uint32_t>(transferWire.size()),
							static_cast<std::uint32_t>(transferFrame.delivery));
						a_context.protocol->FrameDeliveryCompleted(transferFrame, transferResult == CO_GNS_SEND_SENT);
						if (transferResult != CO_GNS_SEND_SENT) {
							std::cerr << "Inventory transfer send failed for connection " << a_frame.connectionId
							          << " with result " << transferResult << '\n';
							retireSession = true;
							retireReason = "inventory transfer send failed";
							break;
						}
					}
				} catch (const std::exception& exception) {
					std::cerr << "Inventory transfer failed for connection " << a_frame.connectionId << ": "
					          << exception.what() << '\n';
					retireSession = true;
					retireReason = "inventory transfer failed";
				}
			}

			if (a_identityReady && result == CO_GNS_SEND_SENT && !retireSession) {
				if (a_context.persistenceGate != nullptr) {
					try {
						const auto replay = a_context.persistenceGate->SessionReadyDelivered(a_frame, a_context.unixTimeSeconds);
						if (replay) {
							if (replay->playerState) {
								const auto replayBytes = JsonBytes(*replay->playerState);
								auto replayFrames = a_context.protocol->HandleMessage(
									a_frame.connectionId,
									std::span<const std::uint8_t>{ replayBytes.data(), replayBytes.size() },
									a_context.monotonicSeconds,
									a_context.unixTimeSeconds);
								SendFrames(
									a_context.server,
									*a_context.protocol,
									replayFrames,
									a_context.identityGate,
									a_context.persistenceGate,
									a_context.monotonicSeconds,
									a_context.unixTimeSeconds,
									a_context.retireConnections,
									a_context.deferred);
							}
							for (const auto& worldPacket : replay->worldPackets) {
								if (!SendReliableJson(a_context.server, a_frame.connectionId, worldPacket)) {
									std::cerr << "Durable world-state replay send failed for connection " << a_frame.connectionId << "; retiring session\n";
									retireSession = true;
									retireReason = "durable world-state replay send failed";
									break;
								}
							}
						}
					} catch (const std::exception& exception) {
						std::cerr << "Post-ready durable replay failed for connection " << a_frame.connectionId << ": " << exception.what() << "; retiring session\n";
						retireSession = true;
						retireReason = "durable world-state replay failed";
					}
				}
				if (a_context.identityGate == nullptr || !a_context.identityGate->CommitReady(a_frame.connectionId)) {
					std::cerr << "Resume credential storage failed for connection " << a_frame.connectionId
					          << "; ending the session instead of establishing it\n";
					retireSession = true;
					retireReason = "resume credential storage failed";
				}
			}

			if (retireSession) {
				std::cerr << "Ending connection " << a_frame.connectionId << ": " << retireReason << '\n';
				if (a_context.retireConnections != nullptr) {
					a_context.retireConnections->push_back(a_frame.connectionId);
				}
			}

			if (result != CO_GNS_SEND_SENT && result != CO_GNS_SEND_DROPPED) {
				std::cerr << "GNS send failed for connection " << a_frame.connectionId << " with result " << result << '\n';
			}
		}

		std::uint64_t PostedTicket(const CommonwealthOnline::Server::DeferredFramePolicy& a_deferred)
		{
			return a_deferred.store != nullptr ? a_deferred.store->LastPostedTicket() : 0;
		}

		bool ReplyMayGoOut(const CommonwealthOnline::Server::DeferredFramePolicy& a_deferred)
		{
			return a_deferred.store == nullptr || a_deferred.store->AllPostedDurable();
		}
	}

	void SendFrames(
		co_gns_server_handle a_server,
		CommonwealthOnline::Server::ProtocolHost& a_protocol,
		std::span<CommonwealthOnline::Server::OutboundFrame> a_frames,
		CommonwealthOnline::Server::IdentityGate* a_identityGate,
		CommonwealthOnline::Server::PersistenceGate* a_persistenceGate,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::vector<std::uint32_t>* a_retireConnections,
		const CommonwealthOnline::Server::DeferredFramePolicy& a_deferred)
	{
		FrameDelivery context{
			.server = a_server,
			.protocol = std::addressof(a_protocol),
			.identityGate = a_identityGate,
			.persistenceGate = a_persistenceGate,
			.monotonicSeconds = a_monotonicSeconds,
			.unixTimeSeconds = a_unixTimeSeconds,
			.retireConnections = a_retireConnections,
			.deferred = a_deferred
		};
		for (auto& frame : a_frames) {
			if (frame.payload.empty()) {
				continue;
			}
			if (IsVoiceFrame(frame)) {
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

			const bool messagePostedCommits = a_deferred.queue != nullptr &&
				a_deferred.gateConnectionId != 0 &&
				PostedTicket(a_deferred) > a_deferred.ticketBefore;
			if (a_deferred.queue != nullptr && a_deferred.queue->Blocked(frame.connectionId)) {
				a_deferred.queue->Park(
					frame.connectionId,
					frame,
					identityReady,
					(messagePostedCommits || sessionReady) ? PostedTicket(a_deferred) : 0,
					a_deferred.expiresAtMonotonic,
					messagePostedCommits ? a_deferred.ticketBefore : 0);
				continue;
			}
			if (messagePostedCommits || (sessionReady && a_deferred.queue != nullptr && !ReplyMayGoOut(a_deferred))) {
				a_deferred.queue->Park(
					frame.connectionId,
					frame,
					identityReady,
					PostedTicket(a_deferred),
					a_deferred.expiresAtMonotonic,
					a_deferred.ticketBefore);
				continue;
			}
			DeliverFrame(context, frame, identityReady);
		}
		auto deferred = a_protocol.TakeDeferredFrames();
		if (!deferred.empty()) {
			SendFrames(a_server, a_protocol, deferred, a_identityGate, a_persistenceGate, a_monotonicSeconds, a_unixTimeSeconds, a_retireConnections, a_deferred);
		}
	}

	std::vector<std::uint32_t> SendDeferredFrames(
		co_gns_server_handle a_server,
		CommonwealthOnline::Server::ProtocolHost& a_protocol,
		CommonwealthOnline::Server::IdentityGate* a_identityGate,
		CommonwealthOnline::Server::PersistenceGate* a_persistenceGate,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::vector<std::uint32_t>* a_retireConnections,
		const CommonwealthOnline::Server::DeferredFramePolicy& a_deferred)
	{
		std::vector<std::uint32_t> abandoned;
		if (a_deferred.queue == nullptr || a_deferred.queue->Empty()) {
			return abandoned;
		}
		FrameDelivery context{
			.server = a_server,
			.protocol = std::addressof(a_protocol),
			.identityGate = a_identityGate,
			.persistenceGate = a_persistenceGate,
			.monotonicSeconds = a_monotonicSeconds,
			.unixTimeSeconds = a_unixTimeSeconds,
			.retireConnections = a_retireConnections,
			.deferred = a_deferred
		};
		auto& entries = a_deferred.queue->Entries();
		std::vector<CommonwealthOnline::Server::DeferredFrameEntry> ready;
		const auto durable = [&](std::uint64_t a_ticket) {
			return a_deferred.store != nullptr && a_deferred.store->TicketDurable(a_ticket);
		};
		const auto quarantined = [&](std::uint64_t a_before, std::uint64_t a_through) {
			return a_deferred.store != nullptr && a_deferred.store->QuarantinedSince(a_before, a_through);
		};
		for (auto entry = entries.begin(); entry != entries.end();) {
			auto sweep = CommonwealthOnline::Server::SweepDeferredFrames(*entry, a_monotonicSeconds, durable, quarantined);
			for (auto& delivered : sweep.delivered) {
				ready.push_back(std::move(delivered));
			}
			if (!sweep.abandoned.empty()) {
				std::cerr << "Dropped " << sweep.abandoned.size() << " held reply frame(s) for connection "
				          << entry->connectionId << " after its commit could not be completed\n";
				for (auto& dropped : sweep.abandoned) {
					a_protocol.FrameDeliveryCompleted(dropped.frame, false);
				}
				abandoned.push_back(entry->connectionId);
			}
			if (entry->frames.empty()) {
				entry = entries.erase(entry);
			} else {
				++entry;
			}
		}
		for (auto& pending : ready) {
			DeliverFrame(context, pending.frame, pending.identityReady);
		}
		return abandoned;
	}
