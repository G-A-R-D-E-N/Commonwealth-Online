		struct PeerSnapshotSender
		{
			SnapshotState baseline;
			SnapshotState lastPublished;
			std::uint32_t baselineSequence{};
			std::uint32_t lastSnapshotSequence{};
			std::uint32_t publishesSinceBaseline{};
			bool hasBaseline{};
			bool pendingRetry{};
		};

		enum class SnapshotSendStatus : std::uint8_t
		{
			Emitted,
			SuppressedUnchanged,
			Failed
		};

		struct SnapshotSendResult
		{
			SnapshotSendStatus status{ SnapshotSendStatus::Failed };
			std::optional<OutboundFrame> frame;
		};

		enum class CompatibilityViewStatus : std::uint8_t
		{
			NoSurface,
			Complete,
			Incomplete
		};

		struct CompatibilityView
		{
			CompatibilityViewStatus status{ CompatibilityViewStatus::NoSurface };
			SnapshotState state;
		};

		struct RouteUpdateResult
		{
			bool accepted{};
			std::vector<OutboundFrame> frames;
			std::set<PeerId> added;
			std::set<PeerId> removed;
		};

	public:
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

		bool IsConsoleAdmin(std::string_view a_profileId) const
		{
			return AdminProfiles::Matches(adminProfiles, a_profileId);
		}

		std::vector<OutboundFrame> HandleConnected(std::uint32_t a_connectionId, double a_unixTimeSeconds)
		{
			Session session;
			session.playerId = NextPlayerId();
			sessions[a_connectionId] = session;

			Json capabilities = BuildCapabilities();
			Json welcome{
				{ "type", "welcome" },
				{ "playerId", session.playerId },
				{ "serverTime", a_unixTimeSeconds },
				{ "serverName", serverName },
				{ "serverDescription", serverDescription },
				{ "friendlyFire", friendlyFire },
				{ "factionFriendlyFire", factionFriendlyFire },
				{ "protocolVersion", kProtocolVersion },
				{ "capabilities", std::move(capabilities) }
			};
			if (!serverInstanceId.empty()) {
				welcome["serverInstanceId"] = serverInstanceId;
			}
			return { ReliableFrame(a_connectionId, welcome) };
		}

		std::vector<OutboundFrame> HandleDisconnected(std::uint32_t a_connectionId, double a_unixTimeSeconds)
		{
			const auto found = sessions.find(a_connectionId);
			if (found == sessions.end()) {
				return {};
			}
			const auto playerId = found->second.playerId;
			const bool wasActive = found->second.active;
			const bool wasHost = wasActive && worldStateHostPlayerId == playerId;
			(void)found->second.lifecycle.MoveTo(F4T::Session::LifecycleState::Disconnected);

			std::vector<OutboundFrame> frames;
			if (wasActive) {
				found->second.active = false;
				downedService.Leave(playerId);
				if (compatibilityMode) {
					AppendFrames(frames, RemoveCompatibilityEntity(playerId));
					RemoveCompatibilityPeerRoutes(playerId);
				}
				peerVisibility.DespawnEntity(playerId);
				peerVisibility.RemovePeer(playerId);
				peerSnapshots.erase(playerId);
			}
			sessions.erase(found);

			if (wasActive) {
				AppendFrames(frames, BroadcastReliable(Json{
					{ "type", "disconnect" },
					{ "playerId", playerId },
					{ "serverTime", a_unixTimeSeconds }
				}));
			}
			AppendFrames(frames, ReconcileNpcAuthority(a_unixTimeSeconds));

			if (wasHost) {
				worldStateHostPlayerId = 0;
				for (const auto& [connectionId, session] : sessions) {
					(void)connectionId;
					if (session.active && (worldStateHostPlayerId == 0 || session.playerId < worldStateHostPlayerId)) {
						worldStateHostPlayerId = session.playerId;
					}
				}
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
			if (*type == "keepAlive") return {};
			if (!session.active) {
				Reject();
				return {};
			}
			if (*type == "pluginList") {
				return HandlePluginList(a_connectionId, session, *packet, a_unixTimeSeconds);
			}
			const auto commitSequence = [&](std::vector<OutboundFrame> a_frames) {
				if (envelopeFamily) {
					session.inboundSnapshotSequences[FamilyIndex(*envelopeFamily)] = envelopeHeader.sequence;
				}
				return a_frames;
			};

			if (*type == "transform") {
				const auto before = stats.movementRejected;
				auto frames = HandleTransform(a_connectionId, session, std::move(*packet), a_monotonicSeconds, a_unixTimeSeconds);
				return stats.movementRejected == before ? commitSequence(std::move(frames)) : std::move(frames);
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
			if (auto frames = DispatchWorldObjectPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds)) return std::move(*frames);
			if (*type == "combatHit") {
				return HandleCombatHit(a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			if (*type == "workshopOp") return HandleWorkshopOp(a_connectionId, session, *packet, a_monotonicSeconds);
			if (auto frames = DispatchSettlementPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds)) return std::move(*frames);
			if (*type == "projectileAim" || *type == "playerAnimEvent") return CommonwealthOnline::Server::ShotReplayRelay::HandleShotReplay(sessions, a_connectionId, session, *packet, a_monotonicSeconds, stats);
			if (auto frames = DispatchNpcCoopPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchChatPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchFactionPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchQuestPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchDownedPacket(*type, session, *packet, a_monotonicSeconds)) return std::move(*frames);
			if (*type == "voiceState" && session.compatibilityCapabilities.Has("voice-v1")) return voiceService.HandleState(a_connectionId, session.voice, *packet, a_monotonicSeconds);
			if (auto frames = DispatchCharacterSlotPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);
			if (auto frames = DispatchAdminPacket(*type, a_connectionId, session, *packet, a_monotonicSeconds, a_unixTimeSeconds)) return std::move(*frames);

			Reject();
			return {};
		}

		static FactionVitals SessionVitals(const Session& a_session)
		{
			FactionVitals vitals;
			if (!a_session.lastDurablePlayerState) return vitals;
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
				if (session.active) peers.push_back({ connectionId, session.playerId, session.profileId, std::string{ SessionNameView(session) }, SessionVitals(session) });
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
			const FactionPeer self{ a_connectionId, a_session.playerId, a_session.profileId, std::string{ SessionNameView(a_session) } };
			return factionService.Dispatch(a_type, self, a_session.faction, FactionPeers(), a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}

		bool SameFactionAsPlayer(const std::string& a_profileId, std::uint32_t a_targetPlayerId) const
		{
			for (const auto& [connectionId, session] : sessions) {
				if (session.active && session.playerId == a_targetPlayerId) return factionService.SameFaction(a_profileId, session.profileId);
			}
			return false;
		}

		std::vector<OutboundFrame> ExistingFactionFrames(std::uint32_t a_connectionId, const Session& a_target, double a_unixTimeSeconds)
		{
			if (!factionService.Enabled()) return {};
			return factionService.ExistingFrames(FactionPeers(), a_connectionId, a_target.profileId, a_unixTimeSeconds);
		}

		std::vector<OutboundFrame> FactionNameFrames(std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
		{
			if (!factionService.Enabled()) return {};
			const auto found = sessions.find(a_connectionId);
			if (found == sessions.end() || !found->second.active) return {};
			if (!factionService.RosterNameChanged(SessionNameView(found->second), found->second.faction, a_monotonicSeconds)) return {};
			return factionService.RosterFrames(FactionPeers(), a_unixTimeSeconds);
		}

		std::vector<OutboundFrame> FactionDisconnectFrames(const std::string& a_profileId, double a_unixTimeSeconds)
		{
			if (!factionService.Enabled()) return {};
			return factionService.DisconnectFrames(FactionPeers(), a_profileId, a_unixTimeSeconds);
		}

		Json AdminListFactions() const
		{
			return factionService.AdminList();
		}

		std::vector<OutboundFrame> AdminDisbandFaction(std::string_view a_id, double a_now, std::string& a_failure) { return factionService.AdminDisband(FactionPeers(), a_id, a_now, a_failure); }
		std::vector<OutboundFrame> AdminPromoteFaction(std::string_view a_id, std::string_view a_target, double a_now, std::string& a_failure) { return factionService.AdminPromote(FactionPeers(), a_id, a_target, a_now, a_failure); }
		std::vector<OutboundFrame> AdminKickFaction(std::string_view a_id, std::string_view a_target, double a_now, std::string& a_failure) { return factionService.AdminKick(FactionPeers(), a_id, a_target, a_now, a_failure); }

		%:include "ServerWorkshopImpl.inc"

		void SetFactionLimits(F4T::ServerConfiguration::FactionLimits a_limits)
		{
			factionService.SetLimits(a_limits);
		}

		void SetDownedConfig(F4T::DownedState::Config a_config)
		{
			downedService.SetConfig(a_config);
		}

		std::vector<OutboundFrame> PollDownedState(double a_monotonicSeconds)
		{
			return downedService.Poll(DownedPeers(), a_monotonicSeconds);
		}

		std::vector<OutboundFrame> AdminRevivePlayer(std::uint32_t a_playerId, double a_monotonicSeconds)
		{
			return downedService.AdminRevive(DownedPeers(), a_playerId, a_monotonicSeconds);
		}

		std::vector<OutboundFrame> AdminKillPlayer(std::uint32_t a_playerId)
		{
			return downedService.AdminKill(DownedPeers(), a_playerId);
		}

	private:
		DownedService::Peers DownedPeers() const
		{
			DownedService::Peers peers;
			for (const auto& [connectionId, session] : sessions) {
				if (!session.active) continue;
				DownedPeer peer{ connectionId, session.playerId };
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
			return downedService.Dispatch(a_type, DownedPeers(), a_session.playerId, a_session.downed, a_packet, a_monotonicSeconds, kMaximumCombatPositionAgeSeconds);
		}

		public:
		std::optional<std::vector<OutboundFrame>> DispatchAdminPacket(const std::string& a_type, std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds);
		bool IsLiveAdminSession(const Session& a_session) const;
		void BindSessionProfileVerified(std::uint32_t a_connectionId, std::string_view a_profileId, bool a_verified, std::string_view a_steamId);
		void BindSessionSteamId(std::uint32_t a_connectionId, std::string_view a_steamId);

		%:include "ServerLoadOrder.inc"

		%:include "ServerWorldPolicy.inc"

		std::size_t ActiveCount() const noexcept
		{
			return static_cast<std::size_t>(std::count_if(
				sessions.begin(), sessions.end(), [](const auto& entry) { return entry.second.active; }));
		}

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

		std::string serverName;
		std::string serverDescription;
		std::string serverInstanceId;
		std::vector<std::string> adminProfiles;
		bool friendlyFire{ true };
		bool factionFriendlyFire{};
		std::unordered_map<std::uint32_t, Session> sessions;
		std::uint32_t nextPlayerId{ 1 };
		std::uint32_t worldStateHostPlayerId{};
		F4T::NpcAuthority::Manager npcAuthority;
		std::map<Scope, Json> lastNpcStateByScope;
		std::map<Scope, double> lastNpcStateMonotonicByScope;
		F4T::Replication::PeerEntitySet peerVisibility;
		F4T::WorldPolicy::Policy worldPolicy;
		SnapshotState dropState;
		PersistenceGate* inventoryAuthority{};
		SnapshotState directAuthoritativeState;
		std::unordered_map<PeerId, std::array<PeerSnapshotSender, 2>> peerSnapshots;
		Compatibility::CompatibilityRegistry compatibilityRegistry;
		bool compatibilityMode{};
		bool hasEverActivatedSession{};
		std::set<CompatibilityRouteKey> compatibilityRoutes;
		std::map<ComponentKey, CompatibilityProjectionState> compatibilityProjections;
		ProtocolStats stats{};
		DownedService downedService{ stats };
		AdminMenuService adminMenu;
		FactionService factionService{ stats };
		ChatService chatService{ stats, factionService };
		VoiceService voiceService{ stats };
		std::optional<F4T::Replication::FrozenComponentDefinitionTable> componentDefinitions;
		std::optional<DirectComponentPublicationContract> directPublicationContract;

	private:
		std::uint32_t NextPlayerId()
		{
			for (;;) {
				if (nextPlayerId == 0 || nextPlayerId > (std::numeric_limits<std::uint16_t>::max)()) nextPlayerId = 1;
				const auto id = nextPlayerId++;
				if (std::none_of(sessions.begin(), sessions.end(), [id](const auto& session) { return session.second.playerId == id; })) return id;
			}
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

		std::vector<OutboundFrame> ReconcilePeerVisibility(const Session& a_self);

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
			double a_unixTimeSeconds)
		{
			(void)a_connectionId;
			auto normalized = NormalizeCombatHit(a_packet);
			if (!normalized) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			if (!friendlyFire) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			const auto sequence = ReadUInt32(*normalized, "sequence").value_or(0);
			if (!F4T::GnsSnapshotEnvelope::IsNewer(sequence, a_session.lastCombatSequence)) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			if (!CombatWindowAllows(a_session, a_monotonicSeconds)) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			std::uint32_t actionType = 0;
			CombatActionState* combatAction = nullptr;
			if (!CombatActionAllows(a_session, *normalized, a_monotonicSeconds, actionType, combatAction) || !combatAction) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			const auto targetPlayerId = ReadUInt32(*normalized, "targetPlayerId").value_or(0);
			if (targetPlayerId == a_session.playerId) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			if (!factionFriendlyFire && SameFactionAsPlayer(a_session.profileId, targetPlayerId)) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			std::uint32_t targetConnectionId = 0;
			Session* target = nullptr;
			for (auto& [connectionId, candidate] : sessions) {
				if (candidate.active && candidate.playerId == targetPlayerId) {
					targetConnectionId = connectionId;
					target = &candidate;
					break;
				}
			}
			if (!target) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			if (!CombatGeometryAllows(a_session, *target, actionType, a_monotonicSeconds)) {
				Reject();
				stats.combatHitsRejected++;
				return {};
			}
			a_session.lastCombatSequence = sequence;
			RecordCombatHit(a_session, *combatAction, a_monotonicSeconds);
			normalized->erase("attackSequence");
			(*normalized)["playerId"] = a_session.playerId;
			(*normalized)["serverTime"] = a_unixTimeSeconds;
			stats.combatHitsReceived++;
			stats.combatHitsRouted++;
			return { ReliableFrame(targetConnectionId, *normalized) };
		}

		%:include "ServerNpcHitImpl.inc"
	%:include "ServerOwnedInventory.inc"
