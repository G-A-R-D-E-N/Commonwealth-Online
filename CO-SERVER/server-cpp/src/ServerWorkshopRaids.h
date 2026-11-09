		[[nodiscard]] bool DeclarationReadyForZone(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_attackerProfileId,
			double a_monotonicSeconds) const
		{
			if (workshopRaidConfig.mode != F4T::WorkshopRaids::RaidMode::Windowed) {
				return true;
			}
			const auto found = workshopRaidDeclarations.find(a_buildZoneId);
			if (found == workshopRaidDeclarations.end()) {
				return false;
			}
			const auto& declaration = found->second;
			const bool declarer = found->second.declarerProfileId == a_attackerProfileId;
			const bool factionMate = !declaration.factionId.empty() &&
				factionService.FactionIdOf(std::string{ a_attackerProfileId }) == declaration.factionId;
			return a_monotonicSeconds >= declaration.readyMonotonic &&
				a_monotonicSeconds <= declaration.expiresMonotonic &&
				(declarer || factionMate);
		}

		std::vector<OutboundFrame> RefuseRaidDeclare(
			std::uint32_t a_connectionId,
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_reason)
		{
			Reject();
			return { ReliableFrame(a_connectionId, F4T::WorkshopProtocol::MakeSettlementRefused(a_buildZoneId, "raidDeclare", a_reason)) };
		}

		std::vector<OutboundFrame> HandleRaidDeclare(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			const auto declare = F4T::WorkshopProtocol::NormalizeRaidDeclare(a_packet);
			if (!declare || a_session.profileId.empty()) {
				Reject();
				return {};
			}
			if (workshopRaidConfig.mode != F4T::WorkshopRaids::RaidMode::Windowed) {
				Reject();
				return {};
			}
			if (!workshopZones.contains(declare->buildZoneId)) {
				Reject();
				return {};
			}
			if (StandingOf(declare->buildZoneId, a_session.profileId) != F4T::WorkshopRaids::SettlementStanding::Raider) {
				return RefuseRaidDeclare(a_connectionId, declare->buildZoneId, "ownSettlement");
			}
			const auto day = F4T::WorkshopRaids::UtcDayIndex(static_cast<std::uint64_t>(a_unixTimeSeconds));
			if (day != workshopRaidDeclareDay) {
				workshopRaidDeclareCounts.clear();
				workshopRaidDeclareDay = day;
			}
			for (const auto& [zoneId, pending] : workshopRaidDeclarations) {
				if (!F4T::WorkshopRaids::RaidDeclarationLive(pending, a_monotonicSeconds)) {
					continue;
				}
				if (zoneId == declare->buildZoneId || pending.declarerProfileId == a_session.profileId) {
					return RefuseRaidDeclare(a_connectionId, declare->buildZoneId, "alreadyDeclared");
				}
			}
			const auto factionId = factionService.FactionIdOf(a_session.profileId);
			const auto dayKey = factionId.empty() ? "p:" + a_session.profileId : "f:" + factionId;
			const auto config = RaidConfigForZone(declare->buildZoneId);
			const auto window = workshopRaidWindows.find(declare->buildZoneId);
			const float offsetHours = window == workshopRaidWindows.end() ? 0.0F : window->second.timezoneOffsetHours;
			const double readyUnix = a_unixTimeSeconds + F4T::WorkshopRaids::DeclarationWarningSeconds;
			const double expiresUnix = readyUnix + static_cast<double>(config.windowDurationHours) * 3600.0;
			if (!F4T::WorkshopRaids::WindowOverlapsInterval(config, offsetHours, readyUnix, expiresUnix)) {
				return RefuseRaidDeclare(a_connectionId, declare->buildZoneId, "outsideRaidWindow");
			}
			auto& dayCount = workshopRaidDeclareCounts[dayKey];
			if (dayCount >= F4T::WorkshopRaids::MaxDeclarationsPerGroupPerDay) {
				return RefuseRaidDeclare(a_connectionId, declare->buildZoneId, "dailyLimit");
			}
			F4T::WorkshopRaids::RaidDeclaration declaration;
			declaration.buildZoneId = declare->buildZoneId;
			declaration.declarerProfileId = a_session.profileId;
			declaration.factionId = factionId;
			declaration.readyMonotonic = a_monotonicSeconds + F4T::WorkshopRaids::DeclarationWarningSeconds;
			declaration.expiresMonotonic = declaration.readyMonotonic + (config.windowDurationHours * 3600.0);
			declaration.readyUnix = readyUnix;
			declaration.expiresUnix = expiresUnix;
			workshopRaidDeclarations[declare->buildZoneId] = declaration;
			PersistRaidDeclaration(declare->buildZoneId);
			dayCount += 1;
			const auto packet = F4T::WorkshopProtocol::MakeRaidDeclareState(
				declaration.buildZoneId,
				declaration.declarerProfileId,
				declaration.readyMonotonic,
				declaration.expiresMonotonic,
				declaration.readyUnix,
				declaration.expiresUnix);
			std::vector<OutboundFrame> frames;
			const auto owner = settlementAccess.OwnerOf(declare->buildZoneId);
			for (const auto& [connectionId, peer] : sessions) {
				if (!peer.active) {
					continue;
				}
				if (connectionId == a_connectionId || peer.workshopZonesInside.contains(declare->buildZoneId) ||
					(!owner.empty() && peer.profileId == owner)) {
					frames.push_back(ReliableFrame(connectionId, packet));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> HandleSettlementWindowSet(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			const auto window = F4T::WorkshopProtocol::NormalizeSettlementWindowSet(a_packet);
			if (!window || a_session.profileId.empty()) {
				Reject();
				return {};
			}
			if (workshopRaidConfig.mode != F4T::WorkshopRaids::RaidMode::Windowed) {
				Reject();
				return {};
			}
			if (settlementAccess.OwnerOf(window->buildZoneId) != a_session.profileId) {
				Reject();
				return {};
			}
			const auto pending = workshopRaidDeclarations.find(window->buildZoneId);
			if (pending != workshopRaidDeclarations.end() &&
				F4T::WorkshopRaids::RaidDeclarationLive(pending->second, a_monotonicSeconds)) {
				Reject();
				return {};
			}
			workshopRaidWindows[window->buildZoneId] = *window;
			workshopStored["raidWindows"][std::to_string(window->buildZoneId)] = Json{
				{ "type", "settlementWindowSet" },
				{ "buildZoneId", window->buildZoneId },
				{ "windowStartHour", window->windowStartHour },
				{ "windowDurationHours", window->windowDurationHours },
				{ "timezoneOffsetHours", window->timezoneOffsetHours }
			};
			SaveWorkshopRoot();
			const auto packet = F4T::WorkshopProtocol::MakeSettlementWindowState(
				window->buildZoneId,
				window->windowStartHour,
				window->windowDurationHours,
				window->timezoneOffsetHours);
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, peer] : sessions) {
				if (peer.active && (connectionId == a_connectionId || peer.workshopZonesInside.contains(window->buildZoneId))) {
					frames.push_back(ReliableFrame(connectionId, packet));
				}
			}
			return frames;
		}

		[[nodiscard]] float OwnerLocalHourForZone(F4T::Workshop::BuildZoneId a_buildZoneId, std::uint64_t a_unixSeconds) const
		{
			const auto found = workshopRaidWindows.find(a_buildZoneId);
			const float offsetHours = found == workshopRaidWindows.end() ? 0.0F : found->second.timezoneOffsetHours;
			return F4T::WorkshopRaids::OwnerLocalHour(a_unixSeconds, offsetHours);
		}

		[[nodiscard]] F4T::WorkshopRaids::RaidConfig RaidConfigForZone(F4T::Workshop::BuildZoneId a_buildZoneId) const
		{
			auto config = workshopRaidConfig;
			if (const auto found = workshopRaidWindows.find(a_buildZoneId); found != workshopRaidWindows.end()) {
				config.windowStartHour = found->second.windowStartHour;
				config.windowDurationHours = found->second.windowDurationHours;
			}
			return config;
		}

		[[nodiscard]] F4T::WorkshopRaids::SettlementStanding StandingOf(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			const std::string& a_profileId) const
		{
			const auto owner = settlementAccess.OwnerOf(a_buildZoneId);
			const bool ownsSettlement = owner.empty() ? settlementAccess.CanBuild(a_buildZoneId, a_profileId) : owner == a_profileId;
			const bool factionMate = !owner.empty() && owner != a_profileId && factionService.SameFaction(a_profileId, owner);
			return F4T::WorkshopRaids::ClassifyStanding(ownsSettlement, settlementAccess.CanBuild(a_buildZoneId, a_profileId), factionMate);
		}

		[[nodiscard]] std::optional<std::pair<F4T::WorkshopRaids::HitRefusal, std::uint64_t>> RaidRefusalFor(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			const std::string& a_profileId,
			std::uint64_t a_unixSeconds,
			double a_monotonicSeconds) const
		{
			if (settlementAccess.IsProtected(a_buildZoneId, a_unixSeconds)) {
				const auto shield = settlementAccess.Find(a_buildZoneId);
				return std::pair{ F4T::WorkshopRaids::HitRefusal::SettlementProtected, shield ? shield->protectedUntilUnix - a_unixSeconds : std::uint64_t{ 0 } };
			}
			const auto owner = settlementAccess.OwnerOf(a_buildZoneId);
			const auto newPlayerUntil = std::max(
				NewPlayerProtectedUntil(a_profileId).value_or(0),
				NewPlayerProtectedUntil(owner).value_or(0));
			if (a_unixSeconds < newPlayerUntil) {
				return std::pair{ F4T::WorkshopRaids::HitRefusal::NewPlayerProtected, newPlayerUntil - a_unixSeconds };
			}
			const auto leaveUntil = FactionLeaveRaidUntil(a_profileId, a_buildZoneId);
			if (F4T::WorkshopRaids::FactionLeaveBlocksRaid(leaveUntil, a_unixSeconds)) {
				return std::pair{ F4T::WorkshopRaids::HitRefusal::FactionLeaveBlocked, leaveUntil - a_unixSeconds };
			}
			if (!F4T::WorkshopRaids::WindowedOutsiderRaidAllowed(
				    RaidConfigForZone(a_buildZoneId),
				    OwnerLocalHourForZone(a_buildZoneId, a_unixSeconds),
				    false,
				    DeclarationReadyForZone(a_buildZoneId, a_profileId, a_monotonicSeconds))) {
				return std::pair{ F4T::WorkshopRaids::HitRefusal::RaidWindowClosed, std::uint64_t{ 0 } };
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::pair<F4T::WorkshopRaids::HitRefusal, std::uint64_t>> CaptureRefusalFor(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			const std::string& a_profileId,
			std::uint64_t a_unixSeconds,
			double a_monotonicSeconds) const
		{
			const auto standing = StandingOf(a_buildZoneId, a_profileId);
			if (standing == F4T::WorkshopRaids::SettlementStanding::Owner) {
				return std::nullopt;
			}
			if (standing == F4T::WorkshopRaids::SettlementStanding::FactionMate) {
				return std::pair{ F4T::WorkshopRaids::HitRefusal::FactionMate, std::uint64_t{ 0 } };
			}
			return RaidRefusalFor(a_buildZoneId, a_profileId, a_unixSeconds, a_monotonicSeconds);
		}

		void NoteFactionLeaveRaidBlocks(
			std::string_view a_leaverProfileId,
			const std::vector<std::string>& a_factionMembers,
			std::uint64_t a_unixSeconds)
		{
			if (a_leaverProfileId.empty() || a_factionMembers.empty()) {
				return;
			}
			std::set<std::string> members(a_factionMembers.begin(), a_factionMembers.end());
			const auto until = a_unixSeconds +
				static_cast<std::uint64_t>(F4T::WorkshopRaids::FactionLeaveRaidCooldownHours * 3600.0F);
			auto& blocks = workshopFactionLeaveRaidUntil[std::string{ a_leaverProfileId }];
			for (const auto& [zoneId, zone] : workshopZones) {
				(void)zone;
				const auto owner = settlementAccess.OwnerOf(zoneId);
				if (owner.empty() || owner == a_leaverProfileId || !members.contains(owner)) {
					continue;
				}
				blocks[zoneId] = until;
			}
			StoreFactionLeaveRaidBlocks();
			SaveWorkshopRoot();
		}

		[[nodiscard]] std::uint64_t FactionLeaveRaidUntil(
			std::string_view a_profileId,
			F4T::Workshop::BuildZoneId a_buildZoneId) const
		{
			const auto profile = workshopFactionLeaveRaidUntil.find(std::string{ a_profileId });
			if (profile == workshopFactionLeaveRaidUntil.end()) {
				return 0;
			}
			const auto zone = profile->second.find(a_buildZoneId);
			return zone == profile->second.end() ? 0 : zone->second;
		}

		void LoadRaidWindowsFromStore()
		{
			const auto stored = workshopStored.find("raidWindows");
			if (stored == workshopStored.end() || !stored->is_object()) {
				return;
			}
			for (const auto& entry : stored->items()) {
				const auto window = F4T::WorkshopProtocol::NormalizeSettlementWindowSet(entry.value());
				if (window) {
					workshopRaidWindows[window->buildZoneId] = *window;
				}
			}
		}

		void PersistRaidDeclaration(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			const auto& declaration = workshopRaidDeclarations.at(a_buildZoneId);
			workshopStored["raidDeclarations"][std::to_string(a_buildZoneId)] = Json{
				{ "buildZoneId", a_buildZoneId },
				{ "declarerProfileId", declaration.declarerProfileId },
				{ "factionId", declaration.factionId },
				{ "readyUnix", declaration.readyUnix },
				{ "expiresUnix", declaration.expiresUnix }
			};
			SaveWorkshopRoot();
		}

		void LoadRaidDeclarationsFromStore(double a_unixSeconds)
		{
			const auto stored = workshopStored.find("raidDeclarations");
			if (stored == workshopStored.end() || !stored->is_object()) {
				return;
			}
			for (const auto& entry : stored->items()) {
				if (!entry.value().is_object()) {
					continue;
				}
				const auto& value = entry.value();
				const auto zoneId = static_cast<F4T::Workshop::BuildZoneId>(value.value("buildZoneId", std::uint64_t{ 0 }));
				const auto expiresUnix = value.value("expiresUnix", 0.0);
				if (zoneId == 0 || expiresUnix <= a_unixSeconds) {
					continue;
				}
				F4T::WorkshopRaids::RaidDeclaration declaration;
				declaration.buildZoneId = zoneId;
				declaration.declarerProfileId = value.value("declarerProfileId", std::string{});
				declaration.factionId = value.value("factionId", std::string{});
				declaration.readyUnix = value.value("readyUnix", 0.0);
				declaration.expiresUnix = expiresUnix;
				workshopRaidDeclarations[zoneId] = declaration;
			}
			workshopRaidDeclarationsNeedRebase = !workshopRaidDeclarations.empty();
		}

		void RebaseRestoredRaidDeclarations(double a_unixSeconds, double a_monotonicSeconds)
		{
			if (!workshopRaidDeclarationsNeedRebase || a_monotonicSeconds <= 0.0) {
				return;
			}
			workshopRaidDeclarationsNeedRebase = false;
			for (auto& entry : workshopRaidDeclarations) {
				auto& declaration = entry.second;
				declaration.readyMonotonic = a_monotonicSeconds + (declaration.readyUnix - a_unixSeconds);
				declaration.expiresMonotonic = a_monotonicSeconds + (declaration.expiresUnix - a_unixSeconds);
			}
		}

		void StoreFactionLeaveRaidBlocks()
		{
			Json profiles = Json::object();
			for (const auto& [profileId, zones] : workshopFactionLeaveRaidUntil) {
				Json entries = Json::array();
				for (const auto& [zoneId, until] : zones) {
					entries.push_back(Json{ { "buildZoneId", zoneId }, { "until", until } });
				}
				profiles[profileId] = std::move(entries);
			}
			workshopStored["factionLeaveRaidUntil"] = std::move(profiles);
		}

		void LoadFactionLeaveRaidFromStore(double a_unixSeconds)
		{
			const auto stored = workshopStored.find("factionLeaveRaidUntil");
			if (stored == workshopStored.end() || !stored->is_object()) {
				return;
			}
			const auto now = static_cast<std::uint64_t>(a_unixSeconds);
			for (const auto& profile : stored->items()) {
				if (!profile.value().is_array()) {
					continue;
				}
				for (const auto& entry : profile.value()) {
					if (!entry.is_object()) {
						continue;
					}
					const auto until = entry.value("until", std::uint64_t{ 0 });
					if (until <= now) {
						continue;
					}
					const auto zoneId = static_cast<F4T::Workshop::BuildZoneId>(entry.value("buildZoneId", std::uint64_t{ 0 }));
					workshopFactionLeaveRaidUntil[profile.key()][zoneId] = until;
				}
			}
			StoreFactionLeaveRaidBlocks();
		}

		void PruneExpiredRaidState(double a_unixSeconds)
		{
			const auto now = static_cast<std::uint64_t>(a_unixSeconds);
			bool changed = false;
			for (auto profile = workshopFactionLeaveRaidUntil.begin(); profile != workshopFactionLeaveRaidUntil.end();) {
				changed = std::erase_if(profile->second, [&](const auto& a_entry) { return a_entry.second <= now; }) > 0 || changed;
				profile = profile->second.empty() ? workshopFactionLeaveRaidUntil.erase(profile) : std::next(profile);
			}
			if (changed) {
				StoreFactionLeaveRaidBlocks();
			}
			std::erase_if(workshopRaidDeclarations, [&](const auto& a_entry) {
				return a_entry.second.expiresUnix <= a_unixSeconds;
			});
			if (auto stored = workshopStored.find("raidDeclarations"); stored != workshopStored.end() && stored->is_object()) {
				for (auto entry = stored->begin(); entry != stored->end();) {
					if (entry.value().value("expiresUnix", 0.0) <= a_unixSeconds) {
						entry = stored->erase(entry);
						changed = true;
					} else {
						++entry;
					}
				}
			}
			if (changed) {
				workshopRootDirty = true;
			}
		}

		std::vector<OutboundFrame> RaidStateFrames(
			std::uint32_t a_connectionId,
			std::optional<F4T::Workshop::BuildZoneId> a_buildZoneId = std::nullopt) const
		{
			std::vector<OutboundFrame> frames;
			for (const auto& [zoneId, window] : workshopRaidWindows) {
				if (a_buildZoneId && *a_buildZoneId != zoneId) {
					continue;
				}
				frames.push_back(ReliableFrame(a_connectionId, F4T::WorkshopProtocol::MakeSettlementWindowState(
					zoneId,
					window.windowStartHour,
					window.windowDurationHours,
					window.timezoneOffsetHours)));
			}
			for (const auto& [zoneId, pending] : workshopRaidDeclarations) {
				if (a_buildZoneId && *a_buildZoneId != zoneId) {
					continue;
				}
				frames.push_back(ReliableFrame(a_connectionId, F4T::WorkshopProtocol::MakeRaidDeclareState(
					zoneId,
					pending.declarerProfileId,
					pending.readyMonotonic,
					pending.expiresMonotonic,
					pending.readyUnix,
					pending.expiresUnix)));
			}
			return frames;
		}

		std::map<F4T::Workshop::BuildZoneId, F4T::WorkshopRaids::RaidDeclaration> workshopRaidDeclarations;
		bool workshopRaidDeclarationsNeedRebase{};
		std::map<F4T::Workshop::BuildZoneId, F4T::WorkshopProtocol::SettlementWindowSet> workshopRaidWindows;
		std::map<std::string, std::uint32_t> workshopRaidDeclareCounts;
		std::uint64_t workshopRaidDeclareDay{};
		std::map<std::tuple<std::uint32_t, F4T::Workshop::BuildZoneId, F4T::WorkshopMaterials::WeaponKind>, double> workshopStructureHitReady;
		std::map<std::uint32_t, F4T::PacketRateLimit::TokenBucket> workshopStructureDamageBudgets;
		std::map<std::pair<F4T::Workshop::BuildZoneId, F4T::Workshop::ObjectId>, double> workshopStructureRepairReady;
