		// source:CO-BETA/docs/protocol/packets.md
		std::map<Scope, std::map<std::uint32_t, CachedNpcLoot>> lastNpcLootByScope;
		std::map<std::pair<std::uint32_t, std::string>, PendingNpcLootTake> pendingNpcLootTakes;

		void ExpireNpcLootTakes(double a_monotonicSeconds)
		{
			std::erase_if(pendingNpcLootTakes, [a_monotonicSeconds](const auto& a_entry) {
				const auto age = a_monotonicSeconds - a_entry.second.createdMonotonicSeconds;
				return !(age >= 0.0) || age > kNpcLootTakeLifetimeSeconds;
			});
		}

		void PruneNpcLoots(double a_unixTimeSeconds)
		{
			std::erase_if(lastNpcLootByScope, [this, a_unixTimeSeconds](auto& a_scope) {
				const auto assignment = npcAuthority.Get(a_scope.first);
				std::erase_if(a_scope.second, [a_unixTimeSeconds, &assignment](const auto& a_entry) {
					const auto age = a_unixTimeSeconds - a_entry.second.unixTimeSeconds;
					return !(age >= 0.0) || age > kNpcLootCacheLifetimeSeconds || !assignment || assignment->epoch != a_entry.second.epoch;
				});
				return a_scope.second.empty() || !lastNpcStateByScope.contains(a_scope.first);
			});
		}

		void CacheNpcLoot(const Scope& a_scope, std::uint32_t a_npcId, CachedNpcLoot a_loot)
		{
			if (!lastNpcLootByScope.contains(a_scope) && lastNpcLootByScope.size() >= kMaximumNpcLootScopes) {
				return;
			}
			auto& npcs = lastNpcLootByScope[a_scope];
			if (!npcs.contains(a_npcId) && npcs.size() >= kMaximumNpcLootsPerScope) {
				npcs.erase(std::min_element(npcs.begin(), npcs.end(), [](const auto& a_lhs, const auto& a_rhs) {
					return a_lhs.second.unixTimeSeconds < a_rhs.second.unixTimeSeconds;
				}));
			}
			npcs[a_npcId] = std::move(a_loot);
		}

		std::vector<OutboundFrame> HandleNpcLoot(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			ExpireNpcLootTakes(a_monotonicSeconds);
			PruneNpcLoots(a_unixTimeSeconds);
			stats.npcLootReceived++;
			auto normalized = NormalizeNpcLoot(a_packet);
			if (!normalized) {
				Reject();
				stats.npcLootRejected++;
				return {};
			}
			const Scope scope{ ReadString(*normalized, "cellId").value_or(""), ReadString(*normalized, "worldspaceId").value_or("") };
			const auto assignment = npcAuthority.Get(scope);
			const auto claimedEpoch = ReadUInt32(*normalized, "authorityEpoch").value_or(0);
			if (!assignment || assignment->epoch != claimedEpoch || assignment->playerId != a_session.playerId) {
				Reject();
				stats.npcLootRejected++;
				return {};
			}
			const auto npcId = ReadUInt32(*normalized, "npcId").value_or(0);
			const Json* npcEntry = FindSnapshotNpc(scope, npcId, ReadString(*normalized, "sourceFormId").value_or(""), a_monotonicSeconds);
			if (!npcEntry || !npcEntry->value("isDead", false)) {
				Reject();
				stats.npcLootRejected++;
				return {};
			}
			const auto revision = ReadUInt32(*normalized, "revision").value_or(0);
			std::uint32_t cachedRevision = 0;
			if (const auto cachedScope = lastNpcLootByScope.find(scope); cachedScope != lastNpcLootByScope.end()) {
				if (const auto cached = cachedScope->second.find(npcId); cached != cachedScope->second.end() && cached->second.epoch == claimedEpoch) {
					cachedRevision = cached->second.revision;
				}
			}
			if (!F4T::GnsSnapshotEnvelope::IsNewer(revision, cachedRevision)) {
				Reject();
				stats.npcLootRejected++;
				return {};
			}

			const auto interest = InterestScopeFromNpc(*npcEntry);
			(*normalized)["playerId"] = a_session.playerId;
			(*normalized)["serverTime"] = a_unixTimeSeconds;
			CacheNpcLoot(scope, npcId, CachedNpcLoot{ *normalized, revision, claimedEpoch, a_unixTimeSeconds, interest });
			std::vector<OutboundFrame> frames;
			for (auto& [connectionId, recipient] : sessions) {
				if (connectionId == a_connectionId || !recipient.active) {
					continue;
				}
				if (F4T::InterestScope::StatesShareInterest(interest, InterestScopeFromMovement(recipient.lastMovement))) {
					frames.push_back(ReliableFrame(connectionId, *normalized));
				}
			}
			stats.npcLootRouted++;
			return frames;
		}

		std::vector<OutboundFrame> HandleNpcLootTake(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			ExpireNpcLootTakes(a_monotonicSeconds);
			stats.npcLootTakeReceived++;
			if (!NpcLootTakeRateAllows(a_session, a_monotonicSeconds)) {
				Reject();
				stats.npcLootTakeRejected++;
				return {};
			}
			auto normalized = NormalizeNpcLootTake(a_packet);
			if (!normalized) {
				Reject();
				stats.npcLootTakeRejected++;
				return {};
			}
			const Scope scope{ ReadString(*normalized, "cellId").value_or(""), ReadString(*normalized, "worldspaceId").value_or("") };
			const auto assignment = npcAuthority.Get(scope);
			const auto claimedEpoch = ReadUInt32(*normalized, "authorityEpoch").value_or(0);
			if (!assignment || assignment->epoch != claimedEpoch || assignment->playerId == a_session.playerId ||
				!NpcLootTakerAllows(a_session, scope, a_monotonicSeconds)) {
				Reject();
				stats.npcLootTakeRejected++;
				return {};
			}
			std::optional<std::uint32_t> authorityConnectionId;
			for (const auto& [connectionId, candidate] : sessions) {
				if (candidate.active && candidate.playerId == assignment->playerId) {
					authorityConnectionId = connectionId;
					break;
				}
			}
			if (!authorityConnectionId) {
				Reject();
				stats.npcLootTakeRejected++;
				return {};
			}
			const auto operationId = ReadString(*normalized, "operationId").value_or("");

			const auto npcId = ReadUInt32(*normalized, "npcId").value_or(0);
			const Json* npcEntry = FindSnapshotNpc(scope, npcId, ReadString(*normalized, "sourceFormId").value_or(""), a_monotonicSeconds);
			if (!npcEntry || !npcEntry->value("isDead", false)) {
				Reject();
				stats.npcLootTakeRejected++;
				return {};
			}
			const auto npcX = ReadDouble(*npcEntry, "x");
			const auto npcY = ReadDouble(*npcEntry, "y");
			const auto npcZ = ReadDouble(*npcEntry, "z");
			if (!npcX || !npcY || !npcZ) {
				Reject();
				stats.npcLootTakeRejected++;
				return {};
			}
			if (!NpcGeometryAllows(
				a_session,
				*npcX,
				*npcY,
				*npcZ,
				scope.cellId,
				ReadString(*npcEntry, "worldspaceId").value_or(""),
				kNpcLootTakeGeometryType,
				a_monotonicSeconds)) {
				Reject();
				stats.npcLootTakeRejected++;
				return {};
			}

			const auto key = std::make_pair(a_session.playerId, operationId);
			if (pendingNpcLootTakes.contains(key) || pendingNpcLootTakes.size() >= kMaximumPendingNpcLootTakes) {
				Reject();
				stats.npcLootTakeRejected++;
				return {};
			}
			a_session.recentNpcLootTakes.push_back(a_monotonicSeconds);
			pendingNpcLootTakes[key] = PendingNpcLootTake{
				a_connectionId,
				a_session.playerId,
				assignment->playerId,
				assignment->epoch,
				scope,
				npcId,
				ReadString(*normalized, "formId").value_or(""),
				ReadUInt32(*normalized, "count").value_or(0),
				a_monotonicSeconds
			};
			(*normalized)["playerId"] = a_session.playerId;
			(*normalized)["serverTime"] = a_unixTimeSeconds;
			stats.npcLootTakeRouted++;
			return { ReliableFrame(*authorityConnectionId, *normalized) };
		}

		std::vector<OutboundFrame> HandleNpcLootResult(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			(void)a_connectionId;
			ExpireNpcLootTakes(a_monotonicSeconds);
			stats.npcLootResultReceived++;
			auto normalized = NormalizeNpcLootResult(a_packet);
			if (!normalized) {
				Reject();
				stats.npcLootResultRejected++;
				return {};
			}
			const auto operationId = ReadString(*normalized, "operationId").value_or("");
			const auto takerPlayerId = ReadUInt32(*normalized, "takerPlayerId").value_or(0);
			const auto pending = pendingNpcLootTakes.find(std::make_pair(takerPlayerId, operationId));
			if (pending == pendingNpcLootTakes.end() || pending->second.authorityPlayerId != a_session.playerId) {
				Reject();
				stats.npcLootResultRejected++;
				return {};
			}
			const auto assignment = npcAuthority.Get(pending->second.scope);
			if (!assignment || assignment->playerId != a_session.playerId || assignment->epoch != pending->second.epoch ||
				ReadString(*normalized, "formId").value_or("") != pending->second.itemId ||
				ReadUInt32(*normalized, "count").value_or(0) > pending->second.count) {
				Reject();
				stats.npcLootResultRejected++;
				return {};
			}

			const auto takerConnectionId = pending->second.takerConnectionId;
			pendingNpcLootTakes.erase(pending);
			const auto taker = sessions.find(takerConnectionId);
			if (taker == sessions.end() || !taker->second.active || taker->second.playerId != takerPlayerId) {
				Reject();
				stats.npcLootResultRejected++;
				return {};
			}
			(*normalized)["authorityPlayerId"] = a_session.playerId;
			(*normalized)["serverTime"] = a_unixTimeSeconds;
			stats.npcLootResultRouted++;
			return { ReliableFrame(takerConnectionId, *normalized) };
		}

		std::vector<OutboundFrame> ExistingNpcCoopFrames(
			std::uint32_t a_connectionId,
			Session& a_target,
			double a_unixTimeSeconds)
		{
			PruneNpcLoots(a_unixTimeSeconds);
			auto frames = ExistingNpcStateFrames(a_connectionId, a_target, a_unixTimeSeconds);
			const auto targetScope = InterestScopeFromMovement(a_target.lastMovement);
			if (!targetScope) {
				return frames;
			}
			for (const auto& [scope, npcs] : lastNpcLootByScope) {
				(void)scope;
				for (const auto& [npcId, cached] : npcs) {
					(void)npcId;
					if (ReadUInt32(cached.packet, "playerId").value_or(0) == a_target.playerId ||
						!F4T::InterestScope::StatesShareInterest(cached.interest, targetScope)) {
						continue;
					}
					Json packet = cached.packet;
					packet["serverTime"] = a_unixTimeSeconds;
					frames.push_back(ReliableFrame(a_connectionId, packet));
				}
			}
			return frames;
		}
