		std::vector<F4T::WorkshopDefenses::TurretStats> CollectTurrets(F4T::Workshop::BuildZoneId a_zoneId) const
		{
			std::vector<F4T::WorkshopDefenses::TurretStats> turrets;
			const auto zone = workshopZones.find(a_zoneId);
			const auto power = workshopPower.find(a_zoneId);
			if (zone == workshopZones.end() || power == workshopPower.end()) {
				return turrets;
			}
			const auto& powered = power->second.Powered();
			for (const auto& node : power->second.Nodes()) {
				if (node.deviceClass != F4T::WorkshopPower::DeviceClass::Turret) {
					continue;
				}
				F4T::WorkshopDefenses::TurretStats stats;
				stats.objectId = node.objectId;
				stats.needsPower = node.needsPower;
				stats.powered = powered.contains(node.objectId);
				const auto* object = zone->second.Find(node.objectId);
				stats.alive = object != nullptr && object->health > 0.0F;
				if (object) {
					stats.x = object->transform.x;
					stats.y = object->transform.y;
					stats.z = object->transform.z;
					if (const auto form = workshopDefenseForms.Find(object->form)) {
						stats.ratingValue = form->ratingValue;
						stats.rangeMeters = form->rangeMeters;
						stats.damage = form->damage;
						stats.fireIntervalSeconds = form->fireIntervalSeconds;
						stats.needsPower = form->needsPower;
						stats.powered = !form->needsPower || powered.contains(node.objectId);
					}
				}
				turrets.push_back(stats);
			}
			return turrets;
		}

		std::vector<F4T::WorkshopDefenses::SettlerStats> CollectSettlerStats(F4T::Workshop::BuildZoneId a_zoneId) const
		{
			std::vector<F4T::WorkshopDefenses::SettlerStats> settlers;
			for (const auto& settler : workshopSettlers.ForZone(a_zoneId)) {
				F4T::WorkshopDefenses::SettlerStats stats;
				stats.settlerId = settler.settlerId;
				stats.alive = !settler.dead && settler.health > 0.0F;
				stats.onGuard = settler.assignment == F4T::WorkshopSettlers::Assignment::Guard;
				settlers.push_back(stats);
			}
			return settlers;
		}

		std::vector<F4T::WorkshopDefenses::HostilePresence> HostilesInZone(
			F4T::Workshop::BuildZoneId a_zoneId,
			std::uint64_t a_unixSeconds,
			double a_monotonicSeconds) const
		{
			std::vector<F4T::WorkshopDefenses::HostilePresence> hostiles;
			const auto zone = workshopZones.find(a_zoneId);
			if (zone == workshopZones.end()) {
				return hostiles;
			}
			const auto& policy = zone->second.Policy();
			const double radius = policy.radius > 0.0F ? static_cast<double>(policy.radius) : 800.0;
			const double radiusSq = radius * radius;
			for (const auto& [connectionId, session] : sessions) {
				if (!session.active || session.profileId.empty() || !session.lastMovement) {
					continue;
				}
				const auto standing = StandingOf(a_zoneId, session.profileId);
				if (standing != F4T::WorkshopRaids::SettlementStanding::Raider ||
					RaidRefusalFor(a_zoneId, session.profileId, a_unixSeconds, a_monotonicSeconds)) {
					continue;
				}
				const auto& movement = *session.lastMovement;
				const double dx = movement.x - policy.centerX;
				const double dy = movement.y - policy.centerY;
				const double dz = movement.z - policy.centerZ;
				if (dx * dx + dy * dy + dz * dz > radiusSq) {
					continue;
				}
				hostiles.push_back(F4T::WorkshopDefenses::HostilePresence{
					connectionId,
					session.playerId,
					session.profileId,
					movement.x,
					movement.y,
					movement.z
				});
			}
			return hostiles;
		}

		float NearestPlayerMeters(F4T::Workshop::BuildZoneId a_zoneId) const
		{
			const auto zone = workshopZones.find(a_zoneId);
			if (zone == workshopZones.end()) {
				return 1.0e9F;
			}
			const auto& policy = zone->second.Policy();
			float nearest = 1.0e9F;
			for (const auto& [connectionId, session] : sessions) {
				(void)connectionId;
				if (!session.active || !session.lastMovement) {
					continue;
				}
				const auto& movement = *session.lastMovement;
				const float dx = static_cast<float>(movement.x - policy.centerX);
				const float dy = static_cast<float>(movement.y - policy.centerY);
				const float dz = static_cast<float>(movement.z - policy.centerZ);
				nearest = (std::min)(nearest, std::sqrt(dx * dx + dy * dy + dz * dz));
			}
			return nearest;
		}

		void RecomputeDefenseRating(F4T::Workshop::BuildZoneId a_zoneId, std::uint64_t a_unixSeconds)
		{
			const auto paidUntil = settlementAccess.UpkeepPaidUntil(a_zoneId);
			const bool unpaid = paidUntil && a_unixSeconds > *paidUntil;
			workshopDefenseRatings[a_zoneId] = F4T::WorkshopDefenses::ComputeDefenseRating(
				CollectTurrets(a_zoneId),
				CollectSettlerStats(a_zoneId),
				unpaid);
		}

		std::vector<F4T::Workshop::ObjectId> KillZoneGuards(F4T::Workshop::BuildZoneId a_zoneId, std::uint64_t a_unixSeconds)
		{
			std::vector<F4T::Workshop::ObjectId> killed;
			for (auto settler : workshopSettlers.ForZone(a_zoneId)) {
				if (settler.assignment != F4T::WorkshopSettlers::Assignment::Guard || settler.dead) {
					continue;
				}
				settler.dead = true;
				settler.health = 0.0F;
				settler.deadUntilUnix = a_unixSeconds + F4T::WorkshopSettlers::DeadRecoverySeconds;
				if (workshopSettlers.Upsert(settler)) {
					killed.push_back(settler.settlerId);
				}
			}
			RecomputeDefenseRating(a_zoneId, a_unixSeconds);
			return killed;
		}

		static Json SettlerStatePacket(const F4T::WorkshopSettlers::SettlerState& a_settler)
		{
			return Json{
				{ "type", "settlerState" },
				{ "buildZoneId", a_settler.buildZoneId },
				{ "settlerId", a_settler.settlerId },
				{ "assignment", a_settler.assignment == F4T::WorkshopSettlers::Assignment::Guard ? "guard" :
					a_settler.assignment == F4T::WorkshopSettlers::Assignment::Farm ? "farm" :
					a_settler.assignment == F4T::WorkshopSettlers::Assignment::Scavenge ? "scavenge" :
					a_settler.assignment == F4T::WorkshopSettlers::Assignment::Purifier ? "purifier" : "idle" },
				{ "health", a_settler.health },
				{ "dead", a_settler.dead }
			};
		}

		std::vector<OutboundFrame> OwnerSettlerFrames(F4T::Workshop::BuildZoneId a_zoneId, const std::vector<F4T::Workshop::ObjectId>& a_settlerIds)
		{
			std::vector<OutboundFrame> frames;
			const auto owner = settlementAccess.OwnerOf(a_zoneId);
			if (owner.empty()) {
				return frames;
			}
			for (const auto settlerId : a_settlerIds) {
				const auto settler = workshopSettlers.Find(settlerId);
				if (!settler) {
					continue;
				}
				const auto packet = SettlerStatePacket(*settler);
				for (const auto& [connectionId, peer] : sessions) {
					if (peer.active && peer.profileId == owner) {
						frames.push_back(ReliableFrame(connectionId, packet));
					}
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> OwnerChangedFrames(F4T::Workshop::BuildZoneId a_zoneId, std::uint64_t a_unixSeconds)
		{
			ClearAuthSteam(a_zoneId);
			const auto owner = settlementAccess.OwnerOf(a_zoneId);
			if (owner.empty()) {
				return {};
			}
			auto frames = OwnerSettlerFrames(a_zoneId, KillZoneGuards(a_zoneId, a_unixSeconds));
			for (const auto& [connectionId, peer] : sessions) {
				(void)connectionId;
				if (peer.active && peer.profileId == owner && !peer.steamId.empty()) {
					BindAuthSteam(a_zoneId, owner, peer.steamId);
					break;
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> TickWorkshopDefenses(double a_unixSeconds, double a_monotonicSeconds)
		{
			std::vector<OutboundFrame> frames;
			const auto unixSeconds = static_cast<std::uint64_t>(a_unixSeconds);
			if (workshopSettlerLastUnix <= 0.0) {
				workshopSettlerLastUnix = a_unixSeconds;
			} else {
				const double dtHours = (a_unixSeconds - workshopSettlerLastUnix) / 3600.0;
				workshopSettlerLastUnix = a_unixSeconds;
				if (dtHours > 0.0) {
					(void)workshopSettlers.TickAll(dtHours, unixSeconds);
				}
			}

			std::vector<std::pair<F4T::Workshop::BuildZoneId, std::uint32_t>> underAttack;
			for (const auto& [zoneId, attack] : workshopAttackStates) {
				if (attack.underAttack) {
					underAttack.emplace_back(zoneId, attack.attackerCount);
				}
			}
			const auto fullSim = F4T::WorkshopDefenses::SelectFullSimZones(underAttack);
			std::unordered_set<F4T::Workshop::BuildZoneId> fullSimSet(fullSim.begin(), fullSim.end());

			for (const auto& [zoneId, zone] : workshopZones) {
				(void)zone;
				RecomputeDefenseRating(zoneId, unixSeconds);
				const auto attack = workshopAttackStates.find(zoneId);
				const bool attacking = attack != workshopAttackStates.end() && attack->second.underAttack;
				const auto detail = F4T::WorkshopSettlers::ChooseDetail(attacking, NearestPlayerMeters(zoneId));
				if (detail == F4T::WorkshopSettlers::DetailLevel::Abstract && !attacking) {
					continue;
				}
				const float rating = workshopDefenseRatings[zoneId];
				const std::uint32_t attackers = attacking ? attack->second.attackerCount : 0;
				const auto packet = F4T::WorkshopProtocol::MakeDefenderState(
					zoneId,
					rating,
					attacking,
					attackers,
					fullSimSet.contains(zoneId) ? F4T::WorkshopSettlers::DetailLevel::Full : detail);
				for (const auto& [connectionId, session] : sessions) {
					if (session.active && session.workshopZonesInside.contains(zoneId)) {
						frames.push_back(ReliableFrame(connectionId, packet));
					}
				}
				if (!attacking || !fullSimSet.contains(zoneId)) {
					continue;
				}
				const auto hostiles = HostilesInZone(zoneId, unixSeconds, a_monotonicSeconds);
				for (const auto& turret : CollectTurrets(zoneId)) {
					if (!turret.alive || (turret.needsPower && !turret.powered)) {
						continue;
					}
					auto& fire = workshopTurretFire[turret.objectId];
					if (fire.nextFireMonotonic > 0.0 && a_monotonicSeconds < fire.nextFireMonotonic) {
						continue;
					}
					const auto target = F4T::WorkshopDefenses::NearestHostileInRange(
						turret,
						hostiles,
						fire.stickyConnectionId == 0 ? std::nullopt : std::optional<std::uint32_t>{ fire.stickyConnectionId },
						a_monotonicSeconds,
						fire.stickyUntilMonotonic);
					if (!target) {
						continue;
					}
					fire.stickyConnectionId = target->connectionId;
					fire.stickyUntilMonotonic = a_monotonicSeconds + F4T::WorkshopDefenses::TargetStickinessSeconds;
					fire.nextFireMonotonic = a_monotonicSeconds + turret.fireIntervalSeconds;
					Json event{
						{ "type", "defenderEvent" },
						{ "buildZoneId", zoneId },
						{ "kind", "turretFire" },
						{ "objectId", turret.objectId },
						{ "targetConnectionId", target->connectionId },
						{ "targetPlayerId", target->playerId },
						{ "damage", turret.damage }
					};
					for (const auto& [connectionId, session] : sessions) {
						if (session.active && (connectionId == target->connectionId || session.workshopZonesInside.contains(zoneId))) {
							frames.push_back(ReliableFrame(connectionId, event));
						}
					}
				}
			}
			return frames;
		}

		struct TurretFireState
		{
			std::uint32_t stickyConnectionId{};
			double stickyUntilMonotonic{};
			double nextFireMonotonic{};
		};

		static constexpr std::size_t kMaximumProfileFirstSeen = 100000;

		[[nodiscard]] bool OwnsAnySettlement(std::string_view a_profileId)
		{
			for (const auto& [zoneId, zone] : workshopZones) {
				(void)zone;
				if (settlementAccess.OwnerOf(zoneId) == a_profileId) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] std::optional<std::uint64_t> SlotCreatedAt(std::string_view a_account) const
		{
			if (workshopPersistence == nullptr) {
				return std::nullopt;
			}
			std::optional<std::uint64_t> earliest;
			for (const auto& record : workshopPersistence->Store().GetCharacterSlotRecords(a_account)) {
				const auto created = record.second.find("createdAt");
				if (created == record.second.end() || !created->is_number() || created->get<double>() < 0.0) {
					continue;
				}
				const auto stamp = static_cast<std::uint64_t>(created->get<double>());
				if (!earliest || stamp < *earliest) {
					earliest = stamp;
				}
			}
			return earliest;
		}

		[[nodiscard]] std::optional<std::uint64_t> KnownFirstSeen(std::string_view a_profileId, std::uint64_t a_unixSeconds) const
		{
			if (profileFirstSeenLookup) {
				const std::optional<double> registered = profileFirstSeenLookup(a_profileId);
				if (registered && *registered >= 0.0 && *registered < static_cast<double>(a_unixSeconds)) {
					return static_cast<std::uint64_t>(*registered);
				}
			}
			const auto created = SlotCreatedAt(a_profileId);
			if (created && *created < a_unixSeconds) {
				return created;
			}
			return std::nullopt;
		}

		void NoteProfileSeen(std::string_view a_profileId, std::uint64_t a_unixSeconds)
		{
			if (a_profileId.empty() || workshopProfileFirstSeen.size() >= kMaximumProfileFirstSeen ||
				workshopProfileFirstSeen.contains(std::string{ a_profileId })) {
				return;
			}
			const std::string profile{ a_profileId };
			std::uint64_t firstSeen = OwnsAnySettlement(a_profileId) ? 0 : a_unixSeconds;
			const auto pending = workshopStored.find("profileFirstSeenPreexisting");
			const bool preexisting = pending != workshopStored.end() && pending->contains(profile);
			if (firstSeen > 0) {
				const auto known = KnownFirstSeen(a_profileId, a_unixSeconds);
				firstSeen = known ? *known : (preexisting ? ProtectionEpochFirstSeen() : a_unixSeconds);
			}
			workshopProfileFirstSeen.emplace(profile, firstSeen);
			workshopStored["profileFirstSeen"][profile] = firstSeen;
			if (preexisting) {
				workshopStored["profileFirstSeenPreexisting"].erase(profile);
			}
			workshopRootDirty = true;
		}

		void LoadProfileFirstSeenFromStore()
		{
			const auto root = workshopStored.find("profileFirstSeen");
			if (root == workshopStored.end() || !root->is_object()) {
				return;
			}
			for (const auto& entry : root->items()) {
				if (entry.value().is_number_unsigned()) {
					workshopProfileFirstSeen.emplace(entry.key(), entry.value().get<std::uint64_t>());
				}
			}
		}

		[[nodiscard]] std::uint64_t ProtectionEpochFirstSeen() const
		{
			const auto window = static_cast<std::uint64_t>(F4T::WorkshopRaids::NewPlayerProtectionHours * 3600.0F);
			const auto epoch = static_cast<std::uint64_t>(workshopStored.value("profileFirstSeenEpoch", 0.0));
			return epoch > window ? epoch - window : 0;
		}

		void EstablishProfileFirstSeenEpoch()
		{
			if (workshopPersistence == nullptr || workshopStored.contains("profileFirstSeenEpoch")) {
				return;
			}
			nlohmann::json preexisting = nlohmann::json::object();
			for (const auto& account : workshopPersistence->Store().PersistedAccountKeys()) {
				if (!workshopProfileFirstSeen.contains(account)) {
					preexisting[account] = true;
				}
			}
			workshopStored["profileFirstSeenEpoch"] = workshopPersistence->Store().Now();
			workshopStored["profileFirstSeenPreexisting"] = std::move(preexisting);
			workshopRootDirty = true;
		}

		[[nodiscard]] std::optional<std::uint64_t> NewPlayerProtectedUntil(std::string_view a_profileId) const
		{
			const auto found = workshopProfileFirstSeen.find(std::string{ a_profileId });
			if (found == workshopProfileFirstSeen.end()) {
				return std::nullopt;
			}
			const auto window = static_cast<std::uint64_t>(F4T::WorkshopRaids::NewPlayerProtectionHours * 3600.0F);
			return found->second + window;
		}

		std::vector<OutboundFrame> HandleSettlerAssign(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_unixTimeSeconds)
		{
			const auto type = a_packet.find("type");
			if (type == a_packet.end() || !type->is_string()) {
				Reject();
				return {};
			}
			const auto name = type->get<std::string>();
			const auto buildZoneId = a_packet.find("buildZoneId");
			if (buildZoneId == a_packet.end() || !buildZoneId->is_number_unsigned() || a_session.profileId.empty()) {
				Reject();
				return {};
			}
			const auto zoneId = buildZoneId->get<F4T::Workshop::BuildZoneId>();
			if (settlementAccess.OwnerOf(zoneId) != a_session.profileId) {
				Reject();
				return {};
			}
			const auto settlerId = a_packet.find("settlerId");
			if (settlerId == a_packet.end() || !settlerId->is_number_unsigned()) {
				Reject();
				return {};
			}
			const auto found = workshopSettlers.Find(settlerId->get<F4T::Workshop::ObjectId>());
			if (!found || found->buildZoneId != zoneId) {
				Reject();
				return {};
			}
			F4T::WorkshopSettlers::SettlerState settler = *found;
			if (name == "settlerRecover") {
				if (!F4T::WorkshopSettlers::CanRecoverSettler(settler)) {
					Reject();
					return {};
				}
				settler.dead = false;
				settler.deadUntilUnix = 0;
				settler.health = settler.maxHealth > 0.0F ? settler.maxHealth : 100.0F;
				settler.assignment = F4T::WorkshopSettlers::Assignment::Idle;
			} else if (name == "settlerAssign") {
				const auto assignment = a_packet.find("assignment");
				if (assignment == a_packet.end() || !assignment->is_string()) {
					Reject();
					return {};
				}
				const auto value = assignment->get<std::string>();
				if (value == "guard") {
					settler.assignment = F4T::WorkshopSettlers::Assignment::Guard;
				} else if (value == "farm") {
					settler.assignment = F4T::WorkshopSettlers::Assignment::Farm;
				} else if (value == "scavenge") {
					settler.assignment = F4T::WorkshopSettlers::Assignment::Scavenge;
				} else if (value == "purifier") {
					settler.assignment = F4T::WorkshopSettlers::Assignment::Purifier;
				} else if (value == "idle") {
					settler.assignment = F4T::WorkshopSettlers::Assignment::Idle;
				} else {
					Reject();
					return {};
				}
			}
			if (!workshopSettlers.Upsert(settler)) {
				Reject();
				return {};
			}
			RecomputeDefenseRating(zoneId, static_cast<std::uint64_t>(a_unixTimeSeconds));
			const auto packet = SettlerStatePacket(settler);
			std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, packet) };
			for (const auto& [connectionId, peer] : sessions) {
				if (peer.active && connectionId != a_connectionId && peer.workshopZonesInside.contains(zoneId)) {
					frames.push_back(ReliableFrame(connectionId, packet));
				}
			}
			return frames;
		}

		std::string_view SettlerCapRefusal(F4T::Workshop::BuildZoneId a_buildZoneId, const std::string& a_profileId, bool a_countPending) const
		{
			std::size_t inZone{ workshopSettlers.ForZone(a_buildZoneId).size() };
			std::size_t byPlayer{};
			std::size_t total{ workshopSettlers.TotalCount() };
			for (const auto& owned : workshopSettlers.Snapshot()) {
				if (settlementAccess.OwnerOf(owned.buildZoneId) == a_profileId) {
					++byPlayer;
				}
			}
			if (a_countPending) {
				for (const auto& entry : pendingSettlementCharges) {
					if (entry.second.purpose != F4T::SettlementCharge::Purpose::SettlerRecruit) {
						continue;
					}
					++total;
					if (entry.second.buildZoneId == a_buildZoneId) {
						++inZone;
					}
					if (settlementAccess.OwnerOf(entry.second.buildZoneId) == a_profileId) {
						++byPlayer;
					}
				}
			}
			if (inZone >= F4T::WorkshopSettlers::MaximumSettlersPerSettlement) {
				return "settlerCapSettlement";
			}
			if (byPlayer >= F4T::WorkshopSettlers::MaximumSettlersPerPlayer) {
				return "settlerCapPlayer";
			}
			if (total >= F4T::WorkshopSettlers::MaximumSettlersPerServer) {
				return "settlerCapServer";
			}
			return {};
		}

		std::vector<OutboundFrame> HandleSettlerRecruit(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			const auto zoneId = F4T::SettlementCharge::NormalizeZoneRequest(a_packet, "settlerRecruit");
			if (!zoneId) {
				Reject();
				return {};
			}
			using ApplyStatus = F4T::SettlementAccess::ApplyStatus;
			const auto gate = SettlementChargeGate(a_session, *zoneId);
			if (gate != ApplyStatus::Committed) {
				return SettlementChargeReject(a_connectionId, *zoneId, gate);
			}
			const auto refusal = SettlerCapRefusal(*zoneId, a_session.profileId, true);
			if (!refusal.empty()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopProtocol::MakeSettlementRefused(*zoneId, "settlerRecruit", refusal)) };
			}
			return BeginSettlementCharge(
				a_connectionId,
				a_session,
				F4T::SettlementCharge::Purpose::SettlerRecruit,
				*zoneId,
				F4T::Workshop::ObjectId{},
				F4T::SettlementCharge::SettlerRecruitCharge(),
				a_monotonicSeconds);
		}

		std::optional<std::vector<OutboundFrame>> CommitSettlerRecruit(const PendingSettlementCharge& a_charge, std::uint64_t a_unixSeconds)
		{
			if (!SettlerCapRefusal(a_charge.buildZoneId, a_charge.profileId, false).empty()) {
				return std::nullopt;
			}
			F4T::WorkshopSettlers::SettlerState settler;
			settler.settlerId = workshopSettlers.MintSettlerId();
			settler.buildZoneId = a_charge.buildZoneId;
			if (!workshopSettlers.Upsert(settler)) {
				return std::nullopt;
			}
			RecomputeDefenseRating(a_charge.buildZoneId, a_unixSeconds);
			const auto packet = SettlerStatePacket(settler);
			std::vector<OutboundFrame> frames{ ReliableFrame(a_charge.connectionId, packet) };
			for (const auto& [connectionId, peer] : sessions) {
				if (peer.active && connectionId != a_charge.connectionId && peer.workshopZonesInside.contains(a_charge.buildZoneId)) {
					frames.push_back(ReliableFrame(connectionId, packet));
				}
			}
			return frames;
		}

		F4T::WorkshopSettlers::Table workshopSettlers;
		F4T::WorkshopDefenseForms::Table workshopDefenseForms;
		std::map<F4T::Workshop::BuildZoneId, float> workshopDefenseRatings;
		std::map<F4T::Workshop::ObjectId, TurretFireState> workshopTurretFire;
		std::map<std::string, std::uint64_t> workshopProfileFirstSeen;
		std::function<std::optional<double>(std::string_view)> profileFirstSeenLookup;
		std::map<std::tuple<std::uint32_t, F4T::Workshop::BuildZoneId>, double> workshopRaidHitNoticeReady;
		double workshopSettlerLastUnix{};
