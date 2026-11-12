		std::vector<OutboundFrame> HandleStructureRepair(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			const auto repair = F4T::WorkshopProtocol::NormalizeStructureRepair(a_packet);
			if (!repair) {
				Reject();
				return {};
			}
			using ApplyStatus = F4T::SettlementAccess::ApplyStatus;
			const auto gate = SettlementChargeGate(a_session, repair->buildZoneId, true);
			if (gate != ApplyStatus::Committed) {
				return SettlementChargeReject(a_connectionId, repair->buildZoneId, gate);
			}
			const auto zone = workshopZones.find(repair->buildZoneId);
			const auto* live = zone->second.Find(repair->objectId);
			if (!live) {
				return SettlementChargeReject(a_connectionId, repair->buildZoneId, ApplyStatus::NothingToRepair);
			}
			if (!(live->health < live->maxHealth)) {
				return SettlementChargeReject(a_connectionId, repair->buildZoneId, ApplyStatus::NothingToRepair);
			}
			const auto repairKey = std::pair{ repair->buildZoneId, repair->objectId };
			const auto ready = workshopStructureRepairReady.find(repairKey);
			if (ready != workshopStructureRepairReady.end() && a_monotonicSeconds < ready->second) {
				Reject();
				return {};
			}
			const auto pendingBefore = pendingSettlementCharges.size();
			auto frames = BeginSettlementCharge(
				a_connectionId,
				a_session,
				F4T::SettlementCharge::Purpose::StructureRepair,
				repair->buildZoneId,
				repair->objectId,
				F4T::SettlementCharge::StructureRepairCharge(
					F4T::SettlementCharge::TierForMaxHealth(live->maxHealth),
					F4T::Workshop::CappedRepairAmount(live->maxHealth - live->health, live->maxHealth),
					live->maxHealth),
				a_monotonicSeconds,
				SettlementChargeAnchor{ live->transform.x, live->transform.y, live->transform.z });
			if (pendingSettlementCharges.size() != pendingBefore) {
				workshopStructureRepairReady[repairKey] = a_monotonicSeconds + F4T::WorkshopRaids::StructureRepairCooldownSeconds;
			}
			return frames;
		}

		std::vector<OutboundFrame> HandleStructureRebuild(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			const auto rebuild = F4T::WorkshopProtocol::NormalizeStructureRebuild(a_packet);
			if (!rebuild) {
				Reject();
				return {};
			}
			using ApplyStatus = F4T::SettlementAccess::ApplyStatus;
			const auto gate = SettlementChargeGate(a_session, rebuild->buildZoneId, true);
			if (gate != ApplyStatus::Committed) {
				return SettlementChargeReject(a_connectionId, rebuild->buildZoneId, gate);
			}
			auto probe = workshopZones.find(rebuild->buildZoneId)->second;
			if (!probe.RebuildFromTombstone(rebuild->objectId, 0.5F)) {
				return SettlementChargeReject(a_connectionId, rebuild->buildZoneId, ApplyStatus::NothingToRepair);
			}
			const auto* rebuilt = probe.Find(rebuild->objectId);
			if (!rebuilt) {
				return SettlementChargeReject(a_connectionId, rebuild->buildZoneId, ApplyStatus::NothingToRepair);
			}
			return BeginSettlementCharge(
				a_connectionId,
				a_session,
				F4T::SettlementCharge::Purpose::StructureRebuild,
				rebuild->buildZoneId,
				rebuild->objectId,
				F4T::SettlementCharge::StructureRebuildCharge(F4T::SettlementCharge::TierForMaxHealth(rebuilt->maxHealth)),
				a_monotonicSeconds,
				SettlementChargeAnchor{ rebuilt->transform.x, rebuilt->transform.y, rebuilt->transform.z });
		}

		std::vector<F4T::SettlementAccessProtocol::DestroyedStructure> DestroyedStructuresFor(F4T::Workshop::BuildZoneId a_buildZoneId) const
		{
			std::vector<F4T::SettlementAccessProtocol::DestroyedStructure> result;
			const auto zone = workshopZones.find(a_buildZoneId);
			if (zone == workshopZones.end()) {
				return result;
			}
			for (const auto& object : zone->second.Tombstones()) {
				if (!(object.maxHealth > 0.0F)) {
					continue;
				}
				const auto tier = F4T::SettlementCharge::TierForMaxHealth(object.maxHealth);
				result.push_back({ object.objectId, std::string{ F4T::SettlementCharge::TierName(tier) }, F4T::SettlementCharge::kFullStructureComponents });
			}
			return result;
		}

		std::vector<OutboundFrame> FanoutUpkeepStates(std::uint64_t a_unixSeconds)
		{
			std::vector<OutboundFrame> frames;
			for (const auto& [zoneId, zone] : workshopZones) {
				const auto paidUntil = settlementAccess.UpkeepPaidUntil(zoneId);
				if (!paidUntil) {
					continue;
				}
				std::uint64_t wood{};
				std::uint64_t scrap{};
				std::uint64_t concrete{};
				std::uint64_t reinforced{};
				for (const auto& object : zone.Objects()) {
					if (object.maxHealth <= 300.0F) {
						wood += 1;
					} else if (object.maxHealth <= 600.0F) {
						scrap += 1;
					} else if (object.maxHealth <= 1000.0F) {
						concrete += 1;
					} else {
						reinforced += 1;
					}
				}
				const auto hourly = F4T::WorkshopUpkeep::HourlyCostFromCounts(wood, scrap, concrete, reinforced);
				const auto upkeep = F4T::WorkshopUpkeep::Evaluate(a_unixSeconds, *paidUntil);
				const std::uint64_t hoursCovered = hourly == 0
					? 0
					: (*paidUntil > a_unixSeconds ? (*paidUntil - a_unixSeconds) / 3600ull : 0);
				const auto packet = F4T::WorkshopProtocol::MakeUpkeepState(zoneId, hourly, hoursCovered, upkeep.decaying);
				const auto owner = settlementAccess.OwnerOf(zoneId);
				for (const auto& [connectionId, peer] : sessions) {
					if (!peer.active) {
						continue;
					}
					if ((!owner.empty() && peer.profileId == owner) || settlementAccess.CanBuild(zoneId, peer.profileId)) {
						frames.push_back(ReliableFrame(connectionId, packet));
					}
				}
			}
			return frames;
		}
