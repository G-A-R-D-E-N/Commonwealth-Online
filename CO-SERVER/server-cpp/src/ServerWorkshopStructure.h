		[[nodiscard]] static bool SessionWithinZoneReach(const Session& a_session, const F4T::Workshop::SettlementState& a_zone)
		{
			return a_session.lastMovement && a_zone.WithinRadius(a_session.lastMovement->x, a_session.lastMovement->y, a_session.lastMovement->z);
		}

		std::vector<OutboundFrame> RefuseRaidHit(
			std::uint32_t a_connectionId,
			F4T::Workshop::BuildZoneId a_buildZoneId,
			F4T::WorkshopRaids::HitRefusal a_refusal,
			std::uint64_t a_secondsLeft,
			double a_monotonicSeconds)
		{
			Reject();
			const auto noticeKey = std::tuple{ a_connectionId, a_buildZoneId };
			const auto ready = workshopRaidHitNoticeReady.find(noticeKey);
			if (ready != workshopRaidHitNoticeReady.end() && a_monotonicSeconds < ready->second) {
				return {};
			}
			if (ready == workshopRaidHitNoticeReady.end() &&
				workshopRaidHitNoticeReady.size() >= F4T::WorkshopRaids::MaximumStructureHitCooldowns) {
				return {};
			}
			workshopRaidHitNoticeReady[noticeKey] = a_monotonicSeconds + F4T::WorkshopRaids::RaidHitRefusedNoticeSeconds;
			std::cerr << "Workshop raid hit refused: connection " << a_connectionId << " zone " << a_buildZoneId
			          << " reason " << F4T::WorkshopRaids::HitRefusalName(a_refusal) << '\n';
			return { ReliableFrame(a_connectionId, F4T::WorkshopProtocol::MakeRaidHitRefused(a_buildZoneId, a_refusal, a_secondsLeft)) };
		}

		std::optional<std::vector<OutboundFrame>> RefuseCaptureOnRaid(
			std::uint32_t a_connectionId,
			const Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			const auto request = F4T::SettlementCapture::NormalizeRequest(a_packet);
			if (!request || (request->op != F4T::SettlementCapture::Op::Start && request->op != F4T::SettlementCapture::Op::Continue)) {
				return std::nullopt;
			}
			const auto settlement = settlementAccess.Find(request->buildZoneId);
			if (!settlement || settlement->ownerProfileId.empty() || !settlement->workbenchBroken) {
				return std::nullopt;
			}
			const auto refusal = CaptureRefusalFor(
				request->buildZoneId,
				a_session.profileId,
				static_cast<std::uint64_t>(a_unixTimeSeconds),
				a_monotonicSeconds);
			if (!refusal) {
				return std::nullopt;
			}
			auto frames = RefuseRaidHit(a_connectionId, request->buildZoneId, refusal->first, refusal->second, a_monotonicSeconds);
			frames.push_back(ReliableFrame(a_connectionId, F4T::SettlementCapture::MakeReject(request->buildZoneId, F4T::SettlementCapture::Status::Cancelled)));
			return frames;
		}

		[[nodiscard]] std::optional<F4T::Workshop::FormKey> HeldWeaponForm(const Session& a_session, std::uint32_t a_itemId) const
		{
			if (!a_session.pluginListPacket) {
				return std::nullopt;
			}
			const auto data = ReadString(*a_session.pluginListPacket, "data");
			const auto list = data ? F4T::LoadOrder::DecodePluginList(*data) : std::nullopt;
			const auto split = F4T::FormIdentity::SplitFormId(a_itemId);
			if (!list || split.kind == F4T::FormIdentity::Kind::Runtime) {
				return std::nullopt;
			}
			const auto& entries = split.kind == F4T::FormIdentity::Kind::Light ? list->light : list->full;
			for (const auto& entry : entries) {
				if (entry.index == split.fileIndex) {
					return F4T::Workshop::FormKey{ entry.name, split.localId };
				}
			}
			return std::nullopt;
		}

		std::optional<F4T::WorkshopMaterials::WeaponKind> HeldWeaponKind(const Session& a_session) const
		{
			const auto rightHand = SessionTypes::EquippedFormId(*a_session.lastDurablePlayerState, F4T::PlayerStateValidation::kRightHandSlot);
			if (rightHand == 0) {
				return F4T::WorkshopMaterials::WeaponKind::Melee;
			}
			const auto form = HeldWeaponForm(a_session, rightHand);
			const auto* weaponClass = form ? workshopWeaponClasses.Find(*form) : nullptr;
			if (weaponClass == nullptr) {
				return std::nullopt;
			}
			return *weaponClass;
		}

		bool EquipmentAllowsHit(const Session& a_session, const F4T::WorkshopProtocol::StructureHit& a_hit, double a_monotonicSeconds) const
		{
			if (!a_session.lastDurablePlayerState) {
				return false;
			}
			if (a_hit.weaponKind == F4T::WorkshopMaterials::WeaponKind::Explosive) {
				return a_session.lastEquippedExplosive == a_hit.explosive &&
					a_monotonicSeconds - a_session.lastEquippedExplosiveMonotonic <= F4T::WorkshopRaids::ExplosiveEquipWindowSeconds;
			}
			const auto held = HeldWeaponKind(a_session);
			return held && *held == a_hit.weaponKind;
		}

		void LoadWeaponClasses(const std::filesystem::path& a_path)
		{
			if (std::ifstream input(a_path, std::ios::binary); input) {
				const auto parsed = Json::parse(input, nullptr, false);
				if (!parsed.is_discarded() && workshopWeaponClasses.LoadFromJson(parsed)) {
					std::cout << "Workshop weapon classes: " << workshopWeaponClasses.Size() << " loaded from "
					          << F4T::Utf8Path::Encode(a_path) << std::endl;
				} else {
					std::cerr << "Workshop weapon classes: " << F4T::Utf8Path::Encode(a_path)
					          << " is invalid; keeping the previous table." << std::endl;
				}
			} else {
				std::cerr << "Workshop weapon classes: " << F4T::Utf8Path::Encode(a_path)
				          << " is missing; weapon hits are refused." << std::endl;
			}
		}

		std::vector<OutboundFrame> HandleStructureHit(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			const auto hit = F4T::WorkshopProtocol::NormalizeStructureHit(a_packet);
			if (!hit || a_session.profileId.empty()) {
				Reject();
				return {};
			}
			const auto zone = workshopZones.find(hit->buildZoneId);
			if (zone == workshopZones.end()) {
				Reject();
				return {};
			}
			if (!a_session.workshopZonesInside.contains(hit->buildZoneId) || !SessionWithinZoneReach(a_session, zone->second)) {
				Reject();
				return {};
			}
			const auto unixSeconds = static_cast<std::uint64_t>(a_unixTimeSeconds);
			if (hit->weaponKind != F4T::WorkshopMaterials::WeaponKind::Explosive && a_session.lastDurablePlayerState && !HeldWeaponKind(a_session)) {
				return RefuseRaidHit(a_connectionId, hit->buildZoneId, F4T::WorkshopRaids::HitRefusal::WeaponUnknown, 0, a_monotonicSeconds);
			}
			if (!EquipmentAllowsHit(a_session, *hit, a_monotonicSeconds)) {
				std::cerr << "Workshop structure hit refused: connection " << a_connectionId << " zone " << hit->buildZoneId
				          << " claimed weapon does not match equipment\n";
				Reject();
				return {};
			}
			const auto standing = StandingOf(hit->buildZoneId, a_session.profileId);
			if (standing == F4T::WorkshopRaids::SettlementStanding::FactionMate) {
				return RefuseRaidHit(a_connectionId, hit->buildZoneId, F4T::WorkshopRaids::HitRefusal::FactionMate, 0, a_monotonicSeconds);
			}
			const bool ownerSide = standing == F4T::WorkshopRaids::SettlementStanding::Owner;
			const auto refusal = ownerSide
				? ShieldRefusalFor(hit->buildZoneId, unixSeconds)
				: RaidRefusalFor(hit->buildZoneId, a_session.profileId, unixSeconds, a_monotonicSeconds);
			if (refusal) {
				return RefuseRaidHit(a_connectionId, hit->buildZoneId, refusal->first, refusal->second, a_monotonicSeconds);
			}
			const auto hitKey = std::tuple{ a_connectionId, hit->buildZoneId, hit->weaponKind };
			const auto ready = workshopStructureHitReady.find(hitKey);
			if (ready != workshopStructureHitReady.end() && a_monotonicSeconds < ready->second) {
				Reject();
				return {};
			}
			if (ready == workshopStructureHitReady.end() &&
				workshopStructureHitReady.size() >= F4T::WorkshopRaids::MaximumStructureHitCooldowns) {
				Reject();
				return {};
			}
			const float rating = [&] {
				const auto found = workshopDefenseRatings.find(hit->buildZoneId);
				return found == workshopDefenseRatings.end() ? 0.0F : found->second;
			}();
			const float base = hit->weaponKind == F4T::WorkshopMaterials::WeaponKind::Explosive
				? F4T::WorkshopRaids::ExplosiveDamage(hit->explosive)
				: F4T::WorkshopRaids::WeaponHitDamage(hit->weaponKind);
			const float damage = ownerSide
				? base
				: F4T::WorkshopDefenses::ApplyStructureResistance(base, rating);
			if (!(damage > 0.0F)) {
				Reject();
				return {};
			}
			const auto budgetFound = workshopStructureDamageBudgets.find(a_connectionId);
			if (budgetFound == workshopStructureDamageBudgets.end() &&
				workshopStructureDamageBudgets.size() >= F4T::WorkshopRaids::MaximumStructureDamageBudgets) {
				Reject();
				return {};
			}
			auto bucket = budgetFound == workshopStructureDamageBudgets.end()
				? F4T::PacketRateLimit::TokenBucket{ F4T::WorkshopRaids::StructureDamageBudgetPerSecond, a_monotonicSeconds }
				: budgetFound->second;
			if (!F4T::PacketRateLimit::TakeToken(
				    bucket,
				    a_monotonicSeconds,
				    F4T::WorkshopRaids::StructureDamageBudgetPerSecond,
				    F4T::WorkshopRaids::StructureDamageBudgetPerSecond,
				    base)) {
				Reject();
				return {};
			}
			if (hit->objectId == F4T::WorkshopRaids::WorkbenchObjectId) {
				const auto before = settlementAccess.Find(hit->buildZoneId);
				const bool wasBroken = before && before->workbenchBroken;
				std::string failure;
				auto frames = settlementAccess.ApplyWorkbenchDamage(
					hit->buildZoneId,
					damage,
					unixSeconds,
					failure);
				if (!failure.empty()) {
					Reject();
					return {};
				}
				workshopStructureHitReady[hitKey] = a_monotonicSeconds + F4T::WorkshopRaids::StructureHitIntervalSeconds(hit->weaponKind);
				workshopStructureDamageBudgets[a_connectionId] = bucket;
				workshopAttackStates[hit->buildZoneId] = F4T::WorkshopRaids::RefreshAttack(
					workshopAttackStates[hit->buildZoneId],
					a_monotonicSeconds,
					!ownerSide);
				const auto settlement = settlementAccess.Find(hit->buildZoneId);
				if (settlement && settlement->workbenchBroken && !wasBroken) {
					AppendFrames(frames, OwnerSettlerFrames(hit->buildZoneId, KillZoneGuards(hit->buildZoneId, unixSeconds)));
				}
				const float maxHp = settlement
					? F4T::SettlementAccess::WorkbenchMaxHpFor(settlement->kind)
					: F4T::WorkshopRaids::WorkbenchMaxHp;
				const float health = settlement ? settlement->workbenchHp : 0.0F;
				Json packet;
				if (settlement && settlement->workbenchBroken) {
					packet = F4T::WorkshopProtocol::MakeStructureDestroyed(F4T::WorkshopProtocol::StructureDestroyed{
						hit->buildZoneId,
						F4T::WorkshopRaids::WorkbenchObjectId,
						unixSeconds,
						zone->second.CurrentRevision()
					});
				} else {
					packet = F4T::WorkshopProtocol::MakeStructureDamage(F4T::WorkshopProtocol::StructureDamage{
						hit->buildZoneId,
						F4T::WorkshopRaids::WorkbenchObjectId,
						health,
						maxHp,
						zone->second.CurrentRevision(),
						F4T::WorkshopRaids::DamageStage(health, maxHp)
					});
				}
				for (const auto& [connectionId, peer] : sessions) {
					if (peer.active && (connectionId == a_connectionId || peer.workshopZonesInside.contains(hit->buildZoneId))) {
						frames.push_back(ReliableFrame(connectionId, packet));
					}
				}
				auto attackFrames = FanoutZoneAttackState(hit->buildZoneId);
				frames.insert(frames.end(), std::make_move_iterator(attackFrames.begin()), std::make_move_iterator(attackFrames.end()));
				return frames;
			}
			const auto* object = zone->second.Find(hit->objectId);
			if (!object || object->maxHealth <= 0.0F) {
				Reject();
				return {};
			}
			const float objectDamage = F4T::WorkshopMaterials::DamageForHit(
				workshopPieces.Find(object->form).tier,
				hit->weaponKind,
				damage);
			const bool holdsGuards = workshopDefenseForms.Find(object->form).has_value();
			auto candidate = zone->second;
			if (!candidate.ApplyStructureDamage(hit->objectId, objectDamage, unixSeconds)) {
				Reject();
				return {};
			}
			if (!SaveZoneWithPower(candidate)) {
				Reject();
				return {};
			}
			zone->second = std::move(candidate);
			workshopStructureHitReady[hitKey] = a_monotonicSeconds + F4T::WorkshopRaids::StructureHitIntervalSeconds(hit->weaponKind);
			workshopStructureDamageBudgets[a_connectionId] = bucket;
			workshopAttackStates[hit->buildZoneId] = F4T::WorkshopRaids::RefreshAttack(
				workshopAttackStates[hit->buildZoneId],
				a_monotonicSeconds,
				!ownerSide);
			const bool destroyed = zone->second.Find(hit->objectId) == nullptr;
			Json packet;
			if (destroyed) {
				packet = F4T::WorkshopProtocol::MakeStructureDestroyed(F4T::WorkshopProtocol::StructureDestroyed{
					hit->buildZoneId,
					hit->objectId,
					unixSeconds,
					zone->second.CurrentRevision()
				});
			} else {
				const auto* live = zone->second.Find(hit->objectId);
				const float health = live ? live->health : 0.0F;
				const float maxHealth = live ? live->maxHealth : 0.0F;
				packet = F4T::WorkshopProtocol::MakeStructureDamage(F4T::WorkshopProtocol::StructureDamage{
					hit->buildZoneId,
					hit->objectId,
					health,
					maxHealth,
					zone->second.CurrentRevision(),
					F4T::WorkshopRaids::DamageStage(health, maxHealth)
				});
			}
			std::vector<OutboundFrame> frames;
			if (destroyed && holdsGuards) {
				AppendFrames(frames, OwnerSettlerFrames(hit->buildZoneId, KillZoneGuards(hit->buildZoneId, unixSeconds)));
			}
			for (const auto& [connectionId, peer] : sessions) {
				if (peer.active && (connectionId == a_connectionId || peer.workshopZonesInside.contains(hit->buildZoneId))) {
					frames.push_back(ReliableFrame(connectionId, packet));
				}
			}
			auto attackFrames = FanoutZoneAttackState(hit->buildZoneId);
			frames.insert(frames.end(), std::make_move_iterator(attackFrames.begin()), std::make_move_iterator(attackFrames.end()));
			if (destroyed) {
				AppendFrames(frames, FanoutPowerState(hit->buildZoneId));
				AppendFrames(frames, settlementAccess.StateFrames(FactionPeers(), hit->buildZoneId));
			}
			return frames;
		}
