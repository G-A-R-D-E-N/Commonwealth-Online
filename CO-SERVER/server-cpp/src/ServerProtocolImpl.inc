	public:
		bool allowDeathBagLootByOthers{ false };
		Impl(
			std::string a_serverName,
			std::string a_serverDescription,
			std::vector<std::string> a_adminProfiles,
			bool a_friendlyFire,
			bool a_factionFriendlyFire) :
			serverName(std::move(a_serverName)),
			serverDescription(std::move(a_serverDescription)),
			adminProfiles(std::move(a_adminProfiles)),
			friendlyFire(a_friendlyFire),
			factionFriendlyFire(a_factionFriendlyFire)
		{}

		std::vector<OutboundFrame> HandleConnected(std::uint32_t a_connectionId, double a_unixTimeSeconds)
		{
			Session session;
			ReleaseExpiredHolds(a_unixTimeSeconds);
			const auto playerId = NextPlayerId();
			if (!playerId) return {};
			session.playerId = *playerId;
			sessions[a_connectionId] = session;
			events.Notify(ServerEvent{ ServerEventKind::PlayerJoined, a_connectionId, session.playerId, session.profileId, nullptr });

			Json capabilities = BuildCapabilities();
			Json welcome{
				{ "type", "welcome" },
				{ "playerId", session.playerId },
				{ "serverTime", a_unixTimeSeconds },
				{ "serverName", serverName },
				{ "serverDescription", serverDescription },
				{ "friendlyFire", friendlyFire },
				{ "stimpaksPerRevive", downedStimpaksPerRevive },
				{ "factionFriendlyFire", factionFriendlyFire },
				{ "survivalMode", survivalMode },
				{ "survivalNeeds", SurvivalNeedsActive() },
				{ "protocolVersion", kProtocolVersion },
				{ "capabilities", std::move(capabilities) }
			};
			if (!serverInstanceId.empty()) {
				welcome["serverInstanceId"] = serverInstanceId;
			}
			return { ReliableFrame(a_connectionId, welcome) };
		}

		std::vector<OutboundFrame> HandleDisconnected(std::uint32_t a_connectionId, double a_unixTimeSeconds, bool a_cleanGoodbye = false, bool a_markDeparture = true)
		{
			const auto found = sessions.find(a_connectionId);
			if (found == sessions.end()) {
				return {};
			}
			if (a_markDeparture && inventoryAuthority != nullptr && !found->second.questProfile.empty()) {
				try { (void)inventoryAuthority->Store().MarkDeathBagDeparted(found->second.questProfile, a_cleanGoodbye); } catch (const std::exception&) {}
			}
			const auto playerId = found->second.playerId;
			const auto playerIncarnation = found->second.playerIncarnation;
			const bool wasActive = found->second.active;
			const bool wasHost = wasActive && worldStateHostPlayerId == playerId;
			if (wasActive && !found->second.profileId.empty() && found->second.compatibilityCapabilities.Has(F4T::Identity::ReconnectResumeCapability)) {
				HoldPlayer(found->second.profileId, playerId, found->second.socialKey, a_unixTimeSeconds);
			}
			(void)found->second.lifecycle.MoveTo(F4T::Session::LifecycleState::Disconnected);

			std::vector<OutboundFrame> frames;
			if (wasActive) {
				found->second.active = false;
				AppendFrames(frames, downedService.Leave(DownedPeers(), playerId));
				if (compatibilityMode) {
					AppendFrames(frames, RemoveCompatibilityEntity(playerId));
					RemoveCompatibilityPeerRoutes(playerId);
				}
				peerVisibility.DespawnEntity(playerId);
				peerVisibility.RemovePeer(playerId);
				peerSnapshots.erase(playerId);
			}
			events.Notify(ServerEvent{ ServerEventKind::PlayerLeft, a_connectionId, playerId, found->second.profileId, nullptr });
			sessions.erase(found);
			sleepRefusedLogMonotonic.erase(playerId);
			workshopStructureDamageBudgets.erase(a_connectionId);
			AppendFrames(frames, EndTradesFor(playerId));

			if (wasActive) {
				for (auto& [connectionId, recipient] : sessions) {
					if (recipient.active) {
						frames.push_back(ReliableFrame(connectionId, Json{
							{ "type", "disconnect" },
							{ "playerId", playerId },
							{ "playerIncarnation", playerIncarnation },
							{ "visibilitySequence", ++recipient.outboundVisibilitySequence },
							{ "serverTime", a_unixTimeSeconds }
						}));
					}
				}
				fireteamService.NotePeers(FireteamPeers());
				AppendFrames(frames, fireteamService.DisconnectFrames(playerId, a_unixTimeSeconds));
				DropDisbandedFireteamTracks();
			}
			AppendFrames(frames, StateBagDisconnectFrames(stateBag, sessions, playerId));
			AppendFrames(frames, ReconcileNpcAuthority(a_unixTimeSeconds));

			if (wasHost) {
				std::vector<std::uint32_t> eligible;
				for (const auto& [connectionId, session] : sessions) {
					(void)connectionId;
					if (session.active) eligible.push_back(session.playerId);
				}
				worldStateHostPlayerId = F4T::Authority::ElectSticky(playerId, std::span<const std::uint32_t>(eligible));
				if (worldStateHostPlayerId != 0) {
					AppendFrames(frames, BroadcastReliable(WorldStateHostPacket(worldStateHostPlayerId, a_unixTimeSeconds)));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> HandleMessage(
			std::uint32_t a_connectionId,
			std::span<const std::uint8_t> a_message,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			if (F4T::Voice::HasVoicePrefix(std::as_bytes(a_message))) return voiceService.HandleFrame(a_connectionId, a_message, VoicePeers(sessions), a_monotonicSeconds);
			auto sessionIt = sessions.find(a_connectionId);
			if (sessionIt == sessions.end()) {
				return {};
			}
			auto& session = sessionIt->second;
			stats.packetsReceived++;

			std::span<const std::uint8_t> payload = a_message;
			std::optional<Family> envelopeFamily;
			F4T::GnsSnapshotEnvelope::Header envelopeHeader{};
			const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_message, envelopeHeader);
			if (decode != DecodeResult::NotEnvelope) {
				if (decode != DecodeResult::Valid) {
					RejectSnapshot();
					return {};
				}
				envelopeFamily = envelopeHeader.family;
				if (a_message.size() > MaximumBytesForFamily(envelopeHeader.family) + F4T::GnsSnapshotEnvelope::HeaderSize) {
					RejectSnapshot();
					return {};
				}
				const auto index = FamilyIndex(envelopeHeader.family);
				if (!F4T::GnsSnapshotEnvelope::IsNewer(envelopeHeader.sequence, session.inboundSnapshotSequences[index])) {
					RejectSnapshot();
					return {};
				}
				payload = a_message.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
			}

			auto packet = ParsePacket(payload);
			if (!packet) {
				Reject();
				return {};
			}
			const auto type = ReadString(*packet, "type");
			if (!type || type->empty()) {
				Reject();
				return {};
			}
			if (payload.size() > MaximumBytesForType(*type)) {
				if (*type == "transform") {
					stats.transformOversizeRejected++;
				}
				Reject();
				return {};
			}

			if (envelopeFamily) {
				const auto expected = FamilyForPacketType(*type);
				if (!expected || *expected != *envelopeFamily) {
					RejectSnapshot();
					return {};
				}
			}

			if (*type == "hello") {
				return HandleHello(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			if (*type == "keepAlive") {
				const auto exempt = packet->find("afkExempt");
				if (exempt != packet->end() && !exempt->is_boolean()) {
					Reject();
					return {};
				}
				session.afkExemption = Afk::NextExemption(session.afkExemption, exempt != packet->end() && exempt->get<bool>(), a_unixTimeSeconds);
				return {};
			}
			if (!session.active) {
				Reject();
				return {};
			}
			const Activity::PlayerActivityCommit activityCommit{ sessions, a_connectionId, Activity::IsPlayerActivity(*type, *packet), stats.packetsRejected, stats.chatRejected };
			if (*type == "pluginList") {
				return HandlePluginList(a_connectionId, session, *packet, a_unixTimeSeconds);
			}
			const auto commitSequence = [&](std::vector<OutboundFrame> a_frames) {
				if (envelopeFamily) {
					session.inboundSnapshotSequences[FamilyIndex(*envelopeFamily)] = envelopeHeader.sequence;
				}
				return a_frames;
			};

			if (*type == "visibilityPause") {
				const auto paused = packet->find("paused");
				const auto epoch = packet->find("movementEpoch");
				if (paused == packet->end() || !paused->is_boolean() || epoch == packet->end() || !epoch->is_number_unsigned()) {
					Reject();
					return {};
				}
				const auto value = epoch->get<std::uint64_t>();
				if (value == 0 || value <= session.movementEpoch) {
					return {};
				}
				session.movementEpoch = value;
				session.lastMovementSequence = 0;
				session.acceptedResumeTransitionId = 0;
				session.acknowledgedResumeTransitionId = 0;
				session.visibilityPaused = paused->get<bool>();
				session.resumeAwaitingTransform = !session.visibilityPaused;
				if (!session.visibilityPaused) {
					return {};
				}
				if (session.lastMovement) {
					session.pausedMovement = session.lastMovement;
					session.lastMovement.reset();
				}
				session.objectScanPoint.reset();
				auto frames = ReconcilePeerVisibility(a_connectionId, session, a_unixTimeSeconds, false);
				if (inventoryAuthority != nullptr) AppendFrames(frames, RefreshInventoryProjection());
				AppendFrames(frames, ReconcileNpcAuthority(a_unixTimeSeconds));
				return frames;
			}
			if (*type == "transform") {
				std::uint64_t movementSequence = 0;
				if (session.movementEpoch != 0) {
					const auto epoch = packet->find("movementEpoch");
					const auto sequence = packet->find("movementSequence");
					if (session.visibilityPaused || epoch == packet->end() || !epoch->is_number_unsigned() ||
						epoch->get<std::uint64_t>() != session.movementEpoch || sequence == packet->end() || !sequence->is_number_unsigned()) {
						return {};
					}
					movementSequence = sequence->get<std::uint64_t>();
					if (movementSequence <= session.lastMovementSequence) {
						return {};
					}
				}
				const auto before = stats.movementRejected;
				auto frames = HandleTransform(a_connectionId, session, std::move(*packet), a_monotonicSeconds, a_unixTimeSeconds);
				if (stats.movementRejected != before) {
					return frames;
				}
				if (movementSequence != 0) {
					session.lastMovementSequence = movementSequence;
				}
				return commitSequence(std::move(frames));
			}
			if (*type == "playerState") {
				return HandlePlayerState(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			if (*type == "worldState") {
				return HandleWorldState(a_connectionId, session, *packet, a_unixTimeSeconds);
			}
			if (*type == "npcState") {
				const auto before = stats.npcStateRejected;
				auto frames = HandleNpcState(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
				return stats.npcStateRejected == before ? commitSequence(std::move(frames)) : std::move(frames);
			}
			if (auto frames = DispatchWorldObjectPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (*type == "combatHit") {
				return HandleCombatHit(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			if (*type == "workshopOp") return HandleWorkshopOp(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (*type == "workshopSnapshotRequest") {
				return HandleWorkshopSnapshotRequest(a_connectionId, session, *packet, a_monotonicSeconds);
			}
			if (*type == "campFound") return HandleCampFound(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (*type == "campMove") return HandleCampMove(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (*type == "campStore") return HandleCampStore(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (*type == "campRemove") return HandleCampRemove(a_connectionId, session, *packet, a_monotonicSeconds);
			if (*type == "campGrantAck") return HandleCampGrantAck(session, *packet);
			if (*type == "structureHit") return HandleStructureHit(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (*type == "structureRepair") return HandleStructureRepair(a_connectionId, session, *packet, a_monotonicSeconds);
			if (*type == "structureRebuild") return HandleStructureRebuild(a_connectionId, session, *packet, a_monotonicSeconds);
			if (*type == "workbenchRepair") return HandleWorkbenchRepair(a_connectionId, session, *packet, a_monotonicSeconds);
			if (*type == "upkeepPay") return HandleUpkeepPay(a_connectionId, session, *packet, a_monotonicSeconds);
			if (*type == "settlementChargeResult") return HandleSettlementChargeResult(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (*type == "raidDeclare") return HandleRaidDeclare(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (*type == "settlementWindowSet") return HandleSettlementWindowSet(a_connectionId, session, *packet, a_monotonicSeconds);
			if (*type == "wireOp") return HandleWireOp(a_connectionId, session, *packet);
			if (*type == "doorOp") return HandleDoorOp(a_connectionId, session, *packet);
			if (*type == "settlerRecruit") return HandleSettlerRecruit(a_connectionId, session, *packet, a_monotonicSeconds);
			if (*type == "settlerAssign" || *type == "settlerRecover") {
				return HandleSettlerAssign(a_connectionId, session, *packet, a_unixTimeSeconds);
			}
			if (auto frames = DispatchSettlementPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (*type == "projectileAim" || *type == "playerAnimEvent") return CommonwealthOnline::Server::ShotReplayRelay::HandleShotReplay(sessions, a_connectionId, session, *packet, a_monotonicSeconds, stats);
			if (*type == "explosionSpawn") return CommonwealthOnline::Server::ShotReplayRelay::HandleExplosionSpawn(sessions, a_connectionId, session, *packet, a_monotonicSeconds, stats, { friendlyFire, factionFriendlyFire, [this](const std::string& a_viewer, const std::string& a_other) { return factionService.SameFaction(a_viewer, a_other); } });
			if (*type == "furnitureUse") return CommonwealthOnline::Server::FurnitureRelay::HandleFurnitureUse(sessions, a_connectionId, session, *packet, a_monotonicSeconds, stats);
			if (*type == "sleepUse") return SleepUseWithNeeds(a_connectionId, session, *packet, a_monotonicSeconds);
			if (*type == "needsConsume") return HandleNeedsConsume(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (*type == "difficultyReport") return HandleDifficultyReport(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			if (auto frames = DispatchNpcCoopPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchChatPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchFactionPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchFriendPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchPrivacyPacket(*type, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (*type == "reportSubmit") {
				return reportService.Submit(a_connectionId, session, sessions, *packet, a_monotonicSeconds, a_unixTimeSeconds, [this](const Session& a_candidate) { return IsLiveAdminSession(a_candidate); });
			}
			if (auto frames = DispatchFireteamPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchQuestPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchDownedPacket(*type, session, *packet, a_monotonicSeconds)) return std::move(*frames);
			if (*type == "voiceState" && session.compatibilityCapabilities.Has("voice-v1")) return voiceService.HandleState(a_connectionId, session.voice, *packet, a_monotonicSeconds);
			if (*type == "resourceMessage") return HandleResourceMessage(session, *packet, a_monotonicSeconds, stats, resourceMessageHandler);
			if (auto frames = DispatchCharacterSlotPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchAdminPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (*type == "adminItemResult") {
				return HandleAdminItemResult(session, *packet);
			}
			if (*type == "inventoryReport") {
				return HandleInventoryReport(session, *packet, a_unixTimeSeconds);
			}

			Reject();
			return {};
		}

		static FactionVitals SessionVitals(const Session& a_session)
		{
			FactionVitals vitals;
			if (a_session.visibilityPaused || a_session.resumeAwaitingTransform || !a_session.lastDurablePlayerState) return vitals;
			const auto found = a_session.lastDurablePlayerState->find("vitals");
			if (found == a_session.lastDurablePlayerState->end() || !found->is_object()) return vitals;
			const auto health = found->find("healthPercent");
			if (health == found->end() || !health->is_number_integer()) return vitals;
			const auto percent = health->get<std::int64_t>();
			if (percent < 0 || percent > 100) return vitals;
			vitals.healthPercent = static_cast<std::uint32_t>(percent);
			vitals.known = true;
			if (const auto downed = found->find("isDowned"); downed != found->end() && downed->is_boolean()) vitals.downed = downed->get<bool>();
			if (const auto dead = found->find("isDead"); dead != found->end() && dead->is_boolean()) vitals.dead = dead->get<bool>();
			return vitals;
		}

		FactionService::Peers FactionPeers() const
		{
			FactionService::Peers peers;
			for (const auto& [connectionId, session] : sessions) {
				if (session.active) peers.push_back({ connectionId, session.playerId, session.profileId, std::string{ SessionNameView(session) }, SessionHasSocialNames(session), SessionVitals(session), std::string{ SessionSocialKeyView(session) }, session.privacy.onlineStatus == F4T::PrivacyValidation::OnlineStatus::Nobody });
			}
			return peers;
		}

		std::optional<std::vector<OutboundFrame>> DispatchFactionPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			if (!factionService.Enabled() || (a_type != "playerRosterRequest" && !a_type.starts_with("faction"))) return std::nullopt;
			const FactionPeer self{ a_connectionId, a_session.playerId, a_session.profileId, std::string{ SessionNameView(a_session) }, SessionHasSocialNames(a_session), {}, std::string{ SessionSocialKeyView(a_session) } };
			std::vector<std::string> leaveMembers;
			std::string leaveProfileId;
			if (a_type == "factionLeave") {
				leaveProfileId = a_session.profileId;
				if (const auto* members = factionService.MembersOf(a_session.profileId)) {
					leaveMembers = *members;
				}
			} else if (a_type == "factionKick") {
				if (const auto target = a_packet.find("targetProfileId"); target != a_packet.end() && target->is_string()) {
					leaveProfileId = target->get<std::string>();
					if (const auto* members = factionService.MembersOf(a_session.profileId)) {
						leaveMembers = *members;
					}
				}
			}
			const std::function<bool()> allow = [&] { return events.Allow(ServerEvent{ ServerEventKind::FactionAction, a_connectionId, a_session.playerId, a_session.profileId, &a_packet }); };
			const auto acceptIt = a_packet.find("accept");
			const bool joiningByInvite = a_type == "factionInviteResponse" && acceptIt != a_packet.end() && acceptIt->is_boolean() && acceptIt->get<bool>() &&
				factionService.FactionIdOf(a_session.profileId).empty();
			auto result = factionService.Dispatch(a_type, self, a_session.faction, FactionPeers(), a_packet, a_monotonicSeconds, a_unixTimeSeconds, allow);
			if (result && !leaveProfileId.empty() && !leaveMembers.empty() &&
				factionService.MembersOf(leaveProfileId) == nullptr) {
				NoteFactionLeaveRaidBlocks(leaveProfileId, leaveMembers, static_cast<std::uint64_t>(a_unixTimeSeconds));
			}
			if (result) MarkFactionTagChanges();
			if (joiningByInvite && result && !factionService.FactionIdOf(a_session.profileId).empty()) {
				AppendFrames(*result, FactionJoinRefreshFrames(a_connectionId, a_session, a_unixTimeSeconds));
			}
			return result;
		}

		std::vector<OutboundFrame> PairRefreshFrames(std::uint32_t a_leftConnectionId, const Session& a_left, std::uint32_t a_rightConnectionId, const Session& a_right, double a_unixTimeSeconds);
		std::optional<std::vector<OutboundFrame>> DispatchFriendPacket(const std::string& a_type, std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::optional<std::vector<OutboundFrame>> DispatchPrivacyPacket(const std::string& a_type, Session& a_session, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);

		std::vector<OutboundFrame> ExistingFriendFrames(std::uint32_t a_connectionId, const Session& a_target, double a_unixTimeSeconds);
		std::vector<OutboundFrame> FriendNameFrames(std::uint32_t a_connectionId, double a_unixTimeSeconds);
		std::vector<OutboundFrame> FriendDisconnectFrames(const std::string& a_profileId, double a_unixTimeSeconds);

		std::vector<OutboundFrame> PollFriendRequests(double a_monotonicSeconds, double a_unixTimeSeconds);

		std::vector<OutboundFrame> FactionJoinRefreshFrames(
			std::uint32_t a_connectionId,
			const Session& a_learner,
			double a_unixTimeSeconds);

		bool SameFactionAsPlayer(const std::string& a_profileId, std::uint32_t a_targetPlayerId) const
		{
			for (const auto& [connectionId, session] : sessions) {
				if (session.active && session.playerId == a_targetPlayerId) return factionService.SameFaction(a_profileId, session.profileId);
			}
			return false;
		}

		Json AdminListFactions() const
		{
			return factionService.AdminList();
		}

		std::vector<OutboundFrame> AdminDisbandFaction(std::string_view a_id, double a_now, std::string& a_failure) { auto frames = factionService.AdminDisband(FactionPeers(), a_id, a_now, a_failure); if (a_failure.empty()) MarkFactionTagChanges(); return frames; }
		std::vector<OutboundFrame> AdminPromoteFaction(std::string_view a_id, std::string_view a_target, double a_now, std::string& a_failure) { return factionService.AdminPromote(FactionPeers(), a_id, a_target, a_now, a_failure); }
		std::vector<OutboundFrame> AdminKickFaction(std::string_view a_id, std::string_view a_target, double a_now, std::string& a_failure) { auto frames = factionService.AdminKick(FactionPeers(), a_id, a_target, a_now, a_failure); if (a_failure.empty()) MarkFactionTagChanges(); return frames; }

		%:include "ServerWorkshopImpl.inc"

		void SetFactionLimits(F4T::ServerConfiguration::FactionLimits a_limits)
		{
			factionService.SetLimits(a_limits);
		}

		void SetDownedConfig(F4T::DownedState::Config a_config)
		{
			downedService.SetConfig(a_config);
			downedStimpaksPerRevive = a_config.stimpaksPerRevive;
		}

		std::vector<OutboundFrame> PollDurablePlayerState(double a_monotonicSeconds, double a_unixTimeSeconds);
		void MarkFactionTagChanges();
		void MaskRelayedName(Json& relay, const Session& a_viewer, const Session& a_subject);
		[[nodiscard]] std::string PlayerKeyOf(const Session& a_session) const;

		std::vector<OutboundFrame> PollDownedState(double a_monotonicSeconds)
		{
			return downedService.Poll(DownedPeers(), a_monotonicSeconds);
		}

		std::vector<OutboundFrame> PollPresence(double a_monotonicSeconds, double a_unixTimeSeconds);

		std::vector<OutboundFrame> AdminRevivePlayer(std::uint32_t a_playerId, double a_monotonicSeconds)
		{
			return downedService.AdminRevive(DownedPeers(), a_playerId, a_monotonicSeconds);
		}

		std::vector<OutboundFrame> AdminKillPlayer(std::uint32_t a_playerId)
		{
			return downedService.AdminKill(DownedPeers(), a_playerId);
		}

	private:
		std::optional<Json> ObservedDeathIntent(std::uint32_t a_playerId) const
		{
			if (inventoryAuthority == nullptr || !inventoryAuthority->SavingEnabled()) return std::nullopt;
			for (const auto& [connection, session] : sessions) {
				if (!session.active || session.playerId != a_playerId || session.visibilityPaused || session.resumeAwaitingTransform || !InventoryNegotiated(session) ||
					!session.lastMovement || !session.lastMovementMonotonic || !session.pluginListPacket ||
					!inventoryAuthority->InventorySaveReady(connection)) continue;
				const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
				const auto age = now - *session.lastMovementMonotonic;
				if (!std::isfinite(age) || age < 0.0 || age > kMaximumCombatPositionAgeSeconds) return std::nullopt;
				const auto data = session.pluginListPacket->value("data", std::string{});
				const auto plugins = F4T::LoadOrder::DecodePluginList(data);
				if (!plugins) return std::nullopt;
				const auto form = [](const std::string& a_text) -> std::optional<std::uint32_t> {
					if (a_text.empty() || a_text.size() > 8) return std::nullopt;
					std::uint32_t id{};
					const auto [end, error] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), id, 16);
					if (error != std::errc{} || end != a_text.data() + a_text.size()) return std::nullopt;
					return id;
				};
				const auto& position = *session.lastMovement;
				const auto cellId = form(position.cellId);
				const auto worldId = position.worldspaceId.empty() ? std::optional<std::uint32_t>{ 0u } : form(position.worldspaceId);
				if (!cellId || !worldId) return std::nullopt;
				const auto cell = F4T::LoadOrder::ResolveStableFormKey(*plugins, *cellId);
				const auto world = *worldId == 0 ? std::optional<std::string>{} : F4T::LoadOrder::ResolveStableFormKey(*plugins, *worldId);
				if (!cell || (*worldId != 0 && !world)) return std::nullopt;
				const auto profile = inventoryAuthority->ProfileForConnection(connection);
				if (!profile || *profile != SessionSocialKeyView(session) || session.questProfile.empty()) return std::nullopt;
				const auto inventory = inventoryAuthority->Store().GetInventory(*profile);
				if (!inventory || !inventory->contains("revision")) return std::nullopt;
				return Json{ { "version", 1u }, { "profileId", *profile }, { "characterId", session.questProfile }, { "inventoryRevision", inventory->at("revision") },
					{ "cellKey", *cell }, { "worldspaceKey", world ? Json(*world) : Json(nullptr) },
					{ "position", Json::array({ position.x, position.y, position.z }) },
					{ "state", "dead" }, { "deadlineUnix", nullptr }, { "locationSource", "client-reported" } };
			}
			return std::nullopt;
		}

		DownedService::Peers DownedPeers() const
		{
			DownedService::Peers peers;
			for (const auto& [connectionId, session] : sessions) {
				if (!session.active) continue;
				DownedPeer peer{ connectionId, session.playerId, {}, session.profileId };
				peer.persistenceProfile = session.questProfile;
				peer.factionId = factionService.FactionIdOf(session.profileId);
				peer.visibilitySuppressed = session.visibilityPaused || session.resumeAwaitingTransform;
				peer.playerIncarnation = session.playerIncarnation;
				if (session.lastMovement && session.lastMovementMonotonic) {
					const auto& movement = *session.lastMovement;
					peer.position = DownedPosition{ movement.x, movement.y, movement.z, movement.cellId, movement.worldspaceId, *session.lastMovementMonotonic };
				}
				peers.push_back(std::move(peer));
			}
			return peers;
		}

		std::optional<std::vector<OutboundFrame>> DispatchDownedPacket(
			const std::string& a_type,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			if (a_type != "reviveRequest" && a_type != "respawnRequest" &&
				a_type != "reviveCancel" && a_type != "checkpoint") return std::nullopt;
			const bool reportsStimpaks = a_session.compatibilityCapabilities.Has(F4T::DownedState::StimpakCountCapability);
			return downedService.Dispatch(a_type, DownedPeers(), a_session.playerId, a_session.downed, a_packet, a_monotonicSeconds, kMaximumCombatPositionAgeSeconds, reportsStimpaks);
		}

		public:
		std::optional<std::vector<OutboundFrame>> DispatchAdminPacket(const std::string& a_type, std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		bool IsLiveAdminSession(const Session& a_session) const;
		bool IsConsoleAdmin(std::string_view a_profileId) const;
		void BindSessionProfileVerified(std::uint32_t a_connectionId, std::string_view a_profileId, bool a_verified, std::string_view a_steamId);
		void BindSessionSteamId(std::uint32_t a_connectionId, std::string_view a_steamId);

		%:include "ServerLoadOrder.inc"

		%:include "ServerWorldPolicy.inc"

		bool ConsumeAdminOp(Session& a_session, std::uint32_t a_opId)
		{
			const auto found = std::find_if(a_session.outstandingAdminOps.begin(), a_session.outstandingAdminOps.end(), [a_opId](const OutstandingAdminOp& a_op) { return a_op.opId == a_opId; });
			if (found == a_session.outstandingAdminOps.end()) {
				return false;
			}
			a_session.outstandingAdminOps.erase(found);
			return true;
		}

		bool ConsumeAdminOpFromRaw(Session& a_session, const Json& a_packet)
		{
			const auto opId = ReadUInt32(a_packet, "opId");
			return opId && *opId >= 1 && ConsumeAdminOp(a_session, *opId);
		}

		std::vector<OutboundFrame> HandleAdminItemResult(Session& a_session, const Json& a_packet)
		{
			ExpireAdminOps(a_session, std::chrono::steady_clock::now());
			stats.adminItemResultsReceived++;
			auto result = F4T::AdminItemValidation::NormalizeAdminItemResult(a_packet);
			const bool consumed = result ? ConsumeAdminOp(a_session, (*result)["opId"].get<std::uint32_t>()) : ConsumeAdminOpFromRaw(a_session, a_packet);
			if (!result || !consumed) {
				Reject();
				stats.adminItemResultsRejected++;
				return {};
			}
			a_session.lastAdminItemResult = AdminItemResultSnapshot{
				(*result)["opId"].get<std::uint32_t>(),
				(*result)["ok"].get<bool>(),
				(*result)["applied"].get<std::uint32_t>(),
				(*result)["error"].get<std::string>()
			};
			return {};
		}

		std::vector<OutboundFrame> HandleInventoryReport(Session& a_session, const Json& a_packet, double a_unixTimeSeconds)
		{
			ExpireAdminOps(a_session, std::chrono::steady_clock::now());
			stats.inventoryReportsReceived++;
			auto report = F4T::AdminItemValidation::NormalizeInventoryReport(a_packet);
			const bool consumed = report ? ConsumeAdminOp(a_session, (*report)["opId"].get<std::uint32_t>()) : ConsumeAdminOpFromRaw(a_session, a_packet);
			if (!report || !consumed) {
				Reject();
				stats.inventoryReportsRejected++;
				return {};
			}
			a_session.inventoryReport = InventoryReportSnapshot{
				(*report)["opId"].get<std::uint32_t>(),
				(*report)["truncated"].get<bool>(),
				std::move((*report)["items"]),
				a_unixTimeSeconds
			};
			return {};
		}

		std::size_t ActiveCount() const noexcept
		{
			return static_cast<std::size_t>(std::count_if(
				sessions.begin(), sessions.end(), [](const auto& entry) { return entry.second.active; }));
		}

		Json ScriptPlayers() const;

		std::vector<OutboundFrame> PublishComponentSnapshot(const SnapshotState& a_state);

		bool RegisterCompatibilityAdapter(Compatibility::FamilyAdapter a_adapter);

		bool SetComponentDefinitions(F4T::Replication::FrozenComponentDefinitionTable a_definitions);

		bool SetDirectComponentPublicationContract(DirectComponentPublicationContract a_contract);

		CompatibilityRouteResult SetCompatibilityRecipients(
			EntityId a_entityId,
			Family a_family,
			std::span<const std::uint32_t> a_exactRecipients);

		CompatibilityPublishResult PublishCompatible(const CompatiblePublication& a_publication);

		std::vector<OutboundFrame> RemoveCompatibilityEntity(EntityId a_entityId);

		void FrameDeliveryCompleted(const OutboundFrame& a_frame, bool a_sent);

		std::vector<OutboundFrame> RetryComponentSnapshots();

		bool EnableInventoryTransactions(PersistenceGate& a_persistence);
		std::vector<OutboundFrame> HandleInventorySnapshotRequest(std::uint32_t a_connectionId, Session& a_session,
			const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> HandleStartingInventoryGrantAck(std::uint32_t a_connectionId, const Json& a_packet);
		std::vector<OutboundFrame> HandleStartingInventoryBegin(std::uint32_t a_connectionId, const Json& a_packet);
		std::vector<OutboundFrame> HandleStartingInventoryVerify(std::uint32_t a_connectionId, const Json& a_packet);

		std::string serverName;
		std::string serverDescription;
		std::string playerKeySecret;
		std::string serverInstanceId;
		std::vector<std::string> adminProfiles;
		std::function<bool(std::string_view)> adminLookup;
		bool friendlyFire{ true };
		std::uint32_t downedStimpaksPerRevive{ F4T::DownedState::StimpaksPerRevive };
		bool factionFriendlyFire{};
		bool survivalMode{ true };
		bool survivalNeeds{ true };
		bool requireDifficultyReport{ true };
		bool difficultyResetPending{};
		std::unordered_map<std::uint32_t, Session> sessions;
		std::uint32_t nextPlayerId{ 1 };
		std::uint64_t nextPlayerIncarnation{};
		std::uint32_t worldStateHostPlayerId{};
		std::optional<std::string> lockedWeatherFormId;
		SessionTypes::WorldDaysBaseline worldDaysBaseline;
		std::unordered_map<std::string, SessionTypes::HeldPlayer> heldPlayers;
		F4T::NpcAuthority::Manager npcAuthority;
		std::map<Scope, Json> lastNpcStateByScope;
		std::map<Scope, double> lastNpcStateMonotonicByScope;
		NpcLedger::Ledger npcLedger;
		F4T::Replication::PeerEntitySet peerVisibility;
		F4T::WorldPolicy::Policy worldPolicy;
		F4T::WorldPolicy::Policy worldPolicyV1;
		SnapshotState dropState;
		std::vector<PendingViolation> violations;
		PersistenceGate* inventoryAuthority{};
		SnapshotState directAuthoritativeState;
		std::unordered_map<PeerId, std::array<PeerSnapshotSender, 2>> peerSnapshots;
		Compatibility::CompatibilityRegistry compatibilityRegistry;
		bool compatibilityMode{};
		bool hasEverActivatedSession{};
		std::set<CompatibilityRouteKey> compatibilityRoutes;
		std::map<ComponentKey, CompatibilityProjectionState> compatibilityProjections;
		ProtocolStats stats{};
		DownedService downedService{ stats, &events };
		AdminMenuService adminMenu;
		SocialGraph socialGraph{ stats };
		FactionService factionService{ stats, socialGraph };
		FriendService friendService{ stats, socialGraph };
		ReportService reportService{ stats };
		ChatService chatService{ stats, factionService, socialGraph };
		FireteamService fireteamService;
		VoiceService voiceService{ stats };
		double nextPresenceMonotonic{};
		bool mapPublicPlayers{ true };
		std::optional<F4T::Replication::FrozenComponentDefinitionTable> componentDefinitions;
		std::optional<DirectComponentPublicationContract> directPublicationContract;

	private:
		std::optional<std::uint32_t> NextPlayerId()
		{
			for (std::uint32_t attempt = 0; attempt < (std::numeric_limits<std::uint16_t>::max)(); ++attempt) {
				if (nextPlayerId == 0 || nextPlayerId > (std::numeric_limits<std::uint16_t>::max)()) nextPlayerId = 1;
				const auto id = nextPlayerId++;
				if (std::none_of(sessions.begin(), sessions.end(), [id](const auto& session) { return session.second.playerId == id; }) && !IsHeldPlayerId(id)) return id;
			}
			return std::nullopt;
		}

		void HoldPlayer(const std::string& a_profileId, std::uint32_t a_playerId, const std::string& a_persistenceProfile, double a_unixTimeSeconds)
		{
			ReleaseExpiredHolds(a_unixTimeSeconds);
			heldPlayers.insert_or_assign(a_profileId, SessionTypes::HeldPlayer{ a_playerId, a_unixTimeSeconds + SessionTypes::kReconnectGraceSeconds, a_persistenceProfile });
			while (heldPlayers.size() > SessionTypes::kHeldPlayersPerMaximumPlayer * maximumPlayers) {
				heldPlayers.erase(std::min_element(heldPlayers.begin(), heldPlayers.end(), [](const auto& a_lhs, const auto& a_rhs) { return a_lhs.second.expiresAtUnixSeconds < a_rhs.second.expiresAtUnixSeconds; }));
			}
		}

		bool IsHeldPlayerId(std::uint32_t a_playerId) const
		{
			return std::any_of(heldPlayers.begin(), heldPlayers.end(), [a_playerId](const auto& a_held) { return a_held.second.playerId == a_playerId; });
		}

		void ReleaseExpiredHolds(double a_unixTimeSeconds)
		{
			std::erase_if(heldPlayers, [a_unixTimeSeconds](const auto& a_held) { return a_held.second.expiresAtUnixSeconds <= a_unixTimeSeconds; });
		}

		void Reject()
		{
			stats.packetsRejected++;
		}

		void RejectSnapshot()
		{
			stats.packetsRejected++;
			stats.snapshotsRejected++;
		}

		void RejectNpcAuthority()
		{
			stats.packetsRejected++;
			stats.npcAuthorityRejected++;
		}

		OutboundFrame PositionCorrectionFrame(
			std::uint32_t a_connectionId,
			const StoredMovementState& a_previous,
			std::string_view a_reason,
			double a_unixTimeSeconds) const;

		OutboundFrame SnapshotFrame(std::uint32_t a_connectionId, Session& a_recipient, Family a_family, const Json& a_packet);

		std::vector<OutboundFrame> BroadcastReliable(const Json& a_packet);

		std::optional<std::pair<std::uint32_t, Session*>> FindActiveSession(PeerId a_peerId)
		{
			for (auto& [connectionId, session] : sessions) {
				if (session.active && session.playerId == a_peerId) {
					return std::pair{ connectionId, std::addressof(session) };
				}
			}
			return std::nullopt;
		}

		const DirectComponentPublicationPeer* FindDirectPublicationPeer(PeerId a_peerId) const;

		bool FamiliesAllowed(const SnapshotState& a_state, const DirectComponentPublicationPeer& a_peer) const;

		bool PeerUsesComponentFamily(const Session& a_session, Family a_family) const;

		CompatibilityView BuildCompatibilityView(PeerId a_peerId, const Session& a_session, F4T::Replication::Channel a_channel);


		%:include "ServerSnapshotDelivery.inc"

		std::vector<OutboundFrame> ReconcileCompatibilityPeer(PeerId a_peerId);


		RouteUpdateResult UpdateCompatibilityRecipients(
			EntityId a_entityId,
			Family a_family,
			std::span<const std::uint32_t> a_exactRecipients,
			bool a_reconcileAdded);

		void RemoveCompatibilityPeerRoutes(PeerId a_peerId);

		void PruneCompatibilityRevisions(PeerId a_peerId, EntityId a_entityId);

		std::vector<OutboundFrame> ReconcilePeerVisibility(std::uint32_t a_connectionId, Session& a_self, double a_unixTimeSeconds, bool a_sendTransformOnShow);

		std::vector<OutboundFrame> ExistingTransformFrames(
			std::uint32_t a_connectionId,
			Session& a_target,
			double a_unixTimeSeconds);

		std::vector<OutboundFrame> ExistingPlayerStateFrames(
			std::uint32_t a_connectionId,
			const Session& a_target,
			double a_unixTimeSeconds);

		std::vector<OutboundFrame> ExistingNpcStateFrames(
			std::uint32_t a_connectionId,
			Session& a_target,
			double a_unixTimeSeconds);

		std::vector<OutboundFrame> ReconcileNpcAuthority(double a_unixTimeSeconds);

		std::vector<OutboundFrame> HandleHello(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);

		std::vector<OutboundFrame> HandleTransform(
			std::uint32_t a_connectionId,
			Session& a_session,
			Json a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);

		std::vector<OutboundFrame> HandlePlayerState(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);

		std::vector<OutboundFrame> HandleWorldState(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_unixTimeSeconds);

		std::vector<OutboundFrame> HandleNpcState(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			auto normalized = NormalizeNpcState(a_packet);
			if (!normalized) {
				Reject();
				stats.npcStateRejected++;
				return {};
			}
			const auto scope = ScopeFromPacket(*normalized);
			const auto epoch = ReadUInt32(*normalized, "authorityEpoch");
			if (!scope || !epoch || *epoch == 0) {
				RejectNpcAuthority();
				stats.npcStateRejected++;
				return {};
			}
			if (!npcAuthority.Authorize(a_session.playerId, *scope, *epoch)) {
				RejectNpcAuthority();
				stats.npcStateRejected++;
				return {};
			}
			for (const auto& npc : (*normalized)["npcs"]) {
				if (ReadString(npc, "cellId").value_or("") != scope->cellId ||
					ReadString(npc, "worldspaceId").value_or("") != scope->worldspaceId) {
					RejectNpcAuthority();
					stats.npcStateRejected++;
					return {};
				}
			}
			(*normalized)["authorityCellId"] = scope->cellId;
			(*normalized)["authorityWorldspaceId"] = scope->worldspaceId;
			(*normalized)["playerId"] = a_session.playerId;
			(*normalized)["serverTime"] = a_unixTimeSeconds;
			(*normalized)["fullReplace"] = true;
			std::vector<NpcLedger::Ledger::NpcObservation> observations;
			for (const auto& npc : (*normalized)["npcs"]) {
				const auto npcId = ReadUInt32(npc, "npcId").value_or(0);
				const auto sourceFormId = ReadString(npc, "sourceFormId").value_or("");
				if (npcId == 0 || sourceFormId.empty()) {
					continue;
				}
				observations.push_back(NpcLedger::Ledger::NpcObservation{
					npcId,
					sourceFormId,
					ReadDouble(npc, "x").value_or(0.0),
					ReadDouble(npc, "y").value_or(0.0),
					ReadDouble(npc, "z").value_or(0.0),
					npc.contains("isDead") ? npc.value("isDead", false) : ReadDouble(npc, "health").value_or(1.0) <= 0.0 });
			}
			npcLedger.ObserveSnapshot(*scope, *epoch, observations, a_monotonicSeconds);
			lastNpcStateByScope[*scope] = *normalized;
			lastNpcStateMonotonicByScope[*scope] = a_monotonicSeconds;
			std::vector<OutboundFrame> frames;
			for (auto& [connectionId, recipient] : sessions) {
				if (connectionId == a_connectionId || !recipient.active) {
					continue;
				}
				const auto recipientInterestScope = InterestScopeFromMovement(recipient.lastMovement);
				if (!recipientInterestScope) {
					continue;
				}
				Json relay = *normalized;
				Json filtered = Json::array();
				for (const auto& npc : (*normalized)["npcs"]) {
					if (F4T::InterestScope::StatesShareInterest(InterestScopeFromNpc(npc), recipientInterestScope)) {
						filtered.push_back(npc);
					}
				}
				relay["npcs"] = std::move(filtered);
				auto frame = SnapshotFrame(connectionId, recipient, Family::NpcState, relay);
				if (!frame.payload.empty()) {
					frames.push_back(std::move(frame));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> HandleCombatHit(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds);

		%:include "ServerNpcHitImpl.inc"
	%:include "ServerOwnedInventory.inc"
