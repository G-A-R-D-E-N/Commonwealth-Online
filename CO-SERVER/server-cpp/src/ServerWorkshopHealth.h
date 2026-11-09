		std::vector<OutboundFrame> TickWorkshopUpkeepDecay(double a_unixSeconds)
		{
			if (workshopUpkeepLastUnix <= 0.0) {
				workshopUpkeepLastUnix = a_unixSeconds;
				return {};
			}
			const double dtHours = (a_unixSeconds - workshopUpkeepLastUnix) / 3600.0;
			workshopUpkeepLastUnix = a_unixSeconds;
			if (dtHours <= 0.0) {
				return {};
			}
			const auto unixSeconds = static_cast<std::uint64_t>(a_unixSeconds);
			std::map<F4T::Workshop::BuildZoneId, std::size_t> tombstonesBefore;
			for (const auto& entry : workshopZones) {
				tombstonesBefore[entry.first] = entry.second.Tombstones().size();
			}
			for (auto& entry : workshopZones) {
				const auto settlement = settlementAccess.Find(entry.first);
				if (settlement && settlement->kind == F4T::SettlementAccess::SettlementKind::Camp &&
					settlement->workbenchBroken) {
					if (entry.second.TickFractionalDecay(
						    F4T::WorkshopCamps::BrokenCampDecayPerHour,
						    dtHours,
						    unixSeconds)) {
						PrunePowerNodes(entry.second);
						MarkWorkshopDirty(entry.first);
					}
					continue;
				}
				const auto paidUntil = settlementAccess.UpkeepPaidUntil(entry.first);
				if (!paidUntil) {
					continue;
				}
				const auto upkeep = F4T::WorkshopUpkeep::Evaluate(unixSeconds, *paidUntil);
				if (!upkeep.decaying) {
					continue;
				}
				if (entry.second.TickUnpaidDecay(dtHours, unixSeconds)) {
					PrunePowerNodes(entry.second);
					MarkWorkshopDirty(entry.first);
				}
			}
			std::vector<OutboundFrame> frames;
			for (const auto& entry : workshopZones) {
				if (entry.second.Tombstones().size() != tombstonesBefore[entry.first]) {
					AppendFrames(frames, settlementAccess.StateFrames(FactionPeers(), entry.first));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> TickWorkshopRaidWindows(double a_monotonicSeconds)
		{
			std::vector<OutboundFrame> frames;
			std::erase_if(workshopRaidDeclarations, [&](const auto& a_entry) {
				return !F4T::WorkshopRaids::RaidDeclarationLive(a_entry.second, a_monotonicSeconds);
			});
			std::erase_if(workshopStructureRepairReady, [&](const auto& a_entry) {
				return a_entry.second <= a_monotonicSeconds;
			});
			auto charges = ResendSettlementCharges(a_monotonicSeconds);
			frames.insert(frames.end(), std::make_move_iterator(charges.begin()), std::make_move_iterator(charges.end()));
			std::erase_if(workshopStructureHitReady, [&](const auto& a_entry) {
				return a_entry.second <= a_monotonicSeconds;
			});
			std::erase_if(workshopRaidHitNoticeReady, [&](const auto& a_entry) {
				return a_entry.second <= a_monotonicSeconds;
			});
			std::erase_if(workshopStructureDamageBudgets, [&](const auto& a_entry) {
				return a_entry.second.tokens + (a_monotonicSeconds - a_entry.second.lastSeconds) * F4T::WorkshopRaids::StructureDamageBudgetPerSecond >= F4T::WorkshopRaids::StructureDamageBudgetPerSecond;
			});
			for (auto it = workshopAttackStates.begin(); it != workshopAttackStates.end();) {
				const auto zoneId = it->first;
				const auto next = F4T::WorkshopRaids::RefreshAttack(it->second, a_monotonicSeconds, false);
				if (!next.underAttack) {
					it = workshopAttackStates.erase(it);
					auto cleared = FanoutZoneAttackState(zoneId);
					frames.insert(frames.end(), std::make_move_iterator(cleared.begin()), std::make_move_iterator(cleared.end()));
					continue;
				}
				it->second = next;
				++it;
			}
			return frames;
		}
