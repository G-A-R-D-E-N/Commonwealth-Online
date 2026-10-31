		auto voiceFrames = protocol.PollVoice(loopMonotonic);
		sendFramesTracked(voiceFrames, loopMonotonic, UnixTimeSeconds());
		if (loopMonotonic >= nextMaintenanceMonotonic) {
			const auto nowUnix = UnixTimeSeconds();
			std::vector<std::uint32_t> expired;
			for (const auto& [connectionId, state] : runtimeConnections) {
				const bool admissionExpired = state.admissionTicket != 0 &&
					sessionAdmissions.IsExpired(state.admissionTicket, nowUnix);
				const bool handshakeExpired = state.profileId.empty() &&
					F4T::ConnectionPolicy::HandshakeExpired(
						 state.connectedAtUnixSeconds,
						 nowUnix,
						 state.active);
				const bool connectionExpired = handshakeExpired ||
					F4T::ConnectionPolicy::IdleExpired(
						 state.lastPacketAtUnixSeconds,
						 nowUnix,
						 state.active);
				if (admissionExpired || connectionExpired) {
					expired.push_back(connectionId);
				}
			}
			for (const auto connectionId : expired) {
				const auto tracked = runtimeConnections.find(connectionId);
				const bool admissionExpired = tracked != runtimeConnections.end() &&
					tracked->second.admissionTicket != 0 &&
					sessionAdmissions.IsExpired(tracked->second.admissionTicket, nowUnix);
				if (admissionExpired) {
					SendSessionEnded(
						server,
						connectionId,
						"admission_timeout",
						"Admission timed out before the session became ready.",
						nowUnix);
				}
				disconnectTracked(connectionId, admissionExpired ? "Admission timed out" : "Commonwealth Online disconnect");
			}
			(void)sessionAdmissions.Sweep(nowUnix);
			if (steamWebApi) {
				for (const auto& verdict : steamWebApi->TakeResults()) {
					const auto tracked = runtimeConnections.find(verdict.connectionId);
					if (tracked == runtimeConnections.end() || tracked->second.profileId != verdict.profileId) continue;
					const auto action = CommonwealthOnline::Server::DecideSteamTicketAction(verdict, [&](std::string_view a_steamId) -> std::optional<std::string> {
						if (const auto ban = banStore->GetSteamId(a_steamId)) return ban->reason;
						return std::nullopt;
					});
					(action.disconnect ? std::cerr : std::cout) << action.log << '\n';
					if (action.disconnect) {
						SendSessionEnded(server, verdict.connectionId, action.code, action.reason, nowUnix);
						disconnectTracked(verdict.connectionId, action.reason.c_str());
					} else if (verdict.outcome == CommonwealthOnline::Server::SteamTicketOutcome::Verified) {
						recordSteamIdentity(tracked->second, verdict.connectionId, verdict.steamId, CommonwealthOnline::Server::SteamIdSource::WebApi, nowUnix);
						protocol.BindSessionSteamId(verdict.connectionId, verdict.steamId);
					}
				}
			}
			steamAdmin->SweepIfDue(nowUnix);
			flushPersistence(false);
			auto retryFrames = protocol.RetryComponentSnapshots();
			sendFramesTracked(retryFrames, loopMonotonic, nowUnix);
			auto downedFrames = protocol.PollDownedState(loopMonotonic);
			sendFramesTracked(downedFrames, loopMonotonic, nowUnix);
			auto captureFrames = protocol.PollSettlementCapture(loopMonotonic);
			sendFramesTracked(captureFrames, loopMonotonic, nowUnix);
			auto inviteFrames = protocol.PollFactionInvites(loopMonotonic, nowUnix);
			sendFramesTracked(inviteFrames, loopMonotonic, nowUnix);
			nextMaintenanceMonotonic = loopMonotonic + 0.5;
		}
		static double s_nextMovementStatsLogMonotonic = 0.0;
		static bool s_hasMovementStatsBaseline = false;
		static std::uint64_t s_lastMovementRejected = 0;
		static std::uint64_t s_lastMovementCorrectionsSent = 0;
		static std::uint64_t s_lastTransformOversizeRejected = 0;
		if (loopMonotonic >= s_nextMovementStatsLogMonotonic) {
			const auto movementStats = protocol.Stats();
			if (s_hasMovementStatsBaseline) {
				std::cout << "Movement stats: movementRejected=" << movementStats.movementRejected
					<< " (+" << movementStats.movementRejected - s_lastMovementRejected << ")"
					<< ", movementCorrectionsSent=" << movementStats.movementCorrectionsSent
					<< " (+" << movementStats.movementCorrectionsSent - s_lastMovementCorrectionsSent << ")"
					<< ", transformOversizeRejected=" << movementStats.transformOversizeRejected
					<< " (+" << movementStats.transformOversizeRejected - s_lastTransformOversizeRejected << ") in the last 60 s." << '\n';
			}
			s_hasMovementStatsBaseline = true;
			s_lastMovementRejected = movementStats.movementRejected;
			s_lastMovementCorrectionsSent = movementStats.movementCorrectionsSent;
			s_lastTransformOversizeRejected = movementStats.transformOversizeRejected;
			s_nextMovementStatsLogMonotonic = loopMonotonic + 60.0;
		}
		CommonwealthOnline::Server::AdvanceManifestChunkDeliveries(server, UnixTimeSeconds(), disconnectTracked);
		CommonwealthOnline::Server::AdvanceModpackFileDeliveries(server, UnixTimeSeconds(), disconnectTracked);
