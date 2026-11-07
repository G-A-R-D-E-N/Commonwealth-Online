		%:include "ServerSettlementCharge.h"
		%:include "ServerWorkshopHealth.h"
		%:include "ServerWorkshopCamps.h"
		%:include "ServerWorkshopStructure.h"
		%:include "ServerWorkshopDefenses.h"
		%:include "ServerWorkshopPower.h"
		%:include "ServerWorkshopDoors.h"
		%:include "ServerWorkshopRepair.h"
		%:include "ServerWorkshopRaids.h"

		bool RegisterWorkshopBuildZone(F4T::Workshop::BuildZonePolicy a_policy)
		{
			F4T::Workshop::SettlementState state{ a_policy };
			if (!state.PolicyValid() || workshopZones.contains(a_policy.buildZoneId)) {
				return false;
			}
			RestoreStoredWorkshopZone(state);
			workshopZones.emplace(a_policy.buildZoneId, std::move(state));
			return true;
		}

		[[nodiscard]] std::size_t WorkshopObjectCount() const
		{
			std::size_t total = 0;
			for (const auto& [zoneId, zone] : workshopZones) {
				(void)zoneId;
				total += zone.ObjectCount();
			}
			return total;
		}

		bool EnableWorkshopPersistence(PersistenceGate* a_persistence)
		{
			if (a_persistence == nullptr || workshopPersistence != nullptr) {
				return false;
			}
			const auto stored = a_persistence->GetWorkshop();
			if (!stored.is_null()) {
				if (auto migrated = stored; F4T::WorkshopPersistence::MigrateWorkshopRoot(migrated)) {
					workshopStored = std::move(migrated);
				} else {
					std::cerr << "Stored workshop root is unreadable; workshop persistence stays disabled so the stored root is not overwritten.\n";
					return false;
				}
			}
			workshopPersistence = a_persistence;
			for (auto& entry : workshopZones) {
				RestoreStoredWorkshopZone(entry.second);
			}
			LoadWorkshopCampsFromStore();
			LoadRaidWindowsFromStore();
			const auto nowUnix = static_cast<double>(workshopPersistence->Store().Now());
			LoadRaidDeclarationsFromStore(nowUnix);
			LoadFactionLeaveRaidFromStore(nowUnix);
			LoadProfileFirstSeenFromStore();
			EstablishProfileFirstSeenEpoch();
			RestoreSettlementCharges(a_persistence->Store().PendingSettlementCharges());
			return true;
		}

		std::size_t ConfigureWorkshop(PersistenceGate* a_persistence, const std::filesystem::path& a_zonesFile)
		{
			if (a_persistence != nullptr) {
				(void)EnableWorkshopPersistence(a_persistence);
			}
			const auto name = F4T::Utf8Path::Encode(a_zonesFile);
			std::ifstream input(a_zonesFile, std::ios::binary);
			if (!input) {
				std::cerr << "Workshop build zones: no zone file at " << name
				          << "; workshop edits answer unknownBuildZone until it exists.\n";
				return 0;
			}
			const auto parsed = Json::parse(input, nullptr, false);
			const auto zones = parsed.is_discarded() ? std::nullopt : F4T::WorkshopProtocol::ParseBuildZones(parsed);
			if (!zones) {
				std::cerr << "Workshop build zones: " << name << " is invalid; no zones registered.\n";
				return 0;
			}
			if (!zones->empty() && workshopPersistence == nullptr) {
				std::cerr << "Workshop build zones: refusing to register zones from " << name
				          << " without durable persistence (enable_persistence).\n";
				return 0;
			}
			std::size_t registered = 0;
			for (const auto& policy : *zones) {
				if (RegisterWorkshopBuildZone(policy)) ++registered;
			}
			std::cout << "Workshop build zones: " << registered << " registered from " << name << std::endl;
			const auto defenseFormsPath = a_zonesFile.parent_path() / "defense-forms.json";
			if (std::ifstream defenseInput(defenseFormsPath, std::ios::binary); defenseInput) {
				const auto defenseParsed = Json::parse(defenseInput, nullptr, false);
				if (!defenseParsed.is_discarded() && workshopDefenseForms.Load(defenseParsed)) {
					std::cout << "Workshop defense forms: loaded from " << F4T::Utf8Path::Encode(defenseFormsPath) << std::endl;
				} else {
					std::cerr << "Workshop defense forms: " << F4T::Utf8Path::Encode(defenseFormsPath)
					          << " is invalid; using turret defaults.\n";
				}
			}
			const auto noBuildPath = a_zonesFile.parent_path() / "no-build-zones.json";
			if (std::ifstream noBuildInput(noBuildPath, std::ios::binary); noBuildInput) {
				const auto noBuildParsed = Json::parse(noBuildInput, nullptr, false);
				if (!noBuildParsed.is_discarded() && workshopNoBuild.LoadFromJson(noBuildParsed)) {
					std::cout << "Workshop no-build zones: " << workshopNoBuild.Size() << " loaded from "
					          << F4T::Utf8Path::Encode(noBuildPath) << std::endl;
				} else {
					workshopNoBuild = {};
					std::cerr << "Workshop no-build zones: " << F4T::Utf8Path::Encode(noBuildPath)
					          << " is invalid; camps ignore the table until repaired.\n";
				}
			}
			LoadWorkshopPieces(a_zonesFile.parent_path() / "workshop-pieces.json");
			LoadWorkshopCosts(a_zonesFile.parent_path() / "workshop-costs.json");
			LoadWeaponClasses(a_zonesFile.parent_path() / "weapon-classes.json");
			return registered;
		}

		void RestoreStoredWorkshopZone(F4T::Workshop::SettlementState& a_state)
		{
			const auto key = std::to_string(a_state.Id());
			const auto stored = workshopStored["zones"].find(key);
			if (stored == workshopStored["zones"].end()) {
				return;
			}
			if (!F4T::WorkshopProtocol::RestoreZoneState(a_state, *stored)) {
				std::cerr << "Stored workshop zone " << key << " is unreadable; starting it empty and keeping the payload under backup.\n";
				workshopStored["backup"][key] = *stored;
				return;
			}
			const auto power = stored->find("power");
			if (power != stored->end()) {
				F4T::WorkshopPower::PowerGraph graph;
				if (graph.FromJson(*power)) {
					workshopPower.emplace(a_state.Id(), std::move(graph));
				}
			}
		}

		[[nodiscard]] bool SaveWorkshopZone(const F4T::Workshop::SettlementState& a_zone)
		{
			if (workshopPersistence == nullptr) {
				return true;
			}
			const auto key = std::to_string(a_zone.Id());
			const auto found = workshopStored["zones"].find(key);
			const std::optional<Json> previous = found == workshopStored["zones"].end() ? std::nullopt : std::optional<Json>{ *found };
			auto& stored = workshopStored["zones"][key];
			stored = F4T::WorkshopProtocol::MakeZoneState(a_zone);
			if (const auto power = workshopPower.find(a_zone.Id()); power != workshopPower.end()) {
				stored["power"] = power->second.ToJson();
			}
			try {
				// Constraint: synchronous, so a failed write throws here.
				workshopPersistence->SaveWorkshop(workshopStored, true);
			} catch (const std::exception& error) {
				std::cerr << "Workshop persistence unavailable: " << error.what() << '\n';
				if (previous) {
					workshopStored["zones"][key] = *previous;
				} else {
					workshopStored["zones"].erase(key);
				}
				return false;
			}
			return true;
		}

		void MarkWorkshopDirty(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			workshopDirtyZones.insert(a_buildZoneId);
		}

		std::vector<OutboundFrame> OwnerSettlementFrames(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			const auto owner = settlementAccess.OwnerOf(a_buildZoneId);
			auto peers = FactionPeers();
			std::erase_if(peers, [&](const auto& a_peer) { return owner.empty() || a_peer.profileId != owner; });
			return settlementAccess.StateFrames(peers, a_buildZoneId);
		}

		void FlushDirtyWorkshopZones(double a_unixSeconds, std::vector<OutboundFrame>& a_frames)
		{
			if (workshopPersistence == nullptr) {
				return;
			}
			bool changed = false;
			const auto dirtyZones = workshopDirtyZones;
			workshopDirtyZones.clear();
			for (const auto buildZoneId : dirtyZones) {
				const auto zone = workshopZones.find(buildZoneId);
				if (zone == workshopZones.end()) {
					continue;
				}
				if (zone->second.SweepExpiredTombstones(static_cast<std::uint64_t>(a_unixSeconds)) > 0) AppendFrames(a_frames, OwnerSettlementFrames(buildZoneId));
				auto& stored = workshopStored["zones"][std::to_string(buildZoneId)];
				stored = F4T::WorkshopProtocol::MakeZoneState(zone->second);
				if (const auto power = workshopPower.find(buildZoneId); power != workshopPower.end()) {
					stored["power"] = power->second.ToJson();
				}
				changed = true;
			}

			const auto dayKey = WorkshopUtcDayKey(a_unixSeconds);
			auto& dailyBackups = workshopStored["dailyBackups"];
			if (!dailyBackups.is_object()) {
				dailyBackups = Json::object();
			}
			if (!dailyBackups.contains(dayKey)) {
				dailyBackups[dayKey] = workshopStored["zones"];
				changed = true;
				while (dailyBackups.size() > 7) {
					std::string oldest;
					for (const auto& entry : dailyBackups.items()) {
						if (oldest.empty() || entry.key() < oldest) {
							oldest = entry.key();
						}
					}
					if (oldest.empty()) {
						break;
					}
					dailyBackups.erase(oldest);
				}
			}

			const bool writeRetryDue = workshopPersistence->Store().LastWriteFailed() &&
				a_unixSeconds >= workshopRootLastSaveUnix + 5.0;
			if (!changed && !workshopRootDirty && !writeRetryDue) {
				return;
			}
			workshopRootDirty = false;
			workshopRootLastSaveUnix = a_unixSeconds;
			if (!SaveWorkshopRoot()) {
				workshopDirtyZones.insert(dirtyZones.begin(), dirtyZones.end());
				workshopRootDirty = true;
			}
		}

		bool SaveWorkshopRoot()
		{
			if (workshopPersistence == nullptr) {
				return false;
			}
			try {
				workshopPersistence->SaveWorkshop(workshopStored);
				return true;
			} catch (const std::exception& error) {
				std::cerr << "Workshop persistence unavailable: " << error.what() << '\n';
				return false;
			}
		}

		std::vector<OutboundFrame> FlushWorkshopPersistence(double a_unixSeconds, double a_monotonicSeconds = 0.0)
		{
			RebaseRestoredRaidDeclarations(a_unixSeconds, a_monotonicSeconds);
			PruneExpiredRaidState(a_unixSeconds);
			auto frames = TickWorkshopUpkeepDecay(a_unixSeconds);
			if (a_monotonicSeconds > 0.0) {
				AppendFrames(frames, TickWorkshopRaidWindows(a_monotonicSeconds));
			}
			auto repairFrames = settlementAccess.TickUnclaimedWorkbenchRepairs(
				FactionPeers(),
				static_cast<std::uint64_t>(a_unixSeconds));
			frames.insert(frames.end(), std::make_move_iterator(repairFrames.begin()), std::make_move_iterator(repairFrames.end()));
			auto defenseFrames = TickWorkshopDefenses(a_unixSeconds, a_monotonicSeconds);
			frames.insert(frames.end(), std::make_move_iterator(defenseFrames.begin()), std::make_move_iterator(defenseFrames.end()));
			auto upkeepFrames = FanoutUpkeepStates(static_cast<std::uint64_t>(a_unixSeconds));
			frames.insert(frames.end(), std::make_move_iterator(upkeepFrames.begin()), std::make_move_iterator(upkeepFrames.end()));
			FlushDirtyWorkshopZones(a_unixSeconds, frames);
			AppendFrames(frames, settlementAccess.FlushWorkbenchDamage([this](F4T::Workshop::BuildZoneId a_buildZoneId) { return WorkbenchHpPeers(a_buildZoneId); }, static_cast<std::uint64_t>(a_unixSeconds)));
			return frames;
		}

		[[nodiscard]] static std::string WorkshopUtcDayKey(double a_unixSeconds)
		{
			const auto seconds = static_cast<std::time_t>(a_unixSeconds);
			std::tm utc{};
#if defined(_WIN32)
			(void)gmtime_s(&utc, &seconds);
#else
			(void)gmtime_r(&seconds, &utc);
#endif
			char buffer[11]{};
			(void)std::strftime(buffer, sizeof buffer, "%Y-%m-%d", &utc);
			return buffer;
		}

		void SetWorkshopAuthority(F4T::Workshop::WorkshopAuthority::CanBuildAt a_canBuildAt)
		{
			if (a_canBuildAt) {
				workshopAuthority = std::move(a_canBuildAt);
				return;
			}
			workshopAuthority = SettlementAuthority();
		}

		void BindSessionProfile(std::uint32_t a_connectionId, std::string_view a_profileId)
		{
			if (const auto session = sessions.find(a_connectionId); session != sessions.end()) {
				session->second.profileId = std::string{ a_profileId };
			}
		}

		std::vector<OutboundFrame> HandleWorkshopOp(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			stats.workshopOpsReceived++;
			auto request = F4T::WorkshopProtocol::NormalizeOperation(a_packet, a_session.playerId);
			if (request) {
				request->profileId = a_session.profileId;
				if (request->kind == F4T::Workshop::OperationKind::Place) {
					FillPieceSpec(*request);
				}
			}
			if (!request) {
				Reject();
				stats.workshopOpsRejected++;
				return {};
			}

			if (auto refused = VetoWorkshop(a_connectionId, a_session, a_packet, *request, a_monotonicSeconds)) return std::move(*refused);
			const auto zone = workshopZones.find(request->buildZoneId);
			const auto currentRevision = zone == workshopZones.end() ? 0 : zone->second.CurrentRevision();
			F4T::Workshop::Result result;
			std::optional<F4T::Workshop::SettlementState> applied;
			if (const auto replay = a_session.workshopOperations.find(request->operationId);
				replay != a_session.workshopOperations.end()) {
				if (!(replay->second.request == *request)) {
					result = {
						F4T::Workshop::ApplyStatus::InvalidRequest,
						currentRevision,
						request->objectId,
						false,
						std::nullopt
					};
				} else {
					result = replay->second.result;
					result.replayed = true;
				}
			} else {
				if (zone == workshopZones.end()) {
					result = {
						F4T::Workshop::ApplyStatus::UnknownBuildZone,
						0,
						0,
						false,
						std::nullopt
					};
				} else if (
					(request->kind == F4T::Workshop::OperationKind::Place ||
						request->kind == F4T::Workshop::OperationKind::ScrapWorld) &&
					(zone->second.ObjectCount() >= F4T::WorkshopScale::MaxObjectsPerSettlement ||
						WorkshopObjectCount() >= F4T::WorkshopScale::MaxObjectsPerServer)) {
					result = {
						F4T::Workshop::ApplyStatus::InvalidRequest,
						currentRevision,
						0,
						false,
						std::nullopt
					};
				} else {
					// Constraint: the live zone is replaced only after the save succeeds.
					applied = zone->second;
					result = applied->Apply(*request, workshopAuthority, static_cast<std::uint64_t>(a_unixTimeSeconds));
				}
			}
			// Replays are not fresh: no persist, count, or broadcast.
			const bool fresh = applied.has_value() && !result.replayed;

			if (result.status != F4T::Workshop::ApplyStatus::Committed) {
				Reject();
				stats.workshopOpsRejected++;
				std::vector<OutboundFrame> rejected{
					ReliableFrame(a_connectionId, F4T::WorkshopProtocol::MakeReject(*request, result))
				};
				if (result.status == F4T::Workshop::ApplyStatus::StaleRevision && zone != workshopZones.end() &&
					F4T::PacketRateLimit::TakeToken(
						a_session.workshopSnapshotRequests,
						a_monotonicSeconds,
						kWorkshopSnapshotRefillPerSecond,
						kWorkshopSnapshotBurst)) {
					AppendFrames(rejected, WorkshopSnapshotFrames(a_connectionId, zone->second));
				}
				return rejected;
			}

			const auto commit = F4T::WorkshopProtocol::MakeCommit(*request, result);
			if (!commit) {
				Reject();
				stats.workshopOpsRejected++;
				return {};
			}
			std::optional<F4T::WorkshopPower::Node> placed;
			if (fresh && request->kind == F4T::Workshop::OperationKind::Place) {
				placed = workshopPieces.Find(request->form).power;
				if (placed) {
					placed->objectId = result.objectId;
				}
			}
			if (fresh) {
				if (!SaveZoneWithPower(*applied, placed)) {
					Reject();
					stats.workshopOpsRejected++;
					const F4T::Workshop::Result unsaved{
						F4T::Workshop::ApplyStatus::PersistenceUnavailable,
						currentRevision,
						request->objectId,
						false,
						std::nullopt
					};
					return { ReliableFrame(a_connectionId, F4T::WorkshopProtocol::MakeReject(*request, unsaved)) };
				}
				zone->second = std::move(*applied);
				stats.workshopOpsCommitted++;
			}
			if (applied) {
				RememberWorkshopOperation(a_session, *request, result);
			}
			std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, *commit) };
			if (fresh) {
				for (const auto& [connectionId, peer] : sessions) {
					if (connectionId != a_connectionId && peer.active && peer.workshopZonesInside.contains(request->buildZoneId)) {
						frames.push_back(ReliableFrame(connectionId, *commit));
					}
				}
				if (placed.has_value() || request->kind == F4T::Workshop::OperationKind::Scrap ||
					request->kind == F4T::Workshop::OperationKind::ScrapWorld) {
					AppendFrames(frames, FanoutPowerState(request->buildZoneId));
				}
			}
			return frames;
		}

		void RememberWorkshopOperation(
			Session& a_session,
			const F4T::Workshop::Request& a_request,
			const F4T::Workshop::Result& a_result)
		{
			if (a_session.workshopOperationOrder.size() == F4T::Workshop::MaximumRememberedOperations) {
				a_session.workshopOperations.erase(a_session.workshopOperationOrder.front());
				a_session.workshopOperationOrder.pop_front();
			}
			a_session.workshopOperationOrder.push_back(a_request.operationId);
			a_session.workshopOperations.emplace(a_request.operationId, WorkshopOperationRecord{ a_request, a_result });
		}

		static constexpr double kWorkshopSnapshotRefillPerSecond = 0.5;
		static constexpr double kWorkshopSnapshotBurst = 2.0;

		std::vector<OutboundFrame> WorkshopSnapshotFrames(
			std::uint32_t a_connectionId,
			const F4T::Workshop::SettlementState& a_zone) const
		{
			std::vector<OutboundFrame> frames;
			for (auto& packet : F4T::WorkshopProtocol::MakeSnapshotPackets(
				     a_zone.Id(),
				     a_zone.CurrentRevision(),
				     a_zone.Objects(),
				     a_zone.Tombstones())) {
				frames.push_back(ReliableFrame(a_connectionId, std::move(packet)));
			}
			AppendDoorStateFrames(frames, a_connectionId, a_zone);
			return frames;
		}

		std::vector<OutboundFrame> HandleWorkshopSnapshotRequest(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds)
		{
			const auto request = F4T::WorkshopProtocol::NormalizeSnapshotRequest(a_packet);
			if (!request) {
				Reject();
				return {};
			}
			if (!F4T::PacketRateLimit::TakeToken(
				    a_session.workshopSnapshotRequests,
				    a_monotonicSeconds,
				    kWorkshopSnapshotRefillPerSecond,
				    kWorkshopSnapshotBurst)) {
				Reject();
				return {};
			}
			const auto zone = workshopZones.find(request->buildZoneId);
			if (zone == workshopZones.end()) {
				Reject();
				return {};
			}
			if (!SessionWithinZoneReach(a_session, zone->second)) {
				Reject();
				return {};
			}
			a_session.workshopZonesInside.insert(request->buildZoneId);
			if (request->haveRevision == zone->second.CurrentRevision()) {
				std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, Json{
					{ "type", "workshopSnapshot" },
					{ "buildZoneId", request->buildZoneId },
					{ "revision", request->haveRevision },
					{ "complete", false },
					{ "unchanged", true },
					{ "objects", Json::array() },
					{ "tombstones", Json::array() }
				}) };
				AppendDoorStateFrames(frames, a_connectionId, zone->second);
				return frames;
			}
			return WorkshopSnapshotFrames(a_connectionId, zone->second);
		}

		std::vector<OutboundFrame> ExistingWorkshopSnapshotFrames(std::uint32_t a_connectionId, Session& a_session)
		{
			std::vector<OutboundFrame> frames;
			if (!a_session.lastMovement) return frames;
			const auto& position = *a_session.lastMovement;
			for (const auto& [zoneId, zone] : workshopZones) {
				if (zone.WithinRadius(position.x, position.y, position.z) && a_session.workshopZonesInside.insert(zoneId).second) {
					AppendFrames(frames, WorkshopSnapshotFrames(a_connectionId, zone));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> WorkshopEntryFrames(std::uint32_t a_connectionId, double a_monotonicSeconds)
		{
			const auto found = sessions.find(a_connectionId);
			if (found == sessions.end() || !found->second.active || !found->second.lastMovement) return {};
			Session& session = found->second;
			const auto& position = *session.lastMovement;
			std::vector<OutboundFrame> frames;
			for (const auto& [zoneId, zone] : workshopZones) {
				if (!zone.WithinRadius(position.x, position.y, position.z)) {
					session.workshopZonesInside.erase(zoneId);
					continue;
				}
				if (session.workshopZonesInside.contains(zoneId)) continue;
				if (!F4T::PacketRateLimit::TakeToken(
					    session.workshopSnapshotRequests,
					    a_monotonicSeconds,
					    kWorkshopSnapshotRefillPerSecond,
					    kWorkshopSnapshotBurst)) {
					continue;
				}
				session.workshopZonesInside.insert(zoneId);
				AppendFrames(frames, RaidStateFrames(a_connectionId, zoneId));
				AppendFrames(frames, WorkshopSnapshotFrames(a_connectionId, zone));
			}
			return frames;
		}

		F4T::Workshop::WorkshopAuthority::CanBuildAt workshopAuthority{ SettlementAuthority() };
		std::map<F4T::Workshop::BuildZoneId, F4T::Workshop::SettlementState> workshopZones;
		std::map<F4T::Workshop::BuildZoneId, F4T::WorkshopPower::PowerGraph> workshopPower;

		bool IsNamedWorkshopZone(F4T::Workshop::BuildZoneId a_buildZoneId) const
		{
			const auto zone = workshopZones.find(a_buildZoneId);
			return zone != workshopZones.end() && !zone->second.Policy().name.empty();
		}

		F4T::Workshop::WorkshopAuthority::CanBuildAt SettlementAuthority()
		{
			return [this](const std::string& a_profileId, F4T::Workshop::BuildZoneId a_buildZoneId) {
				return settlementAccess.CanPlace(a_buildZoneId, a_profileId, IsNamedWorkshopZone(a_buildZoneId));
			};
		}

		bool EnableSettlementAccess(PersistenceGate* a_persistence)
		{
			if (!settlementAccess.Attach(a_persistence)) {
				return false;
			}
			settlementAccess.SetFactionProfilesLookup([this](std::string_view a_profileId) {
				return factionService.MembersOf(std::string{ a_profileId });
			});
			settlementAccess.SetOwnerProtectedUntilLookup([this](std::string_view a_profileId) {
				return NewPlayerProtectedUntil(a_profileId);
			});
			settlementAccess.SetDestroyedStructuresLookup([this](F4T::Workshop::BuildZoneId a_buildZoneId) {
				return DestroyedStructuresFor(a_buildZoneId);
			});
			return true;
		}

		void SetWorkshopRaidConfig(F4T::WorkshopRaids::RaidConfig a_config)
		{
			workshopRaidConfig = a_config;
		}

		[[nodiscard]] F4T::WorkshopRaids::RaidConfig WorkshopRaidConfig() const
		{
			return workshopRaidConfig;
		}

		std::vector<OutboundFrame> SetSettlementOwner(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_ownerProfileId,
			std::string& a_failure,
			double a_unixTimeSeconds = 0.0,
			std::optional<std::uint64_t> a_protectedUntil = std::nullopt)
		{
			const auto previousOwner = settlementAccess.OwnerOf(a_buildZoneId);
			auto frames = settlementAccess.SetOwner(FactionPeers(), workshopZones, a_buildZoneId, a_ownerProfileId, a_failure,
				F4T::WorkshopCamps::IsCampZoneId(a_buildZoneId) ? F4T::SettlementAccess::SettlementKind::Camp : F4T::SettlementAccess::SettlementKind::Vanilla, a_protectedUntil);
			if (!a_failure.empty() || settlementAccess.OwnerOf(a_buildZoneId) == previousOwner) {
				return frames;
			}
			AppendFrames(frames, OwnerChangedFrames(a_buildZoneId, static_cast<std::uint64_t>(a_unixTimeSeconds)));
			PersistAuthSteamAndWorkshop();
			return frames;
		}

		nlohmann::json ScriptSettlementOwner(F4T::Workshop::BuildZoneId a_buildZoneId) const
		{
			return nlohmann::json{ { "buildZoneId", a_buildZoneId }, { "ownerProfileId", settlementAccess.OwnerOf(a_buildZoneId) } };
		}

		SettlementCaptureService::Peers CapturePeers(double a_monotonicSeconds) const
		{
			SettlementCaptureService::Peers peers;
			for (const auto& [connectionId, session] : sessions) {
				if (!session.active || session.profileId.empty()) continue;
				CapturePeer peer{ connectionId, {} };
				peer.presence.profileId = session.profileId;
				if (session.lastMovement && session.lastMovementMonotonic) {
					const auto& movement = *session.lastMovement;
					peer.presence.positionKnown = true;
					peer.presence.positionAgeSeconds = a_monotonicSeconds - *session.lastMovementMonotonic;
					peer.presence.x = movement.x;
					peer.presence.y = movement.y;
					peer.presence.z = movement.z;
				}
				const auto vitals = SessionVitals(session);
				peer.presence.vitalsKnown = vitals.known;
				peer.presence.alive = !vitals.dead;
				peer.presence.downed = vitals.downed;
				peers.push_back(std::move(peer));
			}
			return peers;
		}

		std::optional<std::vector<OutboundFrame>> DispatchSettlementPacket(
			const std::string& a_type,
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds)
		{
			const auto allow = [&]() {
				if (events.Allow(ServerEvent{ ServerEventKind::SettlementAction, a_connectionId, a_session.playerId, a_session.profileId, &a_packet })) return true;
				Reject();
				return false;
			};
			if (a_type == "settlementCapture") {
				if (auto refused = RefuseCaptureOnRaid(a_connectionId, a_session, a_packet, a_monotonicSeconds, a_unixTimeSeconds)) {
					return refused;
				}
				return settlementCapture.Dispatch(a_type, a_connectionId, a_session.profileId, a_session.settlementCaptureRequests, CapturePeers(a_monotonicSeconds), workshopZones, settlementAccess, workshopDefenseRatings, a_packet, a_monotonicSeconds, a_unixTimeSeconds, allow);
			}
			if (!a_type.starts_with("settlementAccess")) return std::nullopt;
			const FactionPeer self{ a_connectionId, a_session.playerId, a_session.profileId, std::string{ SessionNameView(a_session) }, SessionHasSocialNames(a_session), {}, std::string{ SessionSocialKeyView(a_session) } };
			std::string grantTarget;
			std::string grantSteam;
			F4T::Workshop::BuildZoneId grantZone{};
			bool grant = false;
			bool revoke = false;
			if (a_type == "settlementAccessUpdate") {
				if (const auto update = F4T::SettlementAccessProtocol::NormalizeUpdate(a_packet)) {
					grantZone = update->buildZoneId;
					grantTarget = update->targetProfileId;
					grant = update->grant;
					revoke = !update->grant;
					if (grant) {
						for (const auto& [connectionId, peer] : sessions) {
							(void)connectionId;
							if (peer.active && peer.profileId == grantTarget && !peer.steamId.empty()) {
								grantSteam = peer.steamId;
								break;
							}
						}
						if (AuthSteamConflicts(grantZone, grantSteam, grantTarget)) {
							Reject();
							return std::vector<OutboundFrame>{
								ReliableFrame(
									a_connectionId,
									F4T::SettlementAccessProtocol::MakeReject(
										grantZone,
										F4T::SettlementAccess::ApplyStatus::AlreadyAllowed))
							};
						}
					}
				}
			}
			auto frames = settlementAccess.Dispatch(a_type, self, a_session.settlementAccessRequests, FactionPeers(), workshopZones, a_packet, a_monotonicSeconds, allow);
			if (frames && grant && !grantTarget.empty() && !grantSteam.empty()) {
				const auto settlement = settlementAccess.Find(grantZone);
				if (settlement &&
					(settlement->ownerProfileId == grantTarget ||
						std::find(settlement->allowedProfileIds.begin(), settlement->allowedProfileIds.end(), grantTarget) !=
							settlement->allowedProfileIds.end())) {
					BindAuthSteam(grantZone, grantTarget, grantSteam);
					PersistAuthSteam();
					if (workshopPersistence != nullptr) {
						try {
							workshopPersistence->SaveWorkshop(workshopStored);
						} catch (const std::exception&) {
						}
					}
				}
			}
			if (frames && revoke && !grantTarget.empty()) {
				ClearAuthSteamProfile(grantZone, grantTarget);
				PersistAuthSteam();
				if (workshopPersistence != nullptr) {
					try {
						workshopPersistence->SaveWorkshop(workshopStored);
					} catch (const std::exception&) {
					}
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> PollSettlementCapture(double a_monotonicSeconds, double a_unixTimeSeconds = 0.0)
		{
			std::map<F4T::Workshop::BuildZoneId, std::string> ownersBefore;
			for (const auto& [zoneId, zone] : workshopZones) {
				(void)zone;
				ownersBefore[zoneId] = settlementAccess.OwnerOf(zoneId);
			}
			const auto unixSeconds = static_cast<std::uint64_t>(a_unixTimeSeconds);
			const SettlementCaptureService::CaptureRefusal refusalFor = [&](F4T::Workshop::BuildZoneId a_zoneId, const std::string& a_profileId) {
				return CaptureRefusalFor(a_zoneId, a_profileId, unixSeconds, a_monotonicSeconds);
			};
			auto frames = settlementCapture.Poll(CapturePeers(a_monotonicSeconds), workshopZones, settlementAccess, workshopDefenseRatings, a_monotonicSeconds, a_unixTimeSeconds, refusalFor);
			bool authSteamDirty = false;
			for (const auto& [zoneId, previousOwner] : ownersBefore) {
				if (settlementAccess.OwnerOf(zoneId) == previousOwner) {
					continue;
				}
				AppendFrames(frames, OwnerChangedFrames(zoneId, static_cast<std::uint64_t>(a_unixTimeSeconds)));
				authSteamDirty = true;
			}
			if (authSteamDirty) {
				PersistAuthSteamAndWorkshop();
			}
			return frames;
		}

		std::vector<OutboundFrame> PlayerRaidProtectionFrames(double a_unixTimeSeconds)
		{
			std::vector<OutboundFrame> frames;
			const auto unixSeconds = static_cast<std::uint64_t>(a_unixTimeSeconds);
			for (auto& [connectionId, session] : sessions) {
				const auto playerUntil = session.active ? NewPlayerProtectedUntil(session.profileId).value_or(0) : std::uint64_t{ 0 };
				if (playerUntil == session.sentPlayerProtectedUntil) {
					continue;
				}
				session.sentPlayerProtectedUntil = playerUntil;
				frames.push_back(ReliableFrame(connectionId, F4T::WorkshopProtocol::MakePlayerRaidProtection(playerUntil > unixSeconds ? playerUntil - unixSeconds : 0)));
			}
			return frames;
		}

		std::vector<OutboundFrame> SettlementListFrames(std::uint32_t a_connectionId)
		{
			const auto found = sessions.find(a_connectionId);
			if (found == sessions.end() || !found->second.active) {
				return {};
			}
			if (found->second.settlementListSentZones == workshopZones.size()) {
				return {};
			}
			found->second.settlementListSentZones = workshopZones.size();
			auto frames = settlementAccess.SettlementListFrames(a_connectionId, found->second.profileId, workshopZones);
			for (auto& frame : settlementCapture.ZoneFrames(a_connectionId, workshopZones)) frames.push_back(std::move(frame));
			AppendFrames(frames, settlementCapture.ActiveFrames(a_connectionId));
			AppendFrames(frames, RaidStateFrames(a_connectionId));
			frames.push_back(ReliableFrame(a_connectionId, F4T::WorkshopCamps::MakeCampList(workshopCamps)));
			return frames;
		}

		SettlementAccessService::Peers WorkbenchHpPeers(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			SettlementAccessService::Peers peers;
			const auto owner = settlementAccess.OwnerOf(a_buildZoneId);
			for (auto& [connectionId, session] : sessions) {
				if (!session.active || (!session.workshopZonesInside.contains(a_buildZoneId) && (owner.empty() || session.profileId != owner))) {
					continue;
				}
				const FactionPeer peer{ connectionId, session.playerId, session.profileId, std::string{ SessionNameView(session) }, SessionHasSocialNames(session), {}, std::string{ SessionSocialKeyView(session) } };
				peers.push_back(peer);
			}
			return peers;
		}

		SettlementCaptureService settlementCapture;
		SettlementAccessService settlementAccess;
		PersistenceGate* workshopPersistence{};
		Json workshopStored{ { "version", 2 }, { "zones", Json::object() } };
		std::unordered_set<F4T::Workshop::BuildZoneId> workshopDirtyZones;
		bool workshopRootDirty{};
		double workshopRootLastSaveUnix{};
		double workshopUpkeepLastUnix{};
		std::vector<F4T::WorkshopCamps::CampRecord> workshopCamps;
		std::uint64_t workshopNextCampSequence{ 1 };
		F4T::WorkshopRaids::RaidConfig workshopRaidConfig{};
		std::map<F4T::Workshop::BuildZoneId, F4T::WorkshopRaids::AttackState> workshopAttackStates;
		std::map<F4T::Workshop::BuildZoneId, std::map<std::string, std::string>> workshopAuthSteamByZone;
		std::map<std::string, std::map<F4T::Workshop::BuildZoneId, std::uint64_t>> workshopFactionLeaveRaidUntil;
		F4T::WorkshopNoBuild::Table workshopNoBuild;
		F4T::WorkshopPieces::Table workshopPieces;
		F4T::WorkshopCosts::Table workshopCosts;
		F4T::WeaponClasses::Table workshopWeaponClasses;
