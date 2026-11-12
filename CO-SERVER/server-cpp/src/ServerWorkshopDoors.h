		std::set<std::pair<F4T::Workshop::BuildZoneId, F4T::Workshop::ObjectId>> openDoors;

		static Json DoorStatePacket(F4T::Workshop::BuildZoneId a_buildZoneId, F4T::Workshop::ObjectId a_objectId, bool a_open, bool a_locked)
		{
			return Json{ { "type", "doorState" }, { "buildZoneId", a_buildZoneId }, { "objectId", a_objectId }, { "open", a_open }, { "locked", a_locked } };
		}

		void ForgetZoneDoors(F4T::Workshop::BuildZoneId a_buildZoneId)
		{
			std::erase_if(openDoors, [a_buildZoneId](const auto& a_door) { return a_door.first == a_buildZoneId; });
		}

		void AppendDoorStateFrames(std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, const F4T::Workshop::SettlementState& a_zone) const
		{
			for (const auto& object : a_zone.Objects()) {
				if (object.door) {
					const bool open = openDoors.contains(std::make_pair(a_zone.Id(), object.objectId));
					a_frames.push_back(ReliableFrame(a_connectionId, DoorStatePacket(a_zone.Id(), object.objectId, open, object.locked)));
				}
			}
		}

		std::vector<OutboundFrame> HandleDoorOp(
			std::uint32_t a_connectionId,
			Session& a_session,
			const Json& a_packet)
		{
			const auto door = F4T::WorkshopProtocol::NormalizeDoorOp(a_packet);
			if (!door || a_session.profileId.empty()) {
				Reject();
				return {};
			}
			const auto zone = workshopZones.find(door->buildZoneId);
			if (zone == workshopZones.end()) {
				Reject();
				return {};
			}
			const auto* object = zone->second.Find(door->objectId);
			if (!object || !object->door) {
				Reject();
				return {};
			}
			const auto key = std::make_pair(door->buildZoneId, door->objectId);
			const bool wasOpen = openDoors.contains(key);
			const bool wasLocked = object->locked;
			const auto settlement = settlementAccess.Find(door->buildZoneId);
			const bool authorized = settlement && (settlement->ownerProfileId == a_session.profileId ||
				std::find(settlement->allowedProfileIds.begin(), settlement->allowedProfileIds.end(), a_session.profileId) != settlement->allowedProfileIds.end());
			bool accepted = a_session.workshopZonesInside.contains(door->buildZoneId) && SessionWithinZoneReach(a_session, zone->second);
			bool nextOpen = wasOpen;
			bool nextLocked = wasLocked;
			switch (door->kind) {
			case F4T::WorkshopProtocol::DoorOpKind::Open:
				accepted = accepted && F4T::WorkshopLocks::CanOpen(authorized, wasLocked);
				nextOpen = true;
				break;
			case F4T::WorkshopProtocol::DoorOpKind::Close:
				nextOpen = false;
				break;
			case F4T::WorkshopProtocol::DoorOpKind::Lock:
				accepted = accepted && F4T::WorkshopLocks::CanSetLock(authorized);
				nextLocked = true;
				break;
			case F4T::WorkshopProtocol::DoorOpKind::Unlock:
				accepted = accepted && F4T::WorkshopLocks::CanSetLock(authorized);
				nextLocked = false;
				break;
			}
			if (accepted && nextLocked != wasLocked) {
				auto candidate = zone->second;
				if (candidate.SetDoorLocked(door->objectId, nextLocked) && SaveWorkshopZone(candidate)) {
					zone->second = std::move(candidate);
					MarkWorkshopDirty(door->buildZoneId);
				} else {
					accepted = false;
				}
			}
			if (!accepted) {
				Reject();
				return { ReliableFrame(a_connectionId, DoorStatePacket(door->buildZoneId, door->objectId, wasOpen, wasLocked)) };
			}
			if (nextOpen) {
				openDoors.insert(key);
			} else {
				openDoors.erase(key);
			}
			if (nextOpen == wasOpen && nextLocked == wasLocked) {
				return {};
			}
			const auto packet = DoorStatePacket(door->buildZoneId, door->objectId, nextOpen, nextLocked);
			std::vector<OutboundFrame> frames;
			for (const auto& [connectionId, peer] : sessions) {
				if (peer.active && peer.workshopZonesInside.contains(door->buildZoneId)) {
					frames.push_back(ReliableFrame(connectionId, packet));
				}
			}
			return frames;
		}
