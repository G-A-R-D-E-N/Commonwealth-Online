			static constexpr double kTradeMaximumDistance = 400.0;
			F4T::Trade::Book tradeBook{ static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) };

			bool TradeNegotiated(const Session& a_session) const
			{
				return inventoryAuthority != nullptr && InventoryNegotiated(a_session) && a_session.compatibilityCapabilities.Has("player-trade-v1");
			}

			std::optional<std::pair<std::uint32_t, const Session*>> TradeParticipant(std::uint32_t a_playerId) const
			{
				for (const auto& [connectionId, session] : sessions) {
					if (session.active && session.playerId == a_playerId) return std::make_pair(connectionId, &session);
				}
				return std::nullopt;
			}

			bool TradeAlive(std::uint32_t a_playerId) const
			{
				const auto state = downedService.StateOf(a_playerId);
				return !state || F4T::DownedState::IsActive(*state);
			}

			bool TradeFresh(const Session& a_session, double a_monotonicSeconds) const
			{
				return !OwnedInventoryPosition(a_session, a_monotonicSeconds).cellId.empty();
			}

			bool TradeInRange(const Session& a_first, const Session& a_second, double a_monotonicSeconds) const
			{
				const auto first = OwnedInventoryPosition(a_first, a_monotonicSeconds);
				const auto second = OwnedInventoryPosition(a_second, a_monotonicSeconds);
				const auto firstScope = ScopeKeys::Canonical(a_first.pluginList, F4T::NpcAuthority::Scope{ first.cellId, first.worldspaceId });
				const auto secondScope = ScopeKeys::Canonical(a_second.pluginList, F4T::NpcAuthority::Scope{ second.cellId, second.worldspaceId });
				if (first.cellId.empty() || secondScope != firstScope) return false;
				const auto dx = first.x - second.x;
				const auto dy = first.y - second.y;
				const auto dz = first.z - second.z;
				return dx * dx + dy * dy + dz * dz <= kTradeMaximumDistance * kTradeMaximumDistance;
			}

			bool TradeOffersOwned(std::uint32_t a_connectionId, const std::vector<F4T::Trade::Line>& a_lines)
			{
				const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);
				const auto inventory = profile ? inventoryAuthority->Store().GetInventory(*profile) : std::nullopt;
				if (!inventory || !inventory->contains("items")) return false;
				const auto& items = inventory->at("items");
				for (const auto& line : a_lines) {
					const auto found = items.find(line.itemKey);
					if (found == items.end() || found->at("count").get<std::uint32_t>() < line.count) return false;
				}
				return true;
			}

			std::vector<OutboundFrame> TradeStateFrames(const F4T::Trade::Trade& a_trade)
			{
				std::vector<OutboundFrame> frames;
				for (const auto playerId : { a_trade.initiator.playerId, a_trade.target.playerId }) {
					if (const auto participant = TradeParticipant(playerId)) frames.push_back(ReliableFrame(participant->first, F4T::Trade::StatePacket(a_trade, playerId)));
				}
				return frames;
			}

			std::vector<OutboundFrame> TradeEndedFrames()
			{
				std::vector<OutboundFrame> frames;
				for (const auto& ended : tradeBook.TakeEnded()) {
					for (const auto playerId : { ended.initiatorPlayerId, ended.targetPlayerId }) {
						if (const auto participant = TradeParticipant(playerId)) frames.push_back(ReliableFrame(participant->first, F4T::Trade::EndedPacket(ended, playerId)));
					}
				}
				return frames;
			}

			std::vector<OutboundFrame> TradeUpdate(const std::string& a_tradeId)
			{
				std::vector<OutboundFrame> frames;
				if (const auto* trade = tradeBook.Find(a_tradeId)) frames = TradeStateFrames(*trade);
				AppendFrames(frames, TradeEndedFrames());
				return frames;
			}

			std::vector<OutboundFrame> EndTradesFor(std::uint32_t a_playerId)
			{
				tradeBook.EndFor(a_playerId, "disconnected");
				return TradeEndedFrames();
			}

			std::vector<OutboundFrame> SettleTrade(const std::string& a_tradeId, double a_monotonicSeconds)
			{
				const auto trade = *tradeBook.Find(a_tradeId);
				const auto initiator = TradeParticipant(trade.initiator.playerId);
				const auto target = TradeParticipant(trade.target.playerId);
				if (!initiator || !target) {
					tradeBook.Finish(a_tradeId, "partnerUnavailable");
					return TradeEndedFrames();
				}
				if (!TradeAlive(trade.initiator.playerId) || !TradeAlive(trade.target.playerId)) {
					tradeBook.Finish(a_tradeId, "downed");
					return TradeEndedFrames();
				}
				if (!TradeInRange(*initiator->second, *target->second, a_monotonicSeconds)) {
					tradeBook.Unconfirm(a_tradeId);
					std::vector<OutboundFrame> frames{ ReliableFrame(initiator->first, F4T::Trade::RefusalPacket(a_tradeId, "separated")),
						ReliableFrame(target->first, F4T::Trade::RefusalPacket(a_tradeId, "separated")) };
					AppendFrames(frames, TradeUpdate(a_tradeId));
					return frames;
				}
				if (!inventoryAuthority->InventorySaveReady(initiator->first) || !inventoryAuthority->InventorySaveReady(target->first)) {
					tradeBook.Finish(a_tradeId, "startingInventoryPending");
					return TradeEndedFrames();
				}
				const auto request = tradeBook.BuildCommit(a_tradeId, inventoryAuthority->ProfileForConnection(initiator->first).value_or(std::string{}),
					inventoryAuthority->ProfileForConnection(target->first).value_or(std::string{}));
				if (!request) {
					tradeBook.Finish(a_tradeId, "invalidRequest");
					return TradeEndedFrames();
				}
				F4T::Trade::CommitResult result;
				try {
					const auto settled = inventoryAuthority->TransferTrade(initiator->first, target->first, *request);
					if (!settled) {
						tradeBook.Finish(a_tradeId, "startingInventoryPending");
						return TradeEndedFrames();
					}
					result = *settled;
				} catch (const std::exception&) {
					result.outcome = F4T::Drops::Outcome::Rejected;
					result.reason = "inventoryPersistenceUnavailable";
				}
				std::vector<OutboundFrame> frames{ ReliableFrame(initiator->first, F4T::Trade::CommitResultPacket(a_tradeId, *request, result, true)),
					ReliableFrame(target->first, F4T::Trade::CommitResultPacket(a_tradeId, *request, result, false)) };
				if (result.outcome == F4T::Drops::Outcome::Rejected && result.reason == "staleRevision") {
					tradeBook.Unconfirm(a_tradeId);
					AppendFrames(frames, TradeUpdate(a_tradeId));
					return frames;
				}
				const auto reason = result.outcome == F4T::Drops::Outcome::Rejected ? result.reason : std::string{ "committed" };
				tradeBook.Finish(a_tradeId, reason);
				std::cerr << "Trade " << a_tradeId << " ended: " << reason << '\n';
				AppendFrames(frames, TradeEndedFrames());
				return frames;
			}

			std::vector<OutboundFrame> HandleTradeIntent(std::uint32_t a_connectionId, Session& a_session, const Json& a_packet, double a_monotonicSeconds)
			{
				const auto intent = F4T::Trade::ParseClientIntent(a_packet);
				if (!intent) {
					Reject();
					return {};
				}
				const auto refused = [&](std::string_view a_reason) {
					return std::vector<OutboundFrame>{ ReliableFrame(a_connectionId, F4T::Trade::RefusalPacket(intent->tradeId, a_reason)) };
				};
				const auto playerId = a_session.playerId;
				if (!F4T::PacketRateLimit::TakeToken(a_session.inventoryRequests, a_monotonicSeconds)) return refused("rateLimited");
				if (!TradeNegotiated(a_session)) return refused("inventoryAuthorityUnavailable");
				const bool declined = intent->kind == F4T::Trade::IntentKind::Respond && !intent->accept;
				const bool mayChangeInventory = intent->kind != F4T::Trade::IntentKind::Cancel && !declined;
				if (mayChangeInventory && !events.Allow(ServerEvent{ ServerEventKind::InventoryChange, a_connectionId, playerId, a_session.profileId, &a_packet })) {
					Reject();
					return refused("vetoed");
				}
				switch (intent->kind) {
				case F4T::Trade::IntentKind::Request: {
					const auto target = TradeParticipant(intent->targetPlayerId);
					if (!target || !TradeNegotiated(*target->second)) return refused("targetUnavailable");
					if (!TradeAlive(playerId) || !TradeAlive(intent->targetPlayerId)) return refused("downed");
					if (!TradeInRange(a_session, *target->second, a_monotonicSeconds)) return refused("notNearby");
					const auto verdict = tradeBook.Request(playerId, intent->targetPlayerId, a_monotonicSeconds);
					if (!verdict.accepted) return refused(verdict.reason);
					const auto tradeId = tradeBook.ActiveFor(playerId)->tradeId;
					return TradeUpdate(tradeId);
				}
				case F4T::Trade::IntentKind::Respond: {
					const auto verdict = tradeBook.Respond(intent->tradeId, playerId, intent->accept, a_monotonicSeconds);
					if (!verdict.accepted) return refused(verdict.reason);
					return TradeUpdate(intent->tradeId);
				}
				case F4T::Trade::IntentKind::Offer: {
					if (!TradeOffersOwned(a_connectionId, intent->lines)) return refused("insufficientItems");
					const auto verdict = tradeBook.SetOffer(intent->tradeId, playerId, intent->lines, a_monotonicSeconds);
					if (!verdict.accepted) return refused(verdict.reason);
					return TradeUpdate(intent->tradeId);
				}
				case F4T::Trade::IntentKind::Confirm: {
					const auto* trade = tradeBook.Find(intent->tradeId);
					if (trade == nullptr) return refused("notFound");
					const auto partner = TradeParticipant(trade->initiator.playerId == playerId ? trade->target.playerId : trade->initiator.playerId);
					if (!partner) return refused("partnerUnavailable");
					if (!TradeInRange(a_session, *partner->second, a_monotonicSeconds)) return refused("notNearby");
					const auto revision = OwnedInventoryRevision(a_connectionId, a_session);
					if (!revision || !revision->is_number()) return refused("inventoryAuthorityUnavailable");
					const auto verdict = tradeBook.Confirm(intent->tradeId, playerId, revision->get<std::uint32_t>(), intent->offerVersion, a_monotonicSeconds);
					if (!verdict.accepted) return refused(verdict.reason);
					if (tradeBook.Ready(intent->tradeId)) return SettleTrade(intent->tradeId, a_monotonicSeconds);
					return TradeUpdate(intent->tradeId);
				}
				case F4T::Trade::IntentKind::Cancel: {
					const auto verdict = tradeBook.Cancel(intent->tradeId, playerId, a_monotonicSeconds);
					if (!verdict.accepted) return refused(verdict.reason);
					return TradeUpdate(intent->tradeId);
				}
				}
				return refused("invalidRequest");
			}

		public:
			std::vector<OutboundFrame> PollTrades(double a_monotonicSeconds)
			{
				tradeBook.Expire(a_monotonicSeconds);
				const auto trades = tradeBook.Trades();
				for (const auto& trade : trades) {
					const auto initiator = TradeParticipant(trade.initiator.playerId);
					const auto target = TradeParticipant(trade.target.playerId);
					if (!initiator || !target) {
						tradeBook.Finish(trade.tradeId, "partnerUnavailable");
					} else if (!TradeAlive(trade.initiator.playerId) || !TradeAlive(trade.target.playerId)) {
						tradeBook.Finish(trade.tradeId, "downed");
					} else if (TradeFresh(*initiator->second, a_monotonicSeconds) && TradeFresh(*target->second, a_monotonicSeconds) &&
						!TradeInRange(*initiator->second, *target->second, a_monotonicSeconds)) {
						tradeBook.Finish(trade.tradeId, "separated");
					}
				}
				return TradeEndedFrames();
			}
		private:
