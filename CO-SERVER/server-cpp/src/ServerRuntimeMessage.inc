			const auto stateIt = runtimeConnections.find(event.connection_id);
			if (stateIt == runtimeConnections.end()) {
				co_gns_server_disconnect(server, event.connection_id, 0, "Unknown session");
				break;
			}
			auto& state = stateIt->second;
			const auto monotonicNow = MonotonicSeconds();
			const auto unixNow = UnixTimeSeconds();

			if (event.payload_size > payload.size()) {
				disconnectTracked(event.connection_id, "Inbound payload exceeded host buffer");
				break;
			}
			const auto wireRate = CommonwealthOnline::Server::ObserveInboundWire(state.wireRate, monotonicNow, event.payload_size);
			if (wireRate.verdict != F4T::PacketRateLimit::Verdict::Allowed) {
				if (wireRate.verdict == F4T::PacketRateLimit::Verdict::Disconnect) {
					const std::string_view reason = CommonwealthOnline::Server::RateLimitReason(wireRate);
					LogRefusal("rate_limit", ConnectionAddressText(state), "rate_limited", reason);
					SendSessionEnded(server, event.connection_id, "rate_limited", reason, unixNow);
					disconnectTracked(event.connection_id, reason.data());
				}
				break;
			}
			std::span<const std::uint8_t> message{ payload.data(), event.payload_size };
			std::vector<std::uint8_t> assembledStorage;
			if (F4T::MessageFragments::IsFragment(message)) {
				auto assembled = state.fragments.Push(message, monotonicNow);
				if (!assembled) {
					break;
				}
				assembledStorage = std::move(*assembled);
				message = assembledStorage;
			}
			auto decoded = F4T::PayloadCodec::Decode(message, CommonwealthOnline::Server::kMaximumMessageBytes);
			if (!decoded) {
				protocol.QueueViolation(event.connection_id, CommonwealthOnline::Server::Violation::MalformedPayload, "Malformed inbound payload");
				break;
			}
			assembledStorage = std::move(*decoded);
			message = assembledStorage;
			const auto datagram = std::as_bytes(message);
			const auto voiceFrame = F4T::Voice::HasVoicePrefix(datagram);
			const auto inboundBudget = CommonwealthOnline::Server::InboundGameplayLimits(monotonicNow, state.joinedAtMonotonicSeconds, options.inboundBudget);
			const auto rate = CommonwealthOnline::Server::ObserveInboundDatagram(state.rate, state.voiceRate, monotonicNow, datagram, inboundBudget);
			const auto budgetAllows = rate.verdict == F4T::PacketRateLimit::Verdict::Allowed;
			const auto messageType = budgetAllows ? RawPacketType(message) : std::optional<std::string>{};
			if (!voiceFrame) {
				const auto windowLabel = messageType.value_or(budgetAllows ? "binary" : "over_budget");
				CommonwealthOnline::Server::NoteInboundWindowBytes(state.inboundWindowBytes, rate.windowReset, windowLabel, message.size());
			}
			if (rate.verdict != F4T::PacketRateLimit::Verdict::Allowed) {
				if (rate.verdict == F4T::PacketRateLimit::Verdict::Disconnect && rate.newViolationWindow) {
					const std::string detail = std::string{ CommonwealthOnline::Server::RateLimitReason(rate) } + " Window bytes by type: " + CommonwealthOnline::Server::DescribeInboundWindowBytes(state.inboundWindowBytes);
					LogRefusal("rate_limit", ConnectionAddressText(state), "rate_limited", detail);
					protocol.QueueViolation(event.connection_id, rate.byteBudgetExceeded ? CommonwealthOnline::Server::Violation::InboundBudget : CommonwealthOnline::Server::Violation::RateLimited, detail);
				}
				break;
			}
			if (messageType && !F4T::ConnectionPolicy::MessageWithinTypeLimit(*messageType, message.size())) {
				LogRefusal("message_size", ConnectionAddressText(state), "message_too_large", *messageType);
				disconnectTracked(event.connection_id, "Inbound message exceeded its type size limit");
				break;
			}
			state.lastPacketAtUnixSeconds = unixNow;
			if (state.admissionTicket != 0) {
				const bool admissionAlive = sessionAdmissions.Update(
					state.admissionTicket,
					messageType.value_or("inbound"),
					unixNow);
				if (!admissionAlive) {
					SendSessionEnded(server, event.connection_id, "admission_timeout", "Admission timed out before the session became ready.", unixNow);
					disconnectTracked(event.connection_id, "Admission timed out");
					break;
				}
			}

			const auto manifestSourceKey = F4T::ConnectionPolicy::SourcePeerKey(state.remoteIpv4, state.remoteAddress);
			if (CommonwealthOnline::Server::ServeManifestChunksIfRequested(server, event.connection_id, message, options, unixNow, disconnectTracked, sessionAdmissions, state.admissionTicket, identity, manifestSourceKey) != CommonwealthOnline::Server::ManifestChunkRequest::notRequest) {
				break;
			}
			if (CommonwealthOnline::Server::ServeModpackFilesIfRequested(server, event.connection_id, message, options, unixNow, state.remoteAddress.empty() ? Ipv4Text(state.remoteIpv4) : state.remoteAddress, disconnectTracked, sessionAdmissions, state.admissionTicket, identity, manifestSourceKey) != CommonwealthOnline::Server::ModpackFileRequest::notRequest) {
				break;
			}
			if (identity != nullptr) {
				try {
					const bool capacityAvailable = F4T::ConnectionPolicy::CanActivate(protocol.ActiveCount(), options.maximumPlayers);
					const auto decision = identity->InspectInbound(event.connection_id, message, unixNow, capacityAvailable);
					if (!decision.forward) {
						LogRefusal("hello", ConnectionAddressText(state), decision.code, decision.reason);
						if (state.admissionTicket != 0) {
							(void)sessionAdmissions.Reject(state.admissionTicket, decision.code);
							state.admissionTicket = 0;
						}
						if (decision.code == "steam_identity_mismatch") {
							protocol.QueueViolation(event.connection_id, CommonwealthOnline::Server::Violation::IdentityMismatch, decision.reason);
							break;
						}
						const auto refusalReason = decision.code == "steam_banned" || decision.code == "profile_banned" ? CommonwealthOnline::Server::WithAppealLink(decision.reason, options.appealLink) : decision.reason;
						SendSessionEnded(server, event.connection_id, decision.code, refusalReason, unixNow, decision.requiredModpack, decision.modpackFingerprint, decision.modpackChunks, decision.modpackName);
						disconnectTracked(event.connection_id, decision.reason.c_str());
						break;
					}
					if (decision.consumed) {
						if (decision.rejected) {
							std::cerr << "Save attestation rejected for connection " << event.connection_id << ": "
							          << decision.code << ": " << decision.reason << '\n';
						}
						if (decision.response) {
							(void)SendReliableJson(server, event.connection_id, *decision.response);
						}
						break;
					}
					if (!decision.profileId.empty()) {
						state.profileId = decision.profileId;
						const bool hostSelf = decision.steamIdSource == CommonwealthOnline::Server::SteamIdSource::HostSelf; adminRoster.NoteHost(hostSelf, decision.profileId); protocol.BindSessionProfileVerified(event.connection_id, decision.profileId, hostSelf || decision.isAdmin || decision.secretVerified, decision.steamId);
						state.nickname = decision.nickname;
						if (!decision.steamId.empty()) recordSteamIdentity(state, event.connection_id, decision.steamId, decision.steamIdSource, unixNow);
						if (state.admissionTicket != 0) {
							const bool admissionAlive = sessionAdmissions.Claim(
								state.admissionTicket,
								decision.steamId,
								decision.nickname,
								decision.profileId,
								"hello",
								unixNow);
							if (!admissionAlive) {
								SendSessionEnded(server, event.connection_id, "admission_timeout", "Admission timed out before the session became ready.", unixNow);
								disconnectTracked(event.connection_id, "Admission timed out");
								break;
							}
						}
					}
					if (decision.evictConnectionId && *decision.evictConnectionId != event.connection_id) {
						constexpr std::string_view superseded =
							"This session was replaced by a reconnect using the profile's resume credential.";
						SendSessionEnded(server, *decision.evictConnectionId, "session_superseded", superseded, unixNow);
						disconnectTracked(*decision.evictConnectionId, superseded.data());
					}
					if (decision.steamVerdictHeld) {
						break;
					}
				} catch (const std::exception& exception) {
					std::cerr << "Identity validation failed for connection " << event.connection_id << ": " << exception.what() << '\n';
					disconnectTracked(event.connection_id, "Identity validation failed");
					break;
				}
			}

			deliverToProtocol(event.connection_id, state, message, monotonicNow, unixNow);
			break;
