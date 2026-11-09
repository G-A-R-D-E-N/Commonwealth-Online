		void BindAuthSteam(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_profileId,
			std::string_view a_steamId)
		{
			if (a_buildZoneId == 0 || a_profileId.empty() || a_steamId.empty()) {
				return;
			}
			workshopAuthSteamByZone[a_buildZoneId][std::string{ a_profileId }] = std::string{ a_steamId };
		}

		void ClearAuthSteam(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			workshopAuthSteamByZone.erase(a_buildZoneId);
		}

		void ClearAuthSteamProfile(F4T::Workshop::BuildZoneId a_buildZoneId, std::string_view a_profileId)
		{
			const auto zone = workshopAuthSteamByZone.find(a_buildZoneId);
			if (zone == workshopAuthSteamByZone.end()) {
				return;
			}
			zone->second.erase(std::string{ a_profileId });
			if (zone->second.empty()) {
				workshopAuthSteamByZone.erase(zone);
			}
		}

		[[nodiscard]] bool AuthSteamConflicts(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_steamId,
			std::string_view a_ignoreProfileId = {}) const
		{
			if (a_steamId.empty()) {
				return false;
			}
			if (const auto zone = workshopAuthSteamByZone.find(a_buildZoneId); zone != workshopAuthSteamByZone.end()) {
				for (const auto& [profileId, steamId] : zone->second) {
					if (profileId == a_ignoreProfileId) {
						continue;
					}
					if (steamId == a_steamId) {
						return true;
					}
				}
			}
			const auto settlement = settlementAccess.Find(a_buildZoneId);
			if (!settlement) {
				return false;
			}
			std::vector<std::string> profiles{ settlement->ownerProfileId };
			profiles.insert(profiles.end(), settlement->allowedProfileIds.begin(), settlement->allowedProfileIds.end());
			for (const auto& profileId : profiles) {
				if (profileId.empty() || profileId == a_ignoreProfileId) {
					continue;
				}
				for (const auto& [connectionId, peer] : sessions) {
					(void)connectionId;
					if (peer.active && peer.profileId == profileId && !peer.steamId.empty() && peer.steamId == a_steamId) {
						return true;
					}
				}
			}
			return false;
		}

		void PersistAuthSteam()
		{
			Json root = Json::object();
			for (const auto& [zoneId, profiles] : workshopAuthSteamByZone) {
				Json entry = Json::object();
				for (const auto& [profileId, steamId] : profiles) {
					entry[profileId] = steamId;
				}
				root[std::to_string(zoneId)] = std::move(entry);
			}
			workshopStored["authSteamByZone"] = std::move(root);
		}

		void PersistAuthSteamAndWorkshop()
		{
			PersistAuthSteam();
			SaveWorkshopRoot();
		}

		void LoadAuthSteamFromStore()
		{
			workshopAuthSteamByZone.clear();
			const auto root = workshopStored.find("authSteamByZone");
			if (root == workshopStored.end() || !root->is_object()) {
				return;
			}
			for (const auto& [zoneKey, profiles] : root->items()) {
				if (!profiles.is_object()) {
					continue;
				}
				std::uint64_t zoneId = 0;
				try {
					zoneId = std::stoull(zoneKey);
				} catch (...) {
					continue;
				}
				if (zoneId == 0) {
					continue;
				}
				auto& slot = workshopAuthSteamByZone[zoneId];
				for (const auto& [profileId, steamId] : profiles.items()) {
					if (!steamId.is_string() || steamId.get_ref<const std::string&>().empty()) {
						continue;
					}
					slot[profileId] = steamId.get<std::string>();
				}
			}
		}

		void LoadWorkshopCampsFromStore()
		{
			workshopCamps.clear();
			workshopNextCampSequence = 1;
			LoadAuthSteamFromStore();
			const auto sequence = workshopStored.find("nextCampSequence");
			if (sequence != workshopStored.end()) {
				if (sequence->is_number_unsigned()) {
					workshopNextCampSequence = sequence->get<std::uint64_t>();
				} else if (sequence->is_number_integer()) {
					const auto value = sequence->get<std::int64_t>();
					if (value > 0) {
						workshopNextCampSequence = static_cast<std::uint64_t>(value);
					}
				}
			}
			const auto camps = workshopStored.find("camps");
			if (camps == workshopStored.end() || !camps->is_array()) {
				return;
			}
			bool campsDirty = false;
			for (const auto& entry : *camps) {
				const auto camp = F4T::WorkshopCamps::ReadCampRecord(entry);
				if (!camp) {
					continue;
				}
				auto record = *camp;
				if (!record.stored) {
					if (!RegisterWorkshopBuildZone(F4T::WorkshopCamps::MakeCampPolicy(record))) {
						record.stored = true;
						campsDirty = true;
					} else {
						std::string failure;
						(void)settlementAccess.SetOwner(
							FactionPeers(),
							workshopZones,
							record.buildZoneId,
							record.ownerProfileId,
							failure,
							F4T::SettlementAccess::SettlementKind::Camp);
					}
				}
				workshopCamps.push_back(std::move(record));
				if ((camp->buildZoneId & ~F4T::WorkshopCamps::CampZoneIdHighBit) >= workshopNextCampSequence) {
					workshopNextCampSequence = (camp->buildZoneId & ~F4T::WorkshopCamps::CampZoneIdHighBit) + 1;
				}
			}
			if (campsDirty) {
				(void)SaveWorkshopCampsDurable();
			}
		}

		void PersistWorkshopCamps()
		{
			Json camps = Json::array();
			for (const auto& camp : workshopCamps) {
				camps.push_back(F4T::WorkshopCamps::CampRecordJson(camp));
			}
			workshopStored["camps"] = std::move(camps);
			workshopStored["nextCampSequence"] = workshopNextCampSequence;
		}

		[[nodiscard]] bool SaveWorkshopCampsDurable()
		{
			if (workshopPersistence == nullptr) {
				return false;
			}
			PersistWorkshopCamps();
			PersistAuthSteam();
			return SaveWorkshopRoot();
		}

		struct CampZoneState
		{
			F4T::Workshop::BuildZoneId buildZoneId{};
			std::optional<F4T::Workshop::SettlementState> zone;
			std::optional<F4T::WorkshopPower::PowerGraph> power;
			std::optional<Json> stored;
		};

		[[nodiscard]] CampZoneState CaptureCampZone(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			CampZoneState state{ a_buildZoneId };
			if (const auto zone = workshopZones.find(a_buildZoneId); zone != workshopZones.end()) {
				state.zone = zone->second;
			}
			if (const auto power = workshopPower.find(a_buildZoneId); power != workshopPower.end()) {
				state.power = power->second;
			}
			if (const auto zones = workshopStored.find("zones"); zones != workshopStored.end() && zones->is_object()) {
				if (const auto stored = zones->find(std::to_string(a_buildZoneId)); stored != zones->end()) {
					state.stored = *stored;
				}
			}
			return state;
		}

		void ApplyCampZone(const CampZoneState& a_state)
		{
			if (a_state.zone) {
				workshopZones.insert_or_assign(a_state.buildZoneId, *a_state.zone);
			} else {
				workshopZones.erase(a_state.buildZoneId);
				ForgetZoneDoors(a_state.buildZoneId);
			}
			if (a_state.power) {
				workshopPower.insert_or_assign(a_state.buildZoneId, *a_state.power);
			} else {
				workshopPower.erase(a_state.buildZoneId);
			}
			const auto key = std::to_string(a_state.buildZoneId);
			if (a_state.stored) {
				workshopStored["zones"][key] = *a_state.stored;
			} else if (const auto zones = workshopStored.find("zones"); zones != workshopStored.end() && zones->is_object()) {
				zones->erase(key);
			}
		}

		[[nodiscard]] std::vector<F4T::Workshop::BuildZonePolicy> CampSettlementPolicies() const
		{
			std::vector<F4T::Workshop::BuildZonePolicy> settlements;
			settlements.reserve(workshopZones.size());
			for (const auto& entry : workshopZones) {
				settlements.push_back(entry.second.Policy());
			}
			return settlements;
		}

		[[nodiscard]] std::vector<F4T::Workshop::FormKey> CampKnownSpaces() const
		{
			auto spaces = workshopNoBuild.Spaces();
			for (const auto& [zoneId, zone] : workshopZones) {
				if (!F4T::WorkshopCamps::IsCampZoneId(zoneId)) {
					spaces.push_back(zone.Policy().space);
				}
			}
			return spaces;
		}

		[[nodiscard]] std::optional<F4T::WorkshopCamps::CampPlacement> CampPlacementFor(
			const Session& a_session,
			std::string_view a_plugin,
			float a_x,
			float a_y,
			float a_z,
			double a_monotonicSeconds) const
		{
			if (!a_session.lastMovement || !a_session.lastMovementMonotonic) {
				return std::nullopt;
			}
			const auto age = a_monotonicSeconds - *a_session.lastMovementMonotonic;
			if (!(age >= 0.0) || age > kMaximumCampPlacementAgeSeconds) {
				return std::nullopt;
			}
			const auto& movement = *a_session.lastMovement;
			return F4T::WorkshopCamps::ResolvePlacement(
				F4T::WorkshopCamps::PlayerLocation{ movement.worldspaceId, movement.cellId, movement.x, movement.y, movement.z },
				a_plugin,
				CampKnownSpaces(),
				a_x,
				a_y,
				a_z);
		}

		[[nodiscard]] bool CampReachedBy(const Session& a_session, const F4T::WorkshopCamps::CampRecord& a_camp, double a_monotonicSeconds) const
		{
			const auto reach = CampPlacementFor(a_session, a_camp.space.plugin, a_camp.centerX, a_camp.centerY, a_camp.centerZ, a_monotonicSeconds);
			return reach && reach->space.localFormId == a_camp.space.localFormId;
		}

		std::vector<OutboundFrame> HandleCampFound(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			const auto request = F4T::WorkshopCamps::NormalizeCampFound(a_packet, a_session.profileId);
			if (!request) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(0, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			const auto placement = CampPlacementFor(a_session, request->space.plugin, request->x, request->y, request->z, a_monotonicSeconds);
			if (!placement) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			auto withTime = *request;
			withTime.nowUnix = static_cast<std::uint64_t>(a_unixTimeSeconds);
			withTime.space = placement->space;
			withTime.x = placement->x;
			withTime.y = placement->y;
			withTime.z = placement->z;
			const auto kitDrop = inventoryAuthority ? inventoryAuthority->Store().GetLiveDrop(request->dropEntityId) : Json();
			const auto kitStatus = F4T::WorkshopCamps::CheckCampKitDrop(kitDrop, SessionSocialKeyView(a_session));
			if (kitStatus != F4T::WorkshopCamps::FoundStatus::Ok) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, kitStatus)) };
			}
			const bool inNoBuild = workshopNoBuild.Contains(placement->space, placement->x, placement->y, placement->z, placement->cell);
			const auto storedCamp = std::find_if(workshopCamps.begin(), workshopCamps.end(), [&](const F4T::WorkshopCamps::CampRecord& a_camp) {
				return a_camp.stored && a_camp.ownerProfileId == withTime.ownerProfileId;
			});
			const bool reactivating = storedCamp != workshopCamps.end();
			const auto reuseZoneId = reactivating ? storedCamp->buildZoneId : F4T::Workshop::BuildZoneId{ 0 };
			F4T::WorkshopCamps::SeparationDetail separation;
			const auto status = F4T::WorkshopCamps::ValidateFound(withTime, workshopCamps, CampSettlementPolicies(), inNoBuild, reuseZoneId, &separation);
			if (status != F4T::WorkshopCamps::FoundStatus::Ok) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, status, separation)) };
			}
			if (reactivating && workshopCosts.Size() == 0) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::RefundUnavailable)) };
			}
			if (workshopPersistence == nullptr || inventoryAuthority != workshopPersistence || !workshopPersistence->SavingEnabled()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			F4T::WorkshopCamps::CampRecord camp;
			camp.buildZoneId = reactivating ? reuseZoneId : F4T::WorkshopCamps::MakeCampZoneId(workshopNextCampSequence);
			camp.ownerProfileId = withTime.ownerProfileId;
			camp.space = withTime.space;
			camp.centerX = withTime.x;
			camp.centerY = withTime.y;
			camp.centerZ = withTime.z;
			camp.foundedAtUnix = withTime.nowUnix;
			camp.lastMovedAtUnix = withTime.nowUnix;
			auto stagedSettlement = settlementAccess.StageCamp(workshopPersistence, camp.buildZoneId, camp.ownerProfileId,
				withTime.nowUnix + F4T::WorkshopCamps::CampUpkeepGraceSeconds);
			if (!stagedSettlement) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			const auto previousCamps = workshopCamps;
			const auto previousStored = workshopStored;
			const auto previousAuth = workshopAuthSteamByZone;
			const auto previousZone = CaptureCampZone(camp.buildZoneId);
			if (!RegisterWorkshopBuildZone(F4T::WorkshopCamps::MakeCampPolicy(camp))) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::Limit)) };
			}
			const auto zone = workshopZones.find(camp.buildZoneId);
			const auto culled = reactivating ? zone->second.CullAll() : zone->second.CullOutsideRadius(withTime.nowUnix);
			const auto refund = F4T::WorkshopCosts::HalfRefund(workshopCosts, culled);
			auto& graph = workshopPower[camp.buildZoneId];
			PrunePowerNodes(zone->second);
			workshopStored["zones"][std::to_string(camp.buildZoneId)] = F4T::WorkshopProtocol::MakeZoneState(zone->second);
			workshopStored["zones"][std::to_string(camp.buildZoneId)]["power"] = graph.ToJson();
			if (!reactivating) {
				++workshopNextCampSequence;
			}
			std::erase_if(workshopCamps, [&](const F4T::WorkshopCamps::CampRecord& a_camp) {
				return a_camp.buildZoneId == camp.buildZoneId;
			});
			workshopCamps.push_back(camp);
			BindAuthSteam(camp.buildZoneId, camp.ownerProfileId, a_session.steamId);
			PersistWorkshopCamps();
			PersistAuthSteam();
			F4T::CampGrants::Commit grant;
			try {
				grant = workshopPersistence->Store().CommitCamp(request->dropEntityId, kitDrop, workshopStored,
					F4T::SettlementAccessProtocol::MakeStoredJson(stagedSettlement->Snapshot()), withTime.ownerProfileId,
					request->dropEntityId, refund.lines);
			} catch (const std::exception& error) {
				std::cerr << "Workshop camp persistence unavailable: " << error.what() << '\n';
			}
			if (grant.outcome != F4T::CampGrants::Outcome::Committed) {
				workshopCamps = previousCamps;
				workshopAuthSteamByZone = previousAuth;
				if (!reactivating) {
					--workshopNextCampSequence;
				}
				ApplyCampZone(previousZone);
				workshopStored = previousStored;
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			auto settlementFrames = settlementAccess.CommitCamp(std::move(*stagedSettlement), FactionPeers(), camp.buildZoneId);
			const auto created = F4T::WorkshopCamps::MakeCampCreated(camp, withTime.operationId);
			auto ownCreated = created;
			ownCreated["own"] = true;
			std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, ownCreated) };
			AppendFrames(frames, std::move(settlementFrames));
			AppendFrames(frames, WorkshopSnapshotFrames(a_connectionId, zone->second));
			if (!culled.empty()) {
				frames.push_back(ReliableFrame(
					a_connectionId,
					F4T::WorkshopCamps::MakeCampRefund(camp.buildZoneId, withTime.operationId, culled.size(), refund.unpricedPieces,
						F4T::CampGrants::FlattenLines(grant.chunks))));
			}
			if (!grant.chunks.empty()) {
				frames.push_back(ReliableFrame(a_connectionId, F4T::CampGrants::MakeGrantPacket(request->dropEntityId, grant.chunks)));
			}
			AppendFrames(frames, RefreshInventoryProjection());
			for (const auto& [connectionId, peer] : sessions) {
				if (connectionId == a_connectionId || !peer.active) {
					continue;
				}
				frames.push_back(ReliableFrame(connectionId, created));
				AppendFrames(frames, WorkshopSnapshotFrames(connectionId, zone->second));
			}
			return frames;
		}

		std::vector<OutboundFrame> CampMoveReplayFrames(
			std::uint32_t a_connectionId,
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::uint64_t a_operationId,
			const std::string& a_profileId,
			const F4T::CampGrants::Commit& a_replay)
		{
			std::vector<OutboundFrame> frames;
			const auto camp = std::find_if(workshopCamps.begin(), workshopCamps.end(), [&](const F4T::WorkshopCamps::CampRecord& a_camp) {
				return a_camp.buildZoneId == a_buildZoneId;
			});
			if (camp != workshopCamps.end()) {
				if (camp->ownerProfileId != a_profileId) {
					Reject();
					frames.push_back(ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(a_operationId, F4T::WorkshopCamps::FoundStatus::NotOwner)));
				} else {
					auto moved = F4T::WorkshopCamps::MakeCampMoved(*camp, a_operationId);
					moved["own"] = true;
					frames.push_back(ReliableFrame(a_connectionId, moved));
				}
			}
			if (a_replay.pending && !a_replay.chunks.empty()) {
				frames.push_back(ReliableFrame(a_connectionId, F4T::CampGrants::MakeGrantPacket(a_operationId, a_replay.chunks)));
			}
			return frames;
		}

		std::vector<OutboundFrame> CampGrantFrames(std::uint32_t a_connectionId)
		{
			const auto found = sessions.find(a_connectionId);
			if (found == sessions.end() || !found->second.active || found->second.campGrantsSent || found->second.profileId.empty() ||
				workshopPersistence == nullptr || inventoryAuthority != workshopPersistence) {
				return {};
			}
			found->second.campGrantsSent = true;
			std::vector<OutboundFrame> frames;
			try {
				for (const auto& grant : workshopPersistence->Store().PendingCampGrants(found->second.profileId)) {
					frames.push_back(ReliableFrame(a_connectionId, F4T::CampGrants::MakeGrantPacket(grant.operationId, grant.chunks)));
				}
			} catch (const std::exception&) {
			}
			return frames;
		}

		std::vector<OutboundFrame> HandleCampGrantAck(Session& a_session, const Json& a_packet)
		{
			const auto operationId = F4T::CampGrants::ParseGrantAck(a_packet);
			if (!operationId) {
				Reject();
				return {};
			}
			try {
				if (workshopPersistence != nullptr) {
					(void)workshopPersistence->Store().AckCampGrant(a_session.profileId, *operationId);
				}
			} catch (const std::exception&) {
			}
			return {};
		}

		std::vector<OutboundFrame> HandleCampMove(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			const auto request = F4T::WorkshopCamps::NormalizeCampMove(a_packet, a_session.profileId);
			if (!request) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(0, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			if (workshopPersistence != nullptr) {
				try {
					if (const auto replay = workshopPersistence->Store().FindCampMove(a_session.profileId, request->operationId)) {
						return CampMoveReplayFrames(a_connectionId, request->buildZoneId, request->operationId, a_session.profileId, *replay);
					}
				} catch (const std::exception&) {
				}
			}
			const auto campIt = std::find_if(workshopCamps.begin(), workshopCamps.end(), [&](const F4T::WorkshopCamps::CampRecord& a_camp) {
				return a_camp.buildZoneId == request->buildZoneId;
			});
			if (campIt == workshopCamps.end()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::NotFound)) };
			}
			const auto placement = CampPlacementFor(
				a_session,
				request->space.plugin.empty() ? campIt->space.plugin : request->space.plugin,
				request->x,
				request->y,
				request->z,
				a_monotonicSeconds);
			if (!placement) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			auto withTime = *request;
			withTime.nowUnix = static_cast<std::uint64_t>(a_unixTimeSeconds);
			withTime.space = placement->space;
			withTime.x = placement->x;
			withTime.y = placement->y;
			withTime.z = placement->z;
			const auto attack = workshopAttackStates.find(withTime.buildZoneId);
			const bool underAttack = attack != workshopAttackStates.end() && attack->second.underAttack;
			const bool inNoBuild = workshopNoBuild.Contains(placement->space, placement->x, placement->y, placement->z, placement->cell);
			F4T::WorkshopCamps::SeparationDetail separation;
			const auto status = F4T::WorkshopCamps::ValidateMove(withTime, *campIt, workshopCamps, CampSettlementPolicies(), inNoBuild, underAttack, &separation);
			if (status != F4T::WorkshopCamps::FoundStatus::Ok) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, status, separation)) };
			}
			if (placement->space.plugin != campIt->space.plugin || placement->space.localFormId != campIt->space.localFormId) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			if (workshopPersistence == nullptr || inventoryAuthority != workshopPersistence || !workshopPersistence->SavingEnabled()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			if (workshopCosts.Size() == 0) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::RefundUnavailable)) };
			}
			const auto previousCamps = workshopCamps;
			const auto previousAuth = workshopAuthSteamByZone;
			const auto previousZone = CaptureCampZone(withTime.buildZoneId);
			const auto previousSettlement = settlementAccess.Find(withTime.buildZoneId);
			const auto rollback = [&]() {
				workshopCamps = previousCamps;
				workshopAuthSteamByZone = previousAuth;
				ApplyCampZone(previousZone);
				PersistWorkshopCamps();
				PersistAuthSteam();
				(void)settlementAccess.RestoreSettlement(withTime.buildZoneId, previousSettlement);
			};
			campIt->centerX = withTime.x;
			campIt->centerY = withTime.y;
			campIt->centerZ = withTime.z;
			campIt->lastMovedAtUnix = withTime.nowUnix;
			auto zone = workshopZones.find(withTime.buildZoneId);
			if (zone == workshopZones.end()) {
				rollback();
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::NotFound)) };
			}
			auto candidate = zone->second;
			if (!candidate.RelocateCenter(withTime.x, withTime.y, withTime.z)) {
				rollback();
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			const auto culled = candidate.CullAll();
			const auto refund = F4T::WorkshopCosts::HalfRefund(workshopCosts, culled);
			PrunePowerNodes(candidate);
			const auto zoneKey = std::to_string(withTime.buildZoneId);
			workshopStored["zones"][zoneKey] = F4T::WorkshopProtocol::MakeZoneState(candidate);
			workshopStored["zones"][zoneKey]["power"] = workshopPower[withTime.buildZoneId].ToJson();
			PersistWorkshopCamps();
			PersistAuthSteam();
			F4T::CampGrants::Commit grant;
			try {
				grant = workshopPersistence->Store().CommitCampMove(workshopStored, a_session.profileId, withTime.operationId, refund.lines);
			} catch (const std::exception& error) {
				std::cerr << "Workshop camp persistence unavailable: " << error.what() << '\n';
			}
			if (grant.outcome != F4T::CampGrants::Outcome::Committed) {
				rollback();
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(withTime.operationId, F4T::WorkshopCamps::FoundStatus::GrantFailed)) };
			}
			zone->second = std::move(candidate);
			const auto moved = F4T::WorkshopCamps::MakeCampMoved(*campIt, withTime.operationId);
			auto ownMoved = moved;
			ownMoved["own"] = true;
			std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, ownMoved) };
			frames.push_back(ReliableFrame(
				a_connectionId,
				F4T::WorkshopCamps::MakeCampRefund(campIt->buildZoneId, withTime.operationId, culled.size(), refund.unpricedPieces, F4T::CampGrants::FlattenLines(grant.chunks))));
			if (!grant.chunks.empty()) {
				frames.push_back(ReliableFrame(a_connectionId, F4T::CampGrants::MakeGrantPacket(withTime.operationId, grant.chunks)));
			}
			AppendFrames(frames, WorkshopSnapshotFrames(a_connectionId, zone->second));
			AppendFrames(frames, FanoutPowerState(withTime.buildZoneId));
			for (const auto& [connectionId, peer] : sessions) {
				if (connectionId == a_connectionId || !peer.active) {
					continue;
				}
				frames.push_back(ReliableFrame(connectionId, moved));
				AppendFrames(frames, WorkshopSnapshotFrames(connectionId, zone->second));
			}
			return frames;
		}

		std::vector<OutboundFrame> HandleCampStore(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			const auto request = F4T::WorkshopCamps::NormalizeCampZoneOp(a_packet, a_session.profileId, "campStore");
			if (!request) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(0, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			const auto campIt = std::find_if(workshopCamps.begin(), workshopCamps.end(), [&](const F4T::WorkshopCamps::CampRecord& a_camp) {
				return a_camp.buildZoneId == request->buildZoneId;
			});
			if (campIt == workshopCamps.end() || campIt->stored) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::NotFound)) };
			}
			if (campIt->ownerProfileId != a_session.profileId) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::NotOwner)) };
			}
			const auto attack = workshopAttackStates.find(request->buildZoneId);
			if (attack != workshopAttackStates.end() && attack->second.underAttack) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::UnderAttack)) };
			}
			if (!CampReachedBy(a_session, *campIt, a_monotonicSeconds)) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::TooFar)) };
			}
			const auto kitPosition = OwnedInventoryPosition(a_session, a_monotonicSeconds);
			if (kitPosition.cellId.empty() || workshopPersistence == nullptr || inventoryAuthority != workshopPersistence || !workshopPersistence->SavingEnabled()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			if (workshopPersistence->Store().CampKitDropLimited(a_session.profileId)) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::DropLimit)) };
			}
			const auto previousCamps = workshopCamps;
			const auto previousAuth = workshopAuthSteamByZone;
			const auto previousZone = CaptureCampZone(request->buildZoneId);
			const auto previousSettlement = settlementAccess.Find(request->buildZoneId);
			if (const auto zone = workshopZones.find(request->buildZoneId); zone != workshopZones.end() && !SaveZoneWithPower(zone->second)) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			std::string failure;
			auto settlementFrames = settlementAccess.SetOwner(FactionPeers(), workshopZones, request->buildZoneId, {}, failure);
			if (!failure.empty()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			workshopZones.erase(request->buildZoneId);
			ForgetZoneDoors(request->buildZoneId);
			workshopPower.erase(request->buildZoneId);
			campIt->stored = true;
			campIt->lastMovedAtUnix = static_cast<std::uint64_t>(a_unixTimeSeconds);
			ClearAuthSteam(request->buildZoneId);
			PersistWorkshopCamps();
			PersistAuthSteam();
			bool committed = false;
			try {
				committed = workshopPersistence->Store().CommitCampStore(workshopStored, a_session.profileId,
					F4T::WorkshopCamps::CampKitItemKey, kitPosition);
			} catch (const std::exception& error) {
				std::cerr << "Workshop camp persistence unavailable: " << error.what() << '\n';
			}
			if (!committed) {
				workshopCamps = previousCamps;
				workshopAuthSteamByZone = previousAuth;
				ApplyCampZone(previousZone);
				PersistWorkshopCamps();
				PersistAuthSteam();
				(void)settlementAccess.RestoreSettlement(request->buildZoneId, previousSettlement);
				Reject();
				const auto failure = workshopPersistence->Store().CampKitDropLimited(a_session.profileId) ?
					F4T::WorkshopCamps::FoundStatus::DropLimit : F4T::WorkshopCamps::FoundStatus::InvalidRequest;
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, failure)) };
			}
			workshopAttackStates.erase(request->buildZoneId);
			const auto stored = F4T::WorkshopCamps::MakeCampStored(request->buildZoneId, request->operationId);
			std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, stored) };
			AppendFrames(frames, std::move(settlementFrames));
			AppendFrames(frames, RefreshInventoryProjection());
			for (const auto& [connectionId, peer] : sessions) {
				if (connectionId != a_connectionId && peer.active) {
					frames.push_back(ReliableFrame(connectionId, stored));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> HandleCampRemove(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			const auto request = F4T::WorkshopCamps::NormalizeCampZoneOp(a_packet, a_session.profileId, "campRemove");
			if (!request) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(0, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			const auto campIt = std::find_if(workshopCamps.begin(), workshopCamps.end(), [&](const F4T::WorkshopCamps::CampRecord& a_camp) {
				return a_camp.buildZoneId == request->buildZoneId;
			});
			if (campIt == workshopCamps.end()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::NotFound)) };
			}
			if (campIt->ownerProfileId != a_session.profileId) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::NotOwner)) };
			}
			const auto attack = workshopAttackStates.find(request->buildZoneId);
			if (attack != workshopAttackStates.end() && attack->second.underAttack) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::UnderAttack)) };
			}
			if (!CampReachedBy(a_session, *campIt, a_monotonicSeconds)) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::TooFar)) };
			}
			if (workshopPersistence == nullptr || inventoryAuthority != workshopPersistence || !workshopPersistence->SavingEnabled()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			std::vector<F4T::Trade::Line> refund;
			if (!campIt->stored) {
				refund.push_back(F4T::Trade::Line{ std::string(F4T::WorkshopCamps::CampKitItemKey), 1 });
			}
			const auto previousCamps = workshopCamps;
			const auto previousAuth = workshopAuthSteamByZone;
			const auto previousZone = CaptureCampZone(request->buildZoneId);
			const auto previousSettlement = settlementAccess.Find(request->buildZoneId);
			std::string failure;
			auto settlementFrames = settlementAccess.SetOwner(FactionPeers(), workshopZones, request->buildZoneId, {}, failure);
			if (!failure.empty()) {
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			workshopCamps.erase(campIt);
			ClearAuthSteam(request->buildZoneId);
			ApplyCampZone(CampZoneState{ request->buildZoneId });
			PersistWorkshopCamps();
			PersistAuthSteam();
			F4T::CampGrants::Commit grant;
			try {
				grant = workshopPersistence->Store().CommitCampMove(workshopStored, a_session.profileId, request->operationId, refund);
			} catch (const std::exception& error) {
				std::cerr << "Workshop camp persistence unavailable: " << error.what() << '\n';
			}
			if (grant.outcome != F4T::CampGrants::Outcome::Committed) {
				workshopCamps = previousCamps;
				workshopAuthSteamByZone = previousAuth;
				ApplyCampZone(previousZone);
				PersistWorkshopCamps();
				PersistAuthSteam();
				(void)settlementAccess.RestoreSettlement(request->buildZoneId, previousSettlement);
				Reject();
				return { ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampRejected(request->operationId, F4T::WorkshopCamps::FoundStatus::InvalidRequest)) };
			}
			workshopAttackStates.erase(request->buildZoneId);
			const auto removed = F4T::WorkshopCamps::MakeCampRemoved(request->buildZoneId, request->operationId);
			std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, removed) };
			if (!grant.chunks.empty()) {
				frames.push_back(ReliableFrame(a_connectionId, F4T::CampGrants::MakeGrantPacket(request->operationId, grant.chunks)));
			}
			AppendFrames(frames, std::move(settlementFrames));
			for (const auto& [connectionId, peer] : sessions) {
				if (connectionId != a_connectionId && peer.active) {
					frames.push_back(ReliableFrame(connectionId, removed));
				}
			}
			return frames;
		}
