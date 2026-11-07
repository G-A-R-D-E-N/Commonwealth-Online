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
			const auto snapshotEpoch = ReadUInt32(scopeState->second, "authorityEpoch").value_or(0);
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
			if (npcLedger.Quarantined(a_scope, NpcLedger::LedgerSourceKey(snapshotEpoch, a_npcId, a_sourceFormId), a_monotonicSeconds)) {
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
			const auto boundedDamage = CombatChecks::ClampCombatDamageClaim(static_cast<float>(claimedDamage), CombatChecks::CapForAction(actionType));
			if (!(boundedDamage > 0.0F)) {
				Reject();
				stats.npcHitsRejected++;
				return {};
			}
			if (static_cast<float>(claimedDamage) > boundedDamage) {
				stats.npcDamageClaimsClamped++;
			}
			(*normalized)["damage"] = boundedDamage;
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
			if (!assignment || assignment->epoch != claimedEpoch) {
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
			RecordAcceptedHit(
				a_session,
				AcceptedHitRecord{
					0.0,
					sequence,
					ReadUInt32(*normalized, "attackSequence").value_or(0),
					npcId,
					claimedSource,
					scope.cellId,
					scope.worldspaceId,
					true,
					{},
					claimedEpoch
				},
				a_monotonicSeconds);
			if (assignment->playerId == a_session.playerId) {
				stats.npcHitsReceived++;
				return {};
			}
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
			if (a_type == "vendorStock" || a_type == "vendorStockQuery" || a_type == "vendorTrade") {
				return HandleVendorPacket(a_type, a_connectionId, a_session, a_packet, a_monotonicSeconds);
			}
			return std::nullopt;
		}

		VendorTradeBook vendorTrade;

		bool VendorPlayerActive(std::uint32_t a_playerId) const
		{
			for (const auto& [connectionId, candidate] : sessions) {
				if (candidate.active && candidate.playerId == a_playerId) {
					return true;
				}
			}
			return false;
		}

		std::vector<OutboundFrame> HandleVendorPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			auto& recent = a_session.recentVendorOps;
			while (!recent.empty() && a_monotonicSeconds - recent.front() > 10.0) {
				recent.pop_front();
			}
			if (recent.size() >= 20) {
				Reject();
				return {};
			}
			recent.push_back(a_monotonicSeconds);
			std::vector<VendorReply> replies;
			if (a_type == "vendorStock") {
				replies = vendorTrade.Publish(a_session.playerId, a_packet, [this](std::uint32_t a_id) { return VendorPlayerActive(a_id); });
			} else if (a_type == "vendorStockQuery") {
				replies = vendorTrade.Query(a_packet);
			} else {
				replies = vendorTrade.Trade(std::string{ SessionSocialKeyView(a_session) }, a_packet);
			}
			if (replies.empty()) {
				Reject();
				return {};
			}
			std::vector<OutboundFrame> frames;
			for (const auto& reply : replies) {
				if (!reply.broadcast) {
					frames.push_back(ReliableFrame(a_connectionId, reply.packet));
					continue;
				}
				for (const auto& [connectionId, recipient] : sessions) {
					if (recipient.active) {
						frames.push_back(ReliableFrame(connectionId, reply.packet));
					}
				}
			}
			return frames;
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

		bool ChatProfanityFilter(std::uint32_t a_connectionId) const
		{
			const auto found = sessions.find(a_connectionId);
			return found != sessions.end() && found->second.chat.profanityFilter;
		}

		std::uint64_t AllocateChatMessageId()
		{
			return chatService.NextMessageId();
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
			if (const auto preference = a_packet.find("profanityFilter"); preference != a_packet.end() && preference->is_boolean()) a_session.chat.profanityFilter = preference->get<bool>();
			ChatService::Peers peers;
			for (const auto& [connectionId, session] : sessions) {
				if (session.active) peers.push_back({ connectionId, session.playerId, session.profileId, std::string{ SessionSocialKeyView(session) }, {}, SessionHasSocialNames(session), session.chat.profanityFilter });
			}
			const auto characterName = ReadString(a_session.lastDurablePlayerState.value_or(Json::object()), "characterName").value_or("");
			const ChatPeer self{ a_connectionId, a_session.playerId, a_session.profileId, std::string{ SessionSocialKeyView(a_session) }, characterName, SessionHasSocialNames(a_session), a_session.chat.profanityFilter };
			const auto allow = [&](const F4T::ChatValidation::ChatSendResult& a_normalized) {
				const Json eventPacket{ { "channel", a_normalized.channel }, { "text", a_normalized.text } };
				return events.Allow(ServerEvent{ ServerEventKind::Chat, a_connectionId, a_session.playerId, a_session.profileId, &eventPacket });
			};
			return chatService.Dispatch(a_type, self, a_session.chat, peers, a_packet, a_monotonicSeconds, a_unixTimeSeconds, allow);
		}
