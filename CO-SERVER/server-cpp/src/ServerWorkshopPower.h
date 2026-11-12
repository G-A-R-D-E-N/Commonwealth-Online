		static Json MakeZonePowerState(
			const F4T::Workshop::SettlementState& a_zone,
			const F4T::WorkshopPower::PowerGraph& a_graph)
		{
			std::vector<F4T::Workshop::ObjectId> powered(a_graph.Powered().begin(), a_graph.Powered().end());
			std::sort(powered.begin(), powered.end());
			std::vector<F4T::WorkshopProtocol::PowerWire> wires;
			for (const auto& edge : a_graph.Edges()) {
				if (edge.wireId != 0 && a_zone.Find(edge.wireId) != nullptr) {
					wires.push_back({ edge.wireId, edge.fromId, edge.toId });
				}
			}
			return F4T::WorkshopProtocol::MakePowerState(a_zone.Id(), powered, wires);
		}

		std::vector<OutboundFrame> FanoutPowerState(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			std::vector<OutboundFrame> frames;
			const auto zone = workshopZones.find(a_buildZoneId);
			const auto power = workshopPower.find(a_buildZoneId);
			if (zone == workshopZones.end() || power == workshopPower.end()) {
				return frames;
			}
			const auto packet = MakeZonePowerState(zone->second, power->second);
			for (const auto& [connectionId, peer] : sessions) {
				if (peer.active && peer.workshopZonesInside.contains(a_buildZoneId)) {
					frames.push_back(ReliableFrame(connectionId, packet));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> FanoutZoneAttackState(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			std::vector<OutboundFrame> frames;
			const auto attack = workshopAttackStates.find(a_buildZoneId);
			const bool underAttack = attack != workshopAttackStates.end() && attack->second.underAttack;
			const auto packet = F4T::WorkshopProtocol::MakeZoneAttackState(
				a_buildZoneId,
				underAttack,
				underAttack ? attack->second.sinceMonotonic : 0.0,
				underAttack ? attack->second.attackerCount : 0);
			const auto owner = settlementAccess.OwnerOf(a_buildZoneId);
			for (const auto& [connectionId, peer] : sessions) {
				if (!peer.active) {
					continue;
				}
				if (peer.workshopZonesInside.contains(a_buildZoneId) ||
					(!owner.empty() && peer.profileId == owner) ||
					settlementAccess.CanBuild(a_buildZoneId, peer.profileId)) {
					frames.push_back(ReliableFrame(connectionId, packet));
				}
			}
			return frames;
		}

		std::vector<OutboundFrame> HandleWireOp(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet)
		{
			const auto wire = F4T::WorkshopProtocol::NormalizeWireOp(a_packet);
			if (!wire || a_session.profileId.empty()) {
				Reject();
				return {};
			}
			if (!settlementAccess.CanPlace(wire->buildZoneId, a_session.profileId, IsNamedWorkshopZone(wire->buildZoneId))) {
				Reject();
				return {};
			}
			const auto zone = workshopZones.find(wire->buildZoneId);
			if (zone == workshopZones.end()) {
				Reject();
				return {};
			}
			if (!a_session.workshopZonesInside.contains(wire->buildZoneId) || !SessionWithinZoneReach(a_session, zone->second)) {
				Reject();
				return {};
			}
			auto& graph = workshopPower[wire->buildZoneId];
			const auto previous = graph;
			bool ok = false;
			switch (wire->kind) {
			case F4T::WorkshopProtocol::WireOpKind::Connect:
				ok = zone->second.Find(wire->fromId) != nullptr && zone->second.Find(wire->toId) != nullptr &&
					(wire->wireId == 0 || zone->second.Find(wire->wireId) != nullptr) &&
					graph.Connect(wire->fromId, wire->toId, wire->wireId);
				break;
			case F4T::WorkshopProtocol::WireOpKind::Disconnect:
				ok = graph.Disconnect(wire->fromId, wire->toId);
				break;
			case F4T::WorkshopProtocol::WireOpKind::Switch:
				ok = graph.SetSwitch(wire->fromId, wire->switchClosed);
				break;
			}
			if (!ok) {
				Reject();
				return {};
			}
			graph.Solve();
			if (!SaveWorkshopZone(zone->second)) {
				graph = previous;
				Reject();
				return {};
			}
			MarkWorkshopDirty(wire->buildZoneId);
			return FanoutPowerState(wire->buildZoneId);
		}

		void PrunePowerNodes(const F4T::Workshop::SettlementState& a_zone)
		{
			const auto power = workshopPower.find(a_zone.Id());
			if (power == workshopPower.end()) {
				return;
			}
			auto& graph = power->second;
			std::vector<F4T::Workshop::ObjectId> stale;
			for (const auto& node : graph.Nodes()) {
				if (a_zone.Find(node.objectId) == nullptr) {
					stale.push_back(node.objectId);
				}
			}
			std::vector<F4T::Workshop::ObjectId> staleWires;
			for (const auto& edge : graph.Edges()) {
				if (edge.wireId != 0 && a_zone.Find(edge.wireId) == nullptr) {
					staleWires.push_back(edge.wireId);
				}
			}
			for (const auto wireId : staleWires) {
				(void)graph.RemoveWire(wireId);
			}
			for (const auto objectId : stale) {
				(void)graph.RemoveNode(objectId);
			}
			graph.Solve();
		}

		[[nodiscard]] bool SaveZoneWithPower(
			const F4T::Workshop::SettlementState& a_zone,
			std::optional<F4T::WorkshopPower::Node> a_placed = std::nullopt)
		{
			auto& graph = workshopPower[a_zone.Id()];
			const auto previous = graph;
			if (a_placed && !graph.AddNode(*a_placed)) {
				return false;
			}
			PrunePowerNodes(a_zone);
			if (SaveWorkshopZone(a_zone)) {
				return true;
			}
			graph = previous;
			return false;
		}

		void FillPieceSpec(F4T::Workshop::Request& a_request) const
		{
			const auto piece = workshopPieces.Find(a_request.form);
			a_request.maxHealth = piece.door ? F4T::WorkshopMaterials::MaxHpForDoor(piece.tier)
			                                 : F4T::WorkshopMaterials::MaxHpForTier(piece.tier);
			a_request.door = piece.door;
			a_request.foundation = piece.foundation;
		}

		void LoadWorkshopCosts(const std::filesystem::path& a_path)
		{
			if (std::ifstream input(a_path, std::ios::binary); input) {
				const auto parsed = Json::parse(input, nullptr, false);
				if (!parsed.is_discarded() && workshopCosts.LoadFromJson(parsed)) {
					std::cout << "Workshop costs: " << workshopCosts.Size() << " loaded from "
					          << F4T::Utf8Path::Encode(a_path) << std::endl;
					if (workshopCosts.Size() == 0) {
						std::cerr << "Workshop costs: " << F4T::Utf8Path::Encode(a_path)
						          << " lists no prices; camp moves and camp reactivations are refused.\n";
					}
				} else {
					std::cerr << "Workshop costs: " << F4T::Utf8Path::Encode(a_path)
					          << " is invalid; keeping the previous table.\n";
				}
			} else {
				std::cerr << "Workshop costs: " << F4T::Utf8Path::Encode(a_path)
				          << " is missing; camp moves and camp reactivations are refused.\n";
			}
		}

		void LoadWorkshopPieces(const std::filesystem::path& a_path)
		{
			if (std::ifstream input(a_path, std::ios::binary); input) {
				const auto parsed = Json::parse(input, nullptr, false);
				if (!parsed.is_discarded() && workshopPieces.LoadFromJson(parsed)) {
					std::cout << "Workshop pieces: " << workshopPieces.Size() << " loaded from "
					          << F4T::Utf8Path::Encode(a_path) << std::endl;
					if (workshopPieces.Size() == 0) {
						std::cerr << "Workshop pieces: " << F4T::Utf8Path::Encode(a_path)
						          << " lists no pieces; every placed piece uses wood defaults.\n";
					}
				} else {
					std::cerr << "Workshop pieces: " << F4T::Utf8Path::Encode(a_path)
					          << " is invalid; keeping the previous table.\n";
				}
			} else {
				std::cerr << "Workshop pieces: " << F4T::Utf8Path::Encode(a_path)
				          << " is missing; pieces use wood defaults.\n";
			}
		}
