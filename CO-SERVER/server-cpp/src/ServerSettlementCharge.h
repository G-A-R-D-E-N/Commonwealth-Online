			struct SettlementChargeAnchor
			{
				double x{};
				double y{};
				double z{};
			};

			struct PendingSettlementCharge
			{
				std::uint32_t connectionId{};
				std::string profileId;
				F4T::Workshop::BuildZoneId buildZoneId{};
				F4T::Workshop::ObjectId objectId{};
				F4T::SettlementCharge::Purpose purpose{};
				F4T::SettlementCharge::Charge charge;
				std::optional<SettlementChargeAnchor> anchor;
				double resendMonotonic{};
				bool acked{};
				bool frameSent{};
				bool settled{};
				std::map<std::string, std::uint64_t> removedAtBegin;
				double confirmDeadlineMonotonic{};
				bool confirmed{};
				std::optional<double> unackedSinceMonotonic;
				bool stale{};
			};

			static constexpr double kSettlementChargeResendSeconds{ 5.0 };
			static constexpr double kSettlementChargeOfflineExpirySeconds{ 60.0 };
			static constexpr double kSettlementChargeConfirmSeconds{ 600.0 };
			static constexpr double kUnackedChargeExpirySeconds{ 120.0 };
			static constexpr std::size_t kMaxStaleSettlementCharges{ 4 };
			static constexpr double kSettlementChargeReachUnits{ 400.0 };
			static constexpr int kSettlementCommitAttempts{ 3 };
			static constexpr std::uint64_t kSettlementRefundOperationTag{ 1ULL << 62 };
			std::map<std::uint64_t, PendingSettlementCharge> pendingSettlementCharges;
			std::uint64_t nextSettlementChargeOperationId{ static_cast<std::uint64_t>(std::time(nullptr)) * 1000ULL };

			std::vector<OutboundFrame> SettlementChargeReject(
				std::uint32_t a_connectionId,
				F4T::Workshop::BuildZoneId a_buildZoneId,
				F4T::SettlementAccess::ApplyStatus a_status)
			{
				return { ReliableFrame(a_connectionId, F4T::SettlementAccessProtocol::MakeReject(a_buildZoneId, a_status)) };
			}

			bool SettlementChargeAuthorized(const std::string& a_profileId, F4T::Workshop::BuildZoneId a_buildZoneId, bool a_allowBuilders) const
			{
				const auto owner = settlementAccess.OwnerOf(a_buildZoneId);
				if (owner.empty()) {
					return false;
				}
				return owner == a_profileId || (a_allowBuilders && settlementAccess.CanPlace(a_buildZoneId, a_profileId, IsNamedWorkshopZone(a_buildZoneId)));
			}

			F4T::SettlementAccess::ApplyStatus SettlementChargeGate(const Session& a_session, F4T::Workshop::BuildZoneId a_buildZoneId, bool a_allowBuilders = false) const
			{
				using ApplyStatus = F4T::SettlementAccess::ApplyStatus;
				if (a_session.profileId.empty() || !workshopZones.contains(a_buildZoneId)) {
					return ApplyStatus::UnknownBuildZone;
				}
				if (!SettlementChargeAuthorized(a_session.profileId, a_buildZoneId, a_allowBuilders)) {
					return a_allowBuilders ? ApplyStatus::NotAllowed : ApplyStatus::NotOwner;
				}
				const auto attack = workshopAttackStates.find(a_buildZoneId);
				if (attack != workshopAttackStates.end() && attack->second.underAttack) {
					return ApplyStatus::UnderAttack;
				}
				std::size_t stale{};
				for (const auto& entry : pendingSettlementCharges) {
					if (entry.second.settled || entry.second.profileId != a_session.profileId) {
						continue;
					}
					if (!entry.second.stale) {
						return ApplyStatus::ChargeFailed;
					}
					++stale;
				}
				return stale < kMaxStaleSettlementCharges ? ApplyStatus::Committed : ApplyStatus::ChargeFailed;
			}

			static bool SettlementChargeInReach(const Session& a_session, const std::optional<SettlementChargeAnchor>& a_anchor)
			{
				if (!a_anchor) {
					return true;
				}
				if (!a_session.lastMovement) {
					return false;
				}
				const auto& movement = *a_session.lastMovement;
				const double dx = movement.x - a_anchor->x;
				const double dy = movement.y - a_anchor->y;
				const double dz = movement.z - a_anchor->z;
				return dx * dx + dy * dy + dz * dz <= kSettlementChargeReachUnits * kSettlementChargeReachUnits;
			}

			bool SettlementChargeAuthorizedFor(const PendingSettlementCharge& a_charge) const
			{
				const bool builders = a_charge.purpose == F4T::SettlementCharge::Purpose::StructureRepair ||
					a_charge.purpose == F4T::SettlementCharge::Purpose::StructureRebuild ||
					a_charge.purpose == F4T::SettlementCharge::Purpose::WorkbenchRepair;
				return SettlementChargeAuthorized(a_charge.profileId, a_charge.buildZoneId, builders);
			}

			bool SettlementChargeMayCommit(const PendingSettlementCharge& a_charge, const Session& a_session) const
			{
				return SettlementChargeAuthorizedFor(a_charge) && SettlementChargeInReach(a_session, a_charge.anchor);
			}

			std::uint64_t SettlementRemovals(const std::string& a_profileId, const std::string& a_itemKey) const
			{
				return inventoryAuthority == nullptr ? std::uint64_t{} : inventoryAuthority->Store().SnapshotRemovals(a_profileId, a_itemKey);
			}

			std::optional<std::map<std::string, std::uint64_t>> SettlementChargeMarks(const std::string& a_profileId, const F4T::SettlementCharge::Charge& a_charge)
			{
				std::map<std::string, std::uint64_t> marks;
				if (a_charge.empty()) {
					return marks;
				}
				if (inventoryAuthority == nullptr) {
					return std::nullopt;
				}
				const auto inventory = inventoryAuthority->Store().GetInventory(a_profileId);
				if (!inventory || !inventory->contains("items")) {
					return std::nullopt;
				}
				const auto& items = inventory->at("items");
				for (const auto& line : a_charge) {
					const auto found = items.find(line.itemKey);
					if (found == items.end() || found->at("count").get<std::uint32_t>() < line.count) {
						return std::nullopt;
					}
					marks[line.itemKey] = SettlementRemovals(a_profileId, line.itemKey);
				}
				return marks;
			}

			bool SettlementChargeItemsGone(const PendingSettlementCharge& a_pending) const
			{
				for (const auto& line : a_pending.charge) {
					const auto mark = a_pending.removedAtBegin.find(line.itemKey);
					if (mark == a_pending.removedAtBegin.end()) {
						return false;
					}
					const auto removed = SettlementRemovals(a_pending.profileId, line.itemKey);
					if (removed < mark->second || removed - mark->second < line.count) {
						return false;
					}
				}
				return true;
			}

			std::optional<std::uint64_t> SettlementChargeEraseId(std::uint64_t a_operationId) const
			{
				return workshopPersistence == nullptr ? std::nullopt : std::optional<std::uint64_t>{ a_operationId };
			}

			std::uint64_t SettlementChargeRetainFrom(const std::string& a_profileId) const
			{
				for (const auto& [operationId, pending] : pendingSettlementCharges) {
					if (pending.profileId == a_profileId) {
						return operationId;
					}
				}
				return nextSettlementChargeOperationId;
			}

			bool SettlementChargeOwnerOnline(const std::string& a_profileId) const
			{
				for (const auto& entry : sessions) {
					if (entry.second.active && entry.second.profileId == a_profileId) {
						return true;
					}
				}
				return false;
			}

			OutboundFrame SettlementChargeFrame(std::uint32_t a_connectionId, std::uint64_t a_operationId, const PendingSettlementCharge& a_pending)
			{
				auto packet = F4T::SettlementCharge::MakeCharge(a_operationId, a_pending.buildZoneId, a_pending.purpose, a_pending.charge);
				packet["retainFromOperationId"] = SettlementChargeRetainFrom(a_pending.profileId);
				return ReliableFrame(a_connectionId, packet);
			}

			static Json SettlementChargeRecord(std::uint64_t a_operationId, const PendingSettlementCharge& a_pending)
			{
				Json items = Json::array();
				for (const auto& line : a_pending.charge) {
					const auto mark = a_pending.removedAtBegin.find(line.itemKey);
					items.push_back(Json{ { "itemKey", line.itemKey }, { "count", line.count }, { "removedAtBegin", mark == a_pending.removedAtBegin.end() ? std::uint64_t{} : mark->second } });
				}
				Json record{
					{ "operationId", a_operationId },
					{ "profileId", a_pending.profileId },
					{ "buildZoneId", a_pending.buildZoneId },
					{ "objectId", a_pending.objectId },
					{ "purpose", static_cast<std::uint64_t>(a_pending.purpose) },
					{ "items", std::move(items) },
					{ "settled", a_pending.settled }
				};
				if (a_pending.anchor) {
					record["anchor"] = Json{ { "x", a_pending.anchor->x }, { "y", a_pending.anchor->y }, { "z", a_pending.anchor->z } };
				}
				return record;
			}

			static std::optional<PendingSettlementCharge> ParseSettlementChargeRecord(const Json& a_record, std::uint64_t& a_operationId)
			{
				try {
					PendingSettlementCharge pending;
					a_operationId = a_record.at("operationId").get<std::uint64_t>();
					pending.profileId = a_record.at("profileId").get<std::string>();
					pending.buildZoneId = a_record.at("buildZoneId").get<std::uint64_t>();
					pending.objectId = a_record.at("objectId").get<std::uint64_t>();
					const auto purpose = a_record.at("purpose").get<std::uint64_t>();
					if (purpose > static_cast<std::uint64_t>(F4T::SettlementCharge::Purpose::SettlerRecruit)) {
						return std::nullopt;
					}
					pending.purpose = static_cast<F4T::SettlementCharge::Purpose>(purpose);
					for (const auto& line : a_record.at("items")) {
						pending.charge.push_back(F4T::SettlementCharge::Line{ line.at("itemKey").get<std::string>(), line.at("count").get<std::uint32_t>() });
						pending.removedAtBegin[line.at("itemKey").get<std::string>()] = line.at("removedAtBegin").get<std::uint64_t>();
					}
					pending.settled = a_record.value("settled", false);
					if (const auto anchor = a_record.find("anchor"); anchor != a_record.end()) {
						pending.anchor = SettlementChargeAnchor{ anchor->at("x").get<double>(), anchor->at("y").get<double>(), anchor->at("z").get<double>() };
					}
					return pending;
				} catch (const Json::exception&) {
					return std::nullopt;
				}
			}

			bool PersistSettlementCharge(std::uint64_t a_operationId, const PendingSettlementCharge& a_pending)
			{
				return workshopPersistence == nullptr || workshopPersistence->Store().PostSettlementCharge(SettlementChargeRecord(a_operationId, a_pending));
			}

			bool ForgetSettlementCharge(std::uint64_t a_operationId)
			{
				return workshopPersistence == nullptr || workshopPersistence->Store().EraseSettlementCharge(a_operationId);
			}

			void RestoreSettlementCharges(const Json& a_records, std::uint64_t a_nextOperationId)
			{
				nextSettlementChargeOperationId = (std::max)(nextSettlementChargeOperationId, a_nextOperationId);
				for (const auto& record : a_records) {
					std::uint64_t operationId{};
					auto pending = ParseSettlementChargeRecord(record, operationId);
					nextSettlementChargeOperationId = (std::max)(nextSettlementChargeOperationId, operationId + 1);
					if (!pending) {
						if (operationId != 0) {
							const bool erased = ForgetSettlementCharge(operationId);
							std::cerr << "Settlement charge record " << operationId << " is unreadable (no baseline or invalid); " << (erased ? "erased\n" : "erase failed, retried on next start\n");
						}
						continue;
					}
					if (operationId == 0) {
						continue;
					}
					pending->frameSent = true;
					pending->settled = pending->settled || pending->charge.empty();
					pendingSettlementCharges[operationId] = std::move(*pending);
				}
			}

			std::vector<OutboundFrame> BeginSettlementCharge(
				std::uint32_t a_connectionId,
				const Session& a_session,
				F4T::SettlementCharge::Purpose a_purpose,
				F4T::Workshop::BuildZoneId a_buildZoneId,
				F4T::Workshop::ObjectId a_objectId,
				const F4T::SettlementCharge::Charge& a_charge,
				double a_monotonicSeconds,
				std::optional<SettlementChargeAnchor> a_anchor = std::nullopt)
			{
				if (!SettlementChargeInReach(a_session, a_anchor)) {
					return SettlementChargeReject(a_connectionId, a_buildZoneId, F4T::SettlementAccess::ApplyStatus::TooFar);
				}
				const auto marks = SettlementChargeMarks(a_session.profileId, a_charge);
				if (!marks) {
					return SettlementChargeReject(a_connectionId, a_buildZoneId, F4T::SettlementAccess::ApplyStatus::InsufficientMaterials);
				}
				const auto operationId = nextSettlementChargeOperationId++;
				auto& pending = pendingSettlementCharges[operationId];
				pending = PendingSettlementCharge{
					a_connectionId,
					a_session.profileId,
					a_buildZoneId,
					a_objectId,
					a_purpose,
					a_charge,
					a_anchor,
					a_monotonicSeconds + kSettlementChargeResendSeconds
				};
				pending.removedAtBegin = *marks;
				pending.confirmed = a_charge.empty();
				pending.unackedSinceMonotonic = a_monotonicSeconds;
				if (!PersistSettlementCharge(operationId, pending)) {
					pendingSettlementCharges.erase(operationId);
					return SettlementChargeReject(a_connectionId, a_buildZoneId, F4T::SettlementAccess::ApplyStatus::ChargeFailed);
				}
				return { SettlementChargeFrame(a_connectionId, operationId, pending) };
			}

			std::vector<OutboundFrame> ResendSettlementCharges(double a_monotonicSeconds)
			{
				std::vector<OutboundFrame> frames;
				for (auto it = pendingSettlementCharges.begin(); it != pendingSettlementCharges.end();) {
					auto& [operationId, pending] = *it;
					if (!pending.acked && !pending.settled && !pending.unackedSinceMonotonic) {
						pending.unackedSinceMonotonic = a_monotonicSeconds;
					}
					if (!pending.acked) {
						pending.stale = !pending.settled && pending.unackedSinceMonotonic &&
							a_monotonicSeconds >= *pending.unackedSinceMonotonic + kUnackedChargeExpirySeconds;
					}
					const bool awaitingConfirmation = pending.acked && !pending.confirmed;
					if (awaitingConfirmation && !SettlementChargeOwnerOnline(pending.profileId)) {
						pending.confirmDeadlineMonotonic = a_monotonicSeconds + kSettlementChargeConfirmSeconds;
					}
					if (pending.resendMonotonic > a_monotonicSeconds) {
						++it;
						continue;
					}
					bool delivered{};
					if (!pending.settled && !awaitingConfirmation) {
						for (const auto& [connectionId, peer] : sessions) {
							if (peer.active && peer.profileId == pending.profileId) {
								pending.connectionId = connectionId;
								pending.resendMonotonic = a_monotonicSeconds + kSettlementChargeResendSeconds;
								frames.push_back(SettlementChargeFrame(connectionId, operationId, pending));
								delivered = true;
								break;
							}
						}
					}
					const bool expired = !pending.settled && !delivered && !pending.acked && !pending.frameSent &&
						a_monotonicSeconds >= pending.resendMonotonic + kSettlementChargeOfflineExpirySeconds;
					if (!pending.settled && awaitingConfirmation && !pending.stale && a_monotonicSeconds >= pending.confirmDeadlineMonotonic) {
						pending.stale = true;
						for (const auto& [connectionId, peer] : sessions) {
							if (peer.active && peer.profileId == pending.profileId) {
								frames.push_back(ReliableFrame(connectionId, F4T::SettlementAccessProtocol::MakeReject(pending.buildZoneId, F4T::SettlementAccess::ApplyStatus::ChargeFailed)));
								break;
							}
						}
					}
					if (pending.settled || expired) {
						if (ForgetSettlementCharge(operationId)) {
							it = pendingSettlementCharges.erase(it);
							continue;
						}
						pending.settled = true;
						pending.resendMonotonic = a_monotonicSeconds + kSettlementChargeResendSeconds;
					}
					++it;
				}
				return frames;
			}

			std::vector<OutboundFrame> StructureDamageFrames(
				std::uint32_t a_connectionId,
				F4T::Workshop::BuildZoneId a_buildZoneId,
				F4T::Workshop::ObjectId a_objectId)
			{
				const auto zone = workshopZones.find(a_buildZoneId);
				if (zone == workshopZones.end()) {
					return {};
				}
				const auto* live = zone->second.Find(a_objectId);
				const float health = live ? live->health : 0.0F;
				const float maxHealth = live ? live->maxHealth : 0.0F;
				const auto packet = F4T::WorkshopProtocol::MakeStructureDamage(F4T::WorkshopProtocol::StructureDamage{
					a_buildZoneId,
					a_objectId,
					health,
					maxHealth,
					zone->second.CurrentRevision(),
					F4T::WorkshopRaids::DamageStage(health, maxHealth)
				});
				std::vector<OutboundFrame> frames;
				for (const auto& [connectionId, peer] : sessions) {
					if (peer.active && (connectionId == a_connectionId || peer.workshopZonesInside.contains(a_buildZoneId))) {
						frames.push_back(ReliableFrame(connectionId, packet));
					}
				}
				return frames;
			}

			std::optional<std::vector<OutboundFrame>> CommitStructureRepair(std::uint64_t a_operationId, const PendingSettlementCharge& a_charge, std::uint64_t a_unixSeconds)
			{
				const auto zone = workshopZones.find(a_charge.buildZoneId);
				const auto* live = zone == workshopZones.end() ? nullptr : zone->second.Find(a_charge.objectId);
				if (!live || !(live->health < live->maxHealth)) {
					return std::nullopt;
				}
				auto candidate = zone->second;
				if (!candidate.RepairObject(a_charge.objectId, live->maxHealth - live->health, a_unixSeconds) || !SaveWorkshopZone(candidate, SettlementChargeEraseId(a_operationId))) {
					return std::nullopt;
				}
				zone->second = std::move(candidate);
				MarkWorkshopDirty(a_charge.buildZoneId);
				return StructureDamageFrames(a_charge.connectionId, a_charge.buildZoneId, a_charge.objectId);
			}

			std::optional<std::vector<OutboundFrame>> CommitStructureRebuild(std::uint64_t a_operationId, const PendingSettlementCharge& a_charge)
			{
				const auto zone = workshopZones.find(a_charge.buildZoneId);
				if (zone == workshopZones.end()) {
					return std::nullopt;
				}
				auto candidate = zone->second;
				if (!candidate.RebuildFromTombstone(a_charge.objectId, 0.5F) || !SaveWorkshopZone(candidate, SettlementChargeEraseId(a_operationId))) {
					return std::nullopt;
				}
				zone->second = std::move(candidate);
				MarkWorkshopDirty(a_charge.buildZoneId);
				auto frames = StructureDamageFrames(a_charge.connectionId, a_charge.buildZoneId, a_charge.objectId);
				auto states = settlementAccess.StateFrames(FactionPeers(), a_charge.buildZoneId);
				frames.insert(frames.end(), states.begin(), states.end());
				return frames;
			}

			std::optional<std::vector<OutboundFrame>> CommitWorkbenchRepair(std::uint64_t a_operationId, const PendingSettlementCharge& a_charge)
			{
				const auto settlement = settlementAccess.Find(a_charge.buildZoneId);
				if (!settlement) {
					return std::nullopt;
				}
				const auto maxHp = F4T::SettlementAccess::WorkbenchMaxHpFor(settlement->kind);
				const auto current = settlement->workbenchBroken ? 0.0F : settlement->workbenchHp;
				std::string failure;
				auto frames = settlementAccess.RepairWorkbench(FactionPeers(), a_charge.buildZoneId, maxHp - current, failure, SettlementChargeEraseId(a_operationId));
				if (!failure.empty()) {
					return std::nullopt;
				}
				return frames;
			}

			std::optional<std::vector<OutboundFrame>> CommitUpkeepPay(std::uint64_t a_operationId, const PendingSettlementCharge& a_charge, std::uint64_t a_unixSeconds)
			{
				const auto paidUntil = settlementAccess.UpkeepPaidUntil(a_charge.buildZoneId);
				const auto base = paidUntil && *paidUntil > a_unixSeconds ? *paidUntil : a_unixSeconds;
				std::string failure;
				auto frames = settlementAccess.SetUpkeepPaidUntil(
					FactionPeers(),
					a_charge.buildZoneId,
					base + F4T::SettlementCharge::kUpkeepBlockHours * 60ull * 60ull,
					failure,
					SettlementChargeEraseId(a_operationId));
				if (!failure.empty()) {
					return std::nullopt;
				}
				return frames;
			}

			std::optional<std::vector<OutboundFrame>> CommitSettlementCharge(std::uint64_t a_operationId, const PendingSettlementCharge& a_charge, const Session& a_session, std::uint64_t a_unixSeconds)
			{
				if (!SettlementChargeMayCommit(a_charge, a_session)) {
					return std::nullopt;
				}
				switch (a_charge.purpose) {
				case F4T::SettlementCharge::Purpose::StructureRepair:
					return CommitStructureRepair(a_operationId, a_charge, a_unixSeconds);
				case F4T::SettlementCharge::Purpose::StructureRebuild:
					return CommitStructureRebuild(a_operationId, a_charge);
				case F4T::SettlementCharge::Purpose::WorkbenchRepair:
					return CommitWorkbenchRepair(a_operationId, a_charge);
				case F4T::SettlementCharge::Purpose::UpkeepPay:
					return CommitUpkeepPay(a_operationId, a_charge, a_unixSeconds);
				case F4T::SettlementCharge::Purpose::SettlerRecruit:
					return CommitSettlerRecruit(a_charge, a_unixSeconds);
				}
				return std::nullopt;
			}

			std::optional<std::vector<OutboundFrame>> RefundSettlementCharge(std::uint64_t a_operationId, const PendingSettlementCharge& a_charge)
			{
				if (a_charge.charge.empty()) {
					return std::vector<OutboundFrame>{};
				}
				if (workshopPersistence == nullptr) {
					return std::nullopt;
				}
				std::vector<F4T::Trade::Line> lines;
				lines.reserve(a_charge.charge.size());
				for (const auto& line : a_charge.charge) {
					lines.push_back(F4T::Trade::Line{ line.itemKey, line.count });
				}
				const auto refundOperationId = a_operationId | kSettlementRefundOperationTag;
				F4T::CampGrants::Commit grant;
				try {
					grant = workshopPersistence->Store().CommitChargeRefund(a_charge.profileId, refundOperationId, lines);
				} catch (const std::exception& error) {
					std::cerr << "Settlement charge refund unavailable: " << error.what() << '\n';
					return std::nullopt;
				}
				if (grant.outcome == F4T::CampGrants::Outcome::Rejected) {
					return std::nullopt;
				}
				if (grant.outcome == F4T::CampGrants::Outcome::Replayed) {
					return std::vector<OutboundFrame>{};
				}
				return std::vector<OutboundFrame>{ ReliableFrame(a_charge.connectionId, F4T::CampGrants::MakeGrantPacket(refundOperationId, grant.chunks)) };
			}

			void SettleSettlementCharge(std::uint64_t a_operationId, PendingSettlementCharge a_pending, double a_monotonicSeconds)
			{
				if (ForgetSettlementCharge(a_operationId)) {
					pendingSettlementCharges.erase(a_operationId);
					return;
				}
				a_pending.settled = true;
				a_pending.resendMonotonic = a_monotonicSeconds + kSettlementChargeResendSeconds;
				(void)PersistSettlementCharge(a_operationId, a_pending);
				pendingSettlementCharges[a_operationId] = std::move(a_pending);
			}

			void MarkSettlementChargeFrameSent(const OutboundFrame& a_frame)
			{
				const std::string_view bytes{ reinterpret_cast<const char*>(a_frame.payload.data()), a_frame.payload.size() };
				if (bytes.find("\"settlementCharge\"") == std::string_view::npos) {
					return;
				}
				const auto packet = Json::parse(std::string{ bytes }, nullptr, false);
				if (!packet.is_object() || packet.value("type", std::string{}) != "settlementCharge") {
					return;
				}
				const auto pending = pendingSettlementCharges.find(packet.value("operationId", std::uint64_t{}));
				if (pending != pendingSettlementCharges.end()) {
					pending->second.frameSent = true;
				}
			}

			void CreditSettlementRemovals(std::uint64_t a_operationId, const PendingSettlementCharge& a_settled)
			{
				for (auto& [operationId, other] : pendingSettlementCharges) {
					if (operationId == a_operationId || other.settled || other.profileId != a_settled.profileId) {
						continue;
					}
					bool raised{};
					for (const auto& line : a_settled.charge) {
						const auto settledMark = a_settled.removedAtBegin.find(line.itemKey);
						const auto mark = other.removedAtBegin.find(line.itemKey);
						if (settledMark == a_settled.removedAtBegin.end() || mark == other.removedAtBegin.end()) {
							continue;
						}
						const std::uint64_t consumed = settledMark->second + line.count;
						if (mark->second < consumed) {
							mark->second = consumed;
							raised = true;
						}
					}
					if (raised && !PersistSettlementCharge(operationId, other)) {
						std::cerr << "settlement charge " << operationId << " removal mark not persisted; kept in memory\n";
					}
				}
			}

			std::vector<OutboundFrame> CommitConfirmedSettlementCharge(std::uint64_t a_operationId, PendingSettlementCharge a_charge, const Session& a_session, double a_monotonicSeconds, double a_unixTimeSeconds)
			{
				const auto unixSeconds = static_cast<std::uint64_t>(a_unixTimeSeconds);
				const bool outOfReach = SettlementChargeAuthorizedFor(a_charge) && !SettlementChargeInReach(a_session, a_charge.anchor);
				for (int attempt = 0; !outOfReach && attempt < kSettlementCommitAttempts; ++attempt) {
					if (auto frames = CommitSettlementCharge(a_operationId, a_charge, a_session, unixSeconds)) {
						if (a_charge.stale) {
							std::cerr << "late settlement charge commit op=" << a_operationId << '\n';
						}
						SettleSettlementCharge(a_operationId, a_charge, a_monotonicSeconds);
						CreditSettlementRemovals(a_operationId, a_charge);
						return std::move(*frames);
					}
				}
				if (auto grant = RefundSettlementCharge(a_operationId, a_charge)) {
					SettleSettlementCharge(a_operationId, a_charge, a_monotonicSeconds);
					CreditSettlementRemovals(a_operationId, a_charge);
					auto frames = SettlementChargeReject(a_charge.connectionId, a_charge.buildZoneId, outOfReach ? F4T::SettlementAccess::ApplyStatus::TooFar : F4T::SettlementAccess::ApplyStatus::ChargeFailed);
					frames.insert(frames.end(), grant->begin(), grant->end());
					return frames;
				}
				a_charge.resendMonotonic = a_monotonicSeconds + kSettlementChargeResendSeconds;
				pendingSettlementCharges[a_operationId] = std::move(a_charge);
				return {};
			}

			std::vector<OutboundFrame> ConfirmSettlementCharges(std::uint32_t a_connectionId, const Session& a_session, double a_monotonicSeconds, double a_unixTimeSeconds)
			{
				if (inventoryAuthority == nullptr) {
					return {};
				}
				std::vector<std::uint64_t> ready;
				for (auto& [operationId, pending] : pendingSettlementCharges) {
					if (pending.settled || pending.profileId != a_session.profileId) {
						continue;
					}
					pending.confirmed = SettlementChargeItemsGone(pending);
					if (pending.acked && pending.confirmed) {
						ready.push_back(operationId);
					}
				}
				std::vector<OutboundFrame> frames;
				for (const auto operationId : ready) {
					const auto pending = pendingSettlementCharges.find(operationId);
					if (pending == pendingSettlementCharges.end() || pending->second.settled || !SettlementChargeItemsGone(pending->second)) {
						continue;
					}
					auto charge = pending->second;
					charge.connectionId = a_connectionId;
					auto committed = CommitConfirmedSettlementCharge(operationId, std::move(charge), a_session, a_monotonicSeconds, a_unixTimeSeconds);
					frames.insert(frames.end(), committed.begin(), committed.end());
				}
				return frames;
			}

			void ExpireUnconfirmedStaleSettlementCharges(const std::string& a_profileId)
			{
				for (auto it = pendingSettlementCharges.begin(); it != pendingSettlementCharges.end();) {
					const auto& pending = it->second;
					if (pending.settled || !pending.acked || !pending.stale || pending.profileId != a_profileId || SettlementChargeItemsGone(pending)) {
						++it;
						continue;
					}
					if (!ForgetSettlementCharge(it->first)) {
						++it;
						continue;
					}
					std::cerr << "settlement charge " << it->first << " expired without a removal; no refund\n";
					it = pendingSettlementCharges.erase(it);
				}
			}

			std::vector<OutboundFrame> HandleSettlementChargeResult(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds,
				double a_unixTimeSeconds)
			{
				const auto result = F4T::SettlementCharge::NormalizeResult(a_packet);
				const auto pending = result ? pendingSettlementCharges.find(result->operationId) : pendingSettlementCharges.end();
				if (!result || pending == pendingSettlementCharges.end() || pending->second.settled || pending->second.profileId != a_session.profileId) {
					Reject();
					return {};
				}
				auto charge = pending->second;
				charge.connectionId = a_connectionId;
				if (!result->ok) {
					if (pending->second.acked) {
						return {};
					}
					SettleSettlementCharge(result->operationId, charge, a_monotonicSeconds);
					const auto status = result->reason == "missingMaterials"
						? F4T::SettlementAccess::ApplyStatus::InsufficientMaterials
						: F4T::SettlementAccess::ApplyStatus::ChargeFailed;
					return SettlementChargeReject(a_connectionId, charge.buildZoneId, status);
				}
				if (!pending->second.acked) {
					pending->second.acked = true;
					pending->second.stale = false;
					pending->second.confirmDeadlineMonotonic = a_monotonicSeconds + kSettlementChargeConfirmSeconds;
				}
				return ConfirmSettlementCharges(a_connectionId, a_session, a_monotonicSeconds, a_unixTimeSeconds);
			}

			std::vector<OutboundFrame> HandleWorkbenchRepair(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds)
			{
				const auto zoneId = F4T::SettlementCharge::NormalizeZoneRequest(a_packet, "workbenchRepair");
				if (!zoneId) {
					Reject();
					return {};
				}
				using ApplyStatus = F4T::SettlementAccess::ApplyStatus;
				const auto gate = SettlementChargeGate(a_session, *zoneId, true);
				if (gate != ApplyStatus::Committed) {
					return SettlementChargeReject(a_connectionId, *zoneId, gate);
				}
				const auto& policy = workshopZones.at(*zoneId).Policy();
				if (!policy.hasWorkbench) {
					return SettlementChargeReject(a_connectionId, *zoneId, ApplyStatus::NothingToRepair);
				}
				const auto settlement = settlementAccess.Find(*zoneId);
				if (!settlement) {
					return SettlementChargeReject(a_connectionId, *zoneId, ApplyStatus::UnknownBuildZone);
				}
				const auto maxHp = F4T::SettlementAccess::WorkbenchMaxHpFor(settlement->kind);
				const auto missing = settlement->workbenchBroken ? maxHp : maxHp - settlement->workbenchHp;
				if (!(missing > 0.0F)) {
					return SettlementChargeReject(a_connectionId, *zoneId, ApplyStatus::NothingToRepair);
				}
				const SettlementChargeAnchor anchor{ policy.workbenchX, policy.workbenchY, policy.workbenchZ };
				return BeginSettlementCharge(
					a_connectionId,
					a_session,
					F4T::SettlementCharge::Purpose::WorkbenchRepair,
					*zoneId,
					F4T::WorkshopRaids::WorkbenchObjectId,
					F4T::SettlementCharge::WorkbenchRepairCharge(missing, maxHp),
					a_monotonicSeconds,
					anchor);
			}

			std::vector<OutboundFrame> HandleUpkeepPay(
				std::uint32_t a_connectionId,
				Session& a_session,
				const Json& a_packet,
				double a_monotonicSeconds)
			{
				const auto zoneId = F4T::SettlementCharge::NormalizeZoneRequest(a_packet, "upkeepPay");
				if (!zoneId) {
					Reject();
					return {};
				}
				const auto gate = SettlementChargeGate(a_session, *zoneId);
				if (gate != F4T::SettlementAccess::ApplyStatus::Committed) {
					return SettlementChargeReject(a_connectionId, *zoneId, gate);
				}
				std::uint64_t wood{};
				std::uint64_t scrap{};
				std::uint64_t concrete{};
				std::uint64_t reinforced{};
				for (const auto& object : workshopZones.at(*zoneId).Objects()) {
					switch (F4T::SettlementCharge::TierForMaxHealth(object.maxHealth)) {
					case F4T::WorkshopMaterials::MaterialTier::Wood:
						++wood;
						break;
					case F4T::WorkshopMaterials::MaterialTier::ScrapMetal:
						++scrap;
						break;
					case F4T::WorkshopMaterials::MaterialTier::Concrete:
						++concrete;
						break;
					case F4T::WorkshopMaterials::MaterialTier::Reinforced:
						++reinforced;
						break;
					}
				}
				const auto charge = F4T::SettlementCharge::UpkeepBlockCharge(wood, scrap, concrete, reinforced);
				if (charge.empty()) {
					return SettlementChargeReject(a_connectionId, *zoneId, F4T::SettlementAccess::ApplyStatus::NothingToRepair);
				}
				return BeginSettlementCharge(
					a_connectionId,
					a_session,
					F4T::SettlementCharge::Purpose::UpkeepPay,
					*zoneId,
					F4T::Workshop::ObjectId{},
					charge,
					a_monotonicSeconds);
			}
