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

		bool EnableWorkshopPersistence(PersistenceGate* a_persistence)
		{
			if (a_persistence == nullptr || workshopPersistence != nullptr) {
				return false;
			}
			auto stored = a_persistence->GetWorkshop();
			if (stored.is_object() && stored.value("zones", Json::object()).is_object()) {
				workshopStored = std::move(stored);
				workshopStored["version"] = 1;
			}
			workshopPersistence = a_persistence;
			for (auto& entry : workshopZones) {
				RestoreStoredWorkshopZone(entry.second);
			}
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
			std::size_t registered = 0;
			for (const auto& policy : *zones) {
				if (RegisterWorkshopBuildZone(policy)) ++registered;
			}
			std::cout << "Workshop build zones: " << registered << " registered from " << name
			          << (workshopPersistence == nullptr ? "; no durable state, builds reset on restart" : "") << std::endl;
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
			}
		}

		void SaveWorkshopZone(const F4T::Workshop::SettlementState& a_zone)
		{
			if (workshopPersistence == nullptr) {
				return;
			}
			workshopStored["zones"][std::to_string(a_zone.Id())] = F4T::WorkshopProtocol::MakeZoneState(a_zone);
			try {
				workshopPersistence->SaveWorkshop(workshopStored);
			} catch (const std::exception& error) {
				std::cerr << "Workshop persistence unavailable: " << error.what() << '\n';
			}
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
			double a_monotonicSeconds)
		{
			stats.workshopOpsReceived++;
			auto request = F4T::WorkshopProtocol::NormalizeOperation(a_packet, a_session.playerId);
			if (request) {
				request->profileId = a_session.profileId;
			}
			if (!request) {
				Reject();
				stats.workshopOpsRejected++;
				return {};
			}

			const auto zone = workshopZones.find(request->buildZoneId);
			const auto currentRevision = zone == workshopZones.end() ? 0 : zone->second.CurrentRevision();
			F4T::Workshop::Result result;
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
				} else {
					result = zone->second.Apply(*request, workshopAuthority);
				}
				if (result.status == F4T::Workshop::ApplyStatus::Committed) {
					if (a_session.workshopOperationOrder.size() == F4T::Workshop::MaximumRememberedOperations) {
						a_session.workshopOperations.erase(a_session.workshopOperationOrder.front());
						a_session.workshopOperationOrder.pop_front();
					}
					a_session.workshopOperationOrder.push_back(request->operationId);
					a_session.workshopOperations.emplace(
						request->operationId,
						WorkshopOperationRecord{ *request, result });
				}
			}

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
			std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, *commit) };
			if (!result.replayed) {
				stats.workshopOpsCommitted++;
				SaveWorkshopZone(zone->second);
				for (const auto& [connectionId, peer] : sessions) {
					if (connectionId != a_connectionId && peer.active && peer.workshopZonesInside.contains(request->buildZoneId)) {
						frames.push_back(ReliableFrame(connectionId, *commit));
					}
				}
			}
			return frames;
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
			return frames;
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
				AppendFrames(frames, WorkshopSnapshotFrames(a_connectionId, zone));
			}
			return frames;
		}

		F4T::Workshop::WorkshopAuthority::CanBuildAt workshopAuthority{ SettlementAuthority() };
		std::map<F4T::Workshop::BuildZoneId, F4T::Workshop::SettlementState> workshopZones;

		F4T::Workshop::WorkshopAuthority::CanBuildAt SettlementAuthority()
		{
			return [this](const std::string& a_profileId, F4T::Workshop::BuildZoneId a_buildZoneId) {
				return settlementAccess.CanBuild(a_buildZoneId, a_profileId);
			};
		}

		bool EnableSettlementAccess(PersistenceGate* a_persistence)
		{
			return settlementAccess.Attach(a_persistence);
		}

		std::vector<OutboundFrame> SetSettlementOwner(
			F4T::Workshop::BuildZoneId a_buildZoneId,
			std::string_view a_ownerProfileId,
			std::string& a_failure)
		{
			return settlementAccess.SetOwner(FactionPeers(), workshopZones, a_buildZoneId, a_ownerProfileId, a_failure);
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
			double a_monotonicSeconds)
		{
			if (a_type == "settlementCapture") {
				return settlementCapture.Dispatch(a_type, a_connectionId, a_session.profileId, a_session.settlementCaptureRequests, CapturePeers(a_monotonicSeconds), workshopZones, settlementAccess, a_packet, a_monotonicSeconds);
			}
			if (!a_type.starts_with("settlementAccess")) return std::nullopt;
			const FactionPeer self{ a_connectionId, a_session.playerId, a_session.profileId, std::string{ SessionNameView(a_session) }, SessionHasSocialNames(a_session) };
			return settlementAccess.Dispatch(a_type, self, a_session.settlementAccessRequests, FactionPeers(), workshopZones, a_packet, a_monotonicSeconds);
		}

		std::vector<OutboundFrame> PollSettlementCapture(double a_monotonicSeconds)
		{
			return settlementCapture.Poll(CapturePeers(a_monotonicSeconds), workshopZones, settlementAccess, a_monotonicSeconds);
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
			auto frames = settlementAccess.SettlementListFrames(a_connectionId, workshopZones);
			for (auto& frame : settlementCapture.ZoneFrames(a_connectionId, workshopZones)) frames.push_back(std::move(frame));
			return frames;
		}

		SettlementCaptureService settlementCapture;
		SettlementAccessService settlementAccess;
		PersistenceGate* workshopPersistence{};
		Json workshopStored{ { "version", 1 }, { "zones", Json::object() } };
