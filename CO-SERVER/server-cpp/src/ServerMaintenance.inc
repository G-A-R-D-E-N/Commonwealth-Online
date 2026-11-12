		auto voiceFrames = protocol.PollVoice(loopMonotonic);
		sendFramesTracked(voiceFrames, loopMonotonic, UnixTimeSeconds());
		auto presenceFrames = protocol.PollPresence(loopMonotonic, UnixTimeSeconds());
		sendFramesTracked(presenceFrames, loopMonotonic, UnixTimeSeconds());
		auto npcLootFrames = protocol.PollNpcLootTakes(loopMonotonic, UnixTimeSeconds());
		sendFramesTracked(npcLootFrames, loopMonotonic, UnixTimeSeconds());
		auto survivalNeedsFrames = protocol.PollSurvivalNeeds(loopMonotonic, UnixTimeSeconds());
		sendFramesTracked(survivalNeedsFrames, loopMonotonic, UnixTimeSeconds());
		auto difficultyFrames = protocol.PollDifficulty(loopMonotonic, UnixTimeSeconds());
		sendFramesTracked(difficultyFrames, loopMonotonic, UnixTimeSeconds());
		if (loopMonotonic >= nextMaintenanceMonotonic) {
			const auto nowUnix = UnixTimeSeconds();
			struct ExpiredSession
			{
				std::uint32_t connectionId{};
				const char* code{};
				const char* reason{};
				const char* endedBy{};
			};
			std::vector<ExpiredSession> expired;
			for (auto& [connectionId, state] : runtimeConnections) {
				const auto afkVerdict = CommonwealthOnline::Server::Afk::Evaluate(
					state.lastActivityAtMonotonicSeconds,
					protocol.SessionAfkExemption(connectionId),
					loopMonotonic,
					state.active,
					state.steamHost);
				if (state.active && loopMonotonic >= state.nextStatusReportMonotonic) {
					LogConnectionStatus(server, connectionId, state);
					state.nextStatusReportMonotonic = loopMonotonic + F4T::ConnectionPolicy::DefaultStatusReportSeconds;
				}
				const bool admissionExpired = state.admissionTicket != 0 &&
					sessionAdmissions.IsExpired(state.admissionTicket, nowUnix);
				const bool handshakeExpired = state.profileId.empty() &&
					!CommonwealthOnline::Server::HasManifestChunkDelivery(server, connectionId) &&
					F4T::ConnectionPolicy::HandshakeExpired(
						 state.joinedAtMonotonicSeconds,
						 loopMonotonic,
						 state.active);
				const bool afkExpired = afkVerdict == CommonwealthOnline::Server::Afk::Verdict::kick;
				if (admissionExpired) {
					expired.push_back({ connectionId, "admission_timeout", "Admission timed out before the session became ready.", "Admission timed out" });
				} else if (afkExpired) {
					expired.push_back({ connectionId, "afk_kick", "You were kicked for being AFK.", "AFK kick" });
				} else if (handshakeExpired) {
					expired.push_back({ connectionId, nullptr, nullptr, "Commonwealth Online disconnect" });
				}
			}
			for (const auto& entry : expired) {
				CommonwealthOnline::Server::Afk::EndSessionAfterNotifying(
					entry.connectionId,
					[&](std::uint32_t a_expiring) {
						if (entry.code == nullptr) {
							return;
						}
						SendSessionEnded(server, a_expiring, entry.code, entry.reason, nowUnix);
					},
					[&](std::uint32_t a_expiring) { disconnectTracked(a_expiring, entry.endedBy); });
			}
			SweepRefusals();
			(void)sessionAdmissions.Sweep(nowUnix);
			if (steamWebApi) {
				for (const auto& verdict : steamWebApi->TakeResults()) {
					const auto tracked = runtimeConnections.find(verdict.connectionId);
					if (tracked == runtimeConnections.end() || tracked->second.profileId != verdict.profileId) continue;
					const bool held = identity != nullptr && identity->ReleaseSteamVerdictHold(verdict.connectionId);
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
						if (identity != nullptr) (void)identity->NoteSteamVerified(verdict.connectionId, verdict.steamId);
						const auto parked = identity != nullptr ? identity->TakeParkedHello(verdict.connectionId) : std::nullopt;
						if (parked) deliverToProtocol(verdict.connectionId, tracked->second, *parked, loopMonotonic, nowUnix, nullptr);
					} else if (held) {
						SendSessionEnded(server, verdict.connectionId, "steam_required", CommonwealthOnline::Server::kSteamRequiredReason, nowUnix);
						disconnectTracked(verdict.connectionId, "steam_required");
					}
				}
			}
			if (identity != nullptr) {
				for (const auto connectionId : identity->TakeExpiredSteamVerdictHolds()) {
					SendSessionEnded(server, connectionId, "steam_required", CommonwealthOnline::Server::kSteamRequiredReason, nowUnix);
					disconnectTracked(connectionId, "steam_required");
				}
			}
			steamAdmin->SweepIfDue(nowUnix);
			flushPersistence(false);
			if (persistence != nullptr) {
				auto progressionFrames = persistence->TakeProgressionRejections();
				sendFramesTracked(progressionFrames, loopMonotonic, nowUnix);
			}
			auto retryFrames = protocol.RetryComponentSnapshots();
			sendFramesTracked(retryFrames, loopMonotonic, nowUnix);
			auto authorityRetryFrames = protocol.RetryNpcAuthority(nowUnix);
			sendFramesTracked(authorityRetryFrames, loopMonotonic, nowUnix);
			auto stateBagResyncFrames = protocol.RetryStateBagFrames(loopMonotonic);
			sendFramesTracked(stateBagResyncFrames, loopMonotonic, nowUnix);
			auto pluginListFrames = protocol.RetryPluginLists();
			sendFramesTracked(pluginListFrames, loopMonotonic, nowUnix);
			auto heldRelayFrames = protocol.RetryHeldCombatRelays();
			sendFramesTracked(heldRelayFrames, loopMonotonic, nowUnix);
			auto downedFrames = protocol.PollDownedState(loopMonotonic);
			sendFramesTracked(downedFrames, loopMonotonic, nowUnix);
			deferredReplies.BeginMaintenance();
			auto captureFrames = protocol.PollSettlementCapture(loopMonotonic, nowUnix);
			sendFramesTracked(captureFrames, loopMonotonic, nowUnix);
			deferredReplies.EndMessage();
			auto inviteFrames = protocol.PollFactionInvites(loopMonotonic, nowUnix);
			sendFramesTracked(inviteFrames, loopMonotonic, nowUnix);
			deferredReplies.BeginMaintenance();
			protocol.RetryDirtyPersistence(loopMonotonic);
			auto workshopFlushFrames = protocol.FlushWorkshopPersistence(nowUnix, loopMonotonic);
			sendFramesTracked(workshopFlushFrames, loopMonotonic, nowUnix);
			deferredReplies.EndMessage();
			auto durableFrames = protocol.PollDurablePlayerState(loopMonotonic, nowUnix);
			sendFramesTracked(durableFrames, loopMonotonic, nowUnix);
			auto friendExpiryFrames = protocol.PollFriendRequests(loopMonotonic, nowUnix);
			sendFramesTracked(friendExpiryFrames, loopMonotonic, nowUnix);
			auto tradeFrames = protocol.PollTrades(loopMonotonic);
			sendFramesTracked(tradeFrames, loopMonotonic, nowUnix);
			auto fireteamFrames = protocol.PollFireteamInvites(nowUnix);
			sendFramesTracked(fireteamFrames, loopMonotonic, nowUnix);
			auto assistFrames = protocol.PollQuestAssists(loopMonotonic, nowUnix);
			sendFramesTracked(assistFrames, loopMonotonic, nowUnix);
			auto teamQuestFrames = protocol.PollTeamQuestSync(loopMonotonic, nowUnix);
			sendFramesTracked(teamQuestFrames, loopMonotonic, nowUnix);
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
