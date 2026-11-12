		std::vector<OutboundFrame> ExistingPluginListFrames(std::uint32_t a_connectionId) const
		{
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, session] : sessions) {
				if (connectionId == a_connectionId || !session.active || !session.pluginListPacket) {
					continue;
				}
				frames.push_back(ReliableFrame(a_connectionId, *session.pluginListPacket, DeliveryReceipt{ DeliveryReceipt::Kind::PluginList }));
			}
			return frames;
		}

		std::vector<OutboundFrame> RetryPluginLists()
		{
			std::vector<OutboundFrame> frames;
			for (auto& [connectionId, session] : sessions) {
				if (!session.active || !session.pluginListResendPending) {
					continue;
				}
				session.pluginListResendPending = false;
				AppendFrames(frames, ExistingPluginListFrames(connectionId));
			}
			return frames;
		}

		std::vector<OutboundFrame> HandlePluginList(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_unixTimeSeconds)
		{
			if (a_session.pluginListPacket) {
				Reject();
				stats.pluginListsRejected++;
				return {};
			}

			const auto data = ReadString(a_packet, "data");
			const auto decoded = data ? F4T::LoadOrder::DecodePluginList(*data) : std::nullopt;
			if (!decoded) {
				Reject();
				stats.pluginListsRejected++;
				return {};
			}

			stats.pluginListsReceived++;
			Json stored{
				{ "type", "pluginList" },
				{ "playerId", a_session.playerId },
				{ "playerIncarnation", a_session.playerIncarnation },
				{ "data", *data },
				{ "serverTime", a_unixTimeSeconds }
			};
			a_session.pluginListPacket = stored;
			a_session.pluginList = *decoded;
			const auto previousScope = ScopeFromMovement(a_session.lastMovement);
			for (auto* movement : { &a_session.lastMovement, &a_session.pausedMovement }) {
				if (*movement) ScopeKeys::Stamp(**movement, a_session.pluginList);
			}

			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, recipient] : sessions) {
				if (connectionId == a_connectionId || !recipient.active) {
					continue;
				}
				frames.push_back(ReliableFrame(connectionId, stored, DeliveryReceipt{ DeliveryReceipt::Kind::PluginList }));
			}
			if (a_session.lastMovement) {
				AppendFrames(frames, ReconcilePeerVisibility(a_connectionId, a_session, a_unixTimeSeconds, true));
				AppendFrames(frames, ReconcileNpcAuthority(a_unixTimeSeconds));
			}
			AppendFrames(frames, ScopeChangeFrames(a_connectionId, a_session, previousScope, a_unixTimeSeconds));
			return frames;
		}

		std::vector<OutboundFrame> ScopeChangeFrames(
			std::uint32_t a_connectionId,
			Session& a_session,
			const std::optional<Scope>& a_previousScope,
			double a_unixTimeSeconds)
		{
			std::vector<OutboundFrame> frames;
			const auto currentScope = ScopeFromMovement(a_session.lastMovement);
			if (!currentScope || currentScope == a_previousScope) {
				return frames;
			}
			if (!a_previousScope) {
				for (const auto& assignment : npcAuthority.Assignments()) {
					frames.push_back(ReliableFrame(a_connectionId, AuthorityPacket(assignment, a_unixTimeSeconds, ScopeKeys::WireOf(npcAuthorityWire, assignment.scope))));
				}
				AppendFrames(frames, ExistingTransformFrames(a_connectionId, a_session, a_unixTimeSeconds));
			} else if (const auto assignment = npcAuthority.Get(*currentScope)) {
				frames.push_back(ReliableFrame(a_connectionId, AuthorityPacket(*assignment, a_unixTimeSeconds, ScopeKeys::WireOf(npcAuthorityWire, assignment->scope))));
			}
			AppendFrames(frames, ExistingNpcCoopFrames(a_connectionId, a_session, a_unixTimeSeconds));
			return frames;
		}
