		std::vector<OutboundFrame> HandleNpcDamage(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			(void)a_connectionId;
			stats.npcDamageReceived++;
			auto normalized = NormalizeNpcDamage(a_packet);
			if (!normalized) {
				Reject();
				stats.npcDamageRejected++;
				return {};
			}
			const auto sequence = ReadUInt32(*normalized, "sequence").value_or(0);
			if (!F4T::GnsSnapshotEnvelope::IsNewer(sequence, a_session.lastNpcDamageSequence)) {
				Reject();
				stats.npcDamageRejected++;
				return {};
			}
			const auto npcId = ReadUInt32(*normalized, "npcId").value_or(0);
			const auto targetPlayerId = ReadUInt32(*normalized, "targetPlayerId").value_or(0);
			if (!NpcDamageRateAllows(a_session, npcId, targetPlayerId, a_monotonicSeconds)) {
				Reject();
				stats.npcDamageRejected++;
				return {};
			}

			const Scope scope{ ReadString(*normalized, "cellId").value_or(""), ReadString(*normalized, "worldspaceId").value_or("") };
			const auto assignment = npcAuthority.Get(scope);
			const auto claimedEpoch = ReadUInt32(*normalized, "authorityEpoch").value_or(0);
			if (!assignment || assignment->epoch != claimedEpoch || assignment->playerId != a_session.playerId) {
				Reject();
				stats.npcDamageRejected++;
				return {};
			}
			if (targetPlayerId == a_session.playerId) {
				Reject();
				stats.npcDamageRejected++;
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
				stats.npcDamageRejected++;
				return {};
			}
			const auto npcEntry = FindFreshNpc(scope, npcId, ReadString(*normalized, "sourceFormId").value_or(""), a_monotonicSeconds);
			if (!npcEntry) {
				Reject();
				stats.npcDamageRejected++;
				return {};
			}
			const auto npcX = ReadDouble(*npcEntry, "x");
			const auto npcY = ReadDouble(*npcEntry, "y");
			const auto npcZ = ReadDouble(*npcEntry, "z");
			if (!npcX || !npcY || !npcZ) {
				Reject();
				stats.npcDamageRejected++;
				return {};
			}
			if (!target->lastMovement || target->lastMovement->cellId != scope.cellId ||
				target->lastMovement->worldspaceId != scope.worldspaceId ||
				!NpcGeometryAllows(
					*target,
					*npcX,
					*npcY,
					*npcZ,
					scope.cellId,
					ReadString(*npcEntry, "worldspaceId").value_or(""),
					3U,
					a_monotonicSeconds)) {
				Reject();
				stats.npcDamageRejected++;
				return {};
			}

			a_session.lastNpcDamageSequence = sequence;
			a_session.recentNpcDamage.push_back(NpcDamageRecord{ a_monotonicSeconds, npcId, targetPlayerId });
			normalized->erase("authorityEpoch");
			(*normalized)["authorityPlayerId"] = a_session.playerId;
			(*normalized)["serverTime"] = a_unixTimeSeconds;
			stats.npcDamageRouted++;
			return { ReliableFrame(targetConnectionId, *normalized) };
		}
