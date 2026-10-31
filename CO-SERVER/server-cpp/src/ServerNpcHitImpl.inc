		const Json* FindSnapshotNpc(
			const Scope& a_scope,
			std::uint32_t a_npcId,
			const std::string& a_sourceFormId,
			double a_monotonicSeconds)
		{
			const auto scopeState = lastNpcStateByScope.find(a_scope);
			if (scopeState == lastNpcStateByScope.end() || !scopeState->second.contains("npcs")) {
				return nullptr;
			}
			const Json* npcEntry = nullptr;
			for (const auto& npc : scopeState->second["npcs"]) {
				if (ReadUInt32(npc, "npcId").value_or(0) == a_npcId) {
					npcEntry = &npc;
					break;
				}
			}
			if (!npcEntry || a_sourceFormId.empty() || ReadString(*npcEntry, "sourceFormId").value_or("") != a_sourceFormId) {
				return nullptr;
			}
			const auto snapshotMonotonic = lastNpcStateMonotonicByScope.find(a_scope);
			if (snapshotMonotonic == lastNpcStateMonotonicByScope.end() ||
				!(a_monotonicSeconds - snapshotMonotonic->second >= 0.0) ||
				a_monotonicSeconds - snapshotMonotonic->second > kMaximumNpcSnapshotAgeSeconds) {
				return nullptr;
			}
			return npcEntry;
		}

		const Json* FindFreshNpc(
			const Scope& a_scope,
			std::uint32_t a_npcId,
			const std::string& a_sourceFormId,
			double a_monotonicSeconds)
		{
			const auto npcEntry = FindSnapshotNpc(a_scope, a_npcId, a_sourceFormId, a_monotonicSeconds);
			if (!npcEntry || npcEntry->value("isDead", false)) {
				return nullptr;
			}
			const auto storedHealth = ReadDouble(*npcEntry, "health");
			if (storedHealth && !(*storedHealth > 0.0)) {
				return nullptr;
			}
			return npcEntry;
		}

		std::vector<OutboundFrame> HandleNpcHit(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			(void)a_connectionId;
			auto normalized = NormalizeNpcHit(a_packet);
			if (!normalized) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}
			const auto sequence = ReadUInt32(*normalized, "sequence").value_or(0);
			if (!F4T::GnsSnapshotEnvelope::IsNewer(sequence, a_session.lastNpcHitSequence)) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}
			if (!CombatWindowAllows(a_session, a_monotonicSeconds)) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}
			std::uint32_t actionType = 0;
			CombatActionState* combatAction = nullptr;
			if (!CombatActionAllows(a_session, *normalized, a_monotonicSeconds, actionType, combatAction) || !combatAction) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}

			const Scope scope{ ReadString(*normalized, "cellId").value_or(""), ReadString(*normalized, "worldspaceId").value_or("") };
			const auto npcId = ReadUInt32(*normalized, "npcId").value_or(0);
			const auto claimedSource = ReadString(*normalized, "sourceFormId").value_or("");
			const Json* npcEntry = FindFreshNpc(scope, npcId, claimedSource, a_monotonicSeconds);
			if (!npcEntry) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}
			const auto storedMaxHealth = ReadDouble(*npcEntry, "maxHealth");
			const auto claimedDamage = ReadDouble(*normalized, "damage").value_or(0.0);
			if (storedMaxHealth && !(claimedDamage <= *storedMaxHealth * 4.0)) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}
			const auto npcX = ReadDouble(*npcEntry, "x");
			const auto npcY = ReadDouble(*npcEntry, "y");
			const auto npcZ = ReadDouble(*npcEntry, "z");
			if (!npcX || !npcY || !npcZ) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}
			if (!NpcGeometryAllows(
					a_session,
					*npcX,
					*npcY,
					*npcZ,
					scope.cellId,
					ReadString(*npcEntry, "worldspaceId").value_or(""),
					actionType,
					a_monotonicSeconds)) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}

			const auto assignment = npcAuthority.Get(scope);
			const auto claimedEpoch = ReadUInt32(*normalized, "authorityEpoch").value_or(0);
			if (!assignment || assignment->epoch != claimedEpoch || assignment->playerId == a_session.playerId) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}
			std::uint32_t authorityConnectionId = 0;
			bool authorityFound = false;
			for (auto& [connectionId, candidate] : sessions) {
				if (candidate.active && candidate.playerId == assignment->playerId) {
					authorityConnectionId = connectionId;
					authorityFound = true;
					break;
				}
			}
			if (!authorityFound) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}

			a_session.lastNpcHitSequence = sequence;
			RecordCombatHit(a_session, *combatAction, a_monotonicSeconds);
			normalized->erase("attackSequence");
			(*normalized)["playerId"] = a_session.playerId;
			(*normalized)["serverTime"] = a_unixTimeSeconds;
			stats.npcHitsReceived++;
			stats.npcHitsRouted++;
			return { ReliableFrame(authorityConnectionId, *normalized) };
		}

		std::optional<std::vector<OutboundFrame>> DispatchNpcCoopPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			if (a_type == "npcHit") {
				return HandleNpcHit(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			if (a_type == "npcDamage") {
				return HandleNpcDamage(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			if (a_type == "npcLoot") {
				return HandleNpcLoot(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			if (a_type == "npcLootTake") {
				return HandleNpcLootTake(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			if (a_type == "npcLootResult") {
				return HandleNpcLootResult(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
			}
			return std::nullopt;
		}

		%:include "ServerNpcDamageImpl.inc"
		%:include "ServerNpcLootImpl.inc"
	public:
		void SetChatEnabled(bool a_enabled)
		{
			chatService.SetEnabled(a_enabled);
		}

		void SetChatMuted(std::string_view a_profileId, bool a_muted)
		{
			chatService.SetMuted(a_profileId, a_muted);
		}

	private:
		std::optional<std::vector<OutboundFrame>> DispatchChatPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			if (!chatService.Enabled() || (a_type != "chatSend" && a_type != "chatHistoryRequest")) return std::nullopt;
			ChatService::Peers peers;
			for (const auto& [connectionId, session] : sessions) {
				if (session.active) peers.push_back({ connectionId, session.playerId, session.profileId, {}, SessionHasSocialNames(session) });
			}
			const auto characterName = ReadString(a_session.lastDurablePlayerState.value_or(Json::object()), "characterName").value_or("");
			const ChatPeer self{ a_connectionId, a_session.playerId, a_session.profileId, characterName, SessionHasSocialNames(a_session) };
			return chatService.Dispatch(a_type, self, a_session.chat, peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds);
		}
